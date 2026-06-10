#!/usr/bin/env python3
"""
Renode instruction-count benchmark for ARM Cortex-M7.

Companion to build.py (which measures *code size*). This measures *work*: how
many CPU instructions each JSON library executes to parse + validate +
serialize the EmbeddedConfig and RpcCommand samples.

Renode is a functional simulator, not cycle-accurate, so this is an instruction
count, not a clock-cycle count. The upside is that it is perfectly deterministic
and hardware-independent — the same input always yields the same number — which
makes it a clean, reproducible "amount of work" comparison alongside code size.

How it works, per library:
  1. Compile the library's parse_config_*.cpp (its main() renamed away) and link
     it with renode_runner.cpp into a runnable ELF.
  2. Load the ELF into a minimal Cortex-M7 machine (cm7.repl).
  3. Hook the runner's marker functions by symbol address; read
     cpu.ExecutedInstructions at each. The marker functions are translation-block
     boundaries, so the count is exact at the hook.
  4. Report (end - begin) for the config and rpc parses, and confirm the parse
     returned true (ok=1) by reading r0 at the *_end marker.

Usage:
  ./renode_instructions.py                 # all JSON libraries
  ./renode_instructions.py --renode /path/to/renode
  RENODE=/path/to/renode ./renode_instructions.py
"""

import argparse
import json
import os
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Optional

SCRIPT_DIR = Path(__file__).parent.resolve()
PROJECT_ROOT = SCRIPT_DIR.parent.parent.parent

# Default Renode location on macOS; override with --renode or $RENODE.
DEFAULT_RENODE = "/Applications/Renode.app/Contents/MacOS/renode"

CC = "arm-none-eabi-g++"

# Compile/link flags mirror build.py's Cortex-M7 configuration so the measured
# work matches the code the size benchmark reports. Built at two optimization
# levels: -Os (size, what the size benchmark uses) and -O3 (speed).
EMBEDDED_FLAGS = [
    "-Wall", "-fno-exceptions", "-fno-rtti", "-ffunction-sections",
    "-fdata-sections", "-DNDEBUG", "-fno-unwind-tables",
    "-fno-asynchronous-unwind-tables",
]
ARCH_FLAGS = [
    "-flto", "-mcpu=cortex-m7", "-mthumb", "-mfloat-abi=hard",
    "-mfpu=fpv5-d16", "-fno-threadsafe-statics",
]
OPT_LEVELS = ["-Os", "-O3"]  # measured separately
LINK_SPECS = ["-specs=nano.specs", "-specs=nosys.specs"]

INCLUDES = [
    f"-I{PROJECT_ROOT}/include",
    f"-I{SCRIPT_DIR}",
    f"-I{SCRIPT_DIR}/libs",
    f"-I{SCRIPT_DIR}/libs/glaze/include",
    f"-I{SCRIPT_DIR}/libs/cJSON",
    f"-I{SCRIPT_DIR}/libs/yajl/include",
]


@dataclass
class Library:
    name: str
    source_file: str
    extra_flags: List[str] = None

    def __post_init__(self):
        if self.extra_flags is None:
            self.extra_flags = []


# JSON parsers only. CBOR is excluded (binary input, different entry signature).
LIBRARIES = [
    Library("JsonFusion", "parse_config.cpp"),
    Library("ArduinoJson", "parse_config_arduinojson.cpp"),
    Library("cJSON", "parse_config_cjson.cpp"),
    Library("Glaze", "parse_config_glaze.cpp"),
    Library("yajl", "parse_config_yajl.cpp", extra_flags=["-fpermissive"]),
]

MARKERS = ["cfg_begin", "cfg_mid", "cfg_end", "rpc_begin", "rpc_mid", "rpc_end"]


class Colors:
    GREEN = "\033[0;32m"; RED = "\033[0;31m"; YELLOW = "\033[1;33m"
    BLUE = "\033[0;34m"; NC = "\033[0m"


def c(color, text):
    return f"{color}{text}{Colors.NC}"


def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, **kw)


def build_elf(lib: Library, opt: str, build_dir: Path) -> Path:
    """Compile the library + runner into a runnable ELF at optimization level `opt`."""
    olabel = opt.lstrip("-")  # "Os" / "O3"
    opt_flags = [opt, *ARCH_FLAGS]
    parse_o = build_dir / f"parse_{lib.name}_{olabel}.o"
    runner_o = build_dir / f"runner_{lib.name}_{olabel}.o"
    elf = build_dir / f"bench_{lib.name}_{olabel}.elf"

    # Library parse code, with its own main() renamed out of the way.
    # JF_PERF_ROUNDTRIP: serialize the full fixed arrays into a scratch buffer so
    # every library does equivalent serialize work and parse input is exact.
    cc_parse = [
        CC, "-std=c++23", *EMBEDDED_FLAGS, *opt_flags, *lib.extra_flags,
        "-DJF_PERF_ROUNDTRIP", "-Dmain=__renode_unused_main", *INCLUDES,
        "-c", str(SCRIPT_DIR / lib.source_file), "-o", str(parse_o),
    ]
    r = run(cc_parse)
    if r.returncode != 0:
        print(c(Colors.RED, f"  ✗ compile {lib.source_file} ({opt}) failed"))
        print(r.stderr[-2000:])
        return None

    # Generic runner (provides the real main + markers).
    cc_runner = [
        CC, "-std=c++23", *EMBEDDED_FLAGS, *opt_flags, *INCLUDES,
        "-c", str(SCRIPT_DIR / "renode_runner.cpp"), "-o", str(runner_o),
    ]
    r = run(cc_runner)
    if r.returncode != 0:
        print(c(Colors.RED, f"  ✗ compile renode_runner.cpp ({opt}) failed"))
        print(r.stderr[-2000:])
        return None

    link = [
        CC, *opt_flags, *LINK_SPECS, "-Wl,--gc-sections",
        str(runner_o), str(parse_o), "-o", str(elf),
    ]
    r = run(link)
    if r.returncode != 0:
        print(c(Colors.RED, f"  ✗ link {lib.name} ({opt}) failed"))
        print(r.stderr[-2000:])
        return None
    return elf


def write_resc(resc_path: Path, elf: Path) -> None:
    lines = [
        "using sysbus",
        'mach create "bench"',
        f"machine LoadPlatformDescription @{SCRIPT_DIR / 'cm7.repl'}",
        f"sysbus LoadELF @{elf}",
        'cpu PC `sysbus GetSymbolAddress "_start"`',
    ]
    for m in ("cfg_begin", "cfg_mid", "rpc_begin", "rpc_mid"):
        lines.append(
            f'cpu AddHook `sysbus GetSymbolAddress "{m}"` '
            f'"cpu.Log(LogLevel.Error, \'MARK {m} {{0}}\'.format(cpu.ExecutedInstructions))"'
        )
    for m in ("cfg_end", "rpc_end"):
        lines.append(
            f'cpu AddHook `sysbus GetSymbolAddress "{m}"` '
            f'"cpu.Log(LogLevel.Error, \'MARK {m} {{0}} ok={{1}}\'.format(cpu.ExecutedInstructions, cpu.GetRegister(0).RawValue))"'
        )
    lines.append('emulation RunFor "0.05"')
    lines.append("quit")
    resc_path.write_text("\n".join(lines) + "\n")


MARK_RE = re.compile(r"MARK (\w+) (\d+)(?: ok=(\d+))?")


def measure(renode: str, elf: Path, build_dir: Path) -> Optional[Dict]:
    """Run the ELF in Renode and return marker instruction counts."""
    resc = build_dir / f"{elf.stem}.resc"
    write_resc(resc, elf)

    proc = subprocess.Popen(
        [renode, "--disable-xwt", "--console", str(resc)],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
    )
    try:
        out, _ = proc.communicate(timeout=120)
    except subprocess.TimeoutExpired:
        proc.kill()
        out, _ = proc.communicate()

    marks = {}
    oks = {}
    for line in out.splitlines():
        m = MARK_RE.search(line)
        if m:
            marks[m.group(1)] = int(m.group(2))
            if m.group(3) is not None:
                oks[m.group(1)] = int(m.group(3))

    if not all(k in marks for k in MARKERS):
        print(c(Colors.RED, "  ✗ missing markers in Renode output:"))
        print(out[-2000:])
        return None

    return {
        "config_parse": marks["cfg_mid"] - marks["cfg_begin"],
        "config_serialize": marks["cfg_end"] - marks["cfg_mid"],
        "config_total": marks["cfg_end"] - marks["cfg_begin"],
        "rpc_parse": marks["rpc_mid"] - marks["rpc_begin"],
        "rpc_serialize": marks["rpc_end"] - marks["rpc_mid"],
        "rpc_total": marks["rpc_end"] - marks["rpc_begin"],
        "config_ok": oks.get("cfg_end", 0),
        "rpc_ok": oks.get("rpc_end", 0),
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--renode", default=os.environ.get("RENODE", DEFAULT_RENODE),
                    help="Path to the renode binary (or set $RENODE)")
    args = ap.parse_args()

    renode = args.renode
    if not Path(renode).exists():
        print(c(Colors.RED, f"Renode not found at: {renode}"))
        print("Pass --renode /path/to/renode or set $RENODE.")
        sys.exit(1)

    build_dir = SCRIPT_DIR / ".renode_build"
    build_dir.mkdir(exist_ok=True)

    print(c(Colors.GREEN, "=== Renode Instruction-Count Benchmark (Cortex-M7) ==="))
    print(c(Colors.BLUE, f"Renode:   {renode}"))
    print(c(Colors.BLUE, f"Compiler: {CC} ({' / '.join(OPT_LEVELS)}, matches build.py)"))
    print()

    # results[opt][lib_name] = {config_parse, config_serialize, ...}
    results: Dict[str, Dict[str, Dict]] = {opt: {} for opt in OPT_LEVELS}
    for opt in OPT_LEVELS:
        print(c(Colors.YELLOW, f"########## {opt} ##########"))
        for lib in LIBRARIES:
            print(c(Colors.YELLOW, f"[{lib.name} {opt}]"))
            elf = build_elf(lib, opt, build_dir)
            if elf is None:
                continue
            res = measure(renode, elf, build_dir)
            if res is None:
                continue
            flag = "" if (res["config_ok"] and res["rpc_ok"]) else c(Colors.RED, "  ⚠ a parse returned false!")
            print(f"  config: parse {res['config_parse']:>8} + serialize {res['config_serialize']:>8} = {res['config_total']:>8}")
            print(f"  rpc:    parse {res['rpc_parse']:>8} + serialize {res['rpc_serialize']:>8} = {res['rpc_total']:>8}"
                  f"   (ok: cfg={res['config_ok']} rpc={res['rpc_ok']}){flag}")
            results[opt][lib.name] = res
            print()

    if not any(results.values()):
        print(c(Colors.RED, "No results."))
        sys.exit(1)

    # Per opt level: two tables (config + rpc), each split parse / serialize / total.
    name_w = max(len(lib.name) for lib in LIBRARIES)

    def table(opt_results, title, prefix):
        print(f"  {title}: instructions (parse | serialize | total)")
        print(f"  {'Library':<{name_w}}   {'parse':>10}   {'serialize':>10}   {'total':>10}")
        print("  " + "-" * (name_w + 40))
        for name, r in sorted(opt_results.items(), key=lambda kv: kv[1][f"{prefix}_total"]):
            print(f"  {name:<{name_w}}   {r[f'{prefix}_parse']:>10,}   "
                  f"{r[f'{prefix}_serialize']:>10,}   {r[f'{prefix}_total']:>10,}")
        print()

    for opt in OPT_LEVELS:
        if not results[opt]:
            continue
        print(c(Colors.GREEN, f"=== Summary {opt} ==="))
        table(results[opt], "EmbeddedConfig", "config")
        table(results[opt], "RpcCommand", "rpc")

    print("Note: functional simulation — instruction counts (deterministic), not clock cycles.")
    print("Every library serializes the full fixed-size arrays (16 motors/16 sensors,")
    print("4 targets/8 params) for equivalent work. 'parse' includes validation.")

    out_file = SCRIPT_DIR / "results_instructions_arm_cortex-m7.json"
    out_file.write_text(json.dumps({
        "platform": "ARM Cortex-M7 (Renode functional sim)",
        "metric": "executed_instructions",
        "opt_levels": OPT_LEVELS,
        "results": results,
    }, indent=2))
    print()
    print(c(Colors.BLUE, f"Results saved to: {out_file}"))


if __name__ == "__main__":
    main()

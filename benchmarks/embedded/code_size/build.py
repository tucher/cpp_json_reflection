#!/usr/bin/env python3
"""
Build script for ARM Cortex-M7 embedded binary size benchmark
Measures code footprint of different JSON libraries on a realistic embedded target
"""

import argparse
import subprocess
import sys
import shutil
from dataclasses import dataclass
from pathlib import Path
from typing import List, Dict, Optional
import urllib.request


# ============================================================================
# Configuration
# ============================================================================

@dataclass
class TargetPlatform:
    """Target platform configuration"""
    name: str
    compiler_prefix: str
    build_configs: List['BuildConfig']
    linker_specs: List[str] = None  # Additional linker specs
    
    def __post_init__(self):
        if self.linker_specs is None:
            self.linker_specs = []


@dataclass
class Library:
    """Configuration for a library to benchmark"""
    name: str
    source_file: str
    description: str
    dependencies: List[str] = None  # URLs or file paths to download
    extra_flags: List[str] = None   # Per-library compile flags (e.g. -fpermissive)

    def __post_init__(self):
        if self.dependencies is None:
            self.dependencies = []
        if self.extra_flags is None:
            self.extra_flags = []


@dataclass
class BuildConfig:
    """Build configuration (optimization level, flags)"""
    name: str
    opt_flags: List[str]


# Embedded-friendly flags (common across all platforms)
EMBEDDED_FLAGS = [
    "-Wall",                             # Enable all warnings
    "-fno-exceptions",
    "-fno-rtti",
    "-ffunction-sections",
    "-fdata-sections",
    "-DNDEBUG",                          # Disable assertions
    "-fno-unwind-tables",                # Remove exception unwinding tables
    "-fno-asynchronous-unwind-tables",   # Remove async unwind info
]

# ============================================================================
# Target Platform Definitions
# ============================================================================

# ARM Cortex-M Platform
ARM_CORTEX_M = TargetPlatform(
    name="ARM Cortex-M",
    compiler_prefix="arm-none-eabi-",
    linker_specs=["-specs=nano.specs", "-specs=nosys.specs"],
    build_configs=[
        # BuildConfig("M7 -O3", ["-O3", "-flto",
        #     "-mcpu=cortex-m7",
        #     "-mthumb",
        #     "-mfloat-abi=hard",
        #     "-mfpu=fpv5-d16",
        #     "-fno-threadsafe-statics",
        # ]),
        BuildConfig("M7 -Os", ["-Os", "-flto",
            "-mcpu=cortex-m7",
            "-mthumb",
            "-mfloat-abi=hard",
            "-mfpu=fpv5-d16",
            "-fno-threadsafe-statics",
        ]),
        # BuildConfig("M0+ -O3", ["-O3", "-flto",
        #     "-mcpu=cortex-m0plus",
        #     "-mthumb",
        #     "-mfloat-abi=soft",
        #     "-fno-threadsafe-statics",
        # ]),
        BuildConfig("M0+ -Os", ["-Os", "-flto",
            "-mcpu=cortex-m0plus",
            "-mthumb",
            "-mfloat-abi=soft",
            "-fno-threadsafe-statics",
        ]),
    ]
)

# ESP32 Platform (Xtensa LX6)
ESP32 = TargetPlatform(
    name="ESP32 (Xtensa LX6)",
    compiler_prefix="xtensa-esp-elf-",
    linker_specs=[],  # ESP32 doesn't use nano.specs
    build_configs=[
        # BuildConfig("ESP32 -O3", ["-O3", "-flto",
        #     "-mlongcalls",           # Required for Xtensa
        #     "-mtext-section-literals",  # Place literals in .text
        #     "-fno-threadsafe-statics",  # Reduce code size
        # ]),
        BuildConfig("ESP32 -Os", ["-Os", "-flto",
            "-mlongcalls",
            "-mtext-section-literals",
            "-fno-threadsafe-statics",  # Reduce code size
        ]),
    ]
)

# AVR ATmega Platform (8-bit)
AVR_ATMEGA = TargetPlatform(
    name="AVR ATmega2560",
    compiler_prefix="avr-",
    linker_specs=[],
    build_configs=[
        # BuildConfig("ATmega -O3", ["-O3", "-flto",
        #     "-I/libstdcpp",
        #     "-mmcu=atmega2560",
        #     "-g0",
        #     "-fno-threadsafe-statics",
        # ]),
        BuildConfig("ATmega -Os", ["-Os", "-flto",
            "-I/libstdcpp",
            "-mmcu=atmega2560",
            "-g0",
            "-fno-threadsafe-statics",
        ]),
    ]
)

# ============================================================================
# Libraries to Benchmark
# ============================================================================

def get_libraries_for_platform(platform: TargetPlatform) -> List[Library]:
    """Get list of libraries to benchmark for the given platform"""
    libraries = [
        Library(
            name="JsonFusion",
            source_file="parse_config.cpp",
            description="JsonFusion with in-house float parser",
        ),
        Library(
            name="ArduinoJson",
            source_file="parse_config_arduinojson.cpp",
            description="ArduinoJson v7.4.3",
            dependencies=["https://github.com/bblanchon/ArduinoJson/releases/download/v7.4.3/ArduinoJson-v7.4.3.h"],
        ),
        Library(
            name="cJSON",
            source_file="parse_config_cjson.cpp",
            description="cJSON - lightweight JSON parser in C",
            dependencies=["git:https://github.com/DaveGamble/cJSON.git"],
        ),
        # Library(
        #     name="jsmn",
        #     source_file="parse_config_jsmn.cpp",
        #     description="jsmn - minimalist JSON tokenizer",
        #     dependencies=["git:https://github.com/zserge/jsmn.git"],
        # )
    ]
    
    # Glaze only supported on ARM Cortex-M (not AVR or ESP32) Esp32 build is pulling deps on atomics via std::chrono
    # ESP32: uses unimplemented atomic operations
    # AVR: 8-bit architecture not supported
    if True or platform.compiler_prefix == "arm-none-eabi-":
        libraries.append(
            Library(
                name="Glaze(with embedded-friendly config)",
                source_file="parse_config_glaze.cpp",
                description="Glaze - probably the fastest JSON parser in C++",
                dependencies=[],
            )
        )
    if platform.compiler_prefix != "avr-":
        libraries.append(
            Library(
                name="yajl",
                source_file="parse_config_yajl.cpp",
                description="yajl - event-driven (SAX) JSON parser in C",
                dependencies=["git:https://github.com/lloyd/yajl.git"],
                # yajl's C sources are unity-included into the C++ TU. They use
                # C-isms that are hard errors in C++ (implicit void*->T* casts,
                # int->enum). These diagnostics are tagged [-fpermissive]; the
                # flag downgrades them to warnings without changing codegen, so
                # the measured .text size is unaffected.
                extra_flags=["-fpermissive"],
            )
        )
        libraries.append(
            Library(
                name="JsonFusion CBOR",
                source_file="parse_config_cbor.cpp",
                description="JsonFusion CBOR parser",
            )
        )
    return libraries

# Linker warning patterns to filter out
LINKER_WARNING_FILTERS = [
    "is not implemented and will always fail",
    "the message above does not take linker garbage collection into account",
    "thumb/v7e-m+dp/hard/libc_nano.a",
]


# ============================================================================
# Colors
# ============================================================================

class Colors:
    RED = '\033[0;31m'
    GREEN = '\033[0;32m'
    YELLOW = '\033[1;33m'
    BLUE = '\033[0;34m'
    NC = '\033[0m'  # No Color
    
    @staticmethod
    def red(text): return f"{Colors.RED}{text}{Colors.NC}"
    
    @staticmethod
    def green(text): return f"{Colors.GREEN}{text}{Colors.NC}"
    
    @staticmethod
    def yellow(text): return f"{Colors.YELLOW}{text}{Colors.NC}"
    
    @staticmethod
    def blue(text): return f"{Colors.BLUE}{text}{Colors.NC}"


# ============================================================================
# Build System
# ============================================================================

class EmbeddedBenchmark:
    def __init__(self, script_dir: Path, platform: TargetPlatform):
        self.script_dir = script_dir
        self.project_root = script_dir.parent.parent.parent
        self.platform = platform
        self.libraries = get_libraries_for_platform(platform)
        self.includes = [
            f"-I{self.project_root}/include",
            f"-I{self.script_dir}",
            f"-I{self.script_dir}/libs",  # For downloaded headers (ArduinoJson)
            f"-I{self.script_dir}/libs/glaze/include",  # For Glaze library
            f"-I{self.script_dir}/libs/cJSON",  # For cJSON library
            f"-I{self.script_dir}/libs/jsmn",  # For jsmn library
            f"-I{self.script_dir}/libs/yajl/include",  # For yajl public headers (<yajl/...>)
        ]
        self.version_info = {}  # Store version/commit info for each library
        self.results: Dict[str, Dict[str, int]] = {}  # config -> {lib: text_size}
    
    def clean(self):
        """Remove all build artifacts"""
        print(Colors.yellow("=== Cleaning Build Artifacts ==="))
        
        patterns = ["*.o", "*.elf", "*.map"]
        removed_count = 0
        
        for pattern in patterns:
            for file in self.script_dir.glob(pattern):
                # print(f"Removing {file.name}")
                file.unlink()
                removed_count += 1
        
        if removed_count == 0:
            print("No build artifacts to remove")
        else:
            print(Colors.green(f"✓ Removed {removed_count} file(s)"))
        
        print()
    
    def download_dependencies(self):
        """Download required library headers"""
        print(Colors.green("=== Checking Dependencies ==="))
        print()
        
        # Ensure libs directory exists
        libs_dir = self.script_dir / "libs"
        libs_dir.mkdir(exist_ok=True)
        
        for lib in self.libraries:
            # Special handling for Glaze - clone entire repository
            if lib.name == "Glaze(with embedded-friendly config)":
                glaze_dir = libs_dir / "glaze"
                if glaze_dir.exists():
                    print(f"✓ Glaze library already exists at {glaze_dir}")
                else:
                    print("Downloading Glaze library from GitHub...")
                    try:
                        # Clone the repository
                        result = subprocess.run(
                            ["git", "clone", "--depth", "1", 
                             "https://github.com/stephenberry/glaze.git", 
                             str(glaze_dir)],
                            capture_output=True,
                            text=True
                        )
                        if result.returncode != 0:
                            print(Colors.red(f"✗ Failed to clone Glaze: {result.stderr}"))
                            sys.exit(1)
                        print(Colors.green(f"✓ Downloaded Glaze library"))
                    except Exception as e:
                        print(Colors.red(f"✗ Failed to download Glaze: {e}"))
                        sys.exit(1)
                continue
            
            for dep in lib.dependencies:
                if dep.startswith("git:"):
                    # Git clone dependency
                    git_url = dep[4:]  # Remove "git:" prefix
                    repo_name = git_url.rstrip('/').split('/')[-1].replace('.git', '')
                    repo_dir = libs_dir / repo_name
                    
                    if repo_dir.exists():
                        print(f"✓ {repo_name} repository already exists")
                    else:
                        print(f"Cloning {repo_name} from {git_url}...")
                        try:
                            result = subprocess.run(
                                ["git", "clone", "--depth", "1", git_url, str(repo_dir)],
                                capture_output=True,
                                text=True,
                                check=True
                            )
                            print(Colors.green(f"✓ Cloned {repo_name}"))
                        except subprocess.CalledProcessError as e:
                            print(Colors.red(f"✗ Failed to clone {repo_name}: {e.stderr}"))
                            sys.exit(1)
                
                elif dep.startswith("http"):
                    # HTTP download dependency
                    filename = Path(dep).name
                    dest_path = libs_dir / filename

                    if dest_path.exists():
                        print(f"✓ {filename} already exists")
                    else:
                        print(f"Downloading {filename}...")
                        try:
                            urllib.request.urlretrieve(dep, dest_path)
                            print(Colors.green(f"✓ Downloaded {filename}"))
                        except Exception as e:
                            print(Colors.red(f"✗ Failed to download {filename}: {e}"))
                            sys.exit(1)

                    # Special handling for ArduinoJson - create ArduinoJson.h from versioned file
                    if "ArduinoJson" in filename and filename != "ArduinoJson.h":
                        arduino_header = libs_dir / "ArduinoJson.h"
                        if not arduino_header.exists():
                            print(f"Creating ArduinoJson.h from {filename}...")
                            try:
                                shutil.copy(dest_path, arduino_header)
                                print(Colors.green(f"✓ Created ArduinoJson.h"))
                            except Exception as e:
                                print(Colors.red(f"✗ Failed to create ArduinoJson.h: {e}"))
                                sys.exit(1)
                        else:
                            print(f"✓ ArduinoJson.h already exists")

            # Special handling for yajl - assemble the include/yajl/ header dir.
            # yajl normally builds via CMake, which copies its public headers into
            # <build>/include/yajl/ and generates yajl_version.h from a template.
            # We reproduce that here so the single-TU unity build can resolve the
            # <yajl/...> includes without running CMake.
            if lib.name == "yajl":
                self.setup_yajl_headers(libs_dir / "yajl")
        print()

    def setup_yajl_headers(self, yajl_dir: Path):
        """Assemble libs/yajl/include/yajl/ the way yajl's CMake build would.

        Copies the public API headers into an include/yajl/ directory and
        synthesizes yajl_version.h from the .cmake template (reading the version
        numbers out of the top-level CMakeLists.txt).
        """
        import re

        src_api = yajl_dir / "src" / "api"
        inc_dir = yajl_dir / "include" / "yajl"
        version_header = inc_dir / "yajl_version.h"

        if version_header.exists():
            print(f"✓ yajl headers already assembled")
            return

        inc_dir.mkdir(parents=True, exist_ok=True)

        # Copy the public headers
        for header in ("yajl_common.h", "yajl_parse.h", "yajl_gen.h", "yajl_tree.h"):
            src = src_api / header
            if src.exists():
                shutil.copy(src, inc_dir / header)

        # Read version numbers from the top-level CMakeLists.txt
        versions = {"YAJL_MAJOR": "0", "YAJL_MINOR": "0", "YAJL_MICRO": "0"}
        cmake_lists = yajl_dir / "CMakeLists.txt"
        if cmake_lists.exists():
            text = cmake_lists.read_text()
            for key in versions:
                m = re.search(rf"SET\s*\(\s*{key}\s+(\d+)\s*\)", text)
                if m:
                    versions[key] = m.group(1)

        # Generate yajl_version.h from the .cmake template
        template = (src_api / "yajl_version.h.cmake").read_text()
        for key, val in versions.items():
            template = template.replace("${" + key + "}", val)
        version_header.write_text(template)
        print(Colors.green(f"✓ Assembled yajl headers (v{versions['YAJL_MAJOR']}.{versions['YAJL_MINOR']}.{versions['YAJL_MICRO']})"))
    
    def compile(self, lib: Library, config: BuildConfig) -> Path:
        """Compile source to object file"""
        output_o = self.script_dir / f"parse_config_{lib.name.lower()}_{config.name}.o"
        source = self.script_dir / lib.source_file
        
        cmd = [
            f"{self.platform.compiler_prefix}g++",
            "-std=c++23",
            *EMBEDDED_FLAGS,
            *config.opt_flags,
            *lib.extra_flags,
            *self.includes,
            "-c", str(source),
            "-o", str(output_o),
        ]
        
        print("Compiling to object file...")
        result = subprocess.run(cmd, capture_output=True, text=True)
        
        if result.returncode != 0:
            print(Colors.red(f"✗ Compilation failed"))
            print(result.stderr)
            sys.exit(1)
        print(result.stderr)
        print(result.stdout)
        return output_o
    
    def link(self, lib: Library, config: BuildConfig, obj_file: Path) -> Path:
        """Link object file to ELF executable"""
        output_elf = self.script_dir / f"parse_config_{lib.name.lower()}_{config.name}.elf"
        map_file = self.script_dir / f"parse_config_{lib.name.lower()}_{config.name}.map"
        
        cmd = [
            f"{self.platform.compiler_prefix}g++",
            *EMBEDDED_FLAGS,
            *config.opt_flags,
            *self.platform.linker_specs,  # Platform-specific linker specs
            "-Wl,--gc-sections",
            f"-Wl,-Map={map_file}",
            str(obj_file),
            "-o", str(output_elf),
        ]
        
        print("Linking to ELF...")
        result = subprocess.run(cmd, capture_output=True, text=True)
        
        # Filter out expected warnings
        stderr_lines = result.stderr.split('\n')
        filtered_stderr = [
            line for line in stderr_lines
            if not any(pattern in line for pattern in LINKER_WARNING_FILTERS)
        ]
        if filtered_stderr and any(filtered_stderr):
            print('\n'.join(filtered_stderr))
        
        if result.returncode != 0:
            print(Colors.red(f"✗ Linking failed"))
            sys.exit(1)
        
        print(Colors.green(f"✓ Built: {output_elf.name}"))
        return output_elf
    
    def verify_symbols(self, elf_file: Path) -> bool:
        """Verify that key parsing symbols are present in the binary"""
        print("=== Symbol Verification ===")
        
        # Get all symbols
        nm_result = subprocess.run(
            [f"{self.platform.compiler_prefix}gcc-nm", str(elf_file)],
            capture_output=True, text=True
        )
        
        symbols = nm_result.stdout.lower()
        
        # Check for critical symbols that should be present
        critical_patterns = [
            "parse",           # Should have parse functions
            "json",            # Should have json-related symbols
            "embeddedconfig",  # Should have our model type
        ]
        
        missing = []
        found = []
        for pattern in critical_patterns:
            if pattern in symbols:
                found.append(pattern)
            else:
                missing.append(pattern)
        
        if found:
            print(Colors.green(f"✓ Found symbols: {', '.join(found)}"))
        if missing:
            print(Colors.yellow(f"⚠ Missing patterns: {', '.join(missing)}"))
        
        # Check if binary is suspiciously small (might be optimized away)
        file_size = elf_file.stat().st_size
        if file_size < 1024:  # Less than 1KB is suspicious
            print(Colors.red(f"⚠ Warning: ELF file is very small ({file_size} bytes) - code might be optimized away!"))
            return False
        
        print()
        return len(found) > 0
    
    def analyze_size(self, elf_file: Path) -> int:
        """Analyze ELF file and return .text section size"""
        print()
        print("=== Size Analysis ===")
        
        # Verify symbols first
        self.verify_symbols(elf_file)
        
        # Overall size
        result = subprocess.run(
            [f"{self.platform.compiler_prefix}size", str(elf_file)],
            capture_output=True, text=True
        )
        print(result.stdout)
        
        # Detailed by section
        print("=== Detailed Size (by section) ===")
        result = subprocess.run(
            [f"{self.platform.compiler_prefix}size", "-A", str(elf_file)],
            capture_output=True, text=True
        )
        for line in result.stdout.split('\n'):
            if any(x in line for x in ["section", ".text", ".rodata", ".data", ".bss", "Total"]):
                print(line)
        
        # Top symbols
        print()
        top_count = 100
        # print(f"=== Top {top_count} Symbols by Size ===")
        nm_result = subprocess.run(
            [f"{self.platform.compiler_prefix}gcc-nm", "--size-sort", "--reverse-sort", "--radix=d", str(elf_file)],
            capture_output=True, text=True
        )
        # Filter out BSS symbols and take top 10
        lines = [line for line in nm_result.stdout.split('\n') if ' B ' not in line and ' b ' not in line]

        total_bytes = 0
        for s in lines:
            if "__" in s:
                continue
            parts = s.split()
            if parts and parts[0].isdigit():
                count = int(parts[0])
                total_bytes += count

        top_symbols = '\n'.join(lines[:top_count])
        
        # Demangle
        demangle_result = subprocess.run(
            [f"{self.platform.compiler_prefix}c++filt"],
            input=top_symbols, capture_output=True, text=True
        )
        # print(demangle_result.stdout)
        # print(f"\n\tTotal top {top_count} size: {total_bytes}\n")
        
        # Extract .text size
        result = subprocess.run(
            [f"{self.platform.compiler_prefix}size", str(elf_file)],
            capture_output=True, text=True
        )
        lines = result.stdout.strip().split('\n')
        if len(lines) >= 2:
            # Second line has the actual numbers
            text_size = int(lines[1].split()[0])
            return text_size
        
        return 0
    
    def build_library(self, lib: Library, config: BuildConfig):
        """Build a single library with given config"""
        print(Colors.green(f"[{self.libraries.index(lib)+1}/{len(self.libraries)}] Building {lib.name}..."))
        print(f"Description: {lib.description}")
        print()
        
        # Compile
        obj_file = self.compile(lib, config)
        
        # Link
        elf_file = self.link(lib, config, obj_file)
        
        # Analyze
        text_size = self.analyze_size(elf_file)
        
        # Store result
        if config.name not in self.results:
            self.results[config.name] = {}
        self.results[config.name][lib.name] = text_size
        
        print()
        print()
    
    def compare_results(self, config_name: str):
        """Compare all libraries for a given config"""
        if config_name not in self.results:
            return
        
        sizes = self.results[config_name]
        if len(sizes) < 2:
            return
        
        print(Colors.yellow(f"=== Comparison ({config_name}) ==="))
        
        # Print all sizes
        for lib_name, size in sizes.items():
            print(f"{lib_name:20} .text: {size:7} bytes")
        
        # Compare JsonFusion against each other library
        if "JsonFusion" in sizes:
            jf_size = sizes["JsonFusion"]
            print()
            for lib_name, lib_size in sizes.items():
                if lib_name == "JsonFusion":
                    continue
                
                diff = jf_size - lib_size
                pct = abs(diff) * 100 // lib_size
                
                if diff < 0:
                    print(Colors.green(f"→ JsonFusion is {-diff} bytes ({pct}%) smaller than {lib_name}"))
                elif diff > 0:
                    print(Colors.red(f"→ JsonFusion is {diff} bytes ({pct}%) larger than {lib_name}"))
                else:
                    print(f"→ JsonFusion and {lib_name} are the same size")
        
        print()
        print("---")
        print()
    
    def print_summary(self):
        """Print final summary"""
        print(Colors.green("=== Summary ==="))
        print()
        
        # List all built files
        print("Built ELF files:")
        elf_files = sorted(self.script_dir.glob("*.elf"))
        for elf in elf_files:
            size_kb = elf.stat().st_size / 1024
            print(f"  {elf.name:<50} {size_kb:6.1f} KB")
        
        print()
        
        # Results table with versions
        if self.results:
            print("Code Size Summary (.text section):")
            print()
            
            # Header
            config_names = list(self.results.keys())
            lib_names = list(self.results[config_names[0]].keys())
            max_lib_name_width = max(len(lib_name) for lib_name in lib_names)
            
            print(f"{'Library':<{max_lib_name_width}}", end="")
            for config_name in config_names:
                print(f" {config_name:<20}", end="")
            print(f" {'Version':<12}")
            print("-" * (max_lib_name_width + 20 * len(config_names) + 13))
            
            # Data
            for lib_name in lib_names:
                print(f"{lib_name:<{max_lib_name_width}}", end="")
                for config_name in config_names:
                    size = self.results[config_name].get(lib_name, 0)
                    size_kb = size / 1024
                    print(f" {size_kb:6.1f} KB          ", end="")
                version = self.version_info.get(lib_name, "unknown")
                print(f" {version:<12}")
        
        print()
        print(Colors.green("Done!"))
    
    def get_library_version(self, lib: Library) -> str:
        """Get version information for a library (git commit or version from URL)"""
        lib_name = lib.name
        
        # JsonFusion and CBOR - get git commit (same codebase)
        if "JsonFusion" in lib_name:
            try:
                result = subprocess.run(
                    ["git", "rev-parse", "--short=8", "HEAD"],
                    cwd=self.project_root,
                    capture_output=True,
                    text=True,
                    check=True
                )
                return result.stdout.strip()
            except subprocess.CalledProcessError:
                return "unknown"
        
        # Glaze - get git commit from submodule
        elif "Glaze" in lib_name or "glaze" in lib_name.lower():
            glaze_dir = self.script_dir / "libs" / "glaze"
            if glaze_dir.exists():
                try:
                    result = subprocess.run(
                        ["git", "rev-parse", "--short=8", "HEAD"],
                        cwd=glaze_dir,
                        capture_output=True,
                        text=True,
                        check=True
                    )
                    return result.stdout.strip()
                except subprocess.CalledProcessError:
                    return "unknown"
            return "unknown"
        
        # cJSON - get git commit
        elif lib_name == "cJSON":
            cjson_dir = self.script_dir / "libs" / "cJSON"
            if cjson_dir.exists():
                try:
                    result = subprocess.run(
                        ["git", "rev-parse", "--short=8", "HEAD"],
                        cwd=cjson_dir,
                        capture_output=True,
                        text=True,
                        check=True
                    )
                    return result.stdout.strip()
                except subprocess.CalledProcessError:
                    return "unknown"
            return "unknown"
        
        # yajl - get git commit
        elif lib_name == "yajl":
            yajl_dir = self.script_dir / "libs" / "yajl"
            if yajl_dir.exists():
                try:
                    result = subprocess.run(
                        ["git", "rev-parse", "--short=8", "HEAD"],
                        cwd=yajl_dir,
                        capture_output=True,
                        text=True,
                        check=True
                    )
                    return result.stdout.strip()
                except subprocess.CalledProcessError:
                    return "unknown"
            return "unknown"

        # jsmn - get git commit
        elif lib_name == "jsmn":
            jsmn_dir = self.script_dir / "libs" / "jsmn"
            if jsmn_dir.exists():
                try:
                    result = subprocess.run(
                        ["git", "rev-parse", "--short=8", "HEAD"],
                        cwd=jsmn_dir,
                        capture_output=True,
                        text=True,
                        check=True
                    )
                    return result.stdout.strip()
                except subprocess.CalledProcessError:
                    return "unknown"
            return "unknown"
        
        # Other libraries - extract version from dependency URLs
        elif lib.dependencies:
            for dep in lib.dependencies:
                # ArduinoJson - extract version
                if "ArduinoJson" in dep:
                    import re
                    match = re.search(r'v(\d+\.\d+\.\d+)', dep)
                    if match:
                        return match.group(0)  # Return with 'v' prefix
            return "downloaded"
        
        return "unknown"
    
    def collect_version_info(self):
        """Collect version information for all libraries"""
        print(Colors.blue("Collecting version information..."))
        for lib in self.libraries:
            version = self.get_library_version(lib)
            self.version_info[lib.name] = version
            print(f"  {lib.name}: {version}")
        print()
    
    def save_results_json(self):
        """Save results to JSON file for automated processing"""
        import json
        
        output = {
            "platform": self.platform.name,
            "compiler": f"{self.platform.compiler_prefix}g++",
            "versions": self.version_info,
            "results": {}
        }
        
        for config_name in self.results:
            output["results"][config_name] = {}
            for lib_name, size in self.results[config_name].items():
                output["results"][config_name][lib_name] = {
                    "bytes": size,
                    "kb": round(size / 1024, 1)
                }
        
        output_file = self.script_dir / f"results_{self.platform.name.lower().replace(' ', '_').replace('(', '').replace(')', '')}.json"
        with open(output_file, 'w') as f:
            json.dump(output, f, indent=2)
        
        print(Colors.blue(f"Results saved to: {output_file}"))
        print()
    
    def run(self, clean: bool = True, save_json: bool = True):
        """Main build process"""
        print(Colors.green("=== Embedded Binary Size Benchmark ==="))
        print(Colors.blue(f"Target Platform: {self.platform.name}"))
        print(Colors.blue(f"Compiler: {self.platform.compiler_prefix}g++"))
        print(f"Comparing: {', '.join(lib.name for lib in self.libraries)}")
        print()
        
        # Clean old artifacts if requested
        if clean:
            self.clean()
        
        # Download dependencies
        self.download_dependencies()
        
        # Collect version information
        self.collect_version_info()
        
        # Build all combinations
        for config in self.platform.build_configs:
            print(Colors.yellow("=" * 60))
            print(Colors.yellow(f"Configuration: {config.name}"))
            print(Colors.yellow(f"Flags: {' '.join(config.opt_flags)}"))
            print(Colors.yellow("=" * 60))
            print()
            
            for lib in self.libraries:
                self.build_library(lib, config)
            
            # Compare results for this config
            self.compare_results(config.name)
        
        # Print summary
        self.print_summary()
        
        # Save results to JSON for automated processing
        if save_json:
            self.save_results_json()
        
        self.clean()


# ============================================================================
# Main
# ============================================================================

def main():
    parser = argparse.ArgumentParser(
        description="Build and benchmark JSON libraries for embedded platforms",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s                             # Build for ARM Cortex-M (default)
  %(prog)s --platform esp32            # Build for ESP32
  %(prog)s --platform avr              # Build for AVR ATmega2560
  %(prog)s --no-clean                  # Build without cleaning first
  %(prog)s --clean-only                # Just clean, don't build
        """
    )
    parser.add_argument(
        "--platform",
        choices=["arm", "esp32", "avr"],
        default="arm",
        help="Target platform (default: arm)"
    )
    parser.add_argument(
        "--no-clean",
        action="store_false",
        dest="clean",
        help="Don't remove old build artifacts before building (default: clean first)"
    )
    parser.add_argument(
        "--clean-only",
        action="store_true",
        help="Only clean build artifacts, don't build"
    )
    parser.add_argument(
        "--verify",
        action="store_true",
        help="Run additional verification checks (disassembly, detailed symbols)"
    )
    
    args = parser.parse_args()
    
    # Select platform
    platform_map = {
        "arm": ARM_CORTEX_M,
        "esp32": ESP32,
        "avr": AVR_ATMEGA,
    }
    selected_platform = platform_map[args.platform]
    
    script_dir = Path(__file__).parent.resolve()
    benchmark = EmbeddedBenchmark(script_dir, selected_platform)
    
    try:
        # Clean-only mode
        if args.clean_only:
            benchmark.clean()
            return
        
        # Normal build (with or without cleaning)
        benchmark.run(clean=args.clean)
    except KeyboardInterrupt:
        print()
        print(Colors.yellow("Interrupted by user"))
        sys.exit(1)
    except Exception as e:
        print(Colors.red(f"Error: {e}"))
        import traceback
        traceback.print_exc()
        sys.exit(1)


if __name__ == "__main__":
    main()


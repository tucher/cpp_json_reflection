// Generic runnable harness for the Renode instruction-count benchmark.
//
// It is linked together with one library's parse_config_*.cpp (that file's
// main() is renamed away at compile time via -Dmain=...). Every JSON library
// exposes the same two entry points:
//
//     extern "C" bool parse_config(const char* data, size_t size);
//     extern "C" bool parse_rpc_command(const char* data, size_t size);
//
// main() brackets each call with empty marker functions. The Renode script
// hooks those markers by symbol address and reads cpu.ExecutedInstructions at
// each one; the difference is the instruction count for that parse + validate +
// serialize (the same round trip the code-size benchmark compiles). The parse
// result is passed into the *_end marker so the script can read r0 and confirm
// the parse actually succeeded (ok=1) rather than measuring a failure path.
//
// NOTE: this firmware is meant to *run* (unlike the size-only ELFs), so it adds
// two things the bare benchmark never needed: it enables the Cortex-M7 FPU (the
// hard-float build emits FP instructions, but the default startup never turns
// the FPU on) and provides a bump-allocator _sbrk (yajl/cJSON/Glaze call malloc;
// the nosys _sbrk would otherwise fail and the parse would silently return false).

#include "embedded_config.hpp"

#include <cstring>
#include <cstdint>
#include <cstddef>

extern "C" bool parse_config(const char* data, size_t size);
extern "C" bool parse_rpc_command(const char* data, size_t size);

// ---- Samples (compact JSON; validated by the host round-trip test) ----------

static char cfg_buf[8192] =
    "{\"app_name\":\"motorctl\",\"version_major\":2,\"version_minor\":7,"
    "\"network\":{\"name\":\"eth0\",\"address\":\"192.168.0.10\",\"port\":8080,\"enabled\":true},"
    "\"fallback_network_conf\":{\"name\":\"wlan0\",\"address\":\"10.0.0.2\",\"port\":9090,\"enabled\":false},"
    "\"controller\":{\"name\":\"ctrlA\",\"loop_hz\":1000,"
    "\"motors\":["
    "{\"id\":1,\"name\":\"m1\",\"position\":[1.5,2.5,3.5],\"vel_limits\":[10,20,30],\"inverted\":false},"
    "{\"id\":2,\"name\":\"m2\",\"position\":[0,0,0],\"vel_limits\":[1.0,2.0,3.0],\"inverted\":true}],"
    "\"sensors\":[{\"type\":\"imu\",\"model\":\"bmi160\",\"range_min\":-50,\"range_max\":50,\"active\":true}]},"
    "\"logging\":{\"enabled\":true,\"path\":\"/var/log/x\",\"max_files\":5}}";

static char rpc_buf[8192] =
    "{\"command_id\":\"CMD_SET\",\"timestamp_us\":1717000000000000,\"sequence\":42,\"priority\":5,"
    "\"targets\":[{\"device_id\":\"MOTOR_01\",\"subsystem\":\"motor\"},{\"device_id\":\"MOTOR_02\",\"subsystem\":\"x\"}],"
    "\"params\":[{\"key\":\"speed\",\"int_value\":1500},{\"key\":\"ratio\",\"float_value\":0.75},"
    "{\"key\":\"on\",\"bool_value\":true},{\"key\":\"mode\",\"string_value\":\"fast\"}],"
    "\"execution\":{\"timeout_ms\":5000,\"retry_on_failure\":true,\"max_retries\":3},"
    "\"response_config\":{\"callback_url\":\"http://h/cb\",\"acknowledge\":true,\"send_result\":false}}";

// ---- Measurement markers (distinct bodies so they can't be folded together) --

// begin -> mid brackets the parse, mid -> end brackets the serialize. The
// parse_config/parse_rpc_command functions call the *_mid markers between the
// two halves (only under -DJF_PERF_ROUNDTRIP). Distinct nop counts keep the
// linker from folding these empty functions together.
extern "C" __attribute__((used, noinline)) void cfg_begin()     { asm volatile("nop"                 ::: "memory"); }
extern "C" __attribute__((used, noinline)) void cfg_mid()       { asm volatile("nop;nop;nop;nop;nop" ::: "memory"); }
extern "C" __attribute__((used, noinline)) void cfg_end(int ok) { (void)ok; asm volatile("nop;nop"   ::: "memory"); }
extern "C" __attribute__((used, noinline)) void rpc_begin()     { asm volatile("nop;nop;nop"         ::: "memory"); }
extern "C" __attribute__((used, noinline)) void rpc_mid()       { asm volatile("nop;nop;nop;nop;nop;nop" ::: "memory"); }
extern "C" __attribute__((used, noinline)) void rpc_end(int ok) { (void)ok; asm volatile("nop;nop;nop;nop" ::: "memory"); }

// ---- Bump-allocator heap (backs malloc; nosys _sbrk would fail) --------------

static char s_heap[64 * 1024];
extern "C" void* _sbrk(long incr) {
    static size_t off = 0;
    if (off + (size_t)incr > sizeof(s_heap)) return reinterpret_cast<void*>(-1);
    void* p = s_heap + off;
    off += incr;
    return p;
}

int main() {
    // Enable the FPU (CPACR CP10/CP11 = full access) — the default startup does not.
    volatile uint32_t* CPACR = reinterpret_cast<uint32_t*>(0xE000ED88);
    *CPACR |= (0xFu << 20);
    asm volatile("dsb");
    asm volatile("isb");

    // Parse exactly the JSON. Built with -DJF_PERF_ROUNDTRIP, each parse_config
    // serializes into its own scratch buffer (not in place), so no trailing-
    // whitespace padding is needed and the parse measurement carries no noise.
    cfg_begin();
    bool cfg_ok = parse_config(cfg_buf, std::strlen(cfg_buf));
    cfg_end(cfg_ok ? 1 : 0);

    rpc_begin();
    bool rpc_ok = parse_rpc_command(rpc_buf, std::strlen(rpc_buf));
    rpc_end(rpc_ok ? 1 : 0);

    while (1) {}
}

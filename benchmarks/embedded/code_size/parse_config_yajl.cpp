// yajl benchmark implementation
//
// yajl is an event-driven (SAX) JSON parser. Unlike cJSON / ArduinoJson, it
// does NOT build a DOM tree: it streams callbacks (start_map, map_key, string,
// integer, ...) as it walks the text. Consuming it therefore means writing a
// hand-rolled state machine that tracks where we are in the document and routes
// each event into the right field of the target struct.
//
// This is yajl's signature embedded strength (no full-tree allocation), at the
// cost of more consumer code. We validate the same value constraints the model
// documents (range<>, min_items<>, fixed-array sizes, string-length bounds) and
// the top-level required sections; exhaustive per-field presence checking is
// intentionally lighter than the DOM versions, which is itself a fair
// characteristic of streaming parsers.
//
// Serialization back to text uses yajl's generator API (yajl_gen), mirroring the
// parse -> validate -> serialize round trip the other benchmarks perform.

#include <yajl/yajl_parse.h>
#include <yajl/yajl_gen.h>

#include "embedded_config.hpp"

#include <cstring>
#include <cstdlib>
#include <climits>  // yajl_parser.c uses LLONG_MAX/MIN but relies on a transitive
                    // <limits.h>, which C++ headers don't provide on their own

using EC  = embedded_benchmark::EmbeddedConfig;
using Rpc = embedded_benchmark::RpcCommand;

// Global config instance (mirrors the other benchmark files)
EC g_config_yajl;

#ifdef JF_PERF_ROUNDTRIP
// Instruction-benchmark mode: serialize into a dedicated scratch buffer so the
// parse input can be exactly the JSON. The code-size benchmark is unaffected.
static char jf_perf_scratch[16384];
extern "C" void cfg_mid();  // parse/serialize boundary markers (defined in the runner)
extern "C" void rpc_mid();
#endif

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

// Copy a (ptr,len) JSON string into a fixed buffer, failing if it doesn't fit.
static inline bool copy_str(const unsigned char* s, size_t n, char* dst, size_t cap) {
    if (n >= cap) return false;  // must leave room for NUL
    std::memcpy(dst, s, n);
    dst[n] = '\0';
    return true;
}

// ===========================================================================
// EmbeddedConfig SAX consumer
// ===========================================================================

namespace ec_sax {

enum Scope : uint8_t {
    S_ROOT,
    S_NETWORK,
    S_FALLBACK,
    S_CONTROLLER,
    S_MOTORS,     // array
    S_MOTOR,      // object
    S_MOTOR_POS,  // array of double
    S_MOTOR_VEL,  // array of float
    S_SENSORS,    // array
    S_SENSOR,     // object
    S_LOGGING,
    S_IGNORE,     // unknown subtree: keep depth balanced, drop contents
};

struct Frame {
    uint8_t  scope;
    uint16_t idx;  // element counter for array scopes
};

struct Ctx {
    EC*     cfg;
    Frame   stack[24];
    int     sp = -1;
    char    key[48] = {0};  // pending object key
    bool    ok = true;

    // required top-level sections seen (app_name, ver_major, ver_minor,
    // network, controller, logging)
    uint8_t seen = 0;

    uint8_t top()  const { return sp >= 0 ? stack[sp].scope : 0xFF; }
    void push(uint8_t s) { if (sp < 23) stack[++sp] = Frame{s, 0}; else ok = false; }
    void pop()           { if (sp >= 0) --sp; }
};

enum SeenBits : uint8_t {
    SEEN_APP   = 0x01, SEEN_VMAJ = 0x02, SEEN_VMIN = 0x04,
    SEEN_NET   = 0x08, SEEN_CTRL = 0x10, SEEN_LOG  = 0x20,
    SEEN_ALL   = 0x3F,
};

static inline bool keyeq(const Ctx* c, const char* k) { return std::strcmp(c->key, k) == 0; }

static inline EC::Network& net_target(Ctx* c) {
    return (c->top() == S_NETWORK) ? c->cfg->network : *c->cfg->fallback_network_conf;
}
static inline EC::Controller::Motor&  cur_motor(Ctx* c)  { return c->cfg->controller.motors[c->cfg->controller.motors_count - 1]; }
static inline EC::Controller::Sensor& cur_sensor(Ctx* c) { return c->cfg->controller.sensors[c->cfg->controller.sensors_count - 1]; }

#define EC_FAIL() do { c->ok = false; return 0; } while (0)

static int on_start_map(void* ctx) {
    Ctx* c = static_cast<Ctx*>(ctx);
    if (c->sp < 0) { c->push(S_ROOT); return c->ok; }

    switch (c->top()) {
        case S_ROOT:
            if      (keyeq(c, "network"))               { c->seen |= SEEN_NET; c->push(S_NETWORK); }
            else if (keyeq(c, "fallback_network_conf")) { c->cfg->fallback_network_conf.emplace(); c->push(S_FALLBACK); }
            else if (keyeq(c, "controller"))            { c->seen |= SEEN_CTRL; c->push(S_CONTROLLER); }
            else if (keyeq(c, "logging"))               { c->seen |= SEEN_LOG; c->push(S_LOGGING); }
            else                                        { c->push(S_IGNORE); }
            break;
        case S_MOTORS: {
            uint16_t idx = c->stack[c->sp].idx;
            if (idx >= EC::kMaxMotors) EC_FAIL();            // max_items
            c->cfg->controller.motors_count = idx + 1;
            c->push(S_MOTOR);
            break;
        }
        case S_SENSORS: {
            uint16_t idx = c->stack[c->sp].idx;
            if (idx >= EC::kMaxSensors) EC_FAIL();           // max_items
            c->cfg->controller.sensors_count = idx + 1;
            c->push(S_SENSOR);
            break;
        }
        default:
            c->push(S_IGNORE);
            break;
    }
    return c->ok;
}

static int on_end_map(void* ctx) {
    Ctx* c = static_cast<Ctx*>(ctx);
    uint8_t s = c->top();
    c->pop();
    if (c->sp >= 0 && (s == S_MOTOR || s == S_SENSOR)) {
        c->stack[c->sp].idx++;  // advance parent array element counter
    }
    return 1;
}

static int on_start_array(void* ctx) {
    Ctx* c = static_cast<Ctx*>(ctx);
    switch (c->top()) {
        case S_CONTROLLER:
            if      (keyeq(c, "motors"))  c->push(S_MOTORS);
            else if (keyeq(c, "sensors")) c->push(S_SENSORS);
            else                          c->push(S_IGNORE);
            break;
        case S_MOTOR:
            if      (keyeq(c, "position"))   c->push(S_MOTOR_POS);
            else if (keyeq(c, "vel_limits")) c->push(S_MOTOR_VEL);
            else                             c->push(S_IGNORE);
            break;
        default:
            c->push(S_IGNORE);
            break;
    }
    return c->ok;
}

static int on_end_array(void* ctx) {
    Ctx* c = static_cast<Ctx*>(ctx);
    uint16_t idx = (c->sp >= 0) ? c->stack[c->sp].idx : 0;
    switch (c->top()) {
        case S_MOTORS:    if (idx < 1) EC_FAIL(); break;  // min_items<1>
        case S_SENSORS:   if (idx < 1) EC_FAIL(); break;  // min_items<1>
        case S_MOTOR_POS: if (idx < 3) EC_FAIL(); break;  // exactly 3
        case S_MOTOR_VEL: if (idx < 3) EC_FAIL(); break;  // exactly 3
        default: break;
    }
    c->pop();
    return 1;
}

static int on_map_key(void* ctx, const unsigned char* k, size_t n) {
    Ctx* c = static_cast<Ctx*>(ctx);
    if (n >= sizeof(c->key)) n = sizeof(c->key) - 1;
    std::memcpy(c->key, k, n);
    c->key[n] = '\0';
    return 1;
}

// Unified numeric handling: integer & double events both land here so an
// integer literal can still satisfy a double-typed field (e.g. position: [0,0,0]).
static int handle_num(Ctx* c, double d, long long i) {
    switch (c->top()) {
        case S_ROOT:
            if      (keyeq(c, "version_major")) { c->cfg->version_major = static_cast<uint16_t>(i); c->seen |= SEEN_VMAJ; }
            else if (keyeq(c, "version_minor")) { c->cfg->version_minor = static_cast<int>(i);      c->seen |= SEEN_VMIN; }
            break;
        case S_NETWORK:
        case S_FALLBACK:
            if (keyeq(c, "port")) {
                if (i < 0 || i > 65535) EC_FAIL();
                net_target(c).port = static_cast<uint16_t>(i);
            }
            break;
        case S_CONTROLLER:
            if (keyeq(c, "loop_hz")) {
                if (i < 10 || i > 10000) EC_FAIL();          // range<10, 10000>
                c->cfg->controller.loop_hz = static_cast<int>(i);
            }
            break;
        case S_MOTOR:
            if (keyeq(c, "id")) cur_motor(c).id = static_cast<int64_t>(i);
            break;
        case S_MOTOR_POS: {
            uint16_t idx = c->stack[c->sp].idx;
            if (idx >= 3) EC_FAIL();                          // fixed-size array
            if (d < -1000.0 || d > 1000.0) EC_FAIL();         // range<-1000, 1000>
            cur_motor(c).position[idx] = d;
            c->stack[c->sp].idx++;
            break;
        }
        case S_MOTOR_VEL: {
            uint16_t idx = c->stack[c->sp].idx;
            if (idx >= 3) EC_FAIL();
            if (d < -1000.0 || d > 1000.0) EC_FAIL();
            cur_motor(c).vel_limits[idx] = static_cast<float>(d);
            c->stack[c->sp].idx++;
            break;
        }
        case S_SENSOR:
            if (keyeq(c, "range_min")) {
                if (d < -100.0 || d > 100000.0) EC_FAIL();    // range<-100, 100000>
                cur_sensor(c).range_min = static_cast<float>(d);
            } else if (keyeq(c, "range_max")) {
                if (d < -1000.0 || d > 100000.0) EC_FAIL();   // range<-1000, 100000>
                cur_sensor(c).range_max = d;
            }
            break;
        case S_LOGGING:
            if (keyeq(c, "max_files")) c->cfg->logging.max_files = static_cast<uint32_t>(i);
            break;
        default:
            break;
    }
    return c->ok;
}

static int on_integer(void* ctx, long long v) { return handle_num(static_cast<Ctx*>(ctx), static_cast<double>(v), v); }
static int on_double (void* ctx, double v)     { return handle_num(static_cast<Ctx*>(ctx), v, static_cast<long long>(v)); }

static int on_string(void* ctx, const unsigned char* s, size_t n) {
    Ctx* c = static_cast<Ctx*>(ctx);
    bool ok = true;
    switch (c->top()) {
        case S_ROOT:
            if (keyeq(c, "app_name")) { ok = copy_str(s, n, c->cfg->app_name.data(), c->cfg->app_name.size()); c->seen |= SEEN_APP; }
            break;
        case S_NETWORK:
        case S_FALLBACK: {
            EC::Network& net = net_target(c);
            if      (keyeq(c, "name"))    ok = copy_str(s, n, net.name.data(),    net.name.size());
            else if (keyeq(c, "address")) ok = copy_str(s, n, net.address.data(), net.address.size());
            break;
        }
        case S_CONTROLLER:
            if (keyeq(c, "name")) ok = copy_str(s, n, c->cfg->controller.name.data(), c->cfg->controller.name.size());
            break;
        case S_MOTOR:
            if (keyeq(c, "name")) ok = copy_str(s, n, cur_motor(c).name.data(), cur_motor(c).name.size());
            break;
        case S_SENSOR: {
            EC::Controller::Sensor& sn = cur_sensor(c);
            if      (keyeq(c, "type"))  ok = copy_str(s, n, sn.type.data(),  sn.type.size());
            else if (keyeq(c, "model")) ok = copy_str(s, n, sn.model.data(), sn.model.size());
            break;
        }
        case S_LOGGING:
            if (keyeq(c, "path")) ok = copy_str(s, n, c->cfg->logging.path.data(), c->cfg->logging.path.size());
            break;
        default:
            break;
    }
    if (!ok) EC_FAIL();
    return 1;
}

static int on_boolean(void* ctx, int b) {
    Ctx* c = static_cast<Ctx*>(ctx);
    switch (c->top()) {
        case S_NETWORK:
        case S_FALLBACK: if (keyeq(c, "enabled"))  net_target(c).enabled = b;        break;
        case S_MOTOR:    if (keyeq(c, "inverted")) cur_motor(c).inverted = b;        break;
        case S_SENSOR:   if (keyeq(c, "active"))   cur_sensor(c).active = b;         break;
        case S_LOGGING:  if (keyeq(c, "enabled"))  c->cfg->logging.enabled = b;      break;
        default: break;
    }
    return 1;
}

static int on_null(void* /*ctx*/) { return 1; }  // optional field absent

#undef EC_FAIL

static const yajl_callbacks callbacks = {
    on_null,
    on_boolean,
    on_integer,
    on_double,
    nullptr,        // yajl_number: unused, we take integer/double instead
    on_string,
    on_start_map,
    on_map_key,
    on_end_map,
    on_start_array,
    on_end_array,
};

}  // namespace ec_sax

// ---------------------------------------------------------------------------
// EmbeddedConfig serialization (yajl_gen)
// ---------------------------------------------------------------------------

static inline void gen_str(yajl_gen g, const char* s) {
    yajl_gen_string(g, reinterpret_cast<const unsigned char*>(s), std::strlen(s));
}

static void gen_network(yajl_gen g, const EC::Network& net) {
    yajl_gen_map_open(g);
    gen_str(g, "name");    gen_str(g, net.name.data());
    gen_str(g, "address"); gen_str(g, net.address.data());
    gen_str(g, "port");    yajl_gen_integer(g, net.port);
    gen_str(g, "enabled"); yajl_gen_bool(g, net.enabled);
    yajl_gen_map_close(g);
}

static size_t serialize_config_yajl(const EC& cfg, char* buffer, size_t buffer_size) {
    yajl_gen g = yajl_gen_alloc(nullptr);
    if (!g) return 0;

    yajl_gen_map_open(g);

    gen_str(g, "app_name");      gen_str(g, cfg.app_name.data());
    gen_str(g, "version_major"); yajl_gen_integer(g, cfg.version_major);
    gen_str(g, "version_minor"); yajl_gen_integer(g, cfg.version_minor);

    gen_str(g, "network");       gen_network(g, cfg.network);

    if (cfg.fallback_network_conf.has_value()) {
        gen_str(g, "fallback_network_conf");
        gen_network(g, *cfg.fallback_network_conf);
    }

    gen_str(g, "controller");
    yajl_gen_map_open(g);
    gen_str(g, "name");    gen_str(g, cfg.controller.name.data());
    gen_str(g, "loop_hz"); yajl_gen_integer(g, cfg.controller.loop_hz);

    // Round-trip benchmark serializes the full fixed array (see cJSON note).
#ifdef JF_PERF_ROUNDTRIP
    const size_t n_motors = EC::kMaxMotors;
    const size_t n_sensors = EC::kMaxSensors;
#else
    const size_t n_motors = cfg.controller.motors_count;
    const size_t n_sensors = cfg.controller.sensors_count;
#endif

    gen_str(g, "motors");
    yajl_gen_array_open(g);
    for (size_t i = 0; i < n_motors; ++i) {
        const auto& m = cfg.controller.motors[i];
        yajl_gen_map_open(g);
        gen_str(g, "id");   yajl_gen_integer(g, static_cast<long long>(m.id));
        gen_str(g, "name"); gen_str(g, m.name.data());
        gen_str(g, "position");
        yajl_gen_array_open(g);
        for (int k = 0; k < 3; ++k) yajl_gen_double(g, m.position[k]);
        yajl_gen_array_close(g);
        gen_str(g, "vel_limits");
        yajl_gen_array_open(g);
        for (int k = 0; k < 3; ++k) yajl_gen_double(g, static_cast<double>(m.vel_limits[k]));
        yajl_gen_array_close(g);
        gen_str(g, "inverted"); yajl_gen_bool(g, m.inverted);
        yajl_gen_map_close(g);
    }
    yajl_gen_array_close(g);

    gen_str(g, "sensors");
    yajl_gen_array_open(g);
    for (size_t i = 0; i < n_sensors; ++i) {
        const auto& sn = cfg.controller.sensors[i];
        yajl_gen_map_open(g);
        gen_str(g, "type");      gen_str(g, sn.type.data());
        gen_str(g, "model");     gen_str(g, sn.model.data());
        gen_str(g, "range_min"); yajl_gen_double(g, static_cast<double>(sn.range_min));
        gen_str(g, "range_max"); yajl_gen_double(g, sn.range_max);
        gen_str(g, "active");    yajl_gen_bool(g, sn.active);
        yajl_gen_map_close(g);
    }
    yajl_gen_array_close(g);
    yajl_gen_map_close(g);  // controller

    gen_str(g, "logging");
    yajl_gen_map_open(g);
    gen_str(g, "enabled");   yajl_gen_bool(g, cfg.logging.enabled);
    gen_str(g, "path");      gen_str(g, cfg.logging.path.data());
    gen_str(g, "max_files"); yajl_gen_integer(g, cfg.logging.max_files);
    yajl_gen_map_close(g);

    yajl_gen_map_close(g);  // root

    const unsigned char* buf = nullptr;
    size_t len = 0;
    size_t written = 0;
    if (yajl_gen_get_buf(g, &buf, &len) == yajl_gen_status_ok && len < buffer_size) {
        std::memcpy(buffer, buf, len + 1);  // yajl buffer is NUL-terminated
        written = len;
    }
    yajl_gen_free(g);
    return written;
}

// ---------------------------------------------------------------------------
// EmbeddedConfig entry point
// ---------------------------------------------------------------------------

extern "C" __attribute__((used)) bool parse_config(const char* data, size_t size) {
    g_config_yajl.fallback_network_conf.reset();
    g_config_yajl.controller.motors_count = 0;
    g_config_yajl.controller.sensors_count = 0;

    ec_sax::Ctx ctx;
    ctx.cfg = &g_config_yajl;

    yajl_handle h = yajl_alloc(&ec_sax::callbacks, nullptr, &ctx);
    if (!h) return false;

    yajl_status st = yajl_parse(h, reinterpret_cast<const unsigned char*>(data), size);
    if (st == yajl_status_ok) st = yajl_complete_parse(h);
    yajl_free(h);

    bool success = (st == yajl_status_ok) && ctx.ok && (ctx.seen == ec_sax::SEEN_ALL);
    if (success) {
#ifdef JF_PERF_ROUNDTRIP
        cfg_mid();
        success = serialize_config_yajl(g_config_yajl, jf_perf_scratch, sizeof(jf_perf_scratch)) > 0;
#else
        char* d = const_cast<char*>(data);
        success = serialize_config_yajl(g_config_yajl, d, size) > 0;
#endif
    }
    return success;
}

// ===========================================================================
// RpcCommand SAX consumer
// ===========================================================================

namespace rpc_sax {

enum Scope : uint8_t {
    R_ROOT,
    R_TARGETS,  // array
    R_TARGET,   // object
    R_PARAMS,   // array
    R_PARAM,    // object
    R_EXEC,     // object
    R_RESP,     // object
    R_IGNORE,
};

struct Frame { uint8_t scope; uint16_t idx; };

struct Ctx {
    Rpc*  cmd;
    Frame stack[24];
    int   sp = -1;
    char  key[48] = {0};
    bool  ok = true;

    uint8_t seen = 0;        // top-level required fields
    bool exec_timeout = false;
    bool resp_ack = false;
    bool resp_send = false;

    uint8_t top()  const { return sp >= 0 ? stack[sp].scope : 0xFF; }
    void push(uint8_t s) { if (sp < 23) stack[++sp] = Frame{s, 0}; else ok = false; }
    void pop()           { if (sp >= 0) --sp; }
};

enum SeenBits : uint8_t {
    SEEN_CMDID = 0x01, SEEN_TS = 0x02, SEEN_TARGETS = 0x04, SEEN_PARAMS = 0x08,
    SEEN_ALL   = 0x0F,
};

static inline bool keyeq(const Ctx* c, const char* k) { return std::strcmp(c->key, k) == 0; }
static inline Rpc::Target&    cur_target(Ctx* c) { return c->cmd->targets[c->cmd->targets_count - 1]; }
static inline Rpc::Parameter& cur_param(Ctx* c)  { return c->cmd->params[c->cmd->params_count - 1]; }

#define RPC_FAIL() do { c->ok = false; return 0; } while (0)

static int on_start_map(void* ctx) {
    Ctx* c = static_cast<Ctx*>(ctx);
    if (c->sp < 0) { c->push(R_ROOT); return c->ok; }

    switch (c->top()) {
        case R_ROOT:
            if      (keyeq(c, "execution"))       { c->cmd->execution.emplace();       c->exec_timeout = false; c->push(R_EXEC); }
            else if (keyeq(c, "response_config")) { c->cmd->response_config.emplace();  c->resp_ack = false; c->resp_send = false; c->push(R_RESP); }
            else                                  { c->push(R_IGNORE); }
            break;
        case R_TARGETS: {
            uint16_t idx = c->stack[c->sp].idx;
            if (idx >= Rpc::kMaxTargets) RPC_FAIL();
            c->cmd->targets_count = idx + 1;
            c->push(R_TARGET);
            break;
        }
        case R_PARAMS: {
            uint16_t idx = c->stack[c->sp].idx;
            if (idx >= Rpc::kMaxParams) RPC_FAIL();
            c->cmd->params_count = idx + 1;
            c->push(R_PARAM);
            break;
        }
        default:
            c->push(R_IGNORE);
            break;
    }
    return c->ok;
}

static int on_end_map(void* ctx) {
    Ctx* c = static_cast<Ctx*>(ctx);
    uint8_t s = c->top();
    if (s == R_EXEC && !c->exec_timeout) RPC_FAIL();             // timeout_ms required if present
    if (s == R_RESP && !(c->resp_ack && c->resp_send)) RPC_FAIL();
    c->pop();
    if (c->sp >= 0 && (s == R_TARGET || s == R_PARAM)) c->stack[c->sp].idx++;
    return 1;
}

static int on_start_array(void* ctx) {
    Ctx* c = static_cast<Ctx*>(ctx);
    if (c->top() == R_ROOT) {
        if      (keyeq(c, "targets")) { c->seen |= SEEN_TARGETS; c->push(R_TARGETS); }
        else if (keyeq(c, "params"))  { c->seen |= SEEN_PARAMS;  c->push(R_PARAMS); }
        else                          { c->push(R_IGNORE); }
    } else {
        c->push(R_IGNORE);
    }
    return c->ok;
}

static int on_end_array(void* ctx) {
    Ctx* c = static_cast<Ctx*>(ctx);
    uint16_t idx = (c->sp >= 0) ? c->stack[c->sp].idx : 0;
    if (c->top() == R_TARGETS && idx < 1) RPC_FAIL();           // min 1
    if (c->top() == R_PARAMS  && idx < 1) RPC_FAIL();           // min 1
    c->pop();
    return 1;
}

static int on_map_key(void* ctx, const unsigned char* k, size_t n) {
    Ctx* c = static_cast<Ctx*>(ctx);
    if (n >= sizeof(c->key)) n = sizeof(c->key) - 1;
    std::memcpy(c->key, k, n);
    c->key[n] = '\0';
    return 1;
}

static int handle_num(Ctx* c, double d, long long i) {
    switch (c->top()) {
        case R_ROOT:
            if      (keyeq(c, "timestamp_us")) { c->cmd->timestamp_us = static_cast<uint64_t>(i); c->seen |= SEEN_TS; }
            else if (keyeq(c, "sequence"))     { c->cmd->sequence = static_cast<uint16_t>(i); }
            else if (keyeq(c, "priority")) {
                if (i < 0 || i > 10) RPC_FAIL();                // range<0, 10>
                c->cmd->priority = static_cast<uint8_t>(i);
            }
            break;
        case R_PARAM:
            if      (keyeq(c, "int_value"))   cur_param(c).int_value = static_cast<int64_t>(i);
            else if (keyeq(c, "float_value")) {
                if (d < -1000000.0 || d > 1000000.0) RPC_FAIL();  // range<-1e6, 1e6>
                cur_param(c).float_value = d;
            }
            break;
        case R_EXEC:
            if (keyeq(c, "timeout_ms")) {
                if (i < 0 || i > 300000) RPC_FAIL();            // range<0, 300000>
                c->cmd->execution->timeout_ms = static_cast<uint32_t>(i);
                c->exec_timeout = true;
            } else if (keyeq(c, "max_retries")) {
                if (i < 0 || i > 5) RPC_FAIL();                 // range<0, 5>
                c->cmd->execution->max_retries = static_cast<uint8_t>(i);
            }
            break;
        default:
            break;
    }
    return c->ok;
}

static int on_integer(void* ctx, long long v) { return handle_num(static_cast<Ctx*>(ctx), static_cast<double>(v), v); }
static int on_double (void* ctx, double v)     { return handle_num(static_cast<Ctx*>(ctx), v, static_cast<long long>(v)); }

static int on_string(void* ctx, const unsigned char* s, size_t n) {
    Ctx* c = static_cast<Ctx*>(ctx);
    bool ok = true;
    switch (c->top()) {
        case R_ROOT:
            if (keyeq(c, "command_id")) { ok = copy_str(s, n, c->cmd->command_id.data(), c->cmd->command_id.size()); c->seen |= SEEN_CMDID; }
            break;
        case R_TARGET: {
            Rpc::Target& t = cur_target(c);
            if      (keyeq(c, "device_id")) ok = copy_str(s, n, t.device_id.data(), t.device_id.size());
            else if (keyeq(c, "subsystem")) ok = copy_str(s, n, t.subsystem.data(), t.subsystem.size());
            break;
        }
        case R_PARAM: {
            Rpc::Parameter& p = cur_param(c);
            if      (keyeq(c, "key"))          ok = copy_str(s, n, p.key.data(), p.key.size());
            else if (keyeq(c, "string_value")) { p.string_value.emplace(); ok = copy_str(s, n, p.string_value->data(), p.string_value->size()); }
            break;
        }
        case R_RESP:
            if (keyeq(c, "callback_url")) ok = copy_str(s, n, c->cmd->response_config->callback_url.data(), c->cmd->response_config->callback_url.size());
            break;
        default:
            break;
    }
    if (!ok) RPC_FAIL();
    return 1;
}

static int on_boolean(void* ctx, int b) {
    Ctx* c = static_cast<Ctx*>(ctx);
    switch (c->top()) {
        case R_PARAM: if (keyeq(c, "bool_value"))       cur_param(c).bool_value = (b != 0);            break;
        case R_EXEC:  if (keyeq(c, "retry_on_failure")) c->cmd->execution->retry_on_failure = (b != 0); break;
        case R_RESP:
            if      (keyeq(c, "acknowledge")) { c->cmd->response_config->acknowledge = (b != 0); c->resp_ack = true; }
            else if (keyeq(c, "send_result")) { c->cmd->response_config->send_result = (b != 0); c->resp_send = true; }
            break;
        default: break;
    }
    return 1;
}

static int on_null(void* /*ctx*/) { return 1; }

#undef RPC_FAIL

static const yajl_callbacks callbacks = {
    on_null,
    on_boolean,
    on_integer,
    on_double,
    nullptr,
    on_string,
    on_start_map,
    on_map_key,
    on_end_map,
    on_start_array,
    on_end_array,
};

}  // namespace rpc_sax

// ---------------------------------------------------------------------------
// RpcCommand serialization (yajl_gen)
// ---------------------------------------------------------------------------

static size_t serialize_rpc_command_yajl(const Rpc& cmd, char* buffer, size_t buffer_size) {
    yajl_gen g = yajl_gen_alloc(nullptr);
    if (!g) return 0;

    yajl_gen_map_open(g);

    gen_str(g, "command_id");   gen_str(g, cmd.command_id.data());
    gen_str(g, "timestamp_us"); yajl_gen_integer(g, static_cast<long long>(cmd.timestamp_us));
    gen_str(g, "sequence");     yajl_gen_integer(g, cmd.sequence);
    gen_str(g, "priority");     yajl_gen_integer(g, cmd.priority);

#ifdef JF_PERF_ROUNDTRIP
    const size_t n_targets = Rpc::kMaxTargets;
    const size_t n_params = Rpc::kMaxParams;
#else
    const size_t n_targets = cmd.targets_count;
    const size_t n_params = cmd.params_count;
#endif

    gen_str(g, "targets");
    yajl_gen_array_open(g);
    for (size_t i = 0; i < n_targets; ++i) {
        yajl_gen_map_open(g);
        gen_str(g, "device_id"); gen_str(g, cmd.targets[i].device_id.data());
        gen_str(g, "subsystem"); gen_str(g, cmd.targets[i].subsystem.data());
        yajl_gen_map_close(g);
    }
    yajl_gen_array_close(g);

    gen_str(g, "params");
    yajl_gen_array_open(g);
    for (size_t i = 0; i < n_params; ++i) {
        const auto& p = cmd.params[i];
        yajl_gen_map_open(g);
        gen_str(g, "key"); gen_str(g, p.key.data());
        if (p.int_value.has_value())    { gen_str(g, "int_value");    yajl_gen_integer(g, static_cast<long long>(*p.int_value)); }
        if (p.float_value.has_value())  { gen_str(g, "float_value");  yajl_gen_double(g, *p.float_value); }
        if (p.bool_value.has_value())   { gen_str(g, "bool_value");   yajl_gen_bool(g, *p.bool_value); }
        if (p.string_value.has_value()) { gen_str(g, "string_value"); gen_str(g, p.string_value->data()); }
        yajl_gen_map_close(g);
    }
    yajl_gen_array_close(g);

    if (cmd.execution.has_value()) {
        gen_str(g, "execution");
        yajl_gen_map_open(g);
        gen_str(g, "timeout_ms");       yajl_gen_integer(g, cmd.execution->timeout_ms);
        gen_str(g, "retry_on_failure"); yajl_gen_bool(g, cmd.execution->retry_on_failure);
        gen_str(g, "max_retries");      yajl_gen_integer(g, cmd.execution->max_retries);
        yajl_gen_map_close(g);
    }

    if (cmd.response_config.has_value()) {
        gen_str(g, "response_config");
        yajl_gen_map_open(g);
        gen_str(g, "callback_url"); gen_str(g, cmd.response_config->callback_url.data());
        gen_str(g, "acknowledge");  yajl_gen_bool(g, cmd.response_config->acknowledge);
        gen_str(g, "send_result");  yajl_gen_bool(g, cmd.response_config->send_result);
        yajl_gen_map_close(g);
    }

    yajl_gen_map_close(g);

    const unsigned char* buf = nullptr;
    size_t len = 0;
    size_t written = 0;
    if (yajl_gen_get_buf(g, &buf, &len) == yajl_gen_status_ok && len < buffer_size) {
        std::memcpy(buffer, buf, len + 1);
        written = len;
    }
    yajl_gen_free(g);
    return written;
}

// ---------------------------------------------------------------------------
// RpcCommand entry point
// ---------------------------------------------------------------------------

extern "C" __attribute__((used)) bool parse_rpc_command(const char* data, size_t size) {
#ifdef JF_PERF_ROUNDTRIP
    Rpc cmd{};  // zero-init: full-array serialize touches unused slots
#else
    Rpc cmd;
#endif
    rpc_sax::Ctx ctx;
    ctx.cmd = &cmd;

    yajl_handle h = yajl_alloc(&rpc_sax::callbacks, nullptr, &ctx);
    if (!h) return false;

    yajl_status st = yajl_parse(h, reinterpret_cast<const unsigned char*>(data), size);
    if (st == yajl_status_ok) st = yajl_complete_parse(h);
    yajl_free(h);

    bool success = (st == yajl_status_ok) && ctx.ok && (ctx.seen == rpc_sax::SEEN_ALL);
    if (success) {
#ifdef JF_PERF_ROUNDTRIP
        rpc_mid();
        success = serialize_rpc_command_yajl(cmd, jf_perf_scratch, sizeof(jf_perf_scratch)) > 0;
#else
        char* d = const_cast<char*>(data);
        success = serialize_rpc_command_yajl(cmd, d, size) > 0;
#endif
    }
    return success;
}

// ---------------------------------------------------------------------------
// Entry point (mirrors the other benchmark files)
// ---------------------------------------------------------------------------

extern "C" __attribute__((used)) int main() {
    volatile bool result = parse_config("", 0);
    volatile bool rpc_result = parse_rpc_command("", 0);
    (void)result;
    (void)rpc_result;
    while (1) {}
    return 0;
}

// ---------------------------------------------------------------------------
// Unity build: pull in the yajl C sources needed for SAX parse + generation.
// (yajl_tree.c / yajl_version.c are intentionally omitted: the DOM tree API is
// unused, and --gc-sections would strip them anyway.)
// ---------------------------------------------------------------------------
extern "C" {
#include "libs/yajl/src/yajl.c"
#include "libs/yajl/src/yajl_lex.c"
#include "libs/yajl/src/yajl_parser.c"
#include "libs/yajl/src/yajl_buf.c"
#include "libs/yajl/src/yajl_encode.c"
#include "libs/yajl/src/yajl_alloc.c"
#include "libs/yajl/src/yajl_gen.c"
}

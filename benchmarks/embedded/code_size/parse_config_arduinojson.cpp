// ArduinoJson benchmark - manual parsing + validation
// Implements the SAME validation logic as JsonFusion for fair comparison
#define ARDUINOJSON_USE_LONG_LONG 1
#include <ArduinoJson.h>
#include <string_view>
#include "embedded_config.hpp"

using embedded_benchmark::EmbeddedConfig;
using embedded_benchmark::RpcCommand;

// Global config instance
EmbeddedConfig g_config;

#ifdef JF_PERF_ROUNDTRIP
// Instruction-benchmark mode: serialize into a dedicated scratch buffer so the
// parse input can be exactly the JSON. The code-size benchmark is unaffected.
static char jf_perf_scratch[16384];
#endif

// Helper: Copy JSON string to fixed-size array
template<size_t N>
static bool copy_string(JsonVariant src, std::array<char, N>& dst) {
    const char* str = src.as<const char*>();
    if (!str) return false;
    
    size_t len = strlen(str);
    if (len >= N) len = N - 1;
    
    std::copy(str, str + len, dst.begin());
    dst[len] = '\0';
    return true;
}

// Helper: Validate range (for integers)
template<typename T, int64_t Min, int64_t Max>
static bool validate_range_int(T value) {
    return value >= static_cast<T>(Min) && value <= static_cast<T>(Max);
}

// Helper: Validate floating-point range
static bool validate_double_range(double value, double min, double max) {
    return value >= min && value <= max;
}

static bool validate_float_range(float value, float min, float max) {
    return value >= min && value <= max;
}

// Parse and validate Motor
static bool parse_motor(JsonObject motor_obj, EmbeddedConfig::Controller::Motor& motor) {
    // Required fields
    if (!motor_obj["id"].is<int64_t>()) return false;
    motor.id = motor_obj["id"];
    
    if (!copy_string(motor_obj["name"], motor.name)) return false;
    
    // Position: must be array of 3 doubles, each in range [-1000, 1000]
    JsonArray pos_arr = motor_obj["position"];
    if (!pos_arr || pos_arr.size() < 3) return false;  // min_items<3>
    
    for (size_t i = 0; i < 3; ++i) {
        if (!pos_arr[i].is<double>()) return false;
        double val = pos_arr[i];
        if (!validate_double_range(val, -1000.0, 1000.0)) return false;  // range validation!
        motor.position[i] = val;
    }
    
    // Velocity limits: array of 3 floats, each in range [-1000, 1000]
    JsonArray vel_arr = motor_obj["vel_limits"];
    if (!vel_arr || vel_arr.size() < 3) return false;  // min_items<3>
    
    for (size_t i = 0; i < 3; ++i) {
        if (!vel_arr[i].is<float>()) return false;
        float val = vel_arr[i];
        if (!validate_float_range(val, -1000.0f, 1000.0f)) return false;  // range validation!
        motor.vel_limits[i] = val;
    }
    
    if (!motor_obj["inverted"].is<bool>()) return false;
    motor.inverted = motor_obj["inverted"];
    
    return true;
}

// Parse and validate Sensor
static bool parse_sensor(JsonObject sensor_obj, EmbeddedConfig::Controller::Sensor& sensor) {
    if (!copy_string(sensor_obj["type"], sensor.type)) return false;
    if (!copy_string(sensor_obj["model"], sensor.model)) return false;
    
    // range_min: float in range [-100, 100000]
    if (!sensor_obj["range_min"].is<float>()) return false;
    float range_min = sensor_obj["range_min"];
    if (!validate_float_range(range_min, -100.0f, 100000.0f)) return false;  // range validation!
    sensor.range_min = range_min;
    
    // range_max: double in range [-1000, 100000]
    if (!sensor_obj["range_max"].is<double>()) return false;
    double range_max = sensor_obj["range_max"];
    if (!validate_double_range(range_max, -1000.0, 100000.0)) return false;  // range validation!
    sensor.range_max = range_max;
    
    if (!sensor_obj["active"].is<bool>()) return false;
    sensor.active = sensor_obj["active"];
    
    return true;
}

// Parse and validate Network
static bool parse_network(JsonObject net_obj, EmbeddedConfig::Network& network) {
    if (!copy_string(net_obj["name"], network.name)) return false;
    if (!copy_string(net_obj["address"], network.address)) return false;
    if (!net_obj["port"].is<uint16_t>()) return false;
    network.port = net_obj["port"];
    if (!net_obj["enabled"].is<bool>()) return false;
    network.enabled = net_obj["enabled"];
    return true;
}

// Helper: Serialize Motor to JsonObject
static void serialize_motor(JsonObject motor_obj, const EmbeddedConfig::Controller::Motor& motor) {
    motor_obj["id"] = motor.id;
    motor_obj["name"] = motor.name.data();
    JsonArray pos_arr = motor_obj["position"].to<JsonArray>();
    for (size_t i = 0; i < 3; ++i) {
        pos_arr.add(motor.position[i]);
    }
    JsonArray vel_arr = motor_obj["vel_limits"].to<JsonArray>();
    for (size_t i = 0; i < 3; ++i) {
        vel_arr.add(motor.vel_limits[i]);
    }
    motor_obj["inverted"] = motor.inverted;
}

// Helper: Serialize Sensor to JsonObject
static void serialize_sensor(JsonObject sensor_obj, const EmbeddedConfig::Controller::Sensor& sensor) {
    sensor_obj["type"] = sensor.type.data();
    sensor_obj["model"] = sensor.model.data();
    sensor_obj["range_min"] = sensor.range_min;
    sensor_obj["range_max"] = sensor.range_max;
    sensor_obj["active"] = sensor.active;
}

// Helper: Serialize Network to JsonObject
static void serialize_network(JsonObject net_obj, const EmbeddedConfig::Network& network) {
    net_obj["name"] = network.name.data();
    net_obj["address"] = network.address.data();
    net_obj["port"] = network.port;
    net_obj["enabled"] = network.enabled;
}

// Serialize EmbeddedConfig to buffer
static size_t serialize_config(const EmbeddedConfig& config, char* buffer, size_t buffer_size) {
    static JsonDocument doc;
    doc.clear();

    doc["app_name"] = config.app_name.data();
    doc["version_major"] = config.version_major;
    doc["version_minor"] = config.version_minor;

    JsonObject net_obj = doc["network"].to<JsonObject>();
    serialize_network(net_obj, config.network);

    if (config.fallback_network_conf.has_value()) {
        JsonObject fallback_obj = doc["fallback_network_conf"].to<JsonObject>();
        serialize_network(fallback_obj, *config.fallback_network_conf);
    }

    JsonObject ctrl_obj = doc["controller"].to<JsonObject>();
    ctrl_obj["name"] = config.controller.name.data();
    ctrl_obj["loop_hz"] = config.controller.loop_hz;

    // Round-trip benchmark serializes the full fixed array (see cJSON note).
#ifdef JF_PERF_ROUNDTRIP
    const size_t n_motors = EmbeddedConfig::kMaxMotors;
    const size_t n_sensors = EmbeddedConfig::kMaxSensors;
#else
    const size_t n_motors = config.controller.motors_count;
    const size_t n_sensors = config.controller.sensors_count;
#endif

    JsonArray motors_arr = ctrl_obj["motors"].to<JsonArray>();
    for (size_t i = 0; i < n_motors; ++i) {
        JsonObject motor_obj = motors_arr.add<JsonObject>();
        serialize_motor(motor_obj, config.controller.motors[i]);
    }

    JsonArray sensors_arr = ctrl_obj["sensors"].to<JsonArray>();
    for (size_t i = 0; i < n_sensors; ++i) {
        JsonObject sensor_obj = sensors_arr.add<JsonObject>();
        serialize_sensor(sensor_obj, config.controller.sensors[i]);
    }

    JsonObject log_obj = doc["logging"].to<JsonObject>();
    log_obj["enabled"] = config.logging.enabled;
    log_obj["path"] = config.logging.path.data();
    log_obj["max_files"] = config.logging.max_files;

    return serializeJson(doc, buffer, buffer_size);
}

// Main parse function with full validation
extern "C" __attribute__((used)) bool parse_config(const char* data, size_t size) {
    // Allocate JSON document (static to avoid stack overflow)
    static JsonDocument doc;

    // Parse JSON
    DeserializationError error = deserializeJson(doc, data, size);
    if (error) return false;

    JsonObject root = doc.as<JsonObject>();

    // Parse app_name, version_major, version_minor
    if (!copy_string(root["app_name"], g_config.app_name)) return false;
    if (!root["version_major"].is<uint16_t>()) return false;
    g_config.version_major = root["version_major"];
    if (!root["version_minor"].is<int>()) return false;
    g_config.version_minor = root["version_minor"];

    // Parse network (required)
    JsonObject net_obj = root["network"];
    if (!net_obj) return false;
    if (!parse_network(net_obj, g_config.network)) return false;

    // Parse fallback_network_conf (optional)
    if (root["fallback_network_conf"].is<JsonObject>()) {
        EmbeddedConfig::Network fallback;
        if (parse_network(root["fallback_network_conf"], fallback)) {
            g_config.fallback_network_conf = fallback;
        }
    }

    // Parse controller
    JsonObject ctrl_obj = root["controller"];
    if (!ctrl_obj) return false;

    if (!copy_string(ctrl_obj["name"], g_config.controller.name)) return false;

    // loop_hz: validate range [10, 10000]
    if (!ctrl_obj["loop_hz"].is<int>()) return false;
    int loop_hz = ctrl_obj["loop_hz"];
    if (!validate_range_int<int, 10, 10000>(loop_hz)) return false;  // range validation!
    g_config.controller.loop_hz = loop_hz;

    // Parse motors array (min_items<1>)
    JsonArray motors_arr = ctrl_obj["motors"];
    if (!motors_arr || motors_arr.size() == 0) return false;  // min_items validation!
    if (motors_arr.size() > EmbeddedConfig::kMaxMotors) return false;

    g_config.controller.motors_count = 0;
    for (JsonObject motor_obj : motors_arr) {
        if (g_config.controller.motors_count >= EmbeddedConfig::kMaxMotors) break;
        if (!parse_motor(motor_obj, g_config.controller.motors[g_config.controller.motors_count])) {
            return false;
        }
        g_config.controller.motors_count++;
    }

    // Parse sensors array (min_items<1>)
    JsonArray sensors_arr = ctrl_obj["sensors"];
    if (!sensors_arr || sensors_arr.size() == 0) return false;  // min_items validation!
    if (sensors_arr.size() > EmbeddedConfig::kMaxSensors) return false;

    g_config.controller.sensors_count = 0;
    for (JsonObject sensor_obj : sensors_arr) {
        if (g_config.controller.sensors_count >= EmbeddedConfig::kMaxSensors) break;
        if (!parse_sensor(sensor_obj, g_config.controller.sensors[g_config.controller.sensors_count])) {
            return false;
        }
        g_config.controller.sensors_count++;
    }

    // Parse logging
    JsonObject log_obj = root["logging"];
    if (!log_obj) return false;

    if (!log_obj["enabled"].is<bool>()) return false;
    g_config.logging.enabled = log_obj["enabled"];
    if (!copy_string(log_obj["path"], g_config.logging.path)) return false;
    if (!log_obj["max_files"].is<uint32_t>()) return false;
    g_config.logging.max_files = log_obj["max_files"];

    // Serialize back to buffer
#ifdef JF_PERF_ROUNDTRIP
    size_t written = serialize_config(g_config, jf_perf_scratch, sizeof(jf_perf_scratch));
#else
    char* d = const_cast<char*>(data);
    size_t written = serialize_config(g_config, d, size);
#endif

    return written > 0;
}

// Serialize RpcCommand to buffer
static size_t serialize_rpc_command(const RpcCommand& cmd, char* buffer, size_t buffer_size) {
    static JsonDocument doc;
    doc.clear();

    doc["command_id"] = cmd.command_id.data();
    doc["timestamp_us"] = cmd.timestamp_us;
    doc["sequence"] = cmd.sequence;
    doc["priority"] = cmd.priority;

#ifdef JF_PERF_ROUNDTRIP
    const size_t n_targets = RpcCommand::kMaxTargets;
    const size_t n_params = RpcCommand::kMaxParams;
#else
    const size_t n_targets = cmd.targets_count;
    const size_t n_params = cmd.params_count;
#endif

    JsonArray targets_arr = doc["targets"].to<JsonArray>();
    for (size_t i = 0; i < n_targets; ++i) {
        JsonObject target_obj = targets_arr.add<JsonObject>();
        target_obj["device_id"] = cmd.targets[i].device_id.data();
        target_obj["subsystem"] = cmd.targets[i].subsystem.data();
    }

    JsonArray params_arr = doc["params"].to<JsonArray>();
    for (size_t i = 0; i < n_params; ++i) {
        JsonObject param_obj = params_arr.add<JsonObject>();
        param_obj["key"] = cmd.params[i].key.data();
        if (cmd.params[i].int_value.has_value()) {
            param_obj["int_value"] = *cmd.params[i].int_value;
        }
        if (cmd.params[i].float_value.has_value()) {
            param_obj["float_value"] = *cmd.params[i].float_value;
        }
        if (cmd.params[i].bool_value.has_value()) {
            param_obj["bool_value"] = *cmd.params[i].bool_value;
        }
        if (cmd.params[i].string_value.has_value()) {
            param_obj["string_value"] = cmd.params[i].string_value->data();
        }
    }

    if (cmd.execution.has_value()) {
        JsonObject exec_obj = doc["execution"].to<JsonObject>();
        exec_obj["timeout_ms"] = cmd.execution->timeout_ms;
        exec_obj["retry_on_failure"] = cmd.execution->retry_on_failure;
        exec_obj["max_retries"] = cmd.execution->max_retries;
    }

    if (cmd.response_config.has_value()) {
        JsonObject resp_obj = doc["response_config"].to<JsonObject>();
        resp_obj["callback_url"] = cmd.response_config->callback_url.data();
        resp_obj["acknowledge"] = cmd.response_config->acknowledge;
        resp_obj["send_result"] = cmd.response_config->send_result;
    }

    return serializeJson(doc, buffer, buffer_size);
}

// Parse RpcCommand (matching JsonFusion validation logic)
extern "C" __attribute__((used)) bool parse_rpc_command(const char* data, size_t size) {
    static JsonDocument doc;
#ifdef JF_PERF_ROUNDTRIP
    RpcCommand rpc_cmd{};  // zero-init: full-array serialize touches unused slots
#else
    RpcCommand rpc_cmd;  // Local variable
#endif

    DeserializationError error = deserializeJson(doc, data, size);
    if (error) return false;

    JsonObject root = doc.as<JsonObject>();

    // Required: command_id must be present
    if (!root["command_id"].is<const char*>()) return false;
    if (!copy_string(root["command_id"], rpc_cmd.command_id)) return false;

    // Required: timestamp_us must be present
    if (!root["timestamp_us"].is<uint64_t>()) return false;
    rpc_cmd.timestamp_us = root["timestamp_us"];

    // Optional: sequence
    if (root["sequence"].is<uint16_t>()) {
        rpc_cmd.sequence = root["sequence"];
    }

    // Optional: priority (with validation if present)
    if (root["priority"].is<uint8_t>()) {
        uint8_t priority = root["priority"];
        if (!validate_range_int<uint8_t, 0, 10>(priority)) return false;
        rpc_cmd.priority = priority;
    }

    // Required: targets array must be present with min 1 item
    if (!root["targets"].is<JsonArray>()) return false;
    JsonArray targets = root["targets"];
    if (targets.size() == 0) return false;  // min_items<1> validation
    if (targets.size() > RpcCommand::kMaxTargets) return false;

    rpc_cmd.targets_count = 0;
    for (JsonObject target_obj : targets) {
        if (rpc_cmd.targets_count >= RpcCommand::kMaxTargets) break;

        auto& target = rpc_cmd.targets[rpc_cmd.targets_count];

        // Required: device_id must be present in each target
        if (!target_obj["device_id"].is<const char*>()) return false;
        if (!copy_string(target_obj["device_id"], target.device_id)) return false;

        // Optional: subsystem
        if (target_obj["subsystem"].is<const char*>()) {
            copy_string(target_obj["subsystem"], target.subsystem);
        }

        rpc_cmd.targets_count++;
    }

    // Required: params array must be present with min 1 item
    if (!root["params"].is<JsonArray>()) return false;
    JsonArray params = root["params"];
    if (params.size() == 0) return false;  // min_items<1> validation
    if (params.size() > RpcCommand::kMaxParams) return false;

    rpc_cmd.params_count = 0;
    for (JsonObject param_obj : params) {
        if (rpc_cmd.params_count >= RpcCommand::kMaxParams) break;

        auto& param = rpc_cmd.params[rpc_cmd.params_count];

        // Required: key must be present in each parameter
        if (!param_obj["key"].is<const char*>()) return false;
        if (!copy_string(param_obj["key"], param.key)) return false;

        // Optional: value fields (union-like, at least one should be present in practice)
        if (param_obj["int_value"].is<int64_t>()) {
            param.int_value = param_obj["int_value"].as<int64_t>();
        }

        if (param_obj["float_value"].is<double>()) {
            double fval = param_obj["float_value"];
            if (!validate_double_range(fval, -1000000.0, 1000000.0)) return false;
            param.float_value = fval;
        }

        if (param_obj["bool_value"].is<bool>()) {
            param.bool_value = param_obj["bool_value"].as<bool>();
        }

        if (param_obj["string_value"].is<const char*>()) {
            param.string_value.emplace();
            copy_string(param_obj["string_value"], *param.string_value);
        }

        rpc_cmd.params_count++;
    }

    // Optional: execution section
    if (root["execution"].is<JsonObject>()) {
        JsonObject exec_obj = root["execution"];
        RpcCommand::ExecutionOptions exec;

        // Required if section present: timeout_ms
        if (!exec_obj["timeout_ms"].is<uint32_t>()) return false;
        uint32_t timeout = exec_obj["timeout_ms"];
        if (!validate_range_int<uint32_t, 0, 300000>(timeout)) return false;
        exec.timeout_ms = timeout;

        // Optional: retry_on_failure
        if (exec_obj["retry_on_failure"].is<bool>()) {
            exec.retry_on_failure = exec_obj["retry_on_failure"];
        }

        // Optional: max_retries (with validation if present)
        if (exec_obj["max_retries"].is<uint8_t>()) {
            uint8_t retries = exec_obj["max_retries"];
            if (!validate_range_int<uint8_t, 0, 5>(retries)) return false;
            exec.max_retries = retries;
        }

        rpc_cmd.execution = exec;
    }

    // Optional: response_config section
    if (root["response_config"].is<JsonObject>()) {
        JsonObject resp_obj = root["response_config"];
        RpcCommand::ResponseConfig resp;

        // Optional: callback_url
        if (resp_obj["callback_url"].is<const char*>()) {
            copy_string(resp_obj["callback_url"], resp.callback_url);
        }

        // Required if section present: acknowledge
        if (!resp_obj["acknowledge"].is<bool>()) return false;
        resp.acknowledge = resp_obj["acknowledge"];

        // Required if section present: send_result
        if (!resp_obj["send_result"].is<bool>()) return false;
        resp.send_result = resp_obj["send_result"];

        rpc_cmd.response_config = resp;
    }

    // Serialize back to buffer
#ifdef JF_PERF_ROUNDTRIP
    size_t written = serialize_rpc_command(rpc_cmd, jf_perf_scratch, sizeof(jf_perf_scratch));
#else
    char* d = const_cast<char*>(data);
    size_t written = serialize_rpc_command(rpc_cmd, d, size);
#endif

    return written > 0;
}

// Entry point
int main() {
    volatile bool result = parse_config("", 0);
    volatile bool rpc_result = parse_rpc_command("", 0);
    (void)result;
    (void)rpc_result;
    while(1) {}
    return 0;
}


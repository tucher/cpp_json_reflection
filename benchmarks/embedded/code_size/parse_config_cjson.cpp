// cJSON benchmark implementation
// cJSON is a DOM-based JSON parser - parses into a tree structure

// Include cJSON implementation (single-file library pattern)
extern "C" {

#include "cJSON.h"
}
#include "embedded_config.hpp"
#include <cstring>
#include <cstdlib>

// Global config instance
embedded_benchmark::EmbeddedConfig g_config_cjson;

#ifdef JF_PERF_ROUNDTRIP
// Instruction-benchmark mode: serialize into a dedicated scratch buffer so the
// parse input can be exactly the JSON. The code-size benchmark is unaffected.
static char jf_perf_scratch[16384];
extern "C" void cfg_mid();  // parse/serialize boundary markers (defined in the runner)
extern "C" void rpc_mid();
#endif

// Helper: safely copy string from cJSON string value to fixed buffer
inline bool copy_cjson_string(const cJSON* item, char* dest, size_t dest_size) {
    if (!cJSON_IsString(item) || !item->valuestring) return false;
    size_t len = strlen(item->valuestring);
    if (len >= dest_size) return false; // Too long
    strcpy(dest, item->valuestring);
    return true;
}

// Helper: validate int range
template<int Min, int Max>
inline bool validate_int_range(int val) {
    return val >= Min && val <= Max;
}

// Helper: validate float range
inline bool validate_float_range(float val, float min, float max) {
    return val >= min && val <= max;
}

// Helper: validate double range
inline bool validate_double_range(double val, double min, double max) {
    return val >= min && val <= max;
}

// Parse Network object
bool parse_network(const cJSON* obj, embedded_benchmark::EmbeddedConfig::Network& net) {
    if (!cJSON_IsObject(obj)) return false;
    
    cJSON* name = cJSON_GetObjectItem(obj, "name");
    if (!copy_cjson_string(name, net.name.data(), net.name.size())) return false;
    
    cJSON* address = cJSON_GetObjectItem(obj, "address");
    if (!copy_cjson_string(address, net.address.data(), net.address.size())) return false;
    
    cJSON* port = cJSON_GetObjectItem(obj, "port");
    if (!cJSON_IsNumber(port)) return false;
    if (port->valueint < 0 || port->valueint > 65535) return false;
    net.port = static_cast<uint16_t>(port->valueint);
    
    cJSON* enabled = cJSON_GetObjectItem(obj, "enabled");
    if (!cJSON_IsBool(enabled)) return false;
    net.enabled = cJSON_IsTrue(enabled);
    
    return true;
}

// Parse Motor object
bool parse_motor(const cJSON* obj, embedded_benchmark::EmbeddedConfig::Controller::Motor& motor) {
    if (!cJSON_IsObject(obj)) return false;
    
    // id
    cJSON* id = cJSON_GetObjectItem(obj, "id");
    if (!cJSON_IsNumber(id)) return false;
    motor.id = static_cast<int64_t>(id->valuedouble); // cJSON stores all numbers as double
    
    // name
    cJSON* name = cJSON_GetObjectItem(obj, "name");
    if (!copy_cjson_string(name, motor.name.data(), motor.name.size())) return false;
    
    // position [x,y,z]
    cJSON* position = cJSON_GetObjectItem(obj, "position");
    if (!cJSON_IsArray(position)) return false;
    if (cJSON_GetArraySize(position) < 3) return false; // min_items<3> validation
    if (cJSON_GetArraySize(position) > 3) return false; // Fixed size array
    
    for (int i = 0; i < 3; ++i) {
        cJSON* val = cJSON_GetArrayItem(position, i);
        if (!cJSON_IsNumber(val)) return false;
        motor.position[i] = val->valuedouble;
        // range<-1000, 1000> validation
        if (!validate_double_range(motor.position[i], -1000.0, 1000.0)) return false;
    }
    
    // vel_limits [vx,vy,vz]
    cJSON* vel_limits = cJSON_GetObjectItem(obj, "vel_limits");
    if (!cJSON_IsArray(vel_limits)) return false;
    if (cJSON_GetArraySize(vel_limits) < 3) return false; // min_items<3> validation
    if (cJSON_GetArraySize(vel_limits) > 3) return false; // Fixed size array
    
    for (int i = 0; i < 3; ++i) {
        cJSON* val = cJSON_GetArrayItem(vel_limits, i);
        if (!cJSON_IsNumber(val)) return false;
        motor.vel_limits[i] = static_cast<float>(val->valuedouble);
        // range<-1000, 1000> validation
        if (!validate_float_range(motor.vel_limits[i], -1000.0f, 1000.0f)) return false;
    }
    
    // inverted
    cJSON* inverted = cJSON_GetObjectItem(obj, "inverted");
    if (!cJSON_IsBool(inverted)) return false;
    motor.inverted = cJSON_IsTrue(inverted);
    
    return true;
}

// Parse Sensor object
bool parse_sensor(const cJSON* obj, embedded_benchmark::EmbeddedConfig::Controller::Sensor& sensor) {
    if (!cJSON_IsObject(obj)) return false;
    
    // type
    cJSON* type = cJSON_GetObjectItem(obj, "type");
    if (!copy_cjson_string(type, sensor.type.data(), sensor.type.size())) return false;
    
    // model
    cJSON* model = cJSON_GetObjectItem(obj, "model");
    if (!copy_cjson_string(model, sensor.model.data(), sensor.model.size())) return false;
    
    // range_min
    cJSON* range_min = cJSON_GetObjectItem(obj, "range_min");
    if (!cJSON_IsNumber(range_min)) return false;
    sensor.range_min = static_cast<float>(range_min->valuedouble);
    // range<-100, 100000> validation
    if (!validate_float_range(sensor.range_min, -100.0f, 100000.0f)) return false;
    
    // range_max
    cJSON* range_max = cJSON_GetObjectItem(obj, "range_max");
    if (!cJSON_IsNumber(range_max)) return false;
    sensor.range_max = range_max->valuedouble;
    // range<-1000, 100000> validation
    if (!validate_double_range(sensor.range_max, -1000.0, 100000.0)) return false;
    
    // active
    cJSON* active = cJSON_GetObjectItem(obj, "active");
    if (!cJSON_IsBool(active)) return false;
    sensor.active = cJSON_IsTrue(active);
    
    return true;
}

// Parse Controller object
bool parse_controller(const cJSON* obj, embedded_benchmark::EmbeddedConfig::Controller& ctrl) {
    if (!cJSON_IsObject(obj)) return false;
    
    // name
    cJSON* name = cJSON_GetObjectItem(obj, "name");
    if (!copy_cjson_string(name, ctrl.name.data(), ctrl.name.size())) return false;
    
    // loop_hz
    cJSON* loop_hz = cJSON_GetObjectItem(obj, "loop_hz");
    if (!cJSON_IsNumber(loop_hz)) return false;
    ctrl.loop_hz = loop_hz->valueint;
    // range<10, 10000> validation
    if (!validate_int_range<10, 10000>(ctrl.loop_hz)) return false;
    
    // motors array
    cJSON* motors = cJSON_GetObjectItem(obj, "motors");
    if (!cJSON_IsArray(motors)) return false;
    int motor_count = cJSON_GetArraySize(motors);
    if (motor_count < 1) return false; // min_items<1> validation
    if (motor_count > static_cast<int>(embedded_benchmark::EmbeddedConfig::kMaxMotors)) return false; // max_items validation
    
    for (int i = 0; i < motor_count; ++i) {
        cJSON* motor_obj = cJSON_GetArrayItem(motors, i);
        if (!parse_motor(motor_obj, ctrl.motors[i])) return false;
    }
    
    // sensors array
    cJSON* sensors = cJSON_GetObjectItem(obj, "sensors");
    if (!cJSON_IsArray(sensors)) return false;
    int sensor_count = cJSON_GetArraySize(sensors);
    if (sensor_count < 1) return false; // min_items<1> validation
    if (sensor_count > static_cast<int>(embedded_benchmark::EmbeddedConfig::kMaxSensors)) return false; // max_items validation
    
    for (int i = 0; i < sensor_count; ++i) {
        cJSON* sensor_obj = cJSON_GetArrayItem(sensors, i);
        if (!parse_sensor(sensor_obj, ctrl.sensors[i])) return false;
    }
    
    return true;
}

// Parse Logging object
bool parse_logging(const cJSON* obj, embedded_benchmark::EmbeddedConfig::Logging& log) {
    if (!cJSON_IsObject(obj)) return false;
    
    // enabled
    cJSON* enabled = cJSON_GetObjectItem(obj, "enabled");
    if (!cJSON_IsBool(enabled)) return false;
    log.enabled = cJSON_IsTrue(enabled);
    
    // path
    cJSON* path = cJSON_GetObjectItem(obj, "path");
    if (!copy_cjson_string(path, log.path.data(), log.path.size())) return false;
    
    // max_files
    cJSON* max_files = cJSON_GetObjectItem(obj, "max_files");
    if (!cJSON_IsNumber(max_files)) return false;
    log.max_files = static_cast<uint32_t>(max_files->valuedouble);
    
    return true;
}

// Serialize Network to cJSON object
cJSON* serialize_network(const embedded_benchmark::EmbeddedConfig::Network& net) {
    cJSON* obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "name", net.name.data());
    cJSON_AddStringToObject(obj, "address", net.address.data());
    cJSON_AddNumberToObject(obj, "port", net.port);
    cJSON_AddBoolToObject(obj, "enabled", net.enabled);
    return obj;
}

// Serialize Motor to cJSON object
cJSON* serialize_motor(const embedded_benchmark::EmbeddedConfig::Controller::Motor& motor) {
    cJSON* obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(obj, "id", static_cast<double>(motor.id));
    cJSON_AddStringToObject(obj, "name", motor.name.data());

    cJSON* pos_arr = cJSON_CreateArray();
    for (int i = 0; i < 3; ++i) {
        cJSON_AddItemToArray(pos_arr, cJSON_CreateNumber(motor.position[i]));
    }
    cJSON_AddItemToObject(obj, "position", pos_arr);

    cJSON* vel_arr = cJSON_CreateArray();
    for (int i = 0; i < 3; ++i) {
        cJSON_AddItemToArray(vel_arr, cJSON_CreateNumber(motor.vel_limits[i]));
    }
    cJSON_AddItemToObject(obj, "vel_limits", vel_arr);

    cJSON_AddBoolToObject(obj, "inverted", motor.inverted);
    return obj;
}

// Serialize Sensor to cJSON object
cJSON* serialize_sensor(const embedded_benchmark::EmbeddedConfig::Controller::Sensor& sensor) {
    cJSON* obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "type", sensor.type.data());
    cJSON_AddStringToObject(obj, "model", sensor.model.data());
    cJSON_AddNumberToObject(obj, "range_min", sensor.range_min);
    cJSON_AddNumberToObject(obj, "range_max", sensor.range_max);
    cJSON_AddBoolToObject(obj, "active", sensor.active);
    return obj;
}

// Serialize EmbeddedConfig to buffer
size_t serialize_config_cjson(const embedded_benchmark::EmbeddedConfig& config, char* buffer, size_t buffer_size) {
    cJSON* root = cJSON_CreateObject();

    cJSON_AddStringToObject(root, "app_name", config.app_name.data());
    cJSON_AddNumberToObject(root, "version_major", config.version_major);
    cJSON_AddNumberToObject(root, "version_minor", config.version_minor);

    cJSON_AddItemToObject(root, "network", serialize_network(config.network));

    if (config.fallback_network_conf.has_value()) {
        cJSON_AddItemToObject(root, "fallback_network_conf", serialize_network(*config.fallback_network_conf));
    }

    cJSON* ctrl_obj = cJSON_CreateObject();
    cJSON_AddStringToObject(ctrl_obj, "name", config.controller.name.data());
    cJSON_AddNumberToObject(ctrl_obj, "loop_hz", config.controller.loop_hz);

    // In the instruction-benchmark round trip every library serializes the full
    // fixed-size array (matching JsonFusion/Glaze, whose models have no count),
    // so the work compared is equivalent. The code-size benchmark (no macro)
    // serializes only the populated elements.
#ifdef JF_PERF_ROUNDTRIP
    const size_t n_motors = embedded_benchmark::EmbeddedConfig::kMaxMotors;
    const size_t n_sensors = embedded_benchmark::EmbeddedConfig::kMaxSensors;
#else
    const size_t n_motors = config.controller.motors_count;
    const size_t n_sensors = config.controller.sensors_count;
#endif

    cJSON* motors_arr = cJSON_CreateArray();
    for (size_t i = 0; i < n_motors; ++i) {
        cJSON_AddItemToArray(motors_arr, serialize_motor(config.controller.motors[i]));
    }
    cJSON_AddItemToObject(ctrl_obj, "motors", motors_arr);

    cJSON* sensors_arr = cJSON_CreateArray();
    for (size_t i = 0; i < n_sensors; ++i) {
        cJSON_AddItemToArray(sensors_arr, serialize_sensor(config.controller.sensors[i]));
    }
    cJSON_AddItemToObject(ctrl_obj, "sensors", sensors_arr);

    cJSON_AddItemToObject(root, "controller", ctrl_obj);

    cJSON* log_obj = cJSON_CreateObject();
    cJSON_AddBoolToObject(log_obj, "enabled", config.logging.enabled);
    cJSON_AddStringToObject(log_obj, "path", config.logging.path.data());
    cJSON_AddNumberToObject(log_obj, "max_files", config.logging.max_files);
    cJSON_AddItemToObject(root, "logging", log_obj);

    char* printed = cJSON_PrintUnformatted(root);
    size_t len = 0;
    if (printed) {
        len = strlen(printed);
        if (len < buffer_size) {
            memcpy(buffer, printed, len + 1);
        }
        free(printed);
    }
    cJSON_Delete(root);
    return len;
}

// Main parsing function
extern "C" __attribute__((used)) bool parse_config(const char* data, size_t size) {
    // Parse JSON into DOM tree
    cJSON* root = cJSON_ParseWithLength(data, size);
    if (!root) {
        return false;
    }

    // Declare all variables at the top (C++ goto requirement)
    bool success = true;
    cJSON* app_name;
    cJSON* version_major;
    cJSON* version_minor;
    cJSON* network;
    cJSON* fallback_network;
    cJSON* controller;
    cJSON* logging;

    // Parse top-level fields
    app_name = cJSON_GetObjectItem(root, "app_name");
    if (!copy_cjson_string(app_name, g_config_cjson.app_name.data(), g_config_cjson.app_name.size())) {
        success = false;
        goto cleanup;
    }

    version_major = cJSON_GetObjectItem(root, "version_major");
    if (!cJSON_IsNumber(version_major)) {
        success = false;
        goto cleanup;
    }
    g_config_cjson.version_major = static_cast<uint16_t>(version_major->valueint);

    version_minor = cJSON_GetObjectItem(root, "version_minor");
    if (!cJSON_IsNumber(version_minor)) {
        success = false;
        goto cleanup;
    }
    g_config_cjson.version_minor = version_minor->valueint;

    // network (required)
    network = cJSON_GetObjectItem(root, "network");
    if (!parse_network(network, g_config_cjson.network)) {
        success = false;
        goto cleanup;
    }

    // fallback_network_conf (optional)
    fallback_network = cJSON_GetObjectItem(root, "fallback_network_conf");
    if (fallback_network && !cJSON_IsNull(fallback_network)) {
        g_config_cjson.fallback_network_conf.emplace();
        if (!parse_network(fallback_network, *g_config_cjson.fallback_network_conf)) {
            success = false;
            goto cleanup;
        }
    } else {
        g_config_cjson.fallback_network_conf.reset();
    }

    // controller (required)
    controller = cJSON_GetObjectItem(root, "controller");
    if (!parse_controller(controller, g_config_cjson.controller)) {
        success = false;
        goto cleanup;
    }

    // logging (required)
    logging = cJSON_GetObjectItem(root, "logging");
    if (!parse_logging(logging, g_config_cjson.logging)) {
        success = false;
        goto cleanup;
    }

cleanup:
    // Free the DOM tree
    cJSON_Delete(root);

    if (success) {
        // Serialize back to buffer
#ifdef JF_PERF_ROUNDTRIP
        cfg_mid();
        size_t written = serialize_config_cjson(g_config_cjson, jf_perf_scratch, sizeof(jf_perf_scratch));
#else
        char* d = const_cast<char*>(data);
        size_t written = serialize_config_cjson(g_config_cjson, d, size);
#endif
        success = written > 0;
    }

    return success;
}

// Serialize RpcCommand to buffer
size_t serialize_rpc_command_cjson(const embedded_benchmark::RpcCommand& cmd, char* buffer, size_t buffer_size) {
    cJSON* root = cJSON_CreateObject();

    cJSON_AddStringToObject(root, "command_id", cmd.command_id.data());
    cJSON_AddNumberToObject(root, "timestamp_us", static_cast<double>(cmd.timestamp_us));
    cJSON_AddNumberToObject(root, "sequence", cmd.sequence);
    cJSON_AddNumberToObject(root, "priority", cmd.priority);

#ifdef JF_PERF_ROUNDTRIP
    const size_t n_targets = embedded_benchmark::RpcCommand::kMaxTargets;
    const size_t n_params = embedded_benchmark::RpcCommand::kMaxParams;
#else
    const size_t n_targets = cmd.targets_count;
    const size_t n_params = cmd.params_count;
#endif

    cJSON* targets_arr = cJSON_CreateArray();
    for (size_t i = 0; i < n_targets; ++i) {
        cJSON* target_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(target_obj, "device_id", cmd.targets[i].device_id.data());
        cJSON_AddStringToObject(target_obj, "subsystem", cmd.targets[i].subsystem.data());
        cJSON_AddItemToArray(targets_arr, target_obj);
    }
    cJSON_AddItemToObject(root, "targets", targets_arr);

    cJSON* params_arr = cJSON_CreateArray();
    for (size_t i = 0; i < n_params; ++i) {
        cJSON* param_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(param_obj, "key", cmd.params[i].key.data());
        if (cmd.params[i].int_value.has_value()) {
            cJSON_AddNumberToObject(param_obj, "int_value", static_cast<double>(*cmd.params[i].int_value));
        }
        if (cmd.params[i].float_value.has_value()) {
            cJSON_AddNumberToObject(param_obj, "float_value", *cmd.params[i].float_value);
        }
        if (cmd.params[i].bool_value.has_value()) {
            cJSON_AddBoolToObject(param_obj, "bool_value", *cmd.params[i].bool_value);
        }
        if (cmd.params[i].string_value.has_value()) {
            cJSON_AddStringToObject(param_obj, "string_value", cmd.params[i].string_value->data());
        }
        cJSON_AddItemToArray(params_arr, param_obj);
    }
    cJSON_AddItemToObject(root, "params", params_arr);

    if (cmd.execution.has_value()) {
        cJSON* exec_obj = cJSON_CreateObject();
        cJSON_AddNumberToObject(exec_obj, "timeout_ms", cmd.execution->timeout_ms);
        cJSON_AddBoolToObject(exec_obj, "retry_on_failure", cmd.execution->retry_on_failure);
        cJSON_AddNumberToObject(exec_obj, "max_retries", cmd.execution->max_retries);
        cJSON_AddItemToObject(root, "execution", exec_obj);
    }

    if (cmd.response_config.has_value()) {
        cJSON* resp_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(resp_obj, "callback_url", cmd.response_config->callback_url.data());
        cJSON_AddBoolToObject(resp_obj, "acknowledge", cmd.response_config->acknowledge);
        cJSON_AddBoolToObject(resp_obj, "send_result", cmd.response_config->send_result);
        cJSON_AddItemToObject(root, "response_config", resp_obj);
    }

    char* printed = cJSON_PrintUnformatted(root);
    size_t len = 0;
    if (printed) {
        len = strlen(printed);
        if (len < buffer_size) {
            memcpy(buffer, printed, len + 1);
        }
        free(printed);
    }
    cJSON_Delete(root);
    return len;
}

// Parse RpcCommand
extern "C" __attribute__((used)) bool parse_rpc_command(const char* data, size_t size) {
#ifdef JF_PERF_ROUNDTRIP
    embedded_benchmark::RpcCommand rpc_cmd{};  // zero-init: full-array serialize touches unused slots
#else
    embedded_benchmark::RpcCommand rpc_cmd;  // Local variable
#endif
    cJSON* root = cJSON_ParseWithLength(data, size);
    if (!root) return false;

    // Declare all variables at the top
    bool success = true;
    cJSON* command_id;
    cJSON* timestamp_us;
    cJSON* sequence;
    cJSON* priority;
    cJSON* targets;
    cJSON* params;
    cJSON* execution;
    cJSON* response_config;
    int targets_count;
    int params_count;
    
    // Required: command_id must be present
    command_id = cJSON_GetObjectItem(root, "command_id");
    if (!command_id || !cJSON_IsString(command_id)) {
        success = false;
        goto cleanup;
    }
    if (!copy_cjson_string(command_id, rpc_cmd.command_id.data(), rpc_cmd.command_id.size())) {
        success = false;
        goto cleanup;
    }
    
    // Required: timestamp_us must be present
    timestamp_us = cJSON_GetObjectItem(root, "timestamp_us");
    if (!timestamp_us || !cJSON_IsNumber(timestamp_us)) {
        success = false;
        goto cleanup;
    }
    rpc_cmd.timestamp_us = static_cast<uint64_t>(timestamp_us->valuedouble);
    
    // Optional: sequence
    sequence = cJSON_GetObjectItem(root, "sequence");
    if (sequence && cJSON_IsNumber(sequence)) {
        rpc_cmd.sequence = static_cast<uint16_t>(sequence->valueint);
    }
    
    // Optional: priority (with validation if present)
    priority = cJSON_GetObjectItem(root, "priority");
    if (priority && cJSON_IsNumber(priority)) {
        int prio = priority->valueint;
        if (prio < 0 || prio > 10) {
            success = false;
            goto cleanup;
        }
        rpc_cmd.priority = static_cast<uint8_t>(prio);
    }
    
    // Required: targets array must be present with min 1 item
    targets = cJSON_GetObjectItem(root, "targets");
    if (!targets || !cJSON_IsArray(targets)) {
        success = false;
        goto cleanup;
    }
    targets_count = cJSON_GetArraySize(targets);
    if (targets_count < 1 || targets_count > static_cast<int>(embedded_benchmark::RpcCommand::kMaxTargets)) {
        success = false;
        goto cleanup;
    }
    
    rpc_cmd.targets_count = 0;
    for (int i = 0; i < targets_count; ++i) {
        cJSON* target_obj = cJSON_GetArrayItem(targets, i);
        if (!cJSON_IsObject(target_obj)) {
            success = false;
            goto cleanup;
        }
        
        auto& target = rpc_cmd.targets[i];
        
        // Required: device_id must be present
        cJSON* device_id = cJSON_GetObjectItem(target_obj, "device_id");
        if (!device_id || !cJSON_IsString(device_id)) {
            success = false;
            goto cleanup;
        }
        if (!copy_cjson_string(device_id, target.device_id.data(), target.device_id.size())) {
            success = false;
            goto cleanup;
        }
        
        // Optional: subsystem
        cJSON* subsystem = cJSON_GetObjectItem(target_obj, "subsystem");
        if (subsystem && cJSON_IsString(subsystem)) {
            copy_cjson_string(subsystem, target.subsystem.data(), target.subsystem.size());
        }
        
        rpc_cmd.targets_count++;
    }
    
    // Required: params array must be present with min 1 item
    params = cJSON_GetObjectItem(root, "params");
    if (!params || !cJSON_IsArray(params)) {
        success = false;
        goto cleanup;
    }
    params_count = cJSON_GetArraySize(params);
    if (params_count < 1 || params_count > static_cast<int>(embedded_benchmark::RpcCommand::kMaxParams)) {
        success = false;
        goto cleanup;
    }
    
    rpc_cmd.params_count = 0;
    for (int i = 0; i < params_count; ++i) {
        cJSON* param_obj = cJSON_GetArrayItem(params, i);
        if (!cJSON_IsObject(param_obj)) {
            success = false;
            goto cleanup;
        }
        
        auto& param = rpc_cmd.params[i];
        
        // Required: key must be present
        cJSON* key = cJSON_GetObjectItem(param_obj, "key");
        if (!key || !cJSON_IsString(key)) {
            success = false;
            goto cleanup;
        }
        if (!copy_cjson_string(key, param.key.data(), param.key.size())) {
            success = false;
            goto cleanup;
        }
        
        // Optional: value fields
        cJSON* int_value = cJSON_GetObjectItem(param_obj, "int_value");
        if (int_value && cJSON_IsNumber(int_value)) {
            param.int_value = static_cast<int64_t>(int_value->valuedouble);
        }
        
        cJSON* float_value = cJSON_GetObjectItem(param_obj, "float_value");
        if (float_value && cJSON_IsNumber(float_value)) {
            double fval = float_value->valuedouble;
            if (fval < -1000000.0 || fval > 1000000.0) {
                success = false;
                goto cleanup;
            }
            param.float_value = fval;
        }
        
        cJSON* bool_value = cJSON_GetObjectItem(param_obj, "bool_value");
        if (bool_value && cJSON_IsBool(bool_value)) {
            param.bool_value = cJSON_IsTrue(bool_value);
        }
        
        cJSON* string_value = cJSON_GetObjectItem(param_obj, "string_value");
        if (string_value && cJSON_IsString(string_value)) {
            param.string_value.emplace();
            copy_cjson_string(string_value, param.string_value->data(), param.string_value->size());
        }
        
        rpc_cmd.params_count++;
    }
    
    // Optional: execution section
    execution = cJSON_GetObjectItem(root, "execution");
    if (execution && cJSON_IsObject(execution)) {
        embedded_benchmark::RpcCommand::ExecutionOptions exec;
        
        // Required if section present: timeout_ms
        cJSON* timeout_ms = cJSON_GetObjectItem(execution, "timeout_ms");
        if (!timeout_ms || !cJSON_IsNumber(timeout_ms)) {
            success = false;
            goto cleanup;
        }
        uint32_t timeout = static_cast<uint32_t>(timeout_ms->valuedouble);
        if (timeout > 300000) {
            success = false;
            goto cleanup;
        }
        exec.timeout_ms = timeout;
        
        // Optional: retry_on_failure
        cJSON* retry = cJSON_GetObjectItem(execution, "retry_on_failure");
        if (retry && cJSON_IsBool(retry)) {
            exec.retry_on_failure = cJSON_IsTrue(retry);
        }
        
        // Optional: max_retries (with validation if present)
        cJSON* max_retries = cJSON_GetObjectItem(execution, "max_retries");
        if (max_retries && cJSON_IsNumber(max_retries)) {
            int retries = max_retries->valueint;
            if (retries < 0 || retries > 5) {
                success = false;
                goto cleanup;
            }
            exec.max_retries = static_cast<uint8_t>(retries);
        }
        
        rpc_cmd.execution = exec;
    }
    
    // Optional: response_config section
    response_config = cJSON_GetObjectItem(root, "response_config");
    if (response_config && cJSON_IsObject(response_config)) {
        embedded_benchmark::RpcCommand::ResponseConfig resp;
        
        // Optional: callback_url
        cJSON* callback_url = cJSON_GetObjectItem(response_config, "callback_url");
        if (callback_url && cJSON_IsString(callback_url)) {
            copy_cjson_string(callback_url, resp.callback_url.data(), resp.callback_url.size());
        }
        
        // Required if section present: acknowledge
        cJSON* acknowledge = cJSON_GetObjectItem(response_config, "acknowledge");
        if (!acknowledge || !cJSON_IsBool(acknowledge)) {
            success = false;
            goto cleanup;
        }
        resp.acknowledge = cJSON_IsTrue(acknowledge);
        
        // Required if section present: send_result
        cJSON* send_result = cJSON_GetObjectItem(response_config, "send_result");
        if (!send_result || !cJSON_IsBool(send_result)) {
            success = false;
            goto cleanup;
        }
        resp.send_result = cJSON_IsTrue(send_result);
        
        rpc_cmd.response_config = resp;
    }
    
cleanup:
    cJSON_Delete(root);

    if (success) {
        // Serialize back to buffer
#ifdef JF_PERF_ROUNDTRIP
        rpc_mid();
        size_t written = serialize_rpc_command_cjson(rpc_cmd, jf_perf_scratch, sizeof(jf_perf_scratch));
#else
        char* d = const_cast<char*>(data);
        size_t written = serialize_rpc_command_cjson(rpc_cmd, d, size);
#endif
        success = written > 0;
    }

    return success;
}

// Entry point
extern "C" __attribute__((used)) int main() {
    volatile bool result = parse_config("", 0);
    volatile bool rpc_result = parse_rpc_command("", 0);
    (void)result;
    (void)rpc_result;
    while(1) {}
    return 0;
}

extern "C" {
    #include "cJSON.c"
}

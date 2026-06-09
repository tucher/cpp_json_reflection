#define GLZ_DISABLE_ALWAYS_INLINE
#include <string_view>
#include <cstdlib>
#include <glaze/glaze.hpp>

#include "embedded_config.hpp"

// #define JSON_FUSION_BENCHMARK_ADDITIONAL_MODELS
#ifdef JSON_FUSION_BENCHMARK_ADDITIONAL_MODELS
#include "additional_models.hpp"
#endif

// Glaze metadata with validation constraints matching JsonFusion's validations
// Using single error message for fairness (JsonFusion uses generic error codes, not custom messages)

#define ERR "err"

template <std::size_t N>
constexpr bool is_non_empty(const std::array<char, N>& arr) { return arr[0] != '\0'; }

// Motor: position and vel_limits range validation
template <>
struct glz::meta<embedded_benchmark::EmbeddedConfig::Controller::Motor> {
    using T = embedded_benchmark::EmbeddedConfig::Controller::Motor;

    static constexpr auto validate_position = [](const T&, const std::array<double, 3>& pos) {
        for (const auto& v : pos) if (v < -1000.0 || v > 1000.0) return false;
        return true;
    };
    static constexpr auto validate_vel_limits = [](const T&, const std::array<float, 3>& vel) {
        for (const auto& v : vel) if (v < -1000.0f || v > 1000.0f) return false;
        return true;
    };

    static constexpr auto value = object(
        &T::id, &T::name,
        "position", read_constraint<&T::position, validate_position, ERR>,
        "vel_limits", read_constraint<&T::vel_limits, validate_vel_limits, ERR>,
        &T::inverted
    );
};

// Sensor: range_min and range_max validation
template <>
struct glz::meta<embedded_benchmark::EmbeddedConfig::Controller::Sensor> {
    using T = embedded_benchmark::EmbeddedConfig::Controller::Sensor;

    static constexpr auto validate_range_min = [](const T&, float v) { return v >= -100.0f && v <= 100000.0f; };
    static constexpr auto validate_range_max = [](const T&, double v) { return v >= -1000.0 && v <= 100000.0; };

    static constexpr auto value = object(
        &T::type, &T::model,
        "range_min", read_constraint<&T::range_min, validate_range_min, ERR>,
        "range_max", read_constraint<&T::range_max, validate_range_max, ERR>,
        &T::active
    );
};

// Controller: loop_hz range, motors_count/sensors_count min 1
template <>
struct glz::meta<embedded_benchmark::EmbeddedConfig::Controller> {
    using T = embedded_benchmark::EmbeddedConfig::Controller;

    static constexpr auto validate_loop_hz = [](const T&, int v) { return v >= 10 && v <= 10000; };
    static constexpr auto validate_motors_count = [](const T&, size_t v) { return v >= 1; };
    static constexpr auto validate_sensors_count = [](const T&, size_t v) { return v >= 1; };

    static constexpr auto value = object(
        &T::name,
        "loop_hz", read_constraint<&T::loop_hz, validate_loop_hz, ERR>,
        &T::motors,
        "motors_count", read_constraint<&T::motors_count, validate_motors_count, ERR>,
        &T::sensors,
        "sensors_count", read_constraint<&T::sensors_count, validate_sensors_count, ERR>
    );
};

// Target: device_id required
template <>
struct glz::meta<embedded_benchmark::RpcCommand::Target> {
    using T = embedded_benchmark::RpcCommand::Target;
    static constexpr auto value = object(&T::device_id, &T::subsystem);
    static constexpr bool requires_key(std::string_view key, bool) {
        return key == "device_id";
    }
};

// Parameter: key required, float_value range validation
template <>
struct glz::meta<embedded_benchmark::RpcCommand::Parameter> {
    using T = embedded_benchmark::RpcCommand::Parameter;

    static constexpr auto validate_float = [](const T&, const std::optional<double>& v) {
        return !v.has_value() || (*v >= -1000000.0 && *v <= 1000000.0);
    };

    static constexpr auto value = object(
        &T::key, &T::int_value,
        // "float_value", read_constraint<&T::float_value, validate_float, ERR>, // TODO: does not work for round-trip, need to report this to Glaze team
        "float_value", &T::float_value,
        &T::bool_value, &T::string_value
    );
    static constexpr bool requires_key(std::string_view key, bool) {
        return key == "key";
    }
};

// ExecutionOptions: timeout_ms required, range validations
template <>
struct glz::meta<embedded_benchmark::RpcCommand::ExecutionOptions> {
    using T = embedded_benchmark::RpcCommand::ExecutionOptions;

    static constexpr auto validate_timeout = [](const T&, uint32_t v) { return v <= 300000; };
    static constexpr auto validate_retries = [](const T&, uint8_t v) { return v <= 5; };

    static constexpr auto value = object(
        "timeout_ms", read_constraint<&T::timeout_ms, validate_timeout, ERR>,
        &T::retry_on_failure,
        "max_retries", read_constraint<&T::max_retries, validate_retries, ERR>
    );
    static constexpr bool requires_key(std::string_view key, bool) {
        return key == "timeout_ms";
    }
};

// ResponseConfig: acknowledge and send_result required
template <>
struct glz::meta<embedded_benchmark::RpcCommand::ResponseConfig> {
    using T = embedded_benchmark::RpcCommand::ResponseConfig;
    static constexpr auto value = object(&T::callback_url, &T::acknowledge, &T::send_result);
    static constexpr bool requires_key(std::string_view key, bool) {
        return key == "acknowledge" || key == "send_result";
    }
};

// RpcCommand: priority range, counts min 1, required fields
template <>
struct glz::meta<embedded_benchmark::RpcCommand> {
    using T = embedded_benchmark::RpcCommand;

    static constexpr auto validate_priority = [](const T&, uint8_t v) { return v <= 10; };
    static constexpr auto validate_targets_count = [](const T&, size_t v) { return v >= 1; };
    static constexpr auto validate_params_count = [](const T&, size_t v) { return v >= 1; };

    static constexpr auto value = object(
        &T::command_id, &T::timestamp_us, &T::sequence,
        "priority", read_constraint<&T::priority, validate_priority, ERR>,
        &T::targets,
        "targets_count", read_constraint<&T::targets_count, validate_targets_count, ERR>,
        &T::params,
        "params_count", read_constraint<&T::params_count, validate_params_count, ERR>,
        &T::execution, &T::response_config
    );
    static constexpr bool requires_key(std::string_view key, bool) {
        return key == "command_id" || key == "timestamp_us" || key == "targets" || key == "params";
    }
};

// Global config instance (will go in .bss section)
embedded_benchmark::EmbeddedConfig g_config;

#ifdef JF_PERF_ROUNDTRIP
// Instruction-benchmark mode: serialize into a dedicated scratch buffer (also
// avoids glz::write's unbounded write into the caller's buffer). The code-size
// benchmark (no macro) is unaffected.
static char jf_perf_scratch[16384];
#endif

extern "C" __attribute__((used)) bool parse_config(const char* data, size_t size) {
    auto error = glz::read<glz::opts_size{}>(g_config, std::string_view(data, size));

#ifdef JF_PERF_ROUNDTRIP
    auto w_err = glz::write<glz::opts_size{}>(g_config, jf_perf_scratch);
#else
    char* d = const_cast<char*>(data);
    auto w_err = glz::write<glz::opts_size{}>(g_config, d);
#endif

    return !error && !w_err;
}

struct opts_size_strict : glz::opts_size {
    bool error_on_missing_keys = true;
};

extern "C" __attribute__((used)) bool parse_rpc_command(const char* data, size_t size) {
#ifdef JF_PERF_ROUNDTRIP
    embedded_benchmark::RpcCommand cmd{};  // zero-init: full-array serialize touches unused slots
#else
    embedded_benchmark::RpcCommand cmd;
#endif
    auto error = glz::read<opts_size_strict{}>(cmd, std::string_view(data, size));

#ifdef JF_PERF_ROUNDTRIP
    auto w_err = glz::write<glz::opts_size{}>(cmd, jf_perf_scratch);
#else
    char* d = const_cast<char*>(data);
    auto w_err = glz::write<glz::opts_size{}>(cmd, d);
#endif
    return !error && !w_err;
}

#ifdef JSON_FUSION_BENCHMARK_ADDITIONAL_MODELS
// Single unified parsing function for all additional models
extern "C" __attribute__((used)) bool parse_additional_model(int model_id, const char* data, size_t size) {
    std::string_view json(data, size);
    
    switch(model_id) {
        case 1: {
            code_size_test::DeviceMetadata model;
            auto error = glz::read<glz::opts_size{}>(model, json);
            return !error;
        }
        case 2: {
            code_size_test::SensorReadings model;
            auto error = glz::read<glz::opts_size{}>(model, json);
            return !error;
        }
        case 3: {
            code_size_test::SystemStats model;
            auto error = glz::read<glz::opts_size{}>(model, json);
            return !error;
        }
        case 4: {
            code_size_test::NetworkPacket model;
            auto error = glz::read<glz::opts_size{}>(model, json);
            return !error;
        }
        case 5: {
            code_size_test::ImageDescriptor model;
            auto error = glz::read<glz::opts_size{}>(model, json);
            return !error;
        }
        case 6: {
            code_size_test::AudioConfig model;
            auto error = glz::read<glz::opts_size{}>(model, json);
            return !error;
        }
        case 7: {
            code_size_test::CacheEntry model;
            auto error = glz::read<glz::opts_size{}>(model, json);
            return !error;
        }
        case 8: {
            code_size_test::FileMetadata model;
            auto error = glz::read<glz::opts_size{}>(model, json);
            return !error;
        }
        case 9: {
            code_size_test::TransactionRecord model;
            auto error = glz::read<glz::opts_size{}>(model, json);
            return !error;
        }
        case 10: {
            code_size_test::TelemetryPacket model;
            auto error = glz::read<glz::opts_size{}>(model, json);
            return !error;
        }
        case 11: {
            code_size_test::RobotCommand model;
            auto error = glz::read<glz::opts_size{}>(model, json);
            return !error;
        }
        case 12: {
            code_size_test::WeatherData model;
            auto error = glz::read<glz::opts_size{}>(model, json);
            return !error;
        }
        case 13: {
            code_size_test::DatabaseQuery model;
            auto error = glz::read<glz::opts_size{}>(model, json);
            return !error;
        }
        case 14: {
            code_size_test::VideoStream model;
            auto error = glz::read<glz::opts_size{}>(model, json);
            return !error;
        }
        case 15: {
            code_size_test::EncryptionContext model;
            auto error = glz::read<glz::opts_size{}>(model, json);
            return !error;
        }
        case 16: {
            code_size_test::MeshNode model;
            auto error = glz::read<glz::opts_size{}>(model, json);
            return !error;
        }
        case 17: {
            code_size_test::GameState model;
            auto error = glz::read<glz::opts_size{}>(model, json);
            return !error;
        }
        case 18: {
            code_size_test::LogEntry model;
            auto error = glz::read<glz::opts_size{}>(model, json);
            return !error;
        }
        case 19: {
            code_size_test::CalendarEvent model;
            auto error = glz::read<glz::opts_size{}>(model, json);
            return !error;
        }
        case 20: {
            code_size_test::HardwareProfile model;
            auto error = glz::read<glz::opts_size{}>(model, json);
            return !error;
        }
        default:
            return false;
    }
}
#endif

// Entry point - ensures parse_config is not eliminated by linker
// In a real embedded system, this would be your main loop
int main() {
    // Call parse_config to ensure it's included in binary
    volatile bool result = parse_config("", 0);
    volatile bool rpc_result = parse_rpc_command("", 0);
    (void)result;
    (void)rpc_result;
#ifdef JSON_FUSION_BENCHMARK_ADDITIONAL_MODELS
    // Call additional model parser for all 20 models to ensure they're included in binary
    for (int i = 1; i <= 20; i++) {
        volatile bool r = parse_additional_model(i, "", 0);
        (void)r;
    }
#endif
    // Infinite loop (typical for embedded without OS)
    while(1) {}
    return 0;
}


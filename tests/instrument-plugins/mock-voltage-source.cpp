#include <instrument-script-server/plugin/PluginInterface.h>
#include <stdlib.h>
#include <string.h>

// Global state for the mock voltage source
static double g_stored_voltage = 0.0;
static int g_initialized = 0;

INSTRUMENT_PLUGIN_API PluginMetadata plugin_get_metadata(void) {
  PluginMetadata meta = {};
  meta.api_version = INSTRUMENT_PLUGIN_API_VERSION;
  strncpy(meta.name, "Mock Voltage Source", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(meta.version, "1.0.0", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(meta.protocol_type, "MockVoltageSource", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(meta.description,
          "Mock voltage source that stores and retrieves voltage values",
          PLUGIN_MAX_STRING_LEN - 1);
  return meta;
}

INSTRUMENT_PLUGIN_API int32_t plugin_initialize(const PluginConfig *config) {
  // Initialize with zero voltage
  g_stored_voltage = 0.0;
  g_initialized = 1;
  (void)config; // Unused in this simple mock
  return 0;
}

INSTRUMENT_PLUGIN_API int32_t plugin_execute_command(const PluginCommand *cmd,
                                                     PluginResponse *resp) {
  // Always initialize response metadata
  strncpy(resp->command_id, cmd->id, PLUGIN_MAX_STRING_LEN - 1);
  strncpy(resp->instrument_name, cmd->instrument_name,
          PLUGIN_MAX_STRING_LEN - 1);
  resp->success = false;
  resp->error_code = 0;
  resp->binary_response_size = 0;
  resp->has_large_data = false;

  if (!g_initialized) {
    strncpy(resp->error_message, "Plugin not initialized",
            PLUGIN_MAX_STRING_LEN - 1);
    resp->error_code = -1;
    return -1;
  }

  // SET command - set voltage
  if (strcmp(cmd->verb, "SET") == 0) {
    if (cmd->param_count == 0) {
      strncpy(resp->error_message, "Missing voltage parameter",
              PLUGIN_MAX_STRING_LEN - 1);
      resp->error_code = -2;
      return -2;
    }

    // Find voltage parameter
    double voltage = 0.0;
    int found = 0;

    for (uint32_t i = 0; i < cmd->param_count; i++) {
      if (strcmp(cmd->params[i].name, "voltage") == 0) {
        found = 1;

        // Handle different parameter types
        switch (cmd->params[i].value.type) {
        case PARAM_TYPE_DOUBLE:
          voltage = cmd->params[i].value.value.d_val;
          break;
        case PARAM_TYPE_FLOAT:
          voltage = (double)cmd->params[i].value.value.f_val;
          break;
        case PARAM_TYPE_INT32:
          voltage = (double)cmd->params[i].value.value.i32_val;
          break;
        case PARAM_TYPE_INT64:
          voltage = (double)cmd->params[i].value.value.i64_val;
          break;
        case PARAM_TYPE_UINT32:
          voltage = (double)cmd->params[i].value.value.u32_val;
          break;
        case PARAM_TYPE_UINT64:
          voltage = (double)cmd->params[i].value.value.u64_val;
          break;
        default:
          strncpy(resp->error_message, "Voltage parameter has unsupported type",
                  PLUGIN_MAX_STRING_LEN - 1);
          resp->error_code = -3;
          return -3;
        }
        break;
      }
    }

    if (!found) {
      strncpy(resp->error_message, "No voltage parameter found",
              PLUGIN_MAX_STRING_LEN - 1);
      resp->error_code = -2;
      return -2;
    }

    g_stored_voltage = voltage;
    resp->success = true;
    snprintf(resp->text_response, PLUGIN_MAX_PAYLOAD, "Voltage set to %.6f V",
             g_stored_voltage);

    // Set return value
    resp->return_value.type = PARAM_TYPE_DOUBLE;
    resp->return_value.value.d_val = g_stored_voltage;

    return 0;
  }

  // GET command - get stored voltage
  if (strcmp(cmd->verb, "GET") == 0) {
    resp->success = true;
    resp->return_value.type = PARAM_TYPE_DOUBLE;
    resp->return_value.value.d_val = g_stored_voltage;
    snprintf(resp->text_response, PLUGIN_MAX_PAYLOAD, "%.6f", g_stored_voltage);
    return 0;
  }

  // RESET command - reset voltage to zero
  if (strcmp(cmd->verb, "RESET") == 0) {
    g_stored_voltage = 0.0;
    resp->success = true;
    strncpy(resp->text_response, "Voltage reset to 0.0 V",
            PLUGIN_MAX_PAYLOAD - 1);
    resp->return_value.type = PARAM_TYPE_DOUBLE;
    resp->return_value.value.d_val = 0.0;
    return 0;
  }

  // Unknown command
  strncpy(resp->error_message, "Unknown command", PLUGIN_MAX_STRING_LEN - 1);
  resp->error_code = -1;
  return -1;
}

INSTRUMENT_PLUGIN_API void plugin_shutdown(void) { g_initialized = 0; }

#include <plugin-api.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const int MOCK_PLUGIN_MAX_PAYLOAD =
    PLUGIN_MAX_STRING_LEN - 1; // since null terminator takes 1 char
static const int MAX_CHANNEL = 32;
static const int MIN_CHANNEL = 1;
static const int NULL_CHANNEL = -1;
static const int NULL_PARAM_INDEX = -1;
static const float NULL_VOLTAGE = 0.0;
static const char *SET = "SET_VOLTAGE";
static const char *GET = "GET_VOLTAGE";
static const char *RESET = "RESET";
static const char *VOLTAGE_IO_NAME = "voltage";
static const char *ANALOG_IO_NAME = "channel";
static const int PLUGIN_INITIALIZATION_ERROR = 1;
static const int MISSING_PARAMETERS_ERROR = 2;
static const int INVALID_PARAMETER_TYPE_ERROR = 3;
static const int CHANNEL_OUT_OF_RANGE_ERROR = 4;
static const int UNKNOWN_COMMAND_ERROR = 5;

static float g_stored_voltage[MAX_CHANNEL - MIN_CHANNEL] = {NULL_VOLTAGE};
static int g_initialized = 0;
// Produces the matching array index for a given analog channel from config
static int getArrayIndex(int channel) {
  if (channel < MIN_CHANNEL || channel > MAX_CHANNEL) {
    return NULL_CHANNEL; // Invalid channel
  }
  return channel - MIN_CHANNEL; // Convert to 0-based index
}
// Copies a message into the target buffer with proper null-termination,
// ensuring it does not exceed the maximum payload size
static void payloadCopy(char *target, const char *message) {
  strncpy(target, message, MOCK_PLUGIN_MAX_PAYLOAD);
  target[MOCK_PLUGIN_MAX_PAYLOAD] = '\0'; // Ensure null-termination
}
static int setPluginError(PluginResponse *resp, const char *msg,
                          int error_code) {
  (void)resp;
  (void)msg;
  return error_code;
}
static int push_double_response(PluginResponse *resp, const char *name,
                                double value) {
  Variable out = {0};
  out.type = PARAM_TYPE_DOUBLE;
  payloadCopy(out.name, name);
  out.value.d_val = value;
  return plugin_response_push(resp, &out) == 0 ? 0
                                               : PLUGIN_INITIALIZATION_ERROR;
}
// Helper to check parameter count and set error if invalid
// cmd: the command being processed
// resp: the response struct to populate in case of error
// expected: the expected number of parameters
// desc: a description of the expected parameters for error messages
// error_code: the error code to set in case of parameter count mismatch
static int check_param_count(const PluginCommand *cmd, const int expected,
                             PluginResponse *resp) {
  uint8_t actual = param_storage_count(cmd->params);
  if (actual == expected) {
    return 0;
  }
  char msg[MOCK_PLUGIN_MAX_PAYLOAD];
  int count = snprintf(msg, MOCK_PLUGIN_MAX_PAYLOAD,
                       "Invalid number of parameters, found %d but expected %d",
                       actual, expected);
  if (count < 0 || count >= MOCK_PLUGIN_MAX_PAYLOAD) {
    // Handle snprintf error or truncation if needed
    payloadCopy(msg, "Parameters names too long");
  }
  return setPluginError(resp, msg, MISSING_PARAMETERS_ERROR);
}
// Finds the index of the parameter with the given name.
// Returns 0 on success and sets *out_index, or returns error_code and sets
// error in resp.
static int get_param_index(const PluginCommand *cmd, const char *param_name,
                           int *out_index, PluginResponse *resp) {
  uint8_t count = param_storage_count(cmd->params);
  for (uint8_t i = 0; i < count; i++) {
    const Variable *param = param_storage_get(cmd->params, i);
    if (param != NULL && strcmp(param->name, param_name) == 0) {
      *out_index = (int)i;
      return 0;
    }
  }
  char msg[MOCK_PLUGIN_MAX_PAYLOAD];
  snprintf(msg, MOCK_PLUGIN_MAX_PAYLOAD, "No %s parameter found", param_name);
  return setPluginError(resp, msg, MISSING_PARAMETERS_ERROR);
}
// Checks that the parameter at cmd->params[idx] has the expected type.
// Returns 0 if the type matches, or sets an error in resp and returns
// error_code const char *param_name is used for error messages to indicate
// which parameter had the wrong type. idx is the index of the parameter to
// check in cmd->params expected_type is the expected PARAM_TYPE_* value for the
// parameter
static int check_param_type(const PluginCommand *cmd, const char *param_name,
                            const int idx, PluginResponse *resp,
                            const int expected_type) {
  const Variable *param = param_storage_get(cmd->params, (uint8_t)idx);
  if (param == NULL) {
    return setPluginError(resp, "Missing parameter", MISSING_PARAMETERS_ERROR);
  }
  int actual = param->type;
  if (expected_type == PARAM_TYPE_INT64 && actual == PARAM_TYPE_DOUBLE)
    return 0;
  if (actual != expected_type) {
    char msg[MOCK_PLUGIN_MAX_PAYLOAD];
    snprintf(msg, MOCK_PLUGIN_MAX_PAYLOAD, "%s parameter has unsupported type",
             param_name);
    return setPluginError(resp, msg, INVALID_PARAMETER_TYPE_ERROR);
  }
  return 0;
}
// Read an integer param that may be INT32, INT64, or DOUBLE
static int read_int_param(const PluginCommand *cmd, const int idx) {
  const Variable *param = param_storage_get(cmd->params, (uint8_t)idx);
  if (param == NULL)
    return 0;
  if (param->type == PARAM_TYPE_INT64)
    return (int)param->value.i64_val;
  if (param->type == PARAM_TYPE_DOUBLE)
    return (int)param->value.d_val;
  return 0;
}
// Read a floating point param.
static float read_float_param(const PluginCommand *cmd, const int idx) {
  const Variable *param = param_storage_get(cmd->params, (uint8_t)idx);
  if (param == NULL)
    return 0.0f;
  if (param->type == PARAM_TYPE_DOUBLE)
    return (float)param->value.d_val;
  if (param->type == PARAM_TYPE_INT64)
    return (float)param->value.i64_val;
  return 0.0f;
}
// Handler for the SET_VOLTAGE command. Expects parameters "voltage" (float) and
// "analog" (int). Stores the voltage for the specified channel.
static int handle_set(const PluginCommand *cmd, PluginResponse *resp) {
  int voltage_idx, channel_idx;
  int err = check_param_count(cmd, 2, resp);
  if (!err)
    err = get_param_index(cmd, VOLTAGE_IO_NAME, &voltage_idx, resp);
  if (!err)
    err = get_param_index(cmd, ANALOG_IO_NAME, &channel_idx, resp);
  if (!err)
    err = check_param_type(cmd, VOLTAGE_IO_NAME, voltage_idx, resp,
                           PARAM_TYPE_DOUBLE);
  if (!err)
    err = check_param_type(cmd, ANALOG_IO_NAME, channel_idx, resp,
                           PARAM_TYPE_INT64);
  if (err)
    return err;
  float voltage = read_float_param(cmd, voltage_idx);
  int channel = read_int_param(cmd, channel_idx);
  int index = getArrayIndex(channel);
  if (index == NULL_CHANNEL) {
    return setPluginError(resp, "Channel out of range (must be 1-32)",
                          CHANNEL_OUT_OF_RANGE_ERROR);
  }
  g_stored_voltage[index] = voltage;
  return 0;
}
// Handler for the GET_VOLTAGE command. Expects parameter "analog" (int)
// analyzes the specified channel and returns the stored voltage value.
static int handle_get(const PluginCommand *cmd, PluginResponse *resp) {
  int channel_idx;
  int err = check_param_count(cmd, 1, resp);
  if (!err)
    err = get_param_index(cmd, ANALOG_IO_NAME, &channel_idx, resp);
  if (!err)
    err = check_param_type(cmd, ANALOG_IO_NAME, channel_idx, resp,
                           PARAM_TYPE_INT64);
  if (err)
    return err;
  int channel = read_int_param(cmd, channel_idx);
  int index = getArrayIndex(channel);
  if (index == NULL_CHANNEL) {
    return setPluginError(resp, "Channel out of range (must be 1-32)",
                          CHANNEL_OUT_OF_RANGE_ERROR);
  }
  return push_double_response(resp, VOLTAGE_IO_NAME, g_stored_voltage[index]);
}
// Handler for the RESET command. Expects no parameters. Resets all stored
// voltages to 0.0 V.
static int handle_reset(const PluginCommand *cmd, PluginResponse *resp) {
  if (check_param_count(cmd, 0, resp)) {
    return MISSING_PARAMETERS_ERROR;
  }
  for (int i = 0; i < MAX_CHANNEL - MIN_CHANNEL; ++i) {
    g_stored_voltage[i] = NULL_VOLTAGE;
  }
  return 0;
}

INSTRUMENT_PLUGIN_API PluginMetadata plugin_get_metadata(void) {
  PluginMetadata meta = {};
  meta.api_version = INSTRUMENT_PLUGIN_API_VERSION;
  payloadCopy(meta.name, "Mock Voltage Source");
  payloadCopy(meta.version, "1.0.0");
  payloadCopy(meta.protocol_type, "MockVoltageSource");
  payloadCopy(meta.description,
              "Mock voltage source that stores and retrieves voltage values");
  return meta;
}

INSTRUMENT_PLUGIN_API uint8_t plugin_initialize(const PluginConfig *config) {
  // Initialize with zero voltage
  for (int i = 0; i < MAX_CHANNEL - MIN_CHANNEL; ++i) {
    g_stored_voltage[i] = NULL_VOLTAGE;
  }
  g_initialized = 1;
  (void)config; // Unused in this simple mock
  return 0;
}

INSTRUMENT_PLUGIN_API uint8_t plugin_execute_command(const PluginCommand *cmd,
                                                     PluginResponse *resp) {
  if (!g_initialized) {
    return setPluginError(resp, "Plugin not initialized",
                          PLUGIN_INITIALIZATION_ERROR);
  }
  if (strcmp(cmd->command, SET) == 0) {
    return handle_set(cmd, resp);
  } else if (strcmp(cmd->command, GET) == 0) {
    return handle_get(cmd, resp);
  } else if (strcmp(cmd->command, RESET) == 0) {
    return handle_reset(cmd, resp);
  } else {
    return setPluginError(resp, "Unknown command", UNKNOWN_COMMAND_ERROR);
  }
}

INSTRUMENT_PLUGIN_API void plugin_shutdown(void) { g_initialized = 0; }

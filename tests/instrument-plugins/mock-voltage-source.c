#include <instrument-plugin.h>
#include <plugin-api.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const int MAX_CHANNEL = 32;
static const int MIN_CHANNEL = 1;
static const int NULL_CHANNEL = -1;
static const float NULL_VOLTAGE = 0.0;
static const char *SET = "SET_VOLTAGE";
static const char *GET = "GET_VOLTAGE";
static const char *RESET = "RESET";
static const char *VOLTAGE_IO_NAME = "voltage";
static const char *ANALOG_IO_NAME = "analog";

static const int PLUGIN_INITIALIZATION_ERROR = 1;
static const int MISSING_PARAMETERS_ERROR = 2;
static const int INVALID_PARAMETER_TYPE_ERROR = 3;
static const int CHANNEL_OUT_OF_RANGE_ERROR = 4;
static const int UNKNOWN_COMMAND_ERROR = 5;

static float g_stored_voltage[32] = {NULL_VOLTAGE};
static int g_initialized = 0;

static int getArrayIndex(int channel) {
  if (channel < MIN_CHANNEL || channel > MAX_CHANNEL) {
    return NULL_CHANNEL;
  }
  return channel - MIN_CHANNEL;
}

static int check_param_count(const PluginCommand *cmd, const int expected) {
  uint8_t count = param_storage_count(cmd->params);
  if (count == expected) {
    return 0;
  }
  return MISSING_PARAMETERS_ERROR;
}

static int get_param_index(const PluginCommand *cmd, const char *param_name, int *out_index) {
  uint8_t count = param_storage_count(cmd->params);
  for (uint8_t i = 0; i < count; i++) {
    const Variable *v = param_storage_get(cmd->params, i);
    if (v && strcmp(v->name, param_name) == 0) {
      *out_index = (int)i;
      return 0;
    }
  }
  return MISSING_PARAMETERS_ERROR;
}

static int check_param_type(const PluginCommand *cmd, const char *param_name, const int idx, const int expected_type) {
  const Variable *v = param_storage_get(cmd->params, idx);
  if (!v) return INVALID_PARAMETER_TYPE_ERROR;
  int actual = v->type;
  if (expected_type == PARAM_TYPE_DOUBLE) {
    if (actual == PARAM_TYPE_DOUBLE) {
      return 0;
    }
  }
  if (expected_type == PARAM_TYPE_INT64) {
    if (actual == PARAM_TYPE_INT64 || actual == PARAM_TYPE_DOUBLE) {
      return 0;
    }
  }
  if (actual != expected_type) {
    return INVALID_PARAMETER_TYPE_ERROR;
  }
  return 0;
}

static int read_int_param(const PluginCommand *cmd, const int idx) {
  const Variable *v = param_storage_get(cmd->params, idx);
  if (!v) return 0;
  if (v->type == PARAM_TYPE_INT64) {
    return (int)v->value.i64_val;
  }
  if (v->type == PARAM_TYPE_DOUBLE) {
    return (int)v->value.d_val;
  }
  return 0;
}

static float read_float_param(const PluginCommand *cmd, const int idx) {
  const Variable *v = param_storage_get(cmd->params, idx);
  if (!v) return 0.0f;
  if (v->type == PARAM_TYPE_DOUBLE) {
    return (float)v->value.d_val;
  }
  return 0.0f;
}

static int handle_set(const PluginCommand *cmd, PluginResponse *resp) {
  int voltage_idx, channel_idx;
  if (check_param_count(cmd, 2) ||
      get_param_index(cmd, VOLTAGE_IO_NAME, &voltage_idx) ||
      get_param_index(cmd, ANALOG_IO_NAME, &channel_idx) ||
      check_param_type(cmd, VOLTAGE_IO_NAME, voltage_idx, PARAM_TYPE_DOUBLE) ||
      check_param_type(cmd, ANALOG_IO_NAME, channel_idx, PARAM_TYPE_INT64)) {
    return INVALID_PARAMETER_TYPE_ERROR;
  }
  float voltage = read_float_param(cmd, voltage_idx);
  int channel = read_int_param(cmd, channel_idx);
  int index = getArrayIndex(channel);
  if (index == NULL_CHANNEL) {
    return CHANNEL_OUT_OF_RANGE_ERROR;
  }
  g_stored_voltage[index] = voltage;
  return 0;
}

static int handle_get(const PluginCommand *cmd, PluginResponse *resp) {
  int channel_idx;
  if (check_param_count(cmd, 1) ||
      get_param_index(cmd, ANALOG_IO_NAME, &channel_idx) ||
      check_param_type(cmd, ANALOG_IO_NAME, channel_idx, PARAM_TYPE_INT64)) {
    return INVALID_PARAMETER_TYPE_ERROR;
  }
  int channel = read_int_param(cmd, channel_idx);
  int index = getArrayIndex(channel);
  if (index == NULL_CHANNEL) {
    return CHANNEL_OUT_OF_RANGE_ERROR;
  }
  Variable var = {0};
  var.type = PARAM_TYPE_DOUBLE;
  strncpy(var.name, "voltage", PLUGIN_MAX_STRING_LEN - 1);
  var.value.d_val = g_stored_voltage[index];
  plugin_response_push(resp, &var);
  return 0;
}

static int handle_reset(const PluginCommand *cmd, PluginResponse *resp) {
  if (check_param_count(cmd, 0)) {
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
  strncpy(meta.name, "Mock Voltage Source", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(meta.version, "1.0.0", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(meta.protocol_type, "MockVoltageSource", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(meta.description, "Mock voltage source plugin", PLUGIN_MAX_STRING_LEN - 1);
  return meta;
}

INSTRUMENT_PLUGIN_API uint8_t plugin_initialize(const PluginConfig *config) {
  for (int i = 0; i < MAX_CHANNEL - MIN_CHANNEL; ++i) {
    g_stored_voltage[i] = NULL_VOLTAGE;
  }
  g_initialized = 1;
  (void)config;
  return 0;
}

INSTRUMENT_PLUGIN_API uint8_t plugin_execute_command(const PluginCommand *cmd,
                                                     PluginResponse *resp) {
  if (!g_initialized) {
    return PLUGIN_INITIALIZATION_ERROR;
  }
  if (strcmp(cmd->command, SET) == 0) {
    return (uint8_t)handle_set(cmd, resp);
  } else if (strcmp(cmd->command, GET) == 0) {
    return (uint8_t)handle_get(cmd, resp);
  } else if (strcmp(cmd->command, RESET) == 0) {
    return (uint8_t)handle_reset(cmd, resp);
  } else {
    return (uint8_t)UNKNOWN_COMMAND_ERROR;
  }
}

INSTRUMENT_PLUGIN_API void plugin_shutdown(void) { g_initialized = 0; }

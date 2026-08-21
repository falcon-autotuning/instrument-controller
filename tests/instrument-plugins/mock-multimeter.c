#include <instrument-data.h>
<<<<<<< HEAD
#include <instrument-plugin.h>
=======
>>>>>>> hub_iss_dev
#include <plugin-api.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const int MAX_CHANNEL = 8;
static const int MIN_CHANNEL = 1;
static const int NULL_CHANNEL = -1;
static const int NULL_PARAM_INDEX = -1;
static const int NULL_BINS = 0;
static const int NULL_RATE = 0;
static const char *SET_RATE = "SET_SAMPLE_RATE";
static const char *SET_BINS = "SET_BINS";
static const char *GET_DATAPOINT = "GET_DATAPOINT";
static const char *MEASURE_STREAM = "MEASURE_STREAM";
static const char *RESET = "RESET";
static const char *RATE_IO_NAME = "sample_rate";
static const char *BINS_IO_NAME = "bins";
<<<<<<< HEAD
static const char *ANALOG_IO_NAME = "analog";

=======
static const char *ANALOG_IO_NAME = "channel";
static const char *VOLTAGE_IO_NAME = "voltage";
static const char *STREAM_IO_NAME = "stream";
>>>>>>> hub_iss_dev
static const int PLUGIN_INITIALIZATION_ERROR = 1;
static const int MISSING_PARAMETERS_ERROR = 2;
static const int INVALID_PARAMETER_TYPE_ERROR = 3;
static const int BINS_OUT_OF_RANGE_ERROR = 4;
static const int INVALID_RATE_ERROR = 5;
static const int NO_DATA_ERROR = 6;
static const int CHANNEL_OUT_OF_RANGE_ERROR = 7;
static const int MEMORY_ALLOCATION_ERROR = 8;
static const int UNKNOWN_COMMAND_ERROR = 9;

// Global state for the mock multimeter
static char g_data_file_path[8][PLUGIN_MAX_STRING_LEN] = {{0}};
static int g_num_bins[8] = {0};
static double *g_data_buffer[8] = {NULL};
static int g_data_count[8] = {0};
static int g_current_index[8] = {0};
static int g_initialized = 0;
static char g_instrument_name[PLUGIN_MAX_STRING_LEN] = {0};

static int getArrayIndex(int channel) {
  if (channel < MIN_CHANNEL || channel > MAX_CHANNEL) {
    return NULL_CHANNEL;
  }
  return channel - MIN_CHANNEL;
}
<<<<<<< HEAD

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
=======
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
static int push_int_response(PluginResponse *resp, const char *name,
                             int64_t value) {
  Variable out = {0};
  out.type = PARAM_TYPE_INT64;
  payloadCopy(out.name, name);
  out.value.i64_val = value;
  return plugin_response_push(resp, &out) == 0 ? 0 : MEMORY_ALLOCATION_ERROR;
}
static int push_double_response(PluginResponse *resp, const char *name,
                                double value) {
  Variable out = {0};
  out.type = PARAM_TYPE_DOUBLE;
  payloadCopy(out.name, name);
  out.value.d_val = value;
  return plugin_response_push(resp, &out) == 0 ? 0 : MEMORY_ALLOCATION_ERROR;
}
static int push_buffer_response(PluginResponse *resp, const char *name,
                                const char *buffer_id) {
  Variable out = {0};
  out.type = PARAM_TYPE_BUFFER;
  payloadCopy(out.name, name);
  payloadCopy(out.value.str_val, buffer_id);
  return plugin_response_push(resp, &out) == 0 ? 0 : MEMORY_ALLOCATION_ERROR;
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
>>>>>>> hub_iss_dev
      *out_index = (int)i;
      return 0;
    }
  }
  return MISSING_PARAMETERS_ERROR;
}
<<<<<<< HEAD

static int check_param_type(const PluginCommand *cmd, const char *param_name, const int idx, const int expected_type) {
  const Variable *v = param_storage_get(cmd->params, idx);
  if (!v) return INVALID_PARAMETER_TYPE_ERROR;
  int actual = v->type;
  // Since v2 doesn't have INT32, we map expected type to allow INT64 or DOUBLE
  if (expected_type == PARAM_TYPE_INT64) {
    if (actual == PARAM_TYPE_INT64 || actual == PARAM_TYPE_DOUBLE) {
      return 0;
    }
  }
=======
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
>>>>>>> hub_iss_dev
  if (actual != expected_type) {
    return INVALID_PARAMETER_TYPE_ERROR;
  }
  return 0;
}

static int read_int_param(const PluginCommand *cmd, const int idx) {
<<<<<<< HEAD
  const Variable *v = param_storage_get(cmd->params, idx);
  if (!v) return 0;
  if (v->type == PARAM_TYPE_INT64) {
    return (int)v->value.i64_val;
  }
  if (v->type == PARAM_TYPE_DOUBLE) {
    return (int)v->value.d_val;
  }
=======
  const Variable *param = param_storage_get(cmd->params, (uint8_t)idx);
  if (param == NULL)
    return 0;
  if (param->type == PARAM_TYPE_INT64)
    return (int)param->value.i64_val;
  if (param->type == PARAM_TYPE_DOUBLE)
    return (int)param->value.d_val;
>>>>>>> hub_iss_dev
  return 0;
}

static int load_data_from_file(void) {
  const char *env = getenv("MOCK_MULTIMETER_DATA_FILE");
  if (!env) {
    return -1;
  }

  char paths_buf[PLUGIN_MAX_STRING_LEN * MAX_CHANNEL];
  strncpy(paths_buf, env, sizeof(paths_buf) - 1);
  paths_buf[sizeof(paths_buf) - 1] = '\0';

  const char *filepaths[8] = {0};
  int num_paths = 0;
  char *token = strtok(paths_buf, ";");
  while (token && num_paths < MAX_CHANNEL) {
    filepaths[num_paths++] = token;
    token = strtok(NULL, ";");
  }
  if (num_paths == 0) {
    return -1;
  }

  for (int ch = 0; ch < MAX_CHANNEL; ++ch) {
    const char *filepath = filepaths[(ch < num_paths) ? ch : (num_paths - 1)];
    FILE *f = fopen(filepath, "r");
    if (!f) {
      return -1;
    }
    int count = 0;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
      if (line[0] != '#' && line[0] != '\n' && strlen(line) > 1) {
        count++;
      }
    }
    if (count == 0) {
      fclose(f);
      return -1;
    }
    if (g_data_buffer[ch])
      free(g_data_buffer[ch]);
    g_data_buffer[ch] = (double *)malloc(count * sizeof(double));
    if (!g_data_buffer[ch]) {
      fclose(f);
      return -1;
    }
    rewind(f);
    g_data_count[ch] = 0;
    while (fgets(line, sizeof(line), f) && g_data_count[ch] < count) {
      if (line[0] != '#' && line[0] != '\n' && strlen(line) > 1) {
        g_data_buffer[ch][g_data_count[ch]] = strtod(line, NULL);
        g_data_count[ch]++;
      }
    }
    fclose(f);
    strncpy(g_data_file_path[ch], filepath, PLUGIN_MAX_STRING_LEN - 1);
    g_data_file_path[ch][PLUGIN_MAX_STRING_LEN - 1] = '\0';
  }
  return 0;
}

static int handle_bins(const PluginCommand *cmd, PluginResponse *resp) {
  int bins_idx, channel_idx;
<<<<<<< HEAD
  if (check_param_count(cmd, 2) ||
      get_param_index(cmd, BINS_IO_NAME, &bins_idx) ||
      get_param_index(cmd, ANALOG_IO_NAME, &channel_idx) ||
      check_param_type(cmd, BINS_IO_NAME, bins_idx, PARAM_TYPE_INT64) ||
      check_param_type(cmd, ANALOG_IO_NAME, channel_idx, PARAM_TYPE_INT64)) {
    return INVALID_PARAMETER_TYPE_ERROR;
  }
=======
  int err = check_param_count(cmd, 2, resp);
  if (!err)
    err = get_param_index(cmd, BINS_IO_NAME, &bins_idx, resp);
  if (!err)
    err = get_param_index(cmd, ANALOG_IO_NAME, &channel_idx, resp);
  if (!err)
    err = check_param_type(cmd, BINS_IO_NAME, bins_idx, resp,
                           PARAM_TYPE_INT64);
  if (!err)
    err = check_param_type(cmd, ANALOG_IO_NAME, channel_idx, resp,
                           PARAM_TYPE_INT64);
  if (err)
    return err;
>>>>>>> hub_iss_dev
  int bins = read_int_param(cmd, bins_idx);
  int channel = read_int_param(cmd, channel_idx);
  if (bins <= NULL_BINS) {
    return BINS_OUT_OF_RANGE_ERROR;
  }
  int index = getArrayIndex(channel);
  if (index == NULL_CHANNEL) {
    return CHANNEL_OUT_OF_RANGE_ERROR;
  }
  g_num_bins[index] = bins;
  if (g_num_bins[index] > g_data_count[index]) {
    g_num_bins[index] = g_data_count[index];
  }
<<<<<<< HEAD
  Variable var = {0};
  var.type = PARAM_TYPE_INT64;
  strncpy(var.name, "bins", PLUGIN_MAX_STRING_LEN - 1);
  var.value.i64_val = g_num_bins[index];
  plugin_response_push(resp, &var);
  return 0;
=======
  return push_int_response(resp, BINS_IO_NAME, g_num_bins[index]);
>>>>>>> hub_iss_dev
}

static int handle_rate(const PluginCommand *cmd, PluginResponse *resp) {
  int rate_idx, channel_idx;
<<<<<<< HEAD
  if (check_param_count(cmd, 2) ||
      get_param_index(cmd, RATE_IO_NAME, &rate_idx) ||
      get_param_index(cmd, ANALOG_IO_NAME, &channel_idx) ||
      check_param_type(cmd, RATE_IO_NAME, rate_idx, PARAM_TYPE_INT64) ||
      check_param_type(cmd, ANALOG_IO_NAME, channel_idx, PARAM_TYPE_INT64)) {
    return INVALID_PARAMETER_TYPE_ERROR;
  }
  int rate = read_int_param(cmd, rate_idx);
  Variable var = {0};
  var.type = PARAM_TYPE_INT64;
  strncpy(var.name, "sample_rate", PLUGIN_MAX_STRING_LEN - 1);
  var.value.i64_val = rate;
  plugin_response_push(resp, &var);
  return 0;
=======
  int err = check_param_count(cmd, 2, resp);
  if (!err)
    err = get_param_index(cmd, RATE_IO_NAME, &rate_idx, resp);
  if (!err)
    err = get_param_index(cmd, ANALOG_IO_NAME, &channel_idx, resp);
  if (!err)
    err = check_param_type(cmd, RATE_IO_NAME, rate_idx, resp,
                           PARAM_TYPE_INT64);
  if (!err)
    err = check_param_type(cmd, ANALOG_IO_NAME, channel_idx, resp,
                           PARAM_TYPE_INT64);
  if (err)
    return err;
  int rate = read_int_param(cmd, rate_idx);
  int channel = read_int_param(cmd, channel_idx);
  if (rate <= NULL_RATE) {
    return setPluginError(resp, "sample_rate must be > 0", INVALID_RATE_ERROR);
  }
  // TODO: Store and use the rate
  return push_int_response(resp, RATE_IO_NAME, rate);
>>>>>>> hub_iss_dev
}

static int handle_datapoint(const PluginCommand *cmd, PluginResponse *resp) {
  int channel_idx;
<<<<<<< HEAD
  if (check_param_count(cmd, 1) ||
      get_param_index(cmd, ANALOG_IO_NAME, &channel_idx) ||
      check_param_type(cmd, ANALOG_IO_NAME, channel_idx, PARAM_TYPE_INT64)) {
    return INVALID_PARAMETER_TYPE_ERROR;
  }
=======
  int err = check_param_count(cmd, 1, resp);
  if (!err)
    err = get_param_index(cmd, ANALOG_IO_NAME, &channel_idx, resp);
  if (!err)
    err = check_param_type(cmd, ANALOG_IO_NAME, channel_idx, resp,
                           PARAM_TYPE_INT64);
  if (err)
    return err;
>>>>>>> hub_iss_dev
  int channel = read_int_param(cmd, channel_idx);
  int index = getArrayIndex(channel);
  if (index == NULL_CHANNEL) {
    return CHANNEL_OUT_OF_RANGE_ERROR;
  }
  if (g_data_count[index] == 0) {
    return NO_DATA_ERROR;
  }

  if (g_current_index[index] >= g_data_count[index]) {
    g_current_index[index] = 0;
  }
<<<<<<< HEAD
  Variable var = {0};
  var.type = PARAM_TYPE_DOUBLE;
  strncpy(var.name, "voltage", PLUGIN_MAX_STRING_LEN - 1);
  var.value.d_val = g_data_buffer[index][g_current_index[index]];
  g_current_index[index]++;
  plugin_response_push(resp, &var);
  return 0;
=======
  double value = g_data_buffer[index][g_current_index[index]];
  g_current_index[index]++;
  return push_double_response(resp, VOLTAGE_IO_NAME, value);
>>>>>>> hub_iss_dev
}

static int handle_stream(const PluginCommand *cmd, PluginResponse *resp) {
  int channel_idx;
<<<<<<< HEAD
  if (check_param_count(cmd, 1) ||
      get_param_index(cmd, ANALOG_IO_NAME, &channel_idx) ||
      check_param_type(cmd, ANALOG_IO_NAME, channel_idx, PARAM_TYPE_INT64)) {
    return INVALID_PARAMETER_TYPE_ERROR;
  }
=======
  int err = check_param_count(cmd, 1, resp);
  if (!err)
    err = get_param_index(cmd, ANALOG_IO_NAME, &channel_idx, resp);
  if (!err)
    err = check_param_type(cmd, ANALOG_IO_NAME, channel_idx, resp,
                           PARAM_TYPE_INT64);
  if (err)
    return err;
>>>>>>> hub_iss_dev
  int channel = read_int_param(cmd, channel_idx);
  int index = getArrayIndex(channel);
  if (index == NULL_CHANNEL) {
    return CHANNEL_OUT_OF_RANGE_ERROR;
  }
  if (g_data_count[index] == 0) {
    return NO_DATA_ERROR;
  }

  double *result = (double *)malloc(g_num_bins[index] * sizeof(double));
  if (!result) {
    return MEMORY_ALLOCATION_ERROR;
  }
  for (int i = 0; i < g_num_bins[index]; i++) {
    if (g_current_index[index] >= g_data_count[index]) {
      g_current_index[index] = 0;
    }
    result[i] = g_data_buffer[index][g_current_index[index]];
    g_current_index[index]++;
  }
  const char *buffer_id = data_manager_create_buffer(
<<<<<<< HEAD
      cmd->id, cmd->id, INST_DATA_FLOAT64, g_num_bins[index],
      result);
=======
      g_instrument_name, cmd->id, INST_DATA_FLOAT64, g_num_bins[index], result);
>>>>>>> hub_iss_dev
  free(result);
  if (!buffer_id) {
    return MEMORY_ALLOCATION_ERROR;
  }
<<<<<<< HEAD
  Variable var = {0};
  var.type = PARAM_TYPE_BUFFER;
  strncpy(var.name, "data", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(var.value.str_val, buffer_id, PLUGIN_MAX_STRING_LEN - 1);
  plugin_response_push(resp, &var);
  return 0;
=======
  return push_buffer_response(resp, STREAM_IO_NAME, buffer_id);
>>>>>>> hub_iss_dev
}

static int handle_reset(const PluginCommand *cmd, PluginResponse *resp) {
<<<<<<< HEAD
  if (check_param_count(cmd, 0)) {
=======
  if (check_param_count(cmd, 0, resp)) {
>>>>>>> hub_iss_dev
    return MISSING_PARAMETERS_ERROR;
  }
  for (int i = 0; i < MAX_CHANNEL - MIN_CHANNEL + 1; ++i) {
    g_current_index[i] = 0;
  }
  return 0;
}

INSTRUMENT_PLUGIN_API PluginMetadata plugin_get_metadata(void) {
  PluginMetadata meta = {};
  meta.api_version = INSTRUMENT_PLUGIN_API_VERSION;
  strncpy(meta.name, "Mock Multimeter", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(meta.version, "1.0.0", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(meta.protocol_type, "MockMultimeter", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(meta.description, "Mock multimeter plugin", PLUGIN_MAX_STRING_LEN - 1);
  return meta;
}

INSTRUMENT_PLUGIN_API uint8_t plugin_initialize(const PluginConfig *config) {
  if (load_data_from_file() != 0) {
    return PLUGIN_INITIALIZATION_ERROR;
  }
  if (config != NULL) {
    payloadCopy(g_instrument_name, config->instrument_name);
  }
  for (int i = 0; i < MAX_CHANNEL - MIN_CHANNEL + 1; ++i) {
    g_current_index[i] = 0;
  }
  g_initialized = 1;
  return 0;
}

INSTRUMENT_PLUGIN_API uint8_t plugin_execute_command(const PluginCommand *cmd,
                                                     PluginResponse *resp) {
  if (!g_initialized) {
    return PLUGIN_INITIALIZATION_ERROR;
  }
  if (strcmp(cmd->command, SET_RATE) == 0) {
<<<<<<< HEAD
    return (uint8_t)handle_rate(cmd, resp);
  } else if (strcmp(cmd->command, SET_BINS) == 0) {
    return (uint8_t)handle_bins(cmd, resp);
  } else if (strcmp(cmd->command, MEASURE_STREAM) == 0) {
    return (uint8_t)handle_stream(cmd, resp);
  } else if (strcmp(cmd->command, GET_DATAPOINT) == 0) {
    return (uint8_t)handle_datapoint(cmd, resp);
  } else if (strcmp(cmd->command, RESET) == 0) {
    return (uint8_t)handle_reset(cmd, resp);
=======
    return handle_rate(cmd, resp);
  } else if (strcmp(cmd->command, SET_BINS) == 0) {
    return handle_bins(cmd, resp);
  } else if (strcmp(cmd->command, MEASURE_STREAM) == 0) {
    return handle_stream(cmd, resp);
  } else if (strcmp(cmd->command, GET_DATAPOINT) == 0) {
    return handle_datapoint(cmd, resp);
  } else if (strcmp(cmd->command, RESET) == 0) {
    return handle_reset(cmd, resp);
>>>>>>> hub_iss_dev
  } else {
    return (uint8_t)UNKNOWN_COMMAND_ERROR;
  }
}

INSTRUMENT_PLUGIN_API void plugin_shutdown(void) {
  for (int i = 0; i < (MAX_CHANNEL - MIN_CHANNEL + 1); ++i) {
    if (g_data_buffer[i]) {
      free(g_data_buffer[i]);
      g_data_buffer[i] = NULL;
    }
    g_num_bins[i] = 0;
    g_data_count[i] = 0;
    g_current_index[i] = 0;
    g_data_file_path[i][0] = '\0';
  }
  g_instrument_name[0] = '\0';
  g_initialized = 0;
}

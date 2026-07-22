#include <instrument-data.h>
#include <plugin-api.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const int MOCK_PLUGIN_MAX_PAYLOAD =
    PLUGIN_MAX_STRING_LEN - 1; // since null terminator takes 1 char
static const int MAX_CHANNEL = 8;
static const int MIN_CHANNEL = 1;
static const int NULL_CHANNEL = -1;
static const int NULL_PARAM_INDEX = -1;
static const float NULL_VOLTAGE = 0.0;
static const int NULL_BINS = 0;
static const int NULL_RATE = 0;
static const char *SET_RATE = "SET_SAMPLE_RATE";
static const char *SET_BINS = "SET_BINS";
static const char *GET_DATAPOINT = "GET_DATAPOINT";
static const char *MEASURE_STREAM = "MEASURE_STREAM";
static const char *RESET = "RESET";
static const char *RATE_IO_NAME = "sample_rate";
static const char *BINS_IO_NAME = "bins";
static const char *ANALOG_IO_NAME = "channel";
static const char *VOLTAGE_IO_NAME = "voltage";
static const char *STREAM_IO_NAME = "stream";
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
// Array size: MAX_CHANNEL - MIN_CHANNEL + 1 so that getArrayIndex(MAX_CHANNEL)
// = MAX_CHANNEL - MIN_CHANNEL is a valid index.
static char g_data_file_path[MAX_CHANNEL - MIN_CHANNEL + 1][PLUGIN_MAX_STRING_LEN] =
    {{0}};
static int g_num_bins[MAX_CHANNEL - MIN_CHANNEL + 1] = {0};
static double *g_data_buffer[MAX_CHANNEL - MIN_CHANNEL + 1] = {NULL};
static int g_data_count[MAX_CHANNEL - MIN_CHANNEL + 1] = {0};
static int g_current_index[MAX_CHANNEL - MIN_CHANNEL + 1] = {0};
static int g_initialized = 0;
static char g_instrument_name[PLUGIN_MAX_STRING_LEN] = {0};

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
/**
 * Load data from file(s) into memory for all channels.
 * Reads file paths from environment variable MOCK_MULTIMETER_DATA_FILE,
 * separated by semicolons. If fewer than MAX_CHANNEL paths are supplied,
 * the last path is used for all remaining channels.
 */
static int load_data_from_file(void) {
  const char *env = getenv("MOCK_MULTIMETER_DATA_FILE");
  if (!env) {
    return -1; // Environment variable not set
  }

  // Split env string into file paths
  char paths_buf[PLUGIN_MAX_STRING_LEN * MAX_CHANNEL];
  strncpy(paths_buf, env, sizeof(paths_buf) - 1);
  paths_buf[sizeof(paths_buf) - 1] = '\0';

  const char *filepaths[MAX_CHANNEL] = {0};
  int num_paths = 0;
  char *token = strtok(paths_buf, ";");
  while (token && num_paths < MAX_CHANNEL) {
    filepaths[num_paths++] = token;
    token = strtok(NULL, ";");
  }
  if (num_paths == 0) {
    return -1; // No valid paths found
  }

  // Use the last path for remaining channels if fewer than MAX_CHANNEL
  for (int ch = 0; ch < MAX_CHANNEL; ++ch) {
    const char *filepath = filepaths[(ch < num_paths) ? ch : (num_paths - 1)];
    FILE *f = fopen(filepath, "r");
    if (!f) {
      return -1; // File not found
    }
    // Count lines first (skip comments and empty lines)
    int count = 0;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
      if (line[0] != '#' && line[0] != '\n' && strlen(line) > 1) {
        count++;
      }
    }
    if (count == 0) {
      fclose(f);
      return -1; // No valid data found
    }
    // Allocate buffer
    if (g_data_buffer[ch])
      free(g_data_buffer[ch]);
    g_data_buffer[ch] = (double *)malloc(count * sizeof(double));
    if (!g_data_buffer[ch]) {
      fclose(f);
      return -1; // Allocation failed
    }
    // Read data
    rewind(f);
    g_data_count[ch] = 0;
    while (fgets(line, sizeof(line), f) && g_data_count[ch] < count) {
      if (line[0] != '#' && line[0] != '\n' && strlen(line) > 1) {
        g_data_buffer[ch][g_data_count[ch]] = strtod(line, NULL);
        g_data_count[ch]++;
      }
    }
    fclose(f);
    strncpy(g_data_file_path[ch], filepath, MOCK_PLUGIN_MAX_PAYLOAD);
  }
  return 0;
}

// Handler for the SET_BINS command. Expects parameters bins (int)
// and "analog" (int). Stores the number of averaging bins for the specified
// channel.
static int handle_bins(const PluginCommand *cmd, PluginResponse *resp) {
  int bins_idx, channel_idx;
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
  int bins = read_int_param(cmd, bins_idx);
  int channel = read_int_param(cmd, channel_idx);
  if (bins <= NULL_BINS) {
    return setPluginError(resp, "num_bins must be > 0",
                          BINS_OUT_OF_RANGE_ERROR);
  }
  int index = getArrayIndex(channel);
  if (index == NULL_CHANNEL) {
    return setPluginError(resp, "Channel out of range (must be 1-8)",
                          CHANNEL_OUT_OF_RANGE_ERROR);
  }
  g_num_bins[index] = bins;
  if (g_num_bins[index] > g_data_count[index]) {
    g_num_bins[index] = g_data_count[index];
  }
  return push_int_response(resp, BINS_IO_NAME, g_num_bins[index]);
}

// Handler for the SET_RATE command. Expects parameters rate (int)
// and "analog" (int). Stores the sample rate for the specified
// channel.
static int handle_rate(const PluginCommand *cmd, PluginResponse *resp) {
  int rate_idx, channel_idx;
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
}

// Handler for the GET_DATAPOINT command. Expects parameter "analog" (int) for
// channel number (1-8). Returns the next voltage value from the data stream
// for that channel, or an error if the channel is out of range.
static int handle_datapoint(const PluginCommand *cmd, PluginResponse *resp) {
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
    return setPluginError(resp, "Channel out of range (must be 1-8)",
                          CHANNEL_OUT_OF_RANGE_ERROR);
  }
  if (g_data_count[index] == 0) {
    return setPluginError(resp, "No data loaded", NO_DATA_ERROR);
  }

  if (g_current_index[index] >= g_data_count[index]) {
    g_current_index[index] = 0; // Wrap around
  }
  double value = g_data_buffer[index][g_current_index[index]];
  g_current_index[index]++;
  return push_double_response(resp, VOLTAGE_IO_NAME, value);
}

// Handler for the MEASURE_STREAM command. Expects parameter "analog" (int).
static int handle_stream(const PluginCommand *cmd, PluginResponse *resp) {
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
    return setPluginError(resp, "Channel out of range (must be 1-8)",
                          CHANNEL_OUT_OF_RANGE_ERROR);
  }
  if (g_data_count[index] == 0) {
    return setPluginError(resp, "No data loaded", NO_DATA_ERROR);
  }

  // Create array of data points
  double *result = (double *)malloc(g_num_bins[index] * sizeof(double));
  // Fill array
  for (int i = 0; i < g_num_bins[index]; i++) {
    if (g_current_index[index] >= g_data_count[index]) {
      g_current_index[index] = 0; // Wrap around
    }
    result[i] = g_data_buffer[index][g_current_index[index]];
    g_current_index[index]++;
  }
  if (!result) {
    return setPluginError(resp, "Memory allocation failed",
                          MEMORY_ALLOCATION_ERROR);
  }
  // Create buffer
  const char *buffer_id = data_manager_create_buffer(
      g_instrument_name, cmd->id, INST_DATA_FLOAT64, g_num_bins[index], result);
  free(result);
  if (!buffer_id) {
    return setPluginError(resp, "Failed to create data buffer",
                          MEMORY_ALLOCATION_ERROR);
  }
  return push_buffer_response(resp, STREAM_IO_NAME, buffer_id);
}

// Handler for the RESET command. Expects no parameters. Resets the data pointer
// to the beginning of the loaded data stream.
static int handle_reset(const PluginCommand *cmd, PluginResponse *resp) {
  if (check_param_count(cmd, 0, resp)) {
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
  payloadCopy(meta.name, "Mock Multimeter");
  payloadCopy(meta.version, "1.0.0");
  payloadCopy(meta.protocol_type, "MockMultimeter");
  payloadCopy(meta.description,
              "Mock multimeter that loads data from file via "
              "MOCK_MULTIMETER_DATA_FILE environment variable");
  return meta;
}

INSTRUMENT_PLUGIN_API uint8_t plugin_initialize(const PluginConfig *config) {
  if (load_data_from_file() != 0) {
    return PLUGIN_INITIALIZATION_ERROR; // Failed to load data file
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
    return setPluginError(resp, "Plugin not initialized",
                          PLUGIN_INITIALIZATION_ERROR);
  }
  if (strcmp(cmd->command, SET_RATE) == 0) {
    return handle_rate(cmd, resp);
  } else if (strcmp(cmd->command, SET_BINS) == 0) {
    return handle_bins(cmd, resp);
  } else if (strcmp(cmd->command, MEASURE_STREAM) == 0) {
    return handle_stream(cmd, resp);
  } else if (strcmp(cmd->command, GET_DATAPOINT) == 0) {
    return handle_datapoint(cmd, resp);
  } else if (strcmp(cmd->command, RESET) == 0) {
    return handle_reset(cmd, resp);
  } else {
    return setPluginError(resp, "Unknown command", UNKNOWN_COMMAND_ERROR);
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
    g_data_file_path[i][0] = '\0'; // Clear string
  }
  g_instrument_name[0] = '\0';
  g_initialized = 0;
}

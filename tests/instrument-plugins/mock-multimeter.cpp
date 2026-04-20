#include <instrument-script-server/plugin/PluginInterface.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Global state for the mock multimeter
static char g_data_file_path[PLUGIN_MAX_STRING_LEN] = "";
static int g_num_bins = 100;
static double *g_data_buffer = NULL;
static int g_data_count = 0;
static int g_current_index = 0;
static int g_initialized = 0;

/**
 * Load data from file into memory
 * Reads data from environment variable MOCK_MULTIMETER_DATA_FILE
 */
static int load_data_from_file(void) {
  const char *filepath = getenv("MOCK_MULTIMETER_DATA_FILE");
  if (!filepath) {
    return -1; // Environment variable not set
  }

  FILE *f = fopen(filepath, "r");
  if (!f) {
    return -1; // File not found
  }

  // Count lines first (skip comments and empty lines)
  int count = 0;
  char line[256];
  while (fgets(line, sizeof(line), f)) {
    // Skip comments and empty lines
    if (line[0] != '#' && line[0] != '\n' && strlen(line) > 1) {
      count++;
    }
  }

  if (count == 0) {
    fclose(f);
    return -1; // No valid data found
  }

  // Allocate buffer
  g_data_buffer = (double *)malloc(count * sizeof(double));
  if (!g_data_buffer) {
    fclose(f);
    return -1; // Allocation failed
  }

  // Read data
  rewind(f);
  g_data_count = 0;
  while (fgets(line, sizeof(line), f) && g_data_count < count) {
    if (line[0] != '#' && line[0] != '\n' && strlen(line) > 1) {
      g_data_buffer[g_data_count] = strtod(line, NULL);
      g_data_count++;
    }
  }

  fclose(f);
  strncpy(g_data_file_path, filepath, PLUGIN_MAX_STRING_LEN - 1);
  return 0;
}

INSTRUMENT_PLUGIN_API PluginMetadata plugin_get_metadata(void) {
  PluginMetadata meta = {};
  meta.api_version = INSTRUMENT_PLUGIN_API_VERSION;
  strncpy(meta.name, "Mock Multimeter", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(meta.version, "1.0.0", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(meta.protocol_type, "MockMultimeter", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(meta.description,
          "Mock multimeter that loads data from file via "
          "MOCK_MULTIMETER_DATA_FILE environment variable",
          PLUGIN_MAX_STRING_LEN - 1);
  return meta;
}

INSTRUMENT_PLUGIN_API int32_t plugin_initialize(const PluginConfig *config) {
  // Load data from environment variable
  if (load_data_from_file() != 0) {
    return -1; // Failed to load data file
  }

  g_current_index = 0;
  g_initialized = 1;
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

  // SET_BINS command - set number of bins to return per measurement
  if (strcmp(cmd->verb, "SET_BINS") == 0) {
    if (cmd->param_count > 0 && strcmp(cmd->params[0].name, "num_bins") == 0) {
      if (cmd->params[0].value.type == PARAM_TYPE_INT32) {
        int bins = cmd->params[0].value.value.i32_val;
        if (bins <= 0) {
          strncpy(resp->error_message, "num_bins must be > 0",
                  PLUGIN_MAX_STRING_LEN - 1);
          resp->error_code = -2;
          return -2;
        }
        g_num_bins = bins;
        if (g_num_bins > g_data_count) {
          g_num_bins = g_data_count;
        }
        resp->success = true;
        snprintf(resp->text_response, PLUGIN_MAX_PAYLOAD,
                 "Number of bins set to %d", g_num_bins);
        resp->return_value.type = PARAM_TYPE_INT32;
        resp->return_value.value.i32_val = g_num_bins;
        return 0;
      }
    }
    strncpy(resp->error_message, "Invalid parameter for SET_BINS",
            PLUGIN_MAX_STRING_LEN - 1);
    resp->error_code = -1;
    return -1;
  }

  // GET_DATAPOINT command - get a single data point
  if (strcmp(cmd->verb, "GET_DATAPOINT") == 0) {
    if (g_data_count == 0) {
      strncpy(resp->error_message, "No data loaded", PLUGIN_MAX_STRING_LEN - 1);
      resp->error_code = -3;
      return -3;
    }

    if (g_current_index >= g_data_count) {
      g_current_index = 0; // Wrap around
    }

    double value = g_data_buffer[g_current_index];
    g_current_index++;

    resp->success = true;
    resp->return_value.type = PARAM_TYPE_DOUBLE;
    resp->return_value.value.d_val = value;
    snprintf(resp->text_response, PLUGIN_MAX_PAYLOAD, "%.6f", value);
    return 0;
  }

  // MEASURE_STREAM command - get multiple data points at once
  if (strcmp(cmd->verb, "MEASURE_STREAM") == 0) {
    if (g_data_count == 0) {
      strncpy(resp->error_message, "No data loaded", PLUGIN_MAX_STRING_LEN - 1);
      resp->error_code = -3;
      return -3;
    }

    int num_points = g_num_bins;

    // Check for optional count parameter
    if (cmd->param_count > 0) {
      for (uint32_t i = 0; i < cmd->param_count; i++) {
        if (strcmp(cmd->params[i].name, "count") == 0) {
          if (cmd->params[i].value.type == PARAM_TYPE_INT32) {
            num_points = cmd->params[i].value.value.i32_val;
          } else if (cmd->params[i].value.type == PARAM_TYPE_INT64) {
            num_points = (int)cmd->params[i].value.value.i64_val;
          }
          break;
        }
      }
    }

    // Validate count
    if (num_points <= 0) {
      strncpy(resp->error_message, "count must be > 0",
              PLUGIN_MAX_STRING_LEN - 1);
      resp->error_code = -2;
      return -2;
    }

    // Limit to available data
    if (num_points > g_data_count) {
      num_points = g_data_count;
    }

    // Create array of data points
    double *result = (double *)malloc(num_points * sizeof(double));
    if (!result) {
      strncpy(resp->error_message, "Memory allocation failed",
              PLUGIN_MAX_STRING_LEN - 1);
      resp->error_code = -4;
      return -4;
    }

    // Fill array
    for (int i = 0; i < num_points; i++) {
      if (g_current_index >= g_data_count) {
        g_current_index = 0; // Wrap around
      }
      result[i] = g_data_buffer[g_current_index];
      g_current_index++;
    }

    // Return as array
    resp->success = true;
    resp->return_value.type = PARAM_TYPE_ARRAY_DOUBLE;
    resp->return_value.value.array_double.data = result;
    resp->return_value.value.array_double.size = num_points;
    resp->data_element_count = num_points;

    // Build text response with CSV format
    int offset = 0;
    for (int i = 0; i < num_points && offset < PLUGIN_MAX_PAYLOAD - 20; i++) {
      offset += snprintf(resp->text_response + offset,
                         PLUGIN_MAX_PAYLOAD - offset, "%.6f", result[i]);
      if (i < num_points - 1) {
        offset += snprintf(resp->text_response + offset,
                           PLUGIN_MAX_PAYLOAD - offset, ",");
      }
    }

    // Note: In a production system, large data would use the buffer manager
    // For now, we return the data in the response structure
    free(result);
    return 0;
  }

  // RESET command - reset the data pointer
  if (strcmp(cmd->verb, "RESET") == 0) {
    g_current_index = 0;
    resp->success = true;
    strncpy(resp->text_response, "Data stream reset", PLUGIN_MAX_PAYLOAD - 1);
    return 0;
  }

  // Unknown command
  strncpy(resp->error_message, "Unknown command", PLUGIN_MAX_STRING_LEN - 1);
  resp->error_code = -1;
  return -1;
}

INSTRUMENT_PLUGIN_API void plugin_shutdown(void) {
  if (g_data_buffer) {
    free(g_data_buffer);
    g_data_buffer = NULL;
  }
  g_data_count = 0;
  g_current_index = 0;
  g_initialized = 0;
}

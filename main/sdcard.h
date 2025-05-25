#ifndef SDCARD_H
#define SDCARD_H

#include "esp_err.h"
#include <stddef.h>
#include <time.h>
#include <inttypes.h>

// Initialization
esp_err_t sd_init(void);
void sd_deinit(void);

// Unified write function with timestamp hopepfully
esp_err_t sd_read(const char *path, char *buffer, size_t buffer_size);
esp_err_t sd_set_metadata(const char *key, const char *id, const char *geoutm);
esp_err_t sd_write_sensors(uint32_t pressure, float temp, float voltage, const char *filepath);

#endif
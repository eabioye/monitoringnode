#ifndef PT928_H
#define PT928_H

#include <stdint.h>
#include "esp_err.h"

esp_err_t pt928_init(void);
uint32_t pt928_read_pressure(void);
void pt928_deinit(void);

#endif
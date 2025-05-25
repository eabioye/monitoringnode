#ifndef SENSORS_H
#define SENSORS_H

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "soc/soc_caps.h"


#define VOLTSENS_ENABLE GPIO_NUM_2
#define VOLTSENS_READCHANNEL ADC_CHANNEL_2
#define VOLTSENS_UNIT ADC_UNIT_1
#define VOLTSENS_ATTEN ADC_ATTEN_DB_6
#define VOLTSENS_SCALING 11.0f

extern const char *payloadpath;
extern const char *registerpath;


float read_voltage_once(void);
int sensor_single_log(const char *path);


#endif
#include "sensors.h"
#include "sdcard.h"
#include "pt928.h"
#include "driver/temperature_sensor.h"
#include <esp_log.h>
#include <math.h>
#include <stdio.h>
#include <esp_err.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include <inttypes.h>

static const char *TAG = "SENSORS";

const char *payloadpath = "/sdcard/payload.txt";
const char *registerpath = "/sdcard/register.txt";

int sensor_single_log(const char *path) {
    // Init temp sensor
    temperature_sensor_handle_t temp_handle;
    temperature_sensor_config_t temp_cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
    ESP_ERROR_CHECK(temperature_sensor_install(&temp_cfg, &temp_handle));
    ESP_ERROR_CHECK(temperature_sensor_enable(temp_handle));

    float temp = 0.0f;
    temperature_sensor_get_celsius(temp_handle, &temp);

    // Init pressure sensor
    ESP_ERROR_CHECK(pt928_init());
    uint32_t pressure = pt928_read_pressure();

    // Read voltage
    float voltage = read_voltage_once();

    // Log
    if (pressure && !isnan(temp)) {
        ESP_LOGI("SENSOR", "Logging P= %"PRIu32", T= %.2f, V= %.2f", pressure, temp, voltage);
        sd_write_sensors(pressure, temp, voltage, path);
    }

    // Cleanup temp sensor
    temperature_sensor_disable(temp_handle);
    temperature_sensor_uninstall(temp_handle);

    // Sleep tier logic
    if (voltage >= 12.3f) return 2;
    else if (voltage >= 11.8f && voltage <= 12.3f) return 5;
    else if (voltage < 11.8f) return 10;
    else return 10; //Default fallback
}

float read_voltage_once(void) {

    gpio_set_direction(VOLTSENS_ENABLE, GPIO_MODE_OUTPUT);
    gpio_set_level(VOLTSENS_ENABLE,1);

    vTaskDelay(pdMS_TO_TICKS(20));

    adc_oneshot_unit_handle_t adc_handle;
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = VOLTSENS_UNIT,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &adc_handle));

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = VOLTSENS_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, VOLTSENS_READCHANNEL, &chan_cfg));

    
    adc_cali_handle_t cali_handle = NULL;
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = VOLTSENS_UNIT,
        .atten = VOLTSENS_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };

    esp_err_t cal_status = adc_cali_create_scheme_curve_fitting(&cali_cfg, &cali_handle);
    bool calibrated = (cal_status == ESP_OK);

    int raw = 0, mv = 0;
    ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, VOLTSENS_READCHANNEL, &raw));
    ESP_LOGI(TAG, "Raw ADC: %d", raw);

    if (calibrated) {
        ESP_ERROR_CHECK(adc_cali_raw_to_voltage(cali_handle, raw, &mv));
    } else {
        mv = (raw * 3300)/4095;
    }

    float vin = (mv / 1000.0f) * VOLTSENS_SCALING;  // Scale up to Vin

    ESP_LOGI(TAG, "ADC Voltage: %.2f V, Scaled Input: %.2f V", mv / 1000.0f, vin);
    
    gpio_set_level(VOLTSENS_ENABLE,0);
    // Cleanup
    ESP_ERROR_CHECK(adc_oneshot_del_unit(adc_handle));
    if (calibrated) {
        adc_cali_delete_scheme_curve_fitting(cali_handle);
    }

    return vin;
}

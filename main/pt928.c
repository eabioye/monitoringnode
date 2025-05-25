#include <stdio.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
#include <inttypes.h>
#include "pt928.h"

static const char *PTAG = "PT928-I2C";
static i2c_master_bus_handle_t bus_handle = NULL;
static i2c_master_dev_handle_t dev_handle = NULL;

#define I2C_MASTER_SCL_IO           4
#define I2C_MASTER_SDA_IO           5
#define I2C_MASTER_NUM              I2C_NUM_0
#define I2C_MASTER_FREQ_HZ          100000
#define I2C_MASTER_TIMEOUT_MS       1000

#define PT928_SENSOR_ADDR         0x6d
#define PT928_MEASURE_TYPE_ADDR   0x30   // Measurement mode register
#define PT928_PRES_OUT_1_REG_ADDR 0x06   // Pressure data register

esp_err_t pt928_init(void){

    ESP_LOGI(PTAG,"Pt928init starting here");
    if(bus_handle != NULL) return ESP_OK;
    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_MASTER_NUM,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    
    esp_err_t ret = i2c_new_master_bus(&bus_config, &bus_handle);
    if (ret != ESP_OK) return ret;

    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = PT928_SENSOR_ADDR,
        .scl_speed_hz = I2C_MASTER_FREQ_HZ,
    };

    return i2c_master_bus_add_device(bus_handle, &dev_config, &dev_handle);
}

static esp_err_t pt928_register_read(i2c_master_dev_handle_t dev_handle, uint8_t reg_addr, uint8_t *data, size_t len) {
    return i2c_master_transmit_receive(dev_handle, &reg_addr, 1, data, len, I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
}

static esp_err_t pt928_register_write_byte(i2c_master_dev_handle_t dev_handle, uint8_t reg_addr, uint8_t data) {
    uint8_t write_buf[2] = {reg_addr, data};
    return i2c_master_transmit(dev_handle, write_buf, sizeof(write_buf), I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
}


uint32_t pt928_read_pressure() {
    if(!dev_handle) return 0;

    // Set single measurement mode
    esp_err_t ret = pt928_register_write_byte(dev_handle, PT928_MEASURE_TYPE_ADDR, 0x01);
    if (ret != ESP_OK) {
        ESP_LOGE(PTAG, "Measurement mode set failed");
        return UINT32_MAX;
    }
    vTaskDelay(pdMS_TO_TICKS(25));

    // Read pressure data
    uint8_t press_data[3];
    ret = pt928_register_read(dev_handle, PT928_PRES_OUT_1_REG_ADDR, press_data, sizeof(press_data));
    if (ret != ESP_OK) {
        ESP_LOGE(PTAG, "Pressure read failed");
        return UINT32_MAX;
    }

    // Combine bytes into 24-bit value
    return ((uint32_t)press_data[0] << 16) | ((uint32_t)press_data[1] << 8) | press_data[2];

}

void pt928_deinit(void){
    if(dev_handle){
        i2c_master_bus_rm_device(dev_handle);
        dev_handle = NULL;
    }
    if(bus_handle){
        i2c_del_master_bus(bus_handle);
        bus_handle = NULL;
    }
}
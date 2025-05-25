#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_err.h"
#include "driver/spi_master.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"
#include "sdcard.h"
#include <sys/time.h>
#include "esp_system.h"
#include "esp_event.h"
#include "esp_sntp.h"
#include "lwip/ip_addr.h"
#include "esp_netif_sntp.h"
#include "nvs_flash.h"
#include <inttypes.h>
#include <time.h>
#include "esp_attr.h"
#include "esp_sleep.h"
#include "esp_netif.h"

#include "time.c"

// Define SPI pins
#define PIN_NUM_MISO 35
#define PIN_NUM_MOSI 34
#define PIN_NUM_CLK  36
#define PIN_NUM_CS   33


static const char *SDTAG = "SD_CARD";
static sdmmc_card_t *card = NULL;
static spi_host_device_t spi_host = SPI2_HOST;

esp_err_t sd_init(void) {
    static bool initialized = false;
    
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096
    };

    esp_err_t ret = spi_bus_initialize(spi_host, &bus_cfg, SPI_DMA_CH_AUTO);
    if(ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) return ret;

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = spi_host;
    
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = PIN_NUM_CS;
    slot_config.host_id = spi_host;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = true,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    vTaskDelay(pdMS_TO_TICKS(1000));
    ret = esp_vfs_fat_sdspi_mount("/sdcard", &host, &slot_config, &mount_config, &card);
    
    if(ret == ESP_OK) initialized = true;

    if(initialized){
        FILE *file = fopen("/sdcard/payload.txt", "w");
        if (!file){
            initialized = false;
            return ESP_FAIL;
        }
        fclose(file);
        return ESP_OK;
    }
    return ret;
}

void sd_deinit(void) {
    if(card) {
        esp_vfs_fat_sdcard_unmount("/sdcard", card);
        spi_bus_free(spi_host);
        card = NULL;
    }
}

// Modified write function for pressure, temp, and voltage, timestamp is added by default.
esp_err_t sd_write_sensors(uint32_t pressure, float temp, float voltage, const char *filepath){
    ESP_LOGI(SDTAG,"SD Write function starting...");
    if (!card) {
        ESP_LOGE(SDTAG, "SD card not initialized!");
        return ESP_ERR_INVALID_STATE;
    }


    // Open file in append mode
    FILE *file = fopen(filepath, "a");
    if (!file) {
        ESP_LOGE(SDTAG, "Failed to open file: %s", strerror(errno));
        return ESP_FAIL;
    }
   
    int ms;
    struct tm current_time = get_time_now(&ms);

    char timestamp[64];
    snprintf(timestamp, sizeof(timestamp), "%02d-%02d-%04d %02d:%02d:%02d:%03d",
             current_time.tm_mday, current_time.tm_mon + 1, current_time.tm_year + 1900,
             current_time.tm_hour, current_time.tm_min, current_time.tm_sec, ms);

    char data[128];
    snprintf(data, sizeof(data), "'%s','%"PRIu32"','%.2f','%.2f','%.2f'\n", timestamp, pressure, temp, voltage, 0.0);

    // Write data
    ESP_LOGI(SDTAG,"Writing to SD...");
    fprintf(file, "%s", data);
    fflush(file);
    fclose(file);

    return ESP_OK;
}

esp_err_t sd_read(const char *path, char *buffer, size_t buffer_size) {

    // Read from file
    FILE *file = fopen(path, "r");
    if (!file) {
        ESP_LOGE(SDTAG, "Failed to open file for reading");
        esp_vfs_fat_sdcard_unmount("/sdcard", card);
        spi_bus_free(SPI2_HOST);
        return ESP_FAIL;
    }

    size_t bytes_read = fread(buffer, 1, buffer_size - 1, file);
    buffer[bytes_read] = '\0'; // Null-terminate the string
    fclose(file);

    return ESP_OK;
}

esp_err_t sd_set_metadata(const char *key, const char *sensorID, const char *geoutm) {
    FILE *file = fopen("/sdcard/payload.txt", "w");
    if (!file) {
        ESP_LOGE(SDTAG, "Failed to open payload.txt for writing metadata");
        return ESP_FAIL;
    }

    fprintf(file, "key:'%s'\n", key);
    fprintf(file, "sensorID:'%s'\n", sensorID);
    fprintf(file, "geoutm:'%s'\n", geoutm);
    fclose(file);
    ESP_LOGI(SDTAG, "Metadata written to payload.txt");
    return ESP_OK;
}



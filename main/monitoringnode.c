#include "main.h"
#include "html.h"
#include "wifi.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_sntp.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"

#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <esp_err.h>
#include "lwip/dns.h"
#include "lwip/ip_addr.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "esp_netif.h"

#include "sdcard.c"
#include "pt928.h"
#include "sensors.h"
#include "upload.h"
#include "LED.h"

#define REED_SWITCH_GPIO 45
#define REED_SWITCH_RESTART_GPIO 46 // not used yet

static const char *TAG = "Monitoring-Node";

void go_to_sleep_minutes(int minutes)
{
    ESP_LOGI(TAG, "Sleeping for %d minutes...", minutes);
    sd_deinit();
    esp_sleep_enable_timer_wakeup((uint64_t)minutes * 60ULL * 1000000ULL);
    esp_deep_sleep_start();
}

bool check_registration(void)
{
    char key[64] = {0};
    char sensorID[32] = {0};
    char geoutm[128] = {0};

    esp_err_t err = load_registration_metadata(key, sizeof(key), sensorID, sizeof(sensorID), geoutm, sizeof(geoutm));
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "Registration metadata not found in NVS");
        return false;
    }

    ESP_LOGI(TAG, "Loaded registration: key=%s, sensorID=%s, geoutm=%s", key, sensorID, geoutm);
    sd_set_metadata(key, sensorID, geoutm);
    return true;
}

void configure_reed_switch()
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << REED_SWITCH_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE};
    gpio_config(&io_conf);
}

void monitoring_node_task(void *pvParameter)
{
    if (sd_init() != ESP_OK)
    {
        ESP_LOGE(TAG, "SD init failed. Restarting...");
        setColor(8191, 0, 0);
        esp_restart();
    }

    esp_netif_ip_info_t ip_info;
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_get_ip_info(netif, &ip_info);
    ESP_LOGI("NETIF", "My IP: " IPSTR, IP2STR(&ip_info.ip));

    init_time();

    while (!check_registration())
    {
        ESP_LOGW(TAG, "Registration missing. Awaiting user input.");
        vTaskDelay(pdMS_TO_TICKS(5000));
    }

    int sleep_time = sensor_single_log(payloadpath);
    try_upload_now();
    go_to_sleep_minutes(sleep_time);

    vTaskDelete(NULL);
}

void app_main(void)
{
    esp_reset_reason_t reason = esp_reset_reason();
    ESP_LOGI(TAG, "Reset reason: %d", reason);

    
    ESP_ERROR_CHECK(nvs_flash_init());
    init_nvs();

    if (reason == ESP_RST_DEEPSLEEP)
    {
        configure_ledc();
        wifi_init_sta_only();

        int retry = 0;
        while (!wifi_connected && retry++ < 20)
        {
            ESP_LOGI(TAG, "Reconnecting to Wi-Fi... attempt %d", retry);
            vTaskDelay(pdMS_TO_TICKS(2000));
        }

        if (!wifi_connected)
        {
            ESP_LOGE(TAG, "Failed to reconnect to Wi-Fi after deep sleep.");
            setColor(8191, 0, 0);
        }
        else
        {
            ESP_LOGI(TAG, "Wi-Fi reconnected successfully!");
            setColor(0, 8191, 0);
        }

        xTaskCreate(monitoring_node_task, "monitoring_node_task", 12288, NULL, 5, NULL);
        return;
    }

    // ====== Normal Power-On Boot ======
    configure_reed_switch();
    configure_ledc();

    ESP_LOGI(TAG, "Watching for reed switch trigger...");
    bool config_mode = false;
    for (int i = 0; i < 600; i++) {
        if (gpio_get_level(REED_SWITCH_GPIO) == 1) {
            config_mode = true;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    if (config_mode)
    {
        ESP_LOGI(TAG, "Reed switch ACTIVE: Entering config mode");

        wifi_init_softap();
        httpd_handle_t server = start_webserver();
        if (!server)
        {
            ESP_LOGE(TAG, "Webserver failed to start");
            setColor(8191, 0, 0);
        }

        int retries = 0;
        setColor(0, 0, 8191);
        while (!wifi_connected && retries++ < 12)
        {
            vTaskDelay(pdMS_TO_TICKS(5000));
            ESP_LOGI(TAG, "Waiting for Wi-Fi connection in config mode... (%d)", retries);
        }

        if (wifi_connected)
        {
            ESP_LOGI(TAG, "Connected to Wi-Fi!");
            setColor(0, 8191, 0);
            vTaskDelay(pdMS_TO_TICKS(3000));
            setColor(0, 0, 0);
        }
        else
        {
            ESP_LOGW(TAG, "Still not connected to Wi-Fi after config wait.");
        }
    }
    else
    {
        ESP_LOGI(TAG, "No config trigger. Attempting stored Wi-Fi connection.");
        wifi_connect_stored();

        int retries = 0;
        while (!wifi_connected && retries++ < 10)
        {
            ESP_LOGI(TAG, "Waiting for Wi-Fi... attempt %d", retries);
            vTaskDelay(pdMS_TO_TICKS(1000));
        }

        if (!wifi_connected)
        {
            ESP_LOGW(TAG, "Wi-Fi not connected after all attempts.");
        }
    }

    xTaskCreate(monitoring_node_task, "monitoring_node_task", 12288, NULL, 5, NULL); 
}

#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "esp_sleep.h"
#include "nvs_flash.h"
#include "esp_netif_sntp.h"
#include "lwip/ip_addr.h"
#include "esp_sntp.h"
#include <inttypes.h>
#include <esp_log.h>
#include "esp_netif.h"
#include "timex.h"

#include "esp_http_client.h"
#include "esp_sntp.h"


static const char *TIME_TAG = "time";

#ifndef INET6_ADDRSTRLEN
#define INET6_ADDRSTRLEN 48
#endif


void init_time(void) {
    setenv("TZ", "MST7MDT,M3.2.0/2:00:00,M11.1.0/2:00:00", 1);
    tzset();

    // STEP 1: Try SNTP
    ESP_LOGI(TIME_TAG, "Attempting SNTP time sync...");

    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(2,
        ESP_SNTP_SERVER_LIST("time.google.com", "pool.ntp.org"));
    esp_netif_sntp_init(&config);

    struct tm timeinfo = {0};
    struct timeval tv = {0};

    int retry = 0;
    const int retry_count = 15;
    while (esp_netif_sntp_sync_wait(2000 / portTICK_PERIOD_MS) == ESP_ERR_TIMEOUT && ++retry < retry_count) {
        ESP_LOGI(TIME_TAG, "Waiting for SNTP sync... (%d/%d)", retry, retry_count);
    }

    gettimeofday(&tv, NULL);
    localtime_r(&tv.tv_sec, &timeinfo);
    esp_netif_sntp_deinit();

    if (timeinfo.tm_year >= (2024 - 1900)) {
        ESP_LOGI(TIME_TAG, "SNTP success: %s", asctime(&timeinfo));
        return;
    }

    ESP_LOGW(TIME_TAG, "SNTP failed. Trying HTTP time fallback...");

    // STEP 2: Use HTTP fallback
    esp_http_client_config_t http_cfg = {
        .url = "http://worldtimeapi.org/api/timezone/America/Denver",
        .timeout_ms = 5000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    if (esp_http_client_perform(client) == ESP_OK) {
        int len = esp_http_client_get_content_length(client);
        char *response = malloc(len + 1);
        if (response && esp_http_client_read_response(client, response, len) >= 0) {
            response[len] = 0;

            // Parse JSON manually for "datetime"
            char *datetime = strstr(response, "\"datetime\":\"");
            if (datetime) {
                datetime += strlen("\"datetime\":\"");
                char iso_time[30] = {0};
                strncpy(iso_time, datetime, 25); // "2024-03-29T21:13:45.123456"

                struct tm tm_http;
                memset(&tm_http, 0, sizeof(tm_http));
                strptime(iso_time, "%Y-%m-%dT%H:%M:%S", &tm_http);

                time_t t = mktime(&tm_http);
                struct timeval now = { .tv_sec = t };
                settimeofday(&now, NULL);
                ESP_LOGI(TIME_TAG, "Time set via HTTP fallback: %s", asctime(&tm_http));
            } else {
                ESP_LOGE(TIME_TAG, "Failed to parse HTTP time response");
            }

            free(response);
        }
    } else {
        ESP_LOGE(TIME_TAG, "HTTP time request failed");
    }

    esp_http_client_cleanup(client);
}


void time_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI(TIME_TAG, "Notification of a time synchronization event");
}


struct tm get_time_now(int *milliseconds)
{
    struct timeval tv;
    struct tm timeinfo;

    gettimeofday(&tv, NULL);
    localtime_r(&tv.tv_sec, &timeinfo);
    *milliseconds = tv.tv_usec / 1000;

    if (timeinfo.tm_year < (2024 - 1900)) {
        ESP_LOGW(TIME_TAG, "Warning: time not synced, using randomized fallback time");

        int fallback_day  = 21 + (esp_random() % 5);  // 21–25
        int fallback_hour = esp_random() % 24;        // 0–23
        int fallback_min  = esp_random() % 60;        // 0–59
        int fallback_sec  = esp_random() % 60;        // 0–59

        timeinfo.tm_year = 2025 - 1900;
        timeinfo.tm_mon  = 2; // March (0-based)
        timeinfo.tm_mday = fallback_day;
        timeinfo.tm_hour = fallback_hour;
        timeinfo.tm_min  = fallback_min;
        timeinfo.tm_sec  = fallback_sec;

        time_t fallback_time = mktime(&timeinfo);
        struct timeval fallback_tv = {
            .tv_sec = fallback_time,
            .tv_usec = 0
        };
        settimeofday(&fallback_tv, NULL);

        *milliseconds = 0;
        ESP_LOGI(TIME_TAG, "Fallback time set to: %02d-%02d-%04d %02d:%02d:%02d",
                 timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900,
                 timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    }

    return timeinfo;
}



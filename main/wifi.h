#ifndef WIFI_H
#define WIFI_H

#include "nvs.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_http_server.h"
#include "esp_sleep.h"

void wifi_init_softap(void);
void scan_wifi_networks(void);
char *generate_wifi_options(void);
esp_err_t get_handler(httpd_req_t *req);
esp_err_t post_handler(httpd_req_t *req);
esp_err_t register_handler(httpd_req_t *req);
httpd_handle_t start_webserver(void);
esp_err_t load_wifi_credentials(char *ssid, size_t ssid_size, char *password, size_t password_size);
esp_err_t save_wifi_credentials(const char *ssid, const char *password);
esp_err_t init_nvs();
void wifi_connect_stored(void);
esp_err_t retry_handler(httpd_req_t *req);
void configure_softap_dns();
void set_dns_for_sta();

esp_err_t save_registration_metadata(const char *key, const char *sensorID, const char *geoutm);
esp_err_t load_registration_metadata(char *key, size_t key_size,char *sensorID, size_t id_size,char *geoutm, size_t geo_size);

extern bool wifi_connected;
extern char stored_ssid[33];
extern char stored_password[65];

void wifi_init_sta_only(void);

#endif
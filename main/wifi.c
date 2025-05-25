#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "lwip/dns.h"
#include "main.h"
#include "wifi.h"
#include "html.h"
#include "sdcard.h"
#include "upload.h"
#include "cJSON.h"
#include "sensors.h"
#include "LED.h"

#define WIFI_SSID "PCBees_AP1"
#define WIFI_PASS "password123"
#define MAX_APs 20
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define CONNECTION_TIMEOUT_MS 15000
#define RETRY_INTERVAL_MS 1000
#define MAX_RETRY_DURATION_MS 10000

static const char *WIFITAG = "WiFi_WebServer";
static wifi_ap_record_t ap_list[MAX_APs];
static uint16_t ap_count = 0;
bool wifi_connected = false;

char stored_ssid[33] = "";
char stored_password[65] = "";

// Event Handlers
static void esp_wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_id == WIFI_EVENT_STA_CONNECTED)
    {
        ESP_LOGI(WIFITAG, "Connected to AP");
    }
    else if (event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        wifi_connected = false;
        wifi_event_sta_disconnected_t *disc = (wifi_event_sta_disconnected_t *)event_data;
        ESP_LOGW(WIFITAG, "Disconnected from AP, reason: %d", disc->reason);
        switch (disc->reason)
        {
        case WIFI_REASON_AUTH_EXPIRE:
        case WIFI_REASON_AUTH_FAIL:
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
            ESP_LOGE(WIFITAG, "Auth failure — check router WPA settings");
            break;
        case WIFI_REASON_NO_AP_FOUND:
            ESP_LOGE(WIFITAG, "No AP found — check if SSID is correct and visible");
            break;
        default:
            break;
        }
    }
}

static void ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(WIFITAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        wifi_connected = true;
        esp_netif_dns_info_t dns_info;
        esp_netif_get_dns_info(esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"), ESP_NETIF_DNS_MAIN, &dns_info);
        ESP_LOGI(WIFITAG, "DNS Server: " IPSTR, IP2STR(&dns_info.ip.u_addr.ip4));
    }
}

esp_err_t retry_handler(httpd_req_t *req)
{
    ESP_LOGI(WIFITAG, "Manual retry requested via /retry");

    // Disconnect first to clear previous state
    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(500));

    wifi_connect_stored(); // This now has 3-retry logic
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

esp_err_t get_handler(httpd_req_t *req)
{
    char status_text[128] = "";
    char status_color[16] = "black";
    char query[128] = "";

    // Parse query parameters
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK)
    {
        char param[128];
        if (httpd_query_key_value(query, "error", param, sizeof(param)) == ESP_OK)
        {
            url_decode(param, status_text, sizeof(status_text));
            strcpy(status_color, "red");
        }
        else if (httpd_query_key_value(query, "success", param, sizeof(param)) == ESP_OK)
        {
            url_decode(param, status_text, sizeof(status_text));
            strcpy(status_color, "green");
        }
    }

    char *options = generate_wifi_options();
    if (!options)
    {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    char *response = malloc(4096);
    if (!response)
    {
        free(options);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    // Format the HTML content FIRST
    int len = snprintf(response, 4096, HTML_PAGE, options, status_color, status_text);
    if (len < 0 || len >= 4096)
    {
        free(options);
        free(response);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    // Send the formatted response
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, response, len);

    // Cleanup after sending
    free(options);
    free(response);
    return ESP_OK;
}

esp_err_t post_handler(httpd_req_t *req)
{
    char buf[128];           // Buffer for reading POST data in chunks
    char ssid[33] = {0};     // SSID max 32 bytes + null terminator
    char password[65] = {0}; // Password max 64 bytes + null terminator
    int total_received = 0;

    // Read POST data in chunks
    while (total_received < req->content_len)
    {
        int received = httpd_req_recv(req, buf, MIN(sizeof(buf), req->content_len - total_received));
        if (received <= 0)
        {
            if (received == HTTPD_SOCK_ERR_TIMEOUT)
                continue;
            return ESP_FAIL;
        }
        total_received += received;
        buf[received] = '\0';

        // Parse SSID and password from the received data
        char *token = strtok(buf, "&");
        while (token)
        {
            if (strncmp(token, "ssid=", 5) == 0)
            {
                url_decode(token + 5, ssid, sizeof(ssid));
            }
            else if (strncmp(token, "password=", 9) == 0)
            {
                url_decode(token + 9, password, sizeof(password));
            }
            token = strtok(NULL, "&");
        }
    }

    // Validate input length
    if (strlen(ssid) > 32 || strlen(password) > 64)
    {
        httpd_resp_set_status(req, "303 See Other");
        httpd_resp_set_hdr(req, "Location", "/?error=SSID%20or%20Password%20Too%20Long");
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    }

    ESP_LOGI(WIFITAG, "Connecting to: %s", ssid);
    wifi_config_t sta_config = {0};
    strlcpy((char *)sta_config.sta.ssid, ssid, sizeof(sta_config.sta.ssid));
    strlcpy((char *)sta_config.sta.password, password, sizeof(sta_config.sta.password));
    sta_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
    esp_err_t ret = esp_wifi_connect();
    

    if (ret != ESP_OK)
    {
        httpd_resp_set_status(req, "303 See Other");
        httpd_resp_set_hdr(req, "Location", "/?error=Connection%20Failed");
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    }

    // Wait for connection with event-based status
    int timeout = CONNECTION_TIMEOUT_MS / 500;
    wifi_connected = false;
    vTaskDelay(1000); // Give some time for the connection to start
    while (timeout-- > 0 && !wifi_connected)
    {
        vTaskDelay(100 / portTICK_PERIOD_MS);
        ESP_LOGI(WIFITAG, "Waiting for connection to AP");
        
    }

    if (!wifi_connected)
    {
        httpd_resp_set_status(req, "303 See Other");
        httpd_resp_set_hdr(req, "Location", "/?error=Connection%20Timeout");

        vTaskDelay(1000);
        if (!wifi_connected)
        {
            setColor(8191, 0, 0); // Red
            vTaskDelay(1000);
            setColor(0, 0, 8191); // Blue
        }


        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    }

    if (wifi_connected)
    {
        if (save_wifi_credentials(ssid, password) != ESP_OK)
        {
            ESP_LOGE(WIFITAG, "Failed to save Wifi credentials to NVS");
        }
    }

    // URL-encode SSID for the redirect
    char encoded_ssid[65] = {0};
    url_encode(ssid, encoded_ssid, sizeof(encoded_ssid));

    // Build the Location header with a fixed-size buffer
    char location[128]; // Fixed-size buffer for Location header
    snprintf(location, sizeof(location), "/?success=Connected%%20to%%20%.32s", encoded_ssid);
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", location);
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

esp_err_t scan_handler(httpd_req_t *req)
{
    scan_wifi_networks();
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

esp_err_t register_handler(httpd_req_t *req)
{
    char buf[512];
    int received = httpd_req_recv(req, buf, MIN(req->content_len, sizeof(buf) - 1));
    if (received <= 0)
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read request body");
        return ESP_FAIL;
    }
    buf[received] = '\0';

    char key[64] = {0};
    char sensorID[32] = {0};
    char geoutm[128] = {0};

    // Parse form input
    char *token = strtok(buf, "&");
    while (token)
    {
        if (strncmp(token, "key=", 4) == 0)
        {
            url_decode(token + 4, key, sizeof(key));
        }
        else if (strncmp(token, "sensorID=", 9) == 0)
        {
            url_decode(token + 9, sensorID, sizeof(sensorID));
        }
        else if (strncmp(token, "geoutm=", 7) == 0)
        {
            url_decode(token + 7, geoutm, sizeof(geoutm));
        }
        token = strtok(NULL, "&");
    }

    // Write inputs to /sdcard/register.txt
    const char *registerpath = "/sdcard/register.txt";
    FILE *file = fopen(registerpath, "w");
    if (!file)
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to write registration file");
        return ESP_FAIL;
    }
    fprintf(file, "key:'%s'\nsensorID:'%s'\ngeoutm:'%s'\n", key, sensorID, geoutm);
    fclose(file);

    // Log one sensor row to register.txt
    sensor_single_log(registerpath);

    // Upload to server
    const char *url = "https://h2overwatch.ca/DesktopModules/ShiftUP_VolsenseMap/registerDevice.ashx";
    char *server_response = malloc(1024);
    if (!server_response)
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }

    esp_err_t upload_status = upload_file_to_server(registerpath, url, server_response, 1024);
    if (upload_status != ESP_OK || strlen(server_response) == 0)
    {
        free(server_response);
        httpd_resp_set_status(req, "303 See Other");
        httpd_resp_set_hdr(req, "Location", "/?error=Registration%%20Upload%%20Failed");
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    }

    // Line-by-line parser (handles \r\n and missing fields)
    char parsed_key[64] = {0};
    char parsed_sensorID[32] = {0};
    char parsed_geoutm[128] = {0};

    char *line = strtok(server_response, "\r\n");
    while (line)
    {
        if (strncmp(line, "key:'", 5) == 0)
        {
            sscanf(line, "key:'%63[^']", parsed_key);
        }
        else if (strncmp(line, "sensorID:'", 10) == 0)
        {
            sscanf(line, "sensorID:'%31[^']", parsed_sensorID);
        }
        else if (strncmp(line, "geoutm:'", 8) == 0)
        {
            sscanf(line, "geoutm:'%127[^']", parsed_geoutm);
        }
        line = strtok(NULL, "\r\n");
    }

    if (strlen(parsed_key) == 0 || strlen(parsed_sensorID) == 0 || strlen(parsed_geoutm) == 0)
    {
        free(server_response);
        httpd_resp_set_status(req, "303 See Other");
        httpd_resp_set_hdr(req, "Location", "/?error=Failed%%20to%%20parse%%20server%%20response");
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    }

    ESP_LOGI("REG", "Parsed from server: key=%s, sensorID=%s, geoutm=%s",
             parsed_key, parsed_sensorID, parsed_geoutm);

    // Save confirmed values
    save_registration_metadata(parsed_key, parsed_sensorID, parsed_geoutm);
    sd_set_metadata(parsed_key, parsed_sensorID, parsed_geoutm);
    free(server_response);

    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/?success=Device%%20Registered");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

void wifi_init_softap(void)
{
    configure_ledc(); // Configure the LED GPIO
    if (init_nvs() != ESP_OK)
    {
        ESP_LOGE(WIFITAG, "Failed to initialize NVS");
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_ap();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t ap_config = {
        .ap = {
            .ssid = WIFI_SSID,
            .ssid_len = strlen(WIFI_SSID),
            .password = WIFI_PASS,
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK}};

    if (strlen(WIFI_PASS) == 0)
    {
        ap_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &ip_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &esp_wifi_event_handler, NULL));

    ESP_LOGI(WIFITAG, "Wi-Fi initialized in AP+STA mode.");
    ESP_LOGI(WIFITAG, "Config page initialized successfully");

    wifi_connect_stored();
}

int compare_rssi(const void *a, const void *b)
{
    return ((wifi_ap_record_t *)b)->rssi - ((wifi_ap_record_t *)a)->rssi;
}

void scan_wifi_networks(void)
{
    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .show_hidden = false};

    esp_err_t ret = esp_wifi_scan_start(&scan_config, true);
    if (ret != ESP_OK)
    {
        ESP_LOGE(WIFITAG, "Scan failed: %s", esp_err_to_name(ret));
        return;
    }

    uint16_t max_aps = MAX_APs;
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&max_aps, ap_list));
    ap_count = max_aps;
    qsort(ap_list, ap_count, sizeof(wifi_ap_record_t), compare_rssi);
    ESP_LOGI(WIFITAG, "Found %d access points (sorted by strength)", ap_count);
}

char *generate_wifi_options(void)
{
    const size_t buffer_size = 2048;
    char *options = malloc(buffer_size);
    if (!options)
        return NULL;

    size_t offset = 0;
    for (int i = 0; i < ap_count && offset < buffer_size; i++)
    {
        size_t remaining = buffer_size - offset;
        if (remaining <= 0)
            break;
        int written = snprintf(options + offset, buffer_size - offset,
                               "<option value=\"%.32s\">%.32s (%ddBm)</option>",
                               ap_list[i].ssid, ap_list[i].ssid, ap_list[i].rssi);
        if (written >= remaining)
        {
            offset = buffer_size - 1;
            options[offset] = '\0';
            break;
        }
        else if (written > 0)
        {
            offset += written;
        }
    }

    if (offset == 0)
    {
        snprintf(options, buffer_size, "<option disabled>No networks found. Please scan.</option>");
    }
    options[offset] = '\0';
    return options;
}

void url_decode(const char *input, char *output, size_t buf_size)
{
    size_t i = 0, j = 0;
    char ch;
    while ((ch = input[i++]) && j < buf_size - 1)
    {
        if (ch == '+')
        {
            output[j++] = ' ';
        }
        else if (ch == '%')
        {
            if (!isxdigit((unsigned char)input[i]) || !isxdigit((unsigned char)input[i + 1]))
            {
                output[j++] = '%';
            }
            else
            {
                char hex[3] = {input[i], input[i + 1], 0};
                output[j++] = (char)strtol(hex, NULL, 16);
                i += 2;
            }
        }
        else
        {
            output[j++] = ch;
        }
    }
    output[j] = '\0';
}

void url_encode(const char *input, char *output, size_t buf_size)
{
    const char *hex = "0123456789ABCDEF";
    size_t i = 0, j = 0;

    while (input[i] && j < buf_size - 1)
    {
        if (isalnum((unsigned char)input[i]) || input[i] == '-' || input[i] == '_' || input[i] == '.' || input[i] == '~')
        {
            output[j++] = input[i++];
        }
        else
        {
            if (j + 3 >= buf_size)
                break; // Ensure enough space for encoding
            output[j++] = '%';
            output[j++] = hex[(input[i] >> 4) & 0xF];
            output[j++] = hex[input[i] & 0xF];
            i++;
        }
    }
    output[j] = '\0';
}

esp_err_t init_nvs()
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    return ret;
}

esp_err_t save_wifi_credentials(const char *ssid, const char *password)
{
    nvs_handle_t handle;
    esp_err_t err;

    err = nvs_open("wifi_config", NVS_READWRITE, &handle);
    if (err != ESP_OK)
        return err;

    err = nvs_set_str(handle, "ssid", ssid);
    if (err != ESP_OK)
    {
        nvs_close(handle);
        return err;
    }

    err = nvs_set_str(handle, "password", password);
    if (err != ESP_OK)
    {
        nvs_close(handle);
        return err;
    }
    ESP_LOGI(WIFITAG, "Saving credentials - SSID: '%s', Password: %s", ssid, strlen(password) > 0 ? "[exists]" : "[empty]");
    err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

esp_err_t load_wifi_credentials(char *ssid, size_t ssid_size, char *password, size_t password_size)
{
    nvs_handle_t handle;
    esp_err_t err;

    err = nvs_open("wifi_config", NVS_READONLY, &handle);
    if (err != ESP_OK)
        return err;

    // Get SSID
    err = nvs_get_str(handle, "ssid", ssid, &ssid_size);
    if (err != ESP_OK)
    {
        nvs_close(handle);
        return err;
    }

    // Get password
    err = nvs_get_str(handle, "password", password, &password_size);
    if (err != ESP_OK)
    {
        nvs_close(handle);
        return err;
    }

    nvs_close(handle);
    return ESP_OK;
}

void wifi_connect_stored()
{
    char ssid[33] = {0};
    char password[65] = {0};

    esp_err_t ret = load_wifi_credentials(ssid, sizeof(ssid), password, sizeof(password));

    if (ret != ESP_OK)
    {
        ESP_LOGE(WIFITAG, "Failed to load credentials from NVS: %s", esp_err_to_name(ret));
        return;
    }

    // Validate loaded credentials
    if (strlen(ssid) == 0)
    {
        ESP_LOGI(WIFITAG, "No stored SSID found");
        return;
    }

    ESP_LOGI(WIFITAG, "Attempting to connect to stored network: %s", ssid);

    wifi_config_t sta_config = {
        .sta = {
            .threshold = {
                .authmode = WIFI_AUTH_WPA2_PSK,
            },
        },
    };

    strlcpy((char *)sta_config.sta.ssid, ssid, sizeof(sta_config.sta.ssid));

    // Only copy password if it exists
    if (strlen(password) > 0)
    {
        strlcpy((char *)sta_config.sta.password, password, sizeof(sta_config.sta.password));
    }
    else
    {
        ESP_LOGW(WIFITAG, "No password stored - trying open network");
        sta_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));

    ret = esp_wifi_connect();
    if (ret != ESP_OK)
    {
        ESP_LOGE(WIFITAG, "Connection attempt failed: %s", esp_err_to_name(ret));
        wifi_connected = false;
        // Blink red to indicate connection failure
        for (int i = 0; i < 3; i++)
        {
            setColor(8191, 0, 0);
            vTaskDelay(pdMS_TO_TICKS(300));
            setColor(0, 0, 0);
            vTaskDelay(pdMS_TO_TICKS(300));
        }
        return;
    }

    ESP_LOGI(WIFITAG, "Successfully connected and got IP.");
    return;
}

esp_err_t save_registration_metadata(const char *key, const char *sensorID, const char *geoutm)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("registration", NVS_READWRITE, &handle);
    if (err != ESP_OK)
        return err;

    err |= nvs_set_str(handle, "key", key);
    err |= nvs_set_str(handle, "sensorID", sensorID);
    err |= nvs_set_str(handle, "geoutm", geoutm);

    if (err == ESP_OK)
    {
        err = nvs_commit(handle);
    }

    nvs_close(handle);
    return err;
}

esp_err_t load_registration_metadata(char *key, size_t key_size, char *sensorID, size_t id_size, char *geoutm, size_t geo_size)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("registration", NVS_READONLY, &handle);
    if (err != ESP_OK)
        return err;

    err |= nvs_get_str(handle, "key", key, &key_size);
    err |= nvs_get_str(handle, "sensorID", sensorID, &id_size);
    err |= nvs_get_str(handle, "geoutm", geoutm, &geo_size);

    nvs_close(handle);
    return err;
}

httpd_handle_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 16;
    config.max_resp_headers = 40; // Increase header buffer size 32
    config.recv_wait_timeout = 10;
    config.send_wait_timeout = 10;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.stack_size = 8192;
    config.max_open_sockets = 7;

    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) == ESP_OK)
    {
        httpd_uri_t uri_get = {.uri = "/", .method = HTTP_GET, .handler = get_handler};
        httpd_uri_t uri_scan = {.uri = "/scan", .method = HTTP_GET, .handler = scan_handler};
        httpd_uri_t uri_post = {.uri = "/connect", .method = HTTP_POST, .handler = post_handler};
        httpd_uri_t retry_uri = {.uri = "/retry", .method = HTTP_POST, .handler = retry_handler};
        httpd_uri_t uri_register = {.uri = "/register", .method = HTTP_POST, .handler = register_handler};

        httpd_register_uri_handler(server, &uri_register);
        httpd_register_uri_handler(server, &retry_uri);
        httpd_register_uri_handler(server, &uri_get);
        httpd_register_uri_handler(server, &uri_scan);
        httpd_register_uri_handler(server, &uri_post);
    }
    return server;
}

// added to reconnect to wifi after deep sleep
void wifi_init_sta_only(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &ip_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &esp_wifi_event_handler, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    wifi_connect_stored();
}

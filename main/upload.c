#include "upload.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"
#include <netdb.h>
#include "wifi.h"

// Enhanced callback function to handle HTTP events
static esp_err_t http_event_handler(esp_http_client_event_t *evt) {
    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            ESP_LOGI(SENDTAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            if (!esp_http_client_is_chunked_response(evt->client)) {
                ESP_LOG_BUFFER_HEXDUMP(SENDTAG, evt->data, evt->data_len, ESP_LOG_INFO);
            }
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGI(SENDTAG, "HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(SENDTAG, "HTTP_EVENT_DISCONNECTED");
            break;
        case HTTP_EVENT_ERROR:
            ESP_LOGE(SENDTAG, "HTTP_EVENT_ERROR");
            break;
        default:
            break;
    }
    return ESP_OK;
}

// Enhanced file upload function with retry logic
esp_err_t upload_file_to_server(const char *file_path, const char *url, char *response_buf, size_t buf_size) {
    esp_err_t ret = ESP_FAIL;

    for (int retry = 0; retry < UPLOAD_RETRY_COUNT; retry++) {
        if (retry > 0) {
            ESP_LOGW(SENDTAG, "Retrying upload (%d/%d)...", retry, UPLOAD_RETRY_COUNT);
            vTaskDelay(pdMS_TO_TICKS(UPLOAD_RETRY_DELAY_MS));
        }

        // Open and read the full file
        FILE *file = fopen(file_path, "rb");
        if (!file) {
            ESP_LOGE(SENDTAG, "Failed to open file: %s", file_path);
            continue;
        }

        fseek(file, 0, SEEK_END);
        long file_size = ftell(file);
        fseek(file, 0, SEEK_SET);

        char *file_content = malloc(file_size + 1);
        if (!file_content) {
            ESP_LOGE(SENDTAG, "Memory allocation failed");
            fclose(file);
            continue;
        }

        fread(file_content, 1, file_size, file);
        file_content[file_size] = '\0';
        fclose(file);

        // Create full multipart body
        const char *boundary = "----WebKitFormBoundary7MA4YWxkTrZuOgW";
        char *pre = NULL;
        char *post = NULL;

        asprintf(&pre,
            "--%s\r\n"
            "Content-Disposition: form-data; name=\"file\"; filename=\"payload.txt\"\r\n"
            "Content-Type: text/plain\r\n\r\n",
            boundary);

        asprintf(&post,
            "\r\n--%s--\r\n",
            boundary);

        int total_length = strlen(pre) + file_size + strlen(post);
        char *full_body = malloc(total_length + 1);
        if (!full_body) {
            ESP_LOGE(SENDTAG, "Memory allocation failed for full body");
            free(file_content);
            free(pre);
            free(post);
            continue;
        }

        sprintf(full_body, "%s%s%s", pre, file_content, post);
        free(file_content);
        free(pre);
        free(post);

        // Set up HTTP client
        esp_http_client_config_t config = {
            .url = url,
            .cert_pem = DigiCertGlobalRootG2_crt_pem_start,
            .transport_type = HTTP_TRANSPORT_OVER_SSL,
            .method = HTTP_METHOD_POST,
            .event_handler = http_event_handler,
            .timeout_ms = 35000,
        };

        ESP_LOGI(SENDTAG, "HTTPS upload to: %s", config.url);

        esp_http_client_handle_t client = esp_http_client_init(&config);
        if (!client) {
            ESP_LOGE(SENDTAG, "Failed to initialize HTTP client");
            free(full_body);
            continue;
        }

        char content_type_header[128];
        snprintf(content_type_header, sizeof(content_type_header),
                 "multipart/form-data; boundary=%s", boundary);
        esp_http_client_set_header(client, "Content-Type", content_type_header);
        esp_http_client_set_header(client, "Connection", "keep-alive");

        if (esp_http_client_open(client, total_length) != ESP_OK) {
            ESP_LOGE(SENDTAG, "Failed to open HTTP connection");
            esp_http_client_cleanup(client);
            free(full_body);
            continue;
        }

        if (esp_http_client_write(client, full_body, total_length) < 0) {
            ESP_LOGE(SENDTAG, "Failed to write request body");
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            free(full_body);
            continue;
        }

        free(full_body);

        // Read response
        ret = esp_http_client_fetch_headers(client);
        int status_code = esp_http_client_get_status_code(client);
        int response_length = esp_http_client_get_content_length(client);
        ESP_LOGI(SENDTAG, "Upload complete. HTTP status: %d, Response length: %d", status_code, response_length);

        if (response_length > 0 && response_buf && buf_size > 0) {
            int read_len = esp_http_client_read_response(client, response_buf, buf_size - 1);
            if (read_len >= 0) {
                response_buf[read_len] = '\0';
                ESP_LOGI(SENDTAG, "Response Body: %s", response_buf);
            } else {
                response_buf[0] = '\0';
                ESP_LOGW(SENDTAG, "Failed to read response body");
            }
        }

        esp_http_client_close(client);
        esp_http_client_cleanup(client);

        if (status_code == 200) {
            ret = ESP_OK;
            break;
        } else {
            ESP_LOGW(SENDTAG, "Non-200 response. Retrying...");
            ret = ESP_FAIL;
        }
    }

    return ret;
}

void try_upload_now(void) {
    if (!wifi_connected) {
        ESP_LOGW(SENDTAG, "No WiFi. Skipping upload.");
        return;
    }

    const char *file_path = "/sdcard/payload.txt";
    const char *url = "https://h2overwatch.ca/DesktopModules/ShiftUP_VolsenseMap/waterFile.ashx";

    char response_buf[1024] = {0};
    esp_err_t upload_ret = upload_file_to_server(file_path, url, response_buf, sizeof(response_buf));

    if (upload_ret == ESP_OK) {
        ESP_LOGI(SENDTAG, "File uploaded successfully");
    } else {
        ESP_LOGE(SENDTAG, "File upload failed");
    }

    if (strlen(response_buf) > 0) {
        ESP_LOGI(SENDTAG, "Server response: %s", response_buf);
    }
}

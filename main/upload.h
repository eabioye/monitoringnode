#ifndef HTTP_UPLOAD_H
#define HTTP_UPLOAD_H

#include "esp_http_client.h"
#include "esp_log.h"
#include "sdcard.h"
#include <string.h>
#include "esp_system.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"


#define SENDTAG "HTTPS_UPLOAD"
#define UPLOAD_RETRY_COUNT 3
#define UPLOAD_RETRY_DELAY_MS 2000

// Function to upload a file to the server
esp_err_t upload_file_to_server(const char *file_path, const char *url, char *response_buf, size_t buf_size);
void try_upload_now(void);

extern const char DigiCertGlobalRootG2_crt_pem_start[] asm("_binary_DigiCertGlobalRootG2_crt_pem_start");

#endif

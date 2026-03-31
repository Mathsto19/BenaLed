#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_spiffs.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

/* ============================= Constants ============================== */

#define WIFI_AP_SSID "BenaLed"
#define WIFI_AP_PASS ""
#define WIFI_AP_MAX_CONN 4
#define MAX_MATRIX_BODY_SIZE 16384
#define PORTAL_URL "http://BenaLed.com"

static const uint8_t AP_IP_BYTES[4] = {4, 3, 2, 1};

static const char *TAG = "BENALED";

#endif // APP_CONFIG_H
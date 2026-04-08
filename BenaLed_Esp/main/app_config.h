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
#define MATRIX_WIDTH 32
#define MATRIX_HEIGHT 32
#define MATRIX_CHANNELS 3
#define MATRIX_RGB_FRAME_SIZE (MATRIX_WIDTH * MATRIX_HEIGHT * MATRIX_CHANNELS)
#define MATRIX_QUEUE_LEN 2
#define PORTAL_URL "http://BenaLed.com"

// OLED (Heltec HTUT-WB32LA(F))
#define OLED_I2C_PORT_NUM 0
#define OLED_I2C_SDA_GPIO 17
#define OLED_I2C_SCL_GPIO 18
#define OLED_RESET_GPIO 21
#define OLED_VEXT_CTRL_GPIO 36
#define OLED_VEXT_ACTIVE_LEVEL 0
#define OLED_I2C_ADDRESS 0x3C
#define OLED_I2C_FREQ_HZ 400000
#define OLED_WIDTH 128
#define OLED_HEIGHT 64

static const uint8_t AP_IP_BYTES[4] = {4, 3, 2, 1};

#endif // APP_CONFIG_H

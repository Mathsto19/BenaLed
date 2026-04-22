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
#define MATRIX_DATA_GPIO 2

// Physical panel tiling (for chained modules)
#define MATRIX_PANEL_WIDTH 16
#define MATRIX_PANEL_HEIGHT 16

// Panel chain order in the wiring path:
// row-major => 1(top-left), 2(top-right), 3(bottom-left), 4(bottom-right)
#define MATRIX_PANEL_CHAIN_ROW_MAJOR 0
#define MATRIX_PANEL_CHAIN_SERPENTINE 1
// serpentine + ODD_ROWS_REVERSED=0 => 2,1,3,4 (troca apenas 1 <-> 2)
#define MATRIX_PANEL_CHAIN_LAYOUT MATRIX_PANEL_CHAIN_SERPENTINE
#define MATRIX_PANEL_CHAIN_ODD_ROWS_REVERSED 0

// Physical LED order on the strip/panel
#define MATRIX_COLOR_ORDER_RGB 0
#define MATRIX_COLOR_ORDER_GRB 1
#define MATRIX_COLOR_ORDER_BRG 2
#define MATRIX_COLOR_ORDER_RBG 3
#define MATRIX_COLOR_ORDER_GBR 4
#define MATRIX_COLOR_ORDER_BGR 5
#define MATRIX_COLOR_ORDER MATRIX_COLOR_ORDER_GRB

// How pixels are wired physically inside each panel
#define MATRIX_LAYOUT_PROGRESSIVE 0
#define MATRIX_LAYOUT_SERPENTINE 1
#define MATRIX_LAYOUT MATRIX_LAYOUT_SERPENTINE

// On serpentine layout, odd rows are typically reversed (left<->right)
#define MATRIX_SERPENTINE_ODD_ROWS_REVERSED 1

// Which corner is pixel 0 inside each panel
#define MATRIX_ORIGIN_TOP_LEFT 0
#define MATRIX_ORIGIN_TOP_RIGHT 1
#define MATRIX_ORIGIN_BOTTOM_LEFT 3
#define MATRIX_ORIGIN_BOTTOM_RIGHT 2
#define MATRIX_ORIGIN MATRIX_ORIGIN_TOP_RIGHT

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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

#define WIFI_AP_SSID            "BenaLed"
#define WIFI_AP_PASS            "12345678"
#define WIFI_AP_MAX_CONN        4
#define MAX_MATRIX_BODY_SIZE    16384

static const char *TAG = "BENALED";

static esp_err_t send_file(httpd_req_t *req, const char *path, const char *content_type)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        ESP_LOGE(TAG, "Nao foi possivel abrir: %s", path);
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Arquivo nao encontrado");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, content_type);

    char chunk[1024];
    size_t read_bytes = 0;
    while ((read_bytes = fread(chunk, 1, sizeof(chunk), file)) > 0) {
        esp_err_t err = httpd_resp_send_chunk(req, chunk, read_bytes);
        if (err != ESP_OK) {
            fclose(file);
            httpd_resp_sendstr_chunk(req, NULL);
            return err;
        }
    }

    fclose(file);
    return httpd_resp_send_chunk(req, NULL, 0);
}

static esp_err_t index_get_handler(httpd_req_t *req)
{
    return send_file(req, "/spiffs/index.html", "text/html");
}

static esp_err_t css_get_handler(httpd_req_t *req)
{
    return send_file(req, "/spiffs/style.css", "text/css");
}

static esp_err_t js_get_handler(httpd_req_t *req)
{
    return send_file(req, "/spiffs/app.js", "application/javascript");
}

static esp_err_t redirect_to_root_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t matrix_post_handler(httpd_req_t *req)
{
    if (req->content_len <= 0 || req->content_len > MAX_MATRIX_BODY_SIZE) {
        ESP_LOGE(TAG, "Payload invalido: %d bytes", req->content_len);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Payload invalido");
        return ESP_FAIL;
    }

    char *body = calloc(1, req->content_len + 1);
    if (!body) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Sem memoria");
        return ESP_ERR_NO_MEM;
    }

    int received = 0;
    while (received < req->content_len) {
        int ret = httpd_req_recv(req, body + received, req->content_len - received);
        if (ret <= 0) {
            free(body);
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                httpd_resp_send_408(req);
            } else {
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Falha ao ler POST");
            }
            return ESP_FAIL;
        }
        received += ret;
    }

    body[req->content_len] = '\0';

    ESP_LOGI(TAG, "================ MATRIZ RECEBIDA ================");
    ESP_LOGI(TAG, "%s", body);
    ESP_LOGI(TAG, "=================================================");

    free(body);

    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

static esp_err_t favicon_get_handler(httpd_req_t *req)
{
    return send_file(req, "/spiffs/favicon.ico", "image/x-icon");
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base != WIFI_EVENT) {
        return;
    }

    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
        ESP_LOGI(TAG,
                 "Cliente conectado: %02x:%02x:%02x:%02x:%02x:%02x, AID=%d",
                 event->mac[0], event->mac[1], event->mac[2],
                 event->mac[3], event->mac[4], event->mac[5], event->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
        ESP_LOGI(TAG,
                 "Cliente desconectado: %02x:%02x:%02x:%02x:%02x:%02x, AID=%d",
                 event->mac[0], event->mac[1], event->mac[2],
                 event->mac[3], event->mac[4], event->mac[5], event->aid);
    }
}

static void init_spiffs(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = "spiffs",
        .max_files = 8,
        .format_if_mount_failed = true,
    };

    ESP_ERROR_CHECK(esp_vfs_spiffs_register(&conf));

    size_t total = 0;
    size_t used = 0;
    ESP_ERROR_CHECK(esp_spiffs_info(conf.partition_label, &total, &used));
    ESP_LOGI(TAG, "SPIFFS montado. Total=%u bytes, usado=%u bytes", (unsigned)total, (unsigned)used);
}

static void init_wifi_softap(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = WIFI_AP_SSID,
            .ssid_len = sizeof(WIFI_AP_SSID) - 1,
            .channel = 1,
            .password = WIFI_AP_PASS,
            .max_connection = WIFI_AP_MAX_CONN,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK,
            .pmf_cfg = {
                .required = false,
            },
        },
    };

    if (strlen(WIFI_AP_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Wi-Fi AP iniciado");
    ESP_LOGI(TAG, "SSID: %s", WIFI_AP_SSID);
    ESP_LOGI(TAG, "Senha: %s", WIFI_AP_PASS);
    ESP_LOGI(TAG, "Abra no navegador: http://192.168.4.1/");
}

static void dns_server_task(void *arg)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Falha ao criar socket DNS");
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(53),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };

    if (bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        ESP_LOGE(TAG, "Falha no bind da porta 53");
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "DNS captive iniciado na porta 53");

    while (1) {
        uint8_t rx[512];
        uint8_t tx[512];

        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int len = recvfrom(sock, rx, sizeof(rx), 0,
                           (struct sockaddr *)&client_addr, &client_len);
        if (len < 12) {
            continue;
        }

        int qname_end = 12;
        while (qname_end < len && rx[qname_end] != 0) {
            qname_end += rx[qname_end] + 1;
        }

        if (qname_end + 5 >= len) {
            continue;
        }

        memcpy(tx, rx, qname_end + 5);

        tx[2] = 0x81;
        tx[3] = 0x80;

        tx[6] = 0x00;
        tx[7] = 0x01;
        tx[8] = 0x00;
        tx[9] = 0x00;
        tx[10] = 0x00;
        tx[11] = 0x00;

        int p = qname_end + 5;

        tx[p++] = 0xC0;
        tx[p++] = 0x0C;

        tx[p++] = 0x00;
        tx[p++] = 0x01;

        tx[p++] = 0x00;
        tx[p++] = 0x01;

        tx[p++] = 0x00;
        tx[p++] = 0x00;
        tx[p++] = 0x00;
        tx[p++] = 0x00;

        tx[p++] = 0x00;
        tx[p++] = 0x04;

        tx[p++] = 192;
        tx[p++] = 168;
        tx[p++] = 4;
        tx[p++] = 1;

        sendto(sock, tx, p, 0, (struct sockaddr *)&client_addr, client_len);
    }
}

static httpd_handle_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.server_port = 80;
    config.max_uri_handlers = 16;
    config.stack_size = 8192;

    httpd_handle_t server = NULL;
    ESP_ERROR_CHECK(httpd_start(&server, &config));

    httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = index_get_handler,
        .user_ctx = NULL,
    };

    httpd_uri_t index_html_uri = {
        .uri = "/index.html",
        .method = HTTP_GET,
        .handler = index_get_handler,
        .user_ctx = NULL,
    };

    httpd_uri_t css_uri = {
        .uri = "/style.css",
        .method = HTTP_GET,
        .handler = css_get_handler,
        .user_ctx = NULL,
    };

    httpd_uri_t js_uri = {
        .uri = "/app.js",
        .method = HTTP_GET,
        .handler = js_get_handler,
        .user_ctx = NULL,
    };

    httpd_uri_t matrix_uri = {
        .uri = "/matrix",
        .method = HTTP_POST,
        .handler = matrix_post_handler,
        .user_ctx = NULL,
    };

    httpd_uri_t favicon_uri = {
        .uri = "/favicon.ico",
        .method = HTTP_GET,
        .handler = favicon_get_handler,
        .user_ctx = NULL,
    };

    httpd_uri_t gen_204_uri = {
    .uri = "/generate_204",
    .method = HTTP_GET,
    .handler = redirect_to_root_handler,
    .user_ctx = NULL,
    };

    httpd_uri_t hotspot_detect_uri = {
        .uri = "/hotspot-detect.html",
        .method = HTTP_GET,
        .handler = redirect_to_root_handler,
        .user_ctx = NULL,
    };

    httpd_uri_t connecttest_uri = {
        .uri = "/connecttest.txt",
        .method = HTTP_GET,
        .handler = redirect_to_root_handler,
        .user_ctx = NULL,
    };

    httpd_uri_t ncsi_uri = {
        .uri = "/ncsi.txt",
        .method = HTTP_GET,
        .handler = redirect_to_root_handler,
        .user_ctx = NULL,
    };

    httpd_uri_t wildcard_uri = {
        .uri = "/*",
        .method = HTTP_GET,
        .handler = redirect_to_root_handler,
        .user_ctx = NULL,
    };


    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &index_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &index_html_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &css_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &js_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &matrix_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &favicon_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &gen_204_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &hotspot_detect_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &connecttest_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &ncsi_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &wildcard_uri));
    ESP_LOGI(TAG, "Servidor HTTP iniciado na porta %d", config.server_port);
    return server;
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    init_spiffs();
    init_wifi_softap();
    start_webserver();
    xTaskCreate(dns_server_task, "dns_server_task", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "Projeto pronto.");
    ESP_LOGI(TAG, "Quando voce desenhar no site, a matriz vai aparecer aqui no terminal do ESP.");
}

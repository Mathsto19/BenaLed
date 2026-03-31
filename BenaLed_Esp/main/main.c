#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app_config.h"
#include "httpd.h"
#include "wifi_lib.h"
#include "dns_task.h"
/* ======================== Forward Declarations ======================== */

static esp_err_t matrix_ws_handler(httpd_req_t *req);

static void register_captive_uri(httpd_handle_t server, const char *uri);

/* =========================== Matrix WebSocket ========================= */

static esp_err_t matrix_ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET)
    {
        ESP_LOGI(TAG, "Cliente WebSocket conectado em /matrix");
        return ESP_OK;
    }

    httpd_ws_frame_t ws_pkt = {
        .final = true,
        .fragmented = false,
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = NULL,
        .len = 0,
    };

    esp_err_t err = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Falha ao ler tamanho do frame WS: %s", esp_err_to_name(err));
        return err;
    }

    if (ws_pkt.len == 0 || ws_pkt.len > MAX_MATRIX_BODY_SIZE)
    {
        ESP_LOGE(TAG, "Frame WS invalido: %u bytes", (unsigned)ws_pkt.len);
        return ESP_FAIL;
    }

    uint8_t *payload = calloc(1, ws_pkt.len + 1);
    if (!payload)
    {
        ESP_LOGE(TAG, "Sem memoria para frame WS");
        return ESP_ERR_NO_MEM;
    }

    ws_pkt.payload = payload;
    err = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Falha ao ler frame WS: %s", esp_err_to_name(err));
        free(payload);
        return err;
    }

    payload[ws_pkt.len] = '\0';

    ESP_LOGI(TAG, "================ MATRIZ RECEBIDA ================");
    ESP_LOGI(TAG, "%s", (char *)payload);
    ESP_LOGI(TAG, "=================================================");

    free(payload);
    return ESP_OK;
}

/* ============================ Wi-Fi / SPIFFS ========================== */

/* ================================ DNS ================================= */

/* ============================ HTTP Server ============================= */

static void register_captive_uri(httpd_handle_t server, const char *uri)
{
    httpd_uri_t get_uri = {
        .uri = uri,
        .method = HTTP_GET,
        .handler = captive_redirect_handler,
        .user_ctx = NULL,
    };

    httpd_uri_t head_uri = {
        .uri = uri,
        .method = HTTP_HEAD,
        .handler = captive_redirect_head_handler,
        .user_ctx = NULL,
    };

    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &get_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &head_uri));
}

static httpd_handle_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.server_port = 80;
    config.max_uri_handlers = 32;
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

    httpd_uri_t gifuct_uri = {
        .uri = "/gifuct-js.min.js",
        .method = HTTP_GET,
        .handler = gifuct_get_handler,
        .user_ctx = NULL,
    };

    httpd_uri_t matrix_uri = {
        .uri = "/matrix",
        .method = HTTP_GET,
        .handler = matrix_ws_handler,
        .is_websocket = true,
        .user_ctx = NULL,
    };

    httpd_uri_t favicon_uri = {
        .uri = "/favicon.ico",
        .method = HTTP_GET,
        .handler = favicon_get_handler,
        .user_ctx = NULL,
    };

    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &index_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &index_html_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &css_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &js_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &gifuct_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &matrix_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &favicon_uri));

    // Mantém compatibilidade com iPhone/macOS e outros clientes
    register_captive_uri(server, "/generate_204");
    register_captive_uri(server, "/gen_204");
    register_captive_uri(server, "/hotspot-detect.html");
    register_captive_uri(server, "/connecttest.txt");
    register_captive_uri(server, "/ncsi.txt");
    register_captive_uri(server, "/fwlink");
    register_captive_uri(server, "/success.txt");
    register_captive_uri(server, "/library/test/success.html");

    // Catch-all por último
    register_captive_uri(server, "/*");

    ESP_LOGI(TAG, "Servidor HTTP iniciado na porta %d", config.server_port);
    return server;
}

/* ============================== App Main ============================== */

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    init_spiffs();
    init_wifi_softap();
    start_webserver();
    init_dns_task(NULL);

    ESP_LOGI(TAG, "Projeto pronto.");
    ESP_LOGI(TAG, "Quando voce desenhar no site, a matriz vai aparecer aqui no terminal do ESP.");
}
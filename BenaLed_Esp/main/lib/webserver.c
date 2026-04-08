#include "webserver.h"
#include "httpd.h"
#include "matrix_task.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#define TAG "WEB_SERVER"

static bool is_hex_char(char c)
{
    return isxdigit((unsigned char)c) != 0;
}

static uint8_t hex_nibble(char c)
{
    if (c >= '0' && c <= '9')
    {
        return (uint8_t)(c - '0');
    }

    c = (char)tolower((unsigned char)c);
    return (uint8_t)(10 + (c - 'a'));
}

static esp_err_t parse_legacy_text_matrix_to_rgb(const uint8_t *payload, size_t len, uint8_t *rgb_out)
{
    if (payload == NULL || rgb_out == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    size_t pixel_count = (size_t)MATRIX_WIDTH * (size_t)MATRIX_HEIGHT;
    size_t found = 0;

    for (size_t i = 0; i + 7 < len && found < pixel_count; i++)
    {
        if (payload[i] != '"')
        {
            continue;
        }

        size_t start = i + 1;
        if (start < len && payload[start] == '#')
        {
            start++;
        }

        if (start + 6 >= len)
        {
            continue;
        }

        if (!is_hex_char((char)payload[start + 0]) ||
            !is_hex_char((char)payload[start + 1]) ||
            !is_hex_char((char)payload[start + 2]) ||
            !is_hex_char((char)payload[start + 3]) ||
            !is_hex_char((char)payload[start + 4]) ||
            !is_hex_char((char)payload[start + 5]))
        {
            continue;
        }

        if (payload[start + 6] != '"')
        {
            continue;
        }

        size_t out = found * 3;
        rgb_out[out + 0] = (uint8_t)((hex_nibble((char)payload[start + 0]) << 4) | hex_nibble((char)payload[start + 1]));
        rgb_out[out + 1] = (uint8_t)((hex_nibble((char)payload[start + 2]) << 4) | hex_nibble((char)payload[start + 3]));
        rgb_out[out + 2] = (uint8_t)((hex_nibble((char)payload[start + 4]) << 4) | hex_nibble((char)payload[start + 5]));
        found++;

        i = start + 6;
    }

    if (found != pixel_count)
    {
        return ESP_ERR_INVALID_RESPONSE;
    }

    return ESP_OK;
}

void register_captive_uri(httpd_handle_t server, const char *uri)
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

httpd_handle_t start_webserver(void)
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

    httpd_uri_t spfc_gif_uri = {
        .uri = "/Complemento/spfc.gif",
        .method = HTTP_GET,
        .handler = spfc_gif_get_handler,
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
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &spfc_gif_uri));
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

esp_err_t matrix_ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET)
    {
        ESP_LOGI(TAG, "Cliente WebSocket conectado em /matrix");
        return ESP_OK;
    }

    httpd_ws_frame_t ws_pkt = {
        .final = true,
        .fragmented = false,
        .type = HTTPD_WS_TYPE_BINARY,
        .payload = NULL,
        .len = 0,
    };

    esp_err_t err = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Falha ao ler tamanho do frame WS: %s", esp_err_to_name(err));
        return err;
    }

    uint8_t rgb_frame[MATRIX_RGB_FRAME_SIZE];

    if (ws_pkt.len == MATRIX_RGB_FRAME_SIZE)
    {
        ws_pkt.payload = rgb_frame;
        err = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Falha ao ler frame WS: %s", esp_err_to_name(err));
            return err;
        }

        if (ws_pkt.type != HTTPD_WS_TYPE_BINARY)
        {
            ESP_LOGW(TAG, "Frame de %u bytes recebido sem tipo binario (tipo=%d)", (unsigned)ws_pkt.len, ws_pkt.type);
            return ESP_ERR_INVALID_ARG;
        }
    }
    else
    {
        if (ws_pkt.len > MAX_MATRIX_BODY_SIZE)
        {
            ESP_LOGE(TAG, "Frame WS invalido: %u bytes (maximo %u)", (unsigned)ws_pkt.len, (unsigned)MAX_MATRIX_BODY_SIZE);
            return ESP_ERR_INVALID_SIZE;
        }

        uint8_t *raw_payload = (uint8_t *)malloc(ws_pkt.len + 1);
        if (raw_payload == NULL)
        {
            ESP_LOGE(TAG, "Sem memoria para frame WS de %u bytes", (unsigned)ws_pkt.len);
            return ESP_ERR_NO_MEM;
        }

        ws_pkt.payload = raw_payload;
        err = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (err != ESP_OK)
        {
            free(raw_payload);
            ESP_LOGE(TAG, "Falha ao ler frame WS legado: %s", esp_err_to_name(err));
            return err;
        }

        if (ws_pkt.type != HTTPD_WS_TYPE_TEXT)
        {
            ESP_LOGW(TAG, "Frame WS nao suportado: tipo=%d len=%u", ws_pkt.type, (unsigned)ws_pkt.len);
            free(raw_payload);
            return ESP_ERR_INVALID_ARG;
        }

        err = parse_legacy_text_matrix_to_rgb(raw_payload, ws_pkt.len, rgb_frame);
        free(raw_payload);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Frame WS texto invalido: %u bytes (esperado binario de %u ou texto legado parseavel)",
                     (unsigned)ws_pkt.len,
                     (unsigned)MATRIX_RGB_FRAME_SIZE);
            return err;
        }

        ESP_LOGW(TAG, "Cliente legado detectado: frame texto %u bytes convertido para RGB binario", (unsigned)ws_pkt.len);
    }

    err = matrix_queue_push(rgb_frame, MATRIX_RGB_FRAME_SIZE);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "Falha ao enfileirar frame RGB: %s", esp_err_to_name(err));
        return err;
    }

    return ESP_OK;
}

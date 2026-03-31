#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app_config.h"
#include "httpd.h"

/* ======================== Forward Declarations ======================== */

static esp_err_t matrix_ws_handler(httpd_req_t *req);

static void register_captive_uri(httpd_handle_t server, const char *uri);
static uint16_t dns_read_u16(const uint8_t *p);
static size_t dns_skip_name(const uint8_t *packet, size_t packet_len, size_t offset);

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

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base != WIFI_EVENT)
    {
        return;
    }

    if (event_id == WIFI_EVENT_AP_STACONNECTED)
    {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
        ESP_LOGI(TAG,
                 "Cliente conectado: %02x:%02x:%02x:%02x:%02x:%02x, AID=%d",
                 event->mac[0], event->mac[1], event->mac[2],
                 event->mac[3], event->mac[4], event->mac[5], event->aid);
    }
    else if (event_id == WIFI_EVENT_AP_STADISCONNECTED)
    {
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

    // Pega a referência para a interface de rede do AP
    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();

    // ---- MÁGICA DO WLED AQUI ----
    esp_netif_ip_info_t ip_info;
    esp_netif_dhcps_stop(ap_netif); // Pausa o DHCP

    esp_netif_set_ip4_addr(&ip_info.ip, 4, 3, 2, 1);
    esp_netif_set_ip4_addr(&ip_info.gw, 4, 3, 2, 1);
    esp_netif_set_ip4_addr(&ip_info.netmask, 255, 255, 255, 0);

    ESP_ERROR_CHECK(esp_netif_set_ip_info(ap_netif, &ip_info));
    esp_netif_dhcps_start(ap_netif); // Inicia o DHCP de volta com o IP novo
    // -----------------------------

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // O resto da sua função continua exatamente igual...
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

    if (strlen(WIFI_AP_PASS) == 0)
    {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Wi-Fi AP iniciado");
    ESP_LOGI(TAG, "SSID: %s", WIFI_AP_SSID);
    ESP_LOGI(TAG, "Senha: %s", WIFI_AP_PASS);
    ESP_LOGI(TAG, "Abra no navegador: %s", PORTAL_URL);
}

/* ================================ DNS ================================= */

static void dns_server_task(void *arg)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0)
    {
        ESP_LOGE(TAG, "Falha ao criar socket DNS");
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(53),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };

    if (bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        ESP_LOGE(TAG, "Falha no bind da porta 53");
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "DNS captive iniciado na porta 53");

    while (1)
    {
        uint8_t rx[512];
        uint8_t response[512];

        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int len = recvfrom(sock, rx, sizeof(rx), 0,
                           (struct sockaddr *)&client_addr, &client_len);
        if (len < 12)
        {
            continue;
        }

        uint16_t qdcount = dns_read_u16(&rx[4]);
        if (qdcount != 1)
        {
            continue;
        }

        size_t qname_end = dns_skip_name(rx, (size_t)len, 12);
        if (qname_end == 0 || (qname_end + 4) > (size_t)len)
        {
            continue;
        }

        uint16_t qtype = dns_read_u16(&rx[qname_end]);
        uint16_t qclass = dns_read_u16(&rx[qname_end + 2]);

        if (qclass != 0x0001)
        {
            continue;
        }

        size_t question_len = qname_end + 4;
        memcpy(response, rx, question_len);

        // Header
        response[2] = 0x81; // QR=1, OPCODE=0, AA=0, TC=0, RD=1 (mantém compatível)
        response[3] = 0x80; // RA=1, Z=0, RCODE=0

        // QDCOUNT = 1
        response[4] = 0x00;
        response[5] = 0x01;

        // ANCOUNT = 1 somente para consulta A
        response[6] = 0x00;
        response[7] = (qtype == 0x0001) ? 0x01 : 0x00;

        // NSCOUNT = 0
        response[8] = 0x00;
        response[9] = 0x00;

        // ARCOUNT = 0
        response[10] = 0x00;
        response[11] = 0x00;

        int resp_len = (int)question_len;

        if (qtype == 0x0001)
        { // Tipo A
            if ((resp_len + 16) > (int)sizeof(response))
            {
                continue;
            }

            response[resp_len++] = 0xC0;
            response[resp_len++] = 0x0C;
            response[resp_len++] = 0x00;
            response[resp_len++] = 0x01; // Tipo A
            response[resp_len++] = 0x00;
            response[resp_len++] = 0x01; // Classe IN
            response[resp_len++] = 0x00;
            response[resp_len++] = 0x00;
            response[resp_len++] = 0x00;
            response[resp_len++] = 0x3C; // TTL = 60s
            response[resp_len++] = 0x00;
            response[resp_len++] = 0x04; // RDLENGTH = 4
            response[resp_len++] = AP_IP_BYTES[0];
            response[resp_len++] = AP_IP_BYTES[1];
            response[resp_len++] = AP_IP_BYTES[2];
            response[resp_len++] = AP_IP_BYTES[3];
        }

        sendto(sock, response, resp_len, 0,
               (struct sockaddr *)&client_addr, client_len);
    }
}

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
    xTaskCreate(dns_server_task, "dns_server_task", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "Projeto pronto.");
    ESP_LOGI(TAG, "Quando voce desenhar no site, a matriz vai aparecer aqui no terminal do ESP.");
}
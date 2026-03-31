#include "dns_task.h"
#define TAG "DNS_TASK"
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

void init_dns_task(void *pvParameters)
{
    xTaskCreate(dns_server_task, "dns_server_task", 4096, NULL, 5, NULL);
}

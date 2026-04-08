#include "dns_task.h"

#include <stdbool.h>
#include <ctype.h>
#include <string.h>
#include <strings.h>

#define TAG "DNS_TASK"

static bool dns_extract_qname_lower(const uint8_t *packet, size_t packet_len, size_t offset, char *out, size_t out_size)
{
    if (packet == NULL || out == NULL || out_size < 2 || offset >= packet_len)
    {
        return false;
    }

    size_t out_pos = 0;
    while (offset < packet_len)
    {
        uint8_t label_len = packet[offset++];
        if (label_len == 0)
        {
            out[out_pos] = '\0';
            return true;
        }

        if ((label_len & 0xC0) != 0 || (offset + label_len) > packet_len)
        {
            return false;
        }

        if (out_pos != 0)
        {
            if (out_pos + 1 >= out_size)
            {
                return false;
            }
            out[out_pos++] = '.';
        }

        for (uint8_t i = 0; i < label_len; i++)
        {
            if (out_pos + 1 >= out_size)
            {
                return false;
            }

            out[out_pos++] = (char)tolower((unsigned char)packet[offset + i]);
        }

        offset += label_len;
    }

    return false;
}

static bool dns_name_equals_ci(const char *left, const char *right)
{
    return left != NULL && right != NULL && strcasecmp(left, right) == 0;
}

static bool dns_name_ends_with_ci(const char *name, const char *suffix)
{
    if (name == NULL || suffix == NULL)
    {
        return false;
    }

    size_t name_len = strlen(name);
    size_t suffix_len = strlen(suffix);
    if (suffix_len == 0 || name_len < suffix_len)
    {
        return false;
    }

    const char *start = name + (name_len - suffix_len);
    if (strcasecmp(start, suffix) != 0)
    {
        return false;
    }

    return (start == name) || (*(start - 1) == '.');
}

static bool dns_should_redirect_to_ap(const char *qname)
{
    if (qname == NULL || qname[0] == '\0')
    {
        return false;
    }

    if (dns_name_ends_with_ci(qname, "benaled.com"))
    {
        return true;
    }

    // Captive portal checks commonly used by major platforms.
    if (dns_name_equals_ci(qname, "connectivitycheck.gstatic.com") ||
        dns_name_equals_ci(qname, "clients3.google.com") ||
        dns_name_equals_ci(qname, "clients2.google.com") ||
        dns_name_equals_ci(qname, "captive.apple.com") ||
        dns_name_equals_ci(qname, "www.msftconnecttest.com") ||
        dns_name_equals_ci(qname, "msftconnecttest.com") ||
        dns_name_equals_ci(qname, "www.msftncsi.com") ||
        dns_name_equals_ci(qname, "msftncsi.com") ||
        dns_name_equals_ci(qname, "detectportal.firefox.com"))
    {
        return true;
    }

    // Everything else resolves to NXDOMAIN to avoid socket storms from
    // background apps trying many internet endpoints through captive DNS.
    return false;
}

static void dns_server_task(void *arg)
{
    (void)arg;

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

        int len = recvfrom(sock, rx, sizeof(rx), 0, (struct sockaddr *)&client_addr, &client_len);
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

        char qname[256];
        if (!dns_extract_qname_lower(rx, (size_t)len, 12, qname, sizeof(qname)))
        {
            continue;
        }

        uint16_t qtype = dns_read_u16(&rx[qname_end]);
        uint16_t qclass = dns_read_u16(&rx[qname_end + 2]);
        if (qclass != 0x0001)
        {
            continue;
        }

        bool should_redirect = dns_should_redirect_to_ap(qname);

        size_t question_len = qname_end + 4;
        memcpy(response, rx, question_len);

        // Header
        response[2] = 0x81;                    // QR=1, OPCODE=0, AA=0, TC=0, RD=1
        response[3] = should_redirect ? 0x80 : 0x83; // RA=1, RCODE=0 or NXDOMAIN(3)

        // QDCOUNT = 1
        response[4] = 0x00;
        response[5] = 0x01;

        // ANCOUNT = 1 only for A queries when redirecting to AP
        response[6] = 0x00;
        response[7] = (should_redirect && qtype == 0x0001) ? 0x01 : 0x00;

        // NSCOUNT = 0
        response[8] = 0x00;
        response[9] = 0x00;

        // ARCOUNT = 0
        response[10] = 0x00;
        response[11] = 0x00;

        int resp_len = (int)question_len;

        if (should_redirect && qtype == 0x0001)
        {
            if ((resp_len + 16) > (int)sizeof(response))
            {
                continue;
            }

            response[resp_len++] = 0xC0;
            response[resp_len++] = 0x0C;
            response[resp_len++] = 0x00;
            response[resp_len++] = 0x01; // A
            response[resp_len++] = 0x00;
            response[resp_len++] = 0x01; // IN
            response[resp_len++] = 0x00;
            response[resp_len++] = 0x00;
            response[resp_len++] = 0x00;
            response[resp_len++] = 0x3C; // TTL 60s
            response[resp_len++] = 0x00;
            response[resp_len++] = 0x04; // RDLENGTH
            response[resp_len++] = AP_IP_BYTES[0];
            response[resp_len++] = AP_IP_BYTES[1];
            response[resp_len++] = AP_IP_BYTES[2];
            response[resp_len++] = AP_IP_BYTES[3];
        }

        sendto(sock, response, resp_len, 0, (struct sockaddr *)&client_addr, client_len);
    }
}

void init_dns_task(void *pvParameters)
{
    (void)pvParameters;
    xTaskCreate(dns_server_task, "dns_server_task", 4096, NULL, 5, NULL);
}

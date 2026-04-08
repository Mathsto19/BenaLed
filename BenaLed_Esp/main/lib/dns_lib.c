#include "dns_lib.h"
#define TAG "DNS"

uint16_t dns_read_u16(const uint8_t *p)
{
    return ((uint16_t)p[0] << 8) | p[1];
}

size_t dns_skip_name(const uint8_t *packet, size_t packet_len, size_t offset)
{
    while (offset < packet_len)
    {
        uint8_t label_len = packet[offset];

        if (label_len == 0)
        {
            return offset + 1;
        }

        if ((label_len & 0xC0) == 0xC0)
        {
            if (offset + 1 < packet_len)
            {
                return offset + 2;
            }
            return 0;
        }

        offset += 1 + label_len;
    }

    return 0;
}
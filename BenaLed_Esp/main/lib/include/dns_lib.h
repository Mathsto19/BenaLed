#ifndef DNS_LIB_H
#define DNS_LIB_H

#include "app_config.h"

uint16_t dns_read_u16(const uint8_t *p);
size_t dns_skip_name(const uint8_t *packet, size_t packet_len, size_t offset);

#endif // DNS_LIB_H
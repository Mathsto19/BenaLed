#ifndef WIFI_LIB_H
#define WIFI_LIB_H

#include "app_config.h"

void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
void init_wifi_softap(void);
void init_spiffs(void);

#endif
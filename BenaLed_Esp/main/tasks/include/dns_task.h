#ifndef DNS_TASK_H
#define DNS_TASK_H

#include "app_config.h"
#include "dns_lib.h"

void init_dns_task(void *pvParameters);
static void dns_server_task(void *arg);

#endif // DNS_TASK_H
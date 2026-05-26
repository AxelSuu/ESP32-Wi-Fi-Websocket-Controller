#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#define NETWORK_READY_BIT BIT0

extern EventGroupHandle_t net_event_group;

void network_wifi_init_ap(void);

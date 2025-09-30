//
// Created by HaoKun Tong on 2025/9/29.
//
#include "WiFiTask.h"
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "FreeRTOS.h"
#include "task.h"
#include <cstdio>

#define WIFI_SSID "test"
#define WIFI_PASSWORD "thk12345"

void wifi_task(void *param) {
    stdio_init_all();

    if (cyw43_arch_init()) {
        printf("Wi-Fi init failed!\n");
        vTaskDelete(NULL);
    }

    cyw43_arch_enable_sta_mode();

    int res = cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD,
                                                 CYW43_AUTH_WPA2_AES_PSK, 30000);
    if (res) {
        printf("Wi-Fi connect failed, code=%d\n", res);
    } else {
        printf("Wi-Fi connected!\n");
    }

    vTaskDelete(NULL);
}
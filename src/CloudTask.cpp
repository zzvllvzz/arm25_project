//
// Created by HaoKun Tong on 2025/9/29.
//
#include "FreeRTOS.h"
#include "task.h"
#include "CloudTask.h"
#include <cstdio>
#include "lwip/apps/sntp.h"
#include <cstring>
#include "tls_client.h"


extern QueueHandle_t data_queue;


#define THINGSPEAK_WRITE_KEY "IWJ871L0TMWUKKI2"
#define THINGSPEAK_SERVER    "api.thingspeak.com"
#define TALKBACK_ID "55377"
#define TALKBACK_API_KEY "IQ8PEVJEEXQA0GEP"
#define THINGSPEAK_CERT "-----BEGIN CERTIFICATE-----\n\
MIIDjjCCAnagAwIBAgIQAzrx5qcRqaC7KGSxHQn65TANBgkqhkiG9w0BAQsFADBh\n\
MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3\n\
d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBH\n\
MjAeFw0xMzA4MDExMjAwMDBaFw0zODAxMTUxMjAwMDBaMGExCzAJBgNVBAYTAlVT\n\
MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j\n\
b20xIDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IEcyMIIBIjANBgkqhkiG\n\
9w0BAQEFAAOCAQ8AMIIBCgKCAQEAuzfNNNx7a8myaJCtSnX/RrohCgiN9RlUyfuI\n\
2/Ou8jqJkTx65qsGGmvPrC3oXgkkRLpimn7Wo6h+4FR1IAWsULecYxpsMNzaHxmx\n\
1x7e/dfgy5SDN67sH0NO3Xss0r0upS/kqbitOtSZpLYl6ZtrAGCSYP9PIUkY92eQ\n\
q2EGnI/yuum06ZIya7XzV+hdG82MHauVBJVJ8zUtluNJbd134/tJS7SsVQepj5Wz\n\
tCO7TG1F8PapspUwtP1MVYwnSlcUfIKdzXOS0xZKBgyMUNGPHgm+F6HmIcr9g+UQ\n\
vIOlCsRnKPZzFBQ9RnbDhxSJITRNrw9FDKZJobq7nMWxM4MphQIDAQABo0IwQDAP\n\
BgNVHRMBAf8EBTADAQH/MA4GA1UdDwEB/wQEAwIBhjAdBgNVHQ4EFgQUTiJUIBiV\n\
5uNu5g/6+rkS7QYXjzkwDQYJKoZIhvcNAQELBQADggEBAGBnKJRvDkhj6zHd6mcY\n\
1Yl9PMWLSn/pvtsrF9+wX3N3KjITOYFnQoQj8kVnNeyIv/iPsGEMNKSuIEyExtv4\n\
NeF22d+mQrvHRAiGfzZ0JFrabA0UWTW98kndth/Jsw1HKj2ZL7tcu7XUIOGZX1NG\n\
Fdtom/DzMNU+MeKNhJ7jitralj41E6Vf8PlwUHBHQRFXGU7Aj64GxJUTFy8bJZ91\n\
8rGOmaFvE7FBcf6IKshPECBV1/MUReXgRPTqh5Uykw7+U0b6LJ3/iyK5S9kJRaTe\n\
pLiaWN0bfVKfjllDiIGknibVb63dDcY3fe0Dkhvld1927jyNxF1WW6LZZm6zNTfl\n\
MrY=\n\
-----END CERTIFICATE-----\n"


void cloud_task(void *param) {
    all_data d;
    char body[64];
    char request[512];

    while (true) {
        // Try to get latest sensor data from queue
        if (xQueueReceive(data_queue, &d, pdMS_TO_TICKS(1000)) == pdTRUE) {

            // ================== Upload sensor data ==================
            snprintf(body, sizeof(body),
                     "api_key=%s&field1=%.1f&field2=%.1f&field3=%.1f&field4=%d&field5=%.1f",
                     THINGSPEAK_WRITE_KEY,
                     d.co2_data, d.hmp60_rh, d.hmp60_t,
                     50, d.user_set_level);

            snprintf(request, sizeof(request),
                     "POST /update HTTP/1.1\r\n"
                     "Host: %s\r\n"
                     "Connection: close\r\n"
                     "Content-Type: application/x-www-form-urlencoded\r\n"
                     "Content-Length: %d\r\n\r\n"
                     "%s",
                     THINGSPEAK_SERVER, (int)strlen(body), body);

            // Send POST request to ThingSpeak
            const char things_cert[] = THINGSPEAK_CERT;
            bool pass = run_tls_client_test((const uint8_t*)things_cert, sizeof(things_cert),
                                            THINGSPEAK_SERVER, request, 15);
            if (pass) {
                printf("[CLOUD] Upload success\n");
            } else {
                printf("[CLOUD] Upload failed\n");
            }

            // ================== Fetch TalkBack command ==================
            snprintf(request, sizeof(request),
         "GET /talkbacks/%s/commands/execute.json?api_key=%s HTTP/1.1\r\n"
         "Host: %s\r\n"
         "Connection: close\r\n\r\n",
         TALKBACK_ID, TALKBACK_API_KEY, THINGSPEAK_SERVER);

            printf("Request:\n%s\n", request);
            // Execute GET request to retrieve TalkBack command
            char *cmd_resp = run_tls_client_request(
                                 (const uint8_t*)things_cert, sizeof(things_cert),
                                 THINGSPEAK_SERVER, request, 15);

            if (cmd_resp) {
                printf("[CLOUD] TalkBack response:\n%s\n", cmd_resp);

                // Parse response: look for "command_string"
                char *pos = strstr(cmd_resp, "\"command_string\":\"");
                if (pos) {
                    pos += strlen("\"command_string\":\"");
                    char *end = strchr(pos, '"');
                    if (end) *end = '\0';

                    // Convert command string to float value
                    char *endptr;
                    double val = strtod(pos, &endptr);
                    if (endptr != pos) {
                        float new_setpoint = (float)val;
                        if (new_setpoint > 0) {
                            printf("[CLOUD] user_set = %.1f ppm\n", new_setpoint);
                            d.user_set_level = new_setpoint(data_queue, &d);
                            xQueueSend(data_queue, &d, portMAX_DELAY);
                        }
                    }
                }
                free(cmd_resp);  // Free allocated memory from TLS client
            } else {
                printf("[CLOUD] TalkBack failed\n");
            }

            // Push updated data back to queue
            xQueueSend(data_queue, &d,portMAX_DELAY);
        }

        // Run every 15 seconds
        vTaskDelay(pdMS_TO_TICKS(15000));
    }
}
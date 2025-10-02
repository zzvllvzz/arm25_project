//
// CloudTask.cpp
//
#include "FreeRTOS.h"
#include "task.h"
#include "CloudTask.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>   // strtod, free
#include "tls_client.h"
#include "Manager.h" // for struct all_data

// Queues/vars defined in main.cpp
extern QueueHandle_t ui_queue;
extern float user_set_level;
extern SemaphoreHandle_t user_level_mutex;
extern QueueHandle_t user_queue;

#define THINGSPEAK_WRITE_KEY "IWJ871L0TMWUKKI2"
#define THINGSPEAK_SERVER    "api.thingspeak.com"
#define TALKBACK_ID          "55377"
#define TALKBACK_API_KEY     "3M1MMIOO2AOWR0O6"

// DigiCert Global Root G2 (for api.thingspeak.com)
#define THINGSPEAK_CERT "-----BEGIN CERTIFICATE-----\n" \
"MIIDjjCCAnagAwIBAgIQAzrx5qcRqaC7KGSxHQn65TANBgkqhkiG9w0BAQsFADBh\n" \
"MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3\n" \
"d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBH\n" \
"MjAeFw0xMzA4MDExMjAwMDBaFw0zODAxMTUxMjAwMDBaMGExCzAJBgNVBAYTAlVT\n" \
"MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j\n" \
"b20xIDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IEcyMIIBIjANBgkqhkiG\n" \
"9w0BAQEFAAOCAQ8AMIIBCgKCAQEAuzfNNNx7a8myaJCtSnX/RrohCgiN9RlUyfuI\n" \
"2/Ou8jqJkTx65qsGGmvPrC3oXgkkRLpimn7Wo6h+4FR1IAWsULecYxpsMNzaHxmx\n" \
"1x7e/dfgy5SDN67sH0NO3Xss0r0upS/kqbitOtSZpLYl6ZtrAGCSYP9PIUkY92eQ\n" \
"q2EGnI/yuum06ZIya7XzV+hdG82MHauVBJVJ8zUtluNJbd134/tJS7SsVQepj5Wz\n" \
"tCO7TG1F8PapspUwtP1MVYwnSlcUfIKdzXOS0xZKBgyMUNGPHgm+F6HmIcr9g+UQ\n" \
"vIOlCsRnKPZzFBQ9RnbDhxSJITRNrw9FDKZJobq7nMWxM4MphQIDAQABo0IwQDAP\n" \
"BgNVHRMBAf8EBTADAQH/MA4GA1UdDwEB/wQEAwIBhjAdBgNVHQ4EFgQUTiJUIBiV\n" \
"5uNu5g/6+rkS7QYXjzkwDQYJKoZIhvcNAQELBQADggEBAGBnKJRvDkhj6zHd6mcY\n" \
"1Yl9PMWLSn/pvtsrF9+wX3N3KjITOYFnQoQj8kVnNeyIv/iPsGEMNKSuIEyExtv4\n" \
"NeF22d+mQrvHRAiGfzZ0JFrabA0UWTW98kndth/Jsw1HKj2ZL7tcu7XUIOGZX1NG\n" \
"Fdtom/DzMNU+MeKNhJ7jitralj41E6Vf8PlwUHBHQRFXGU7Aj64GxJUTFy8bJZ91\n" \
"8rGOmaFvE7FBcf6IKshPECBV1/MUReXgRPTqh5Uykw7+U0b6LJ3/iyK5S9kJRaTe\n" \
"pLiaWN0bfVKfjllDiIGknibVb63dDcY3fe0Dkhvld1927jyNxF1WW6LZZm6zNTfl\n" \
"MrY=\n" \
"-----END CERTIFICATE-----\n"

void cloud_task(void *param) {
    (void)param;

    // Bigger buffers to avoid truncation
    char body[256];
    char request[1024];
    const char things_cert[] = THINGSPEAK_CERT;

    all_data d;

    while (true) {
        // 1) Read latest sensor frame without consuming the queue

        if (xQueuePeek(ui_queue, &d, pdMS_TO_TICKS(1000)) == pdTRUE) {

            // Read user_set_level with a mutex (non-blocking fallback to current)
            float level = 0.0f;
            if (user_level_mutex && xSemaphoreTake(user_level_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                level = user_set_level;
                xSemaphoreGive(user_level_mutex);
            } else {
                level = user_set_level;
            }

            // 2) POST latest data to ThingSpeak (field5 = user_set_level)
            int n = snprintf(body, sizeof(body),
                             "api_key=%s&field1=%.2f&field2=%.2f&field3=%.2f&field4=%.2f&field5=%.2f",
                             THINGSPEAK_WRITE_KEY,
                             d.co2_data, d.hmp60_rh, d.hmp60_t, d.fan_percent, level);
            if (n < 0 || n >= (int)sizeof(body)) {
                printf("[CLOUD] body overflow (%d)\n", n);
                vTaskDelay(pdMS_TO_TICKS(2000));
                continue;
            }

            n = snprintf(request, sizeof(request),
                         "POST /update HTTP/1.1\r\n"
                         "Host: %s\r\n"
                         "Connection: close\r\n"
                         "Content-Type: application/x-www-form-urlencoded\r\n"
                         "Content-Length: %d\r\n\r\n"
                         "%s",
                         THINGSPEAK_SERVER, (int)strlen(body), body);
            if (n < 0 || n >= (int)sizeof(request)) {
                printf("[CLOUD] request overflow (%d)\n", n);
                vTaskDelay(pdMS_TO_TICKS(2000));
                continue;
            }

            bool pass = run_tls_client_test((const uint8_t*)things_cert, sizeof(things_cert),
                                            THINGSPEAK_SERVER, request, 15);
            printf(pass ? "[CLOUD] Upload success\n" : "[CLOUD] Upload failed\n");

            // 3) GET TalkBack command and update user_set_level if present
            n = snprintf(request, sizeof(request),
             "POST /talkbacks/%s/commands/execute.json HTTP/1.1\r\n"
             "Host: %s\r\n"
             "Connection: close\r\n"
             "Content-Type: application/x-www-form-urlencoded\r\n"
             "Content-Length: %d\r\n\r\n"
             "api_key=%s",
             TALKBACK_ID, THINGSPEAK_SERVER,
             (int)strlen("api_key=" TALKBACK_API_KEY),
             TALKBACK_API_KEY);
            if (n < 0 || n >= (int)sizeof(request)) {
                printf("[CLOUD] talkback req overflow (%d)\n", n);
                vTaskDelay(pdMS_TO_TICKS(2000));
                continue;
            }

            char *cmd_resp = run_tls_client_request(
                                (const uint8_t*)things_cert, sizeof(things_cert),
                                THINGSPEAK_SERVER, request, 15);
            if (cmd_resp) {
                printf("[CLOUD] TalkBack response:\n%s\n", cmd_resp);

                char *pos = strstr(cmd_resp, "\"command_string\":\"");
                if (pos) {
                    pos += strlen("\"command_string\":\"");
                    char *end = strchr(pos, '"');
                    if (end) *end = '\0';

                    char *endptr;
                    double val = strtod(pos, &endptr);
                    if (endptr != pos && val > 0.0) {
                        float new_set = (float)val;
                        if (user_level_mutex &&
                            xSemaphoreTake(user_level_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                            user_set_level = new_set;
                            xSemaphoreGive(user_level_mutex);
                        } else {
                            user_set_level = new_set;
                        }
                        printf("[CLOUD] user_set_level updated to %.1f ppm\n", user_set_level);
                        xQueueSend(user_queue,&new_set,0);
                    }
                }
                free(cmd_resp);
            } else {
                printf("[CLOUD] TalkBack failed\n");
            }
        }

        //every 15s sceconds the server can request the command and send data
        vTaskDelay(pdMS_TO_TICKS(20000));
    }
}

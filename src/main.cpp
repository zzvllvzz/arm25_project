#include <iostream>
#include <sstream>

#include "CloudTask.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "hardware/gpio.h"
#include "PicoOsUart.h"
#include "ssd1306.h"
#include "hardware/timer.h"
#include "Manager.h"
#include "eeprom.h"
#include "rotary.h"
#include "WiFiTask.h"

extern "C" {
uint32_t read_runtime_ctr(void) {
    return timer_hw->timerawl;
}
}

#include "blinker.h"

SemaphoreHandle_t gpio_sem;
QueueHandle_t data_queue;
QueueHandle_t user_queue;
QueueHandle_t rotary_queue;


SemaphoreHandle_t user_level_mutex;   // suojaus
float user_set_level = 200;       // ppm


void modbus_task(void *param);
void display_task(void *param);
void i2c_task(void *param);
void user_input_task(void *param);
void rotary_task(void *param);
extern "C" {
    void tls_test(void);
}
void tls_task(void *param)
{
    tls_test();
    while(true) {
        vTaskDelay(100);
    }
}

int main()
{
    stdio_init_all();
    printf("\nBoot\n");
    setup_rotary(); // rotary pins

    data_queue = xQueueCreate(1, sizeof(all_data));
    user_queue = xQueueCreate(1, sizeof(float));
    rotary_queue = xQueueCreate(4, sizeof(float));
    user_level_mutex = xSemaphoreCreateMutex();



    gpio_sem = xSemaphoreCreateBinary();

#if 1
    xTaskCreate(modbus_task, "Modbus", 512, (void *) nullptr,
    2, nullptr);


    xTaskCreate(display_task, "SSD1306", 512,  (void *) nullptr,
                tskIDLE_PRIORITY + 1, nullptr);
    xTaskCreate(rotary_task, "rotary ", 512,  (void *) nullptr,
                2, nullptr);
    // xTaskCreate(tls_task, "tls test", 6000, (void *) nullptr,
    //            tskIDLE_PRIORITY + 1, nullptr);
    xTaskCreate(wifi_task,"wifi",512,nullptr,3,nullptr);

    xTaskCreate(cloud_task,"cloud",2048,nullptr,2,nullptr);
#endif
#if 1
    // xTaskCreate(i2c_task, "i2c test", 512,  (void *) nullptr,
    //             tskIDLE_PRIORITY + 1, nullptr);
#endif
#if 0

#endif
    vTaskStartScheduler();

    while(true){};
}

#include <cstdio>
#include "ModbusClient.h"
#include "ModbusRegister.h"

// We are using pins 0 and 1, but see the GPIO function select table in the
// datasheet for information on which other pins can be used.
#if 0
#define UART_NR 0
#define UART_TX_PIN 0
#define UART_RX_PIN 1
#else
#define UART_NR 1
#define UART_TX_PIN 4
#define UART_RX_PIN 5
#endif

#define BAUD_RATE 9600
#define STOP_BITS 2 // for real system (pico simualtor also requires 2 stop bits)

#define USE_MODBUS

void rotary_task(void *param) {

    // you can set these
    constexpr float STEP = 10;
    constexpr float MINP = 100.0f;
    constexpr float MAXP = 1500.0f;

    event_t ev;
    for (;;) {
        if (xQueueReceive(rotary_queue, &ev, portMAX_DELAY) == pdTRUE) {
            if (xSemaphoreTake(user_level_mutex, portMAX_DELAY) == pdTRUE) {
                switch (ev.type) {
                    case EVENT_ENCODER_CW:
                        user_set_level += STEP;
                        if (user_set_level > MAXP) user_set_level = MAXP;
                        break;

                    case EVENT_ENCODER_CCW:
                        user_set_level -= STEP;
                        if (user_set_level < MINP) user_set_level = MINP;
                        break;

                }
                xSemaphoreGive(user_level_mutex);
            }
        }
    }
}

void modbus_task(void *param) {
    (void)param;
    Manager modbus_manager;

    for(;;) {
        all_data d = modbus_manager.read_data();
        if (user_set_level > d.co2_data) {
            printf("CO2 below user set limit opening the valve\n");
            printf("user set limit: %.f\n");

           // d = modbus_manager.valve_open(user_set_level);
        } if (d.co2_data> user_set_level) {
            printf("CO2 above user set limit opening the valve\n");
            printf("user set limit: %.f\n");
        }

        xQueueSend(data_queue, &d, portMAX_DELAY);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}





#include "ssd1306os.h"
void display_task(void *param)
{
    auto i2cbus{std::make_shared<PicoI2C>(1, 400000)};
    ssd1306os display(i2cbus);
    all_data d{};
    all_data last_data{};  // save the last data
    char line[64];

    while (true) {
        // check for new data if not print old data
        if (xQueueReceive(data_queue, &d, pdMS_TO_TICKS(50)) == pdTRUE) {
            last_data = d; // update last data
        }

        display.fill(0);

        //
        snprintf(line,sizeof(line),"CO2:%.2f ppm", last_data.co2_data);
        display.text(line,0,0);

        snprintf(line,sizeof(line),"RH: %.2f %%", last_data.hmp60_rh);
        display.text(line,0,10);

        snprintf(line,sizeof(line),"T: %.2f C", last_data.hmp60_t);
        display.text(line,0,20);

        // set level protected with semaphore
        float level;
        if (xSemaphoreTake(user_level_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            level = user_set_level;
            xSemaphoreGive(user_level_mutex);
        } else {
            level = 0; // fallback
        }
        snprintf(line,sizeof(line),"Set: %.1f ppm", level);
        display.text(line,0,30);

        display.show();

        vTaskDelay(pdMS_TO_TICKS(200)); // update the screen 5 times a sec
    }
}



void i2c_task(void *param) {
    auto i2cbus{std::make_shared<PicoI2C>(0, 100000)};

    const uint led_pin = 21;
    const uint delay = pdMS_TO_TICKS(250);
    gpio_init(led_pin);
    gpio_set_dir(led_pin, GPIO_OUT);

    uint8_t buffer[64] = {0};
    i2cbus->write(0x50, buffer, 2);

    auto rv = i2cbus->read(0x50, buffer, 64);
    printf("rv=%u\n", rv);
    for(int i = 0; i < 64; ++i) {
        printf("%c", isprint(buffer[i]) ? buffer[i] : '_');
    }
    printf("\n");

    buffer[0]=0;
    buffer[1]=64;
    rv = i2cbus->transaction(0x50, buffer, 2, buffer, 64);
    printf("rv=%u\n", rv);
    for(int i = 0; i < 64; ++i) {
        printf("%c", isprint(buffer[i]) ? buffer[i] : '_');
    }
    printf("\n");

    while(true) {
        gpio_put(led_pin, 1);
        vTaskDelay(delay);
        gpio_put(led_pin, 0);
        vTaskDelay(delay);
    }


}

void user_input_task(void *params) {
    char buffer[128];
    int idx=0;

    const float CO2_MIN = 100.0f;

    const float CO2_MAX = 2000.0f; // in document
    printf("Enter the new CO2 limit (ppm, e.g. 120.4): ");
    fflush(stdout);


    for (;;) {
        int c = getchar_timeout_us(0);
        if (c != PICO_ERROR_TIMEOUT) {
            char ch = (char)c;

            if (ch == '\r' || ch == '\n') {
                if (idx > 0) {
                    //space or enter has been pressed then add
                    buffer[idx] = '\0';

                    float new_limit = 0.0f;
                    // scan the buffer for float
                    if (sscanf(buffer, "%f", &new_limit) == 1) {

                        // checking the limits
                        if (new_limit < CO2_MIN)  new_limit = CO2_MIN;
                        if (new_limit > CO2_MAX)  new_limit = CO2_MAX;

                        // send to queue (float)
                        if (xQueueSend(user_queue, &new_limit, portMAX_DELAY) == pdPASS) {
                            printf("\n[OK] new CO2 limit: %.1f ppm\n", new_limit);
                        } else {
                            printf("\n[ERR] queue send failed\n");
                        }
                    } else {
                        //this if there is no float in the buffer
                        printf("\nInvalid input. Try again.\n");
                    }

                    //start over
                    idx = 0;
                }
                printf("\nEnter the new CO2 limit (ppm, e.g. 120.4): ");
                fflush(stdout);

            } else if (isprint((unsigned char)ch)) {
                if (idx< (int)sizeof(buffer) - 1) {
                    buffer[idx++] = ch;
                    fflush(stdout);
                }
            }
        }

        // small delay
        vTaskDelay(pdMS_TO_TICKS(10));
    }
};
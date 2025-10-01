#include <iostream>
#include <sstream>
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "hardware/gpio.h"
#include "blinker.h"
#include "PicoOsUart.h"
#include "ssd1306.h"
#include "hardware/timer.h"
#include "Manager.h"
#include "eeprom.h"
#include "ssd1306os.h"

#define BAUD_RATE 9600
#define STOP_BITS 2 // for real system (pico simualtor also requires 2 stop bits)

#define USE_MODBUS

extern "C" {
uint32_t read_runtime_ctr(void) {
    return timer_hw->timerawl;
}
}

SemaphoreHandle_t gpio_sem;
QueueHandle_t data_queue;
QueueHandle_t user_queue;

// void gpio_callback(uint gpio, uint32_t events) {
//     BaseType_t xHigherPriorityTaskWoken = pdFALSE;
//     // signal task that a button was pressed
//     xSemaphoreGiveFromISR(gpio_sem, &xHigherPriorityTaskWoken);
//     portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
// }

void modbus_task(void *param) {
    (void)param;
    Manager modbus_manager;
    for(;;) {
        float new_user_level;
        all_data d = modbus_manager.read_data();


        if (xQueueReceive(user_queue, &new_user_level, 0)==pdTRUE){
            printf("new_user_level: %f\n", new_user_level);
            d.user_set_level = new_user_level;
        }


        if (d.user_set_level > d.co2_data) {
            printf("CO2 below user set limit opening the valve\n");
           d = modbus_manager.valve_open();
        } else {
        //     printf("[DATA] CO2=%.1f ppm | RH=%.1f %% | T=%.1f C | user_set=%.1f ppm | t=%lu\n",
        //                       d.co2_data, d.hmp60_rh, d.hmp60_t, d.user_set_level, d.timestamp);
         }

        xQueueSend(data_queue, &d, portMAX_DELAY);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

void display_task(void *param) {
    auto i2cbus{std::make_shared<PicoI2C>(1, 400000)};
    ssd1306os display(i2cbus);
    EEPROM_24C256 eeprom(i2c0);

    all_data d;
    char line[64];

    while (true) {
        display.fill(0);

        if (xQueueReceive(data_queue,&d,portMAX_DELAY)==pdTRUE) {
            snprintf(line,sizeof(line),"CO2:%.1f ppm",d.co2_data);
            display.text(line,0,10);
            snprintf(line,sizeof(line),"RH: %.1f %%",d.hmp60_rh);
            display.text(line,0,20);
            snprintf(line,sizeof(line),"T: %.1f C",d.hmp60_t);
            display.text(line,0,30);

            uint16_t co2_setpoint;
            if (eeprom.read_co2_setpoint(co2_setpoint)) {
                snprintf(line, sizeof(line), "CO2 Set: %u ppm", co2_setpoint);
            } else {
                snprintf(line, sizeof(line), "CO2 Set: Error");
            }
            display.text(line, 0, 40);
            display.show();
        }

        display.show();

        vTaskDelay(pdMS_TO_TICKS(1000)); // Update every second
    }
}

// void blink_task(void *param)
// {
//     auto lpr = (led_params *) param;
//     const uint led_pin = lpr->pin;
//     const uint delay = pdMS_TO_TICKS(lpr->delay);
//     gpio_init(led_pin);
//     gpio_set_dir(led_pin, GPIO_OUT);
//     while (true) {
//         gpio_put(led_pin, true);
//         vTaskDelay(delay);
//         gpio_put(led_pin, false);
//         vTaskDelay(delay);
//     }
// }

// void gpio_task(void *param) {
//     (void) param;
//     const uint button_pin = 9;
//     const uint led_pin = 22;
//     const uint delay = pdMS_TO_TICKS(250);
//     gpio_init(led_pin);
//     gpio_set_dir(led_pin, GPIO_OUT);
//     gpio_init(button_pin);
//     gpio_set_dir(button_pin, GPIO_IN);
//     gpio_set_pulls(button_pin, true, false);
//     gpio_set_irq_enabled_with_callback(button_pin, GPIO_IRQ_EDGE_FALL, true, &gpio_callback);
//     while(true) {
//         if(xSemaphoreTake(gpio_sem, portMAX_DELAY) == pdTRUE) {
//             //std::cout << "button event\n";
//             gpio_put(led_pin, 1);
//             vTaskDelay(delay);
//             gpio_put(led_pin, 0);
//             vTaskDelay(delay);
//         }
//     }
// }

// void serial_task(void *param)
// {
//     PicoOsUart u(0, 0, 1, 115200);
//     Blinker blinky(20);
//     uint8_t buffer[64];
//     std::string line;
//     while (true) {
//         if(int count = u.read(buffer, 63, 30); count > 0) {
//             u.write(buffer, count);
//             buffer[count] = '\0';
//             line += reinterpret_cast<const char *>(buffer);
//             if(line.find_first_of("\n\r") != std::string::npos){
//                 u.send("\n");
//                 std::istringstream input(line);
//                 std::string cmd;
//                 input >> cmd;
//                 if(cmd == "delay") {
//                     uint32_t i = 0;
//                     input >> i;
//                     blinky.on(i);
//                 }
//                 else if (cmd == "off") {
//                     blinky.off();
//                 }
//                 line.clear();
//             }
//         }
//     }
// }

// void modbus_task(void *param);
//void display_task(void *param);
// void i2c_task(void *param);
// void user_input_task(void *param);
// extern "C" {
//     void tls_test(void);
// }
// void tls_task(void *param)
// {
//     tls_test();
//     while(true) {
//         vTaskDelay(100);
//     }
// }

void user_input_task(void *params) {
    char buffer[128];
    int idx = 0;

    const float CO2_MIN = 100.0f;

    const float CO2_MAX = 2000.0f; // in document
    printf("Enter the new CO2 limit (ppm, e.g. 120.4): ");
    fflush(stdout);


    for (;;) {
        int c = getchar_timeout_us(0);
        if (c != PICO_ERROR_TIMEOUT) {
            char ch = (char) c;

            if (ch == '\r' || ch == '\n') {
                if (idx > 0) {
                    //space or enter has been pressed then add
                    buffer[idx] = '\0';

                    float new_limit = 0.0f;
                    // scan the buffer for float
                    if (sscanf(buffer, "%f", &new_limit) == 1) {
                        // checking the limits
                        if (new_limit < CO2_MIN) new_limit = CO2_MIN;
                        if (new_limit > CO2_MAX) new_limit = CO2_MAX;

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
            } else if (isprint((unsigned char) ch)) {
                if (idx < (int) sizeof(buffer) - 1) {
                    buffer[idx++] = ch;
                    fflush(stdout);
                }
            }
        }

        // small delay
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

int main() {
    stdio_init_all();
    printf("\nBoot\n");

    data_queue = xQueueCreate(1, sizeof(all_data));
    user_queue = xQueueCreate(1, sizeof(float));

#if 1
    xTaskCreate(modbus_task, "Modbus", 512, (void *) nullptr,
                tskIDLE_PRIORITY + 1, nullptr);
    xTaskCreate(
        display_task,
        "SSD1306",
        512,
        (void *) nullptr,
        tskIDLE_PRIORITY + 1,
        nullptr
        );
    xTaskCreate(
        user_input_task,
        "User input ",
        512,
        (void *) nullptr,
        2,
        nullptr
        );
    xTaskCreate(
        eepromTask,
        "EEPROM Task",
        1024,
        nullptr,
        tskIDLE_PRIORITY + 1,
        nullptr
    );

#endif
#if 1
    //xTaskCreate(i2c_task, "i2c test", 512,  (void *) nullptr,
    //           tskIDLE_PRIORITY + 1, nullptr);
#endif
#if 0

#endif
    vTaskStartScheduler();

    while (true) {
    };
}

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

// void i2c_task(void *param) {
//     auto i2cbus{std::make_shared<PicoI2C>(0, 100000)};
//
//     const uint led_pin = 21;
//     const uint delay = pdMS_TO_TICKS(250);
//     gpio_init(led_pin);
//     gpio_set_dir(led_pin, GPIO_OUT);
//
//     uint8_t buffer[64] = {0};
//     i2cbus->write(0x50, buffer, 2);
//
//     auto rv = i2cbus->read(0x50, buffer, 64);
//     printf("rv=%u\n", rv);
//     for(int i = 0; i < 64; ++i) {
//         printf("%c", isprint(buffer[i]) ? buffer[i] : '_');
//     }
//     printf("\n");
//
//     buffer[0]=0;
//     buffer[1]=64;
//     rv = i2cbus->transaction(0x50, buffer, 2, buffer, 64);
//     printf("rv=%u\n", rv);
//     for(int i = 0; i < 64; ++i) {
//         printf("%c", isprint(buffer[i]) ? buffer[i] : '_');
//     }
//     printf("\n");
//
//     while(true) {
//         gpio_put(led_pin, 1);
//         vTaskDelay(delay);
//         gpio_put(led_pin, 0);
//         vTaskDelay(delay);
//     }
//
//
// }

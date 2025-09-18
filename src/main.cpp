#include <iostream>
#include <sstream>
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "hardware/gpio.h"
#include "PicoI2C.h"
#include "PicoOsUart.h"
#include "ssd1306.h"
#include "hardware/timer.h"
#include "blinker.h"




extern "C" {
uint32_t read_runtime_ctr(void) {
    return timer_hw->timerawl;
}
}

static constexpr uint8_t SDP610_ADDR = 0x40;     // 7-bit address (64 decimal)
static constexpr uint8_t CMD_MEASURE  = 0xF1;    // trigger differential pressure measurement
static constexpr uint8_t CMD_SOFT_RST = 0xFE;    // soft reset (datasheet)
static constexpr float   SCALE_125PA  = 240.0f;  // scale factor for SDP610-125Pa

// CRC-8 for SF04 (SDP600 family): polynomial x^8 + x^5 + x^4 + 1 (0x31),
// CRC register initialised to 0x00 — implemented bitwise (per Sensirion app note).
static uint8_t sdp_crc8(const uint8_t *data, size_t len) {
    uint8_t crc = 0x00;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (uint8_t b = 0; b < 8; ++b) {
            if (crc & 0x80) crc = (uint8_t)((crc << 1) ^ 0x31);
            else            crc <<= 1;
        }
    }
    return crc;
}

// Read one measurement. Returns true on success (raw_out filled), false on error.
static bool sdp610_read_raw(std::shared_ptr<PicoI2C> i2c, int16_t &raw_out) {
    uint8_t cmd = CMD_MEASURE;
    uint8_t rx[3] = {0,0,0};
    // transaction(addr, write_buf, write_len, read_buf, read_len)
    int rv = i2c->transaction(SDP610_ADDR, &cmd, 1, rx, 3); // read 2 bytes + CRC (if master clocks it)
    if (rv < 2) return false; // no data
    // If the sensor provided a CRC byte (rv >= 3) verify it
    if (rv >= 3) {
        uint8_t calc = sdp_crc8(rx, 2);
        if (calc != rx[2]) {
            // CRC mismatch: try soft reset (recommended by app note) and fail
            uint8_t rst = CMD_SOFT_RST;
            i2c->write(SDP610_ADDR, &rst, 1);
            return false;
        }
    }
    // assemble signed 16-bit two's complement (MSB first)
    raw_out = static_cast<int16_t>((rx[0] << 8) | rx[1]);
    return true;
}

static float sdp610_raw_to_pa(int16_t raw) {
    return static_cast<float>(raw) / SCALE_125PA;
}

// datasheet: DPeff = DPsensor * (Pcal / Pamb), Pcal = 966 mbar (sensor calibration pressure)
float apply_altitude_correction(float dp_pa, float ambient_mbar) {
    const float Pcal = 966.0f;
    return dp_pa * (Pcal / ambient_mbar);
}


// Example FreeRTOS task: reads every 250 ms and prints Pa
void sdp610_task(void *param) {
    (void)param;
    // Use I2C1: in the project display and the pressure sensor share I2C1 (SDA=14, SCL=15).
    auto i2c = std::make_shared<PicoI2C>(1, 100000); // 100 kHz (or 400000 if you prefer - datasheet allows up to 400 kHz)
    int16_t raw;
    while (true) {
        if (sdp610_read_raw(i2c, raw)) {
            float pa = sdp610_raw_to_pa(raw);
            float dp_pa = apply_altitude_correction(pa, 1013);
            printf("SDP610 raw=%d -> %.3f Pa. Altitude Correction: %.3f\n", raw, pa, dp_pa);
        } else {g
            printf("SDP610 read failed or CRC mismatch\n");
        }
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

SemaphoreHandle_t gpio_sem;

void gpio_callback(uint gpio, uint32_t events) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    // signal task that a button was pressed
    xSemaphoreGiveFromISR(gpio_sem, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

struct led_params{
    uint pin;
    uint delay;
};

void blink_task(void *param)
{
    auto lpr = (led_params *) param;
    const uint led_pin = lpr->pin;
    const uint delay = pdMS_TO_TICKS(lpr->delay);
    gpio_init(led_pin);
    gpio_set_dir(led_pin, GPIO_OUT);
    while (true) {
        gpio_put(led_pin, true);
        vTaskDelay(delay);
        gpio_put(led_pin, false);
        vTaskDelay(delay);
    }
}

void gpio_task(void *param) {
    (void) param;
    const uint button_pin = 9;
    const uint led_pin = 22;
    const uint delay = pdMS_TO_TICKS(250);
    gpio_init(led_pin);
    gpio_set_dir(led_pin, GPIO_OUT);
    gpio_init(button_pin);
    gpio_set_dir(button_pin, GPIO_IN);
    gpio_set_pulls(button_pin, true, false);
    gpio_set_irq_enabled_with_callback(button_pin, GPIO_IRQ_EDGE_FALL, true, &gpio_callback);
    while(true) {
        if(xSemaphoreTake(gpio_sem, portMAX_DELAY) == pdTRUE) {
            //std::cout << "button event\n";
            gpio_put(led_pin, 1);
            vTaskDelay(delay);
            gpio_put(led_pin, 0);
            vTaskDelay(delay);
        }
    }
}

void serial_task(void *param)
{
    PicoOsUart u(0, 0, 1, 115200);
    Blinker blinky(20);
    uint8_t buffer[64];
    std::string line;
    while (true) {
        if(int count = u.read(buffer, 63, 30); count > 0) {
            u.write(buffer, count);
            buffer[count] = '\0';
            line += reinterpret_cast<const char *>(buffer);
            if(line.find_first_of("\n\r") != std::string::npos){
                u.send("\n");
                std::istringstream input(line);
                std::string cmd;
                input >> cmd;
                if(cmd == "delay") {
                    uint32_t i = 0;
                    input >> i;
                    blinky.on(i);
                }
                else if (cmd == "off") {
                    blinky.off();
                }
                line.clear();
            }
        }
    }
}

void modbus_task(void *param);
void display_task(void *param);
void i2c_task(void *param);
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
    static led_params lp1 = { .pin = 20, .delay = 300 };
    stdio_init_all();
    printf("\nBoot\n");

    gpio_sem = xSemaphoreCreateBinary();
    //xTaskCreate(blink_task, "LED_1", 256, (void *) &lp1, tskIDLE_PRIORITY + 1, nullptr);
    //xTaskCreate(gpio_task, "BUTTON", 256, (void *) nullptr, tskIDLE_PRIORITY + 1, nullptr);
    //xTaskCreate(serial_task, "UART0", 256, (void *) nullptr,
    //            tskIDLE_PRIORITY + 1, nullptr);
    //xTaskCreate(display_task, "SSD1306", 512, (void *) nullptr,
    //            tskIDLE_PRIORITY + 1, nullptr);
    xTaskCreate(sdp610_task, "SDP610", 512, nullptr, tskIDLE_PRIORITY + 1, nullptr);
#if 0
    xTaskCreate(modbus_task, "Modbus", 512, (void *) nullptr,
                tskIDLE_PRIORITY + 1, nullptr);


    xTaskCreate(display_task, "SSD1306", 512, (void *) nullptr,
                tskIDLE_PRIORITY + 1, nullptr);
#endif
#if 1
    xTaskCreate(i2c_task, "i2c test", 512, (void *) nullptr,
                tskIDLE_PRIORITY + 1, nullptr);
#endif
#if 0
    xTaskCreate(tls_task, "tls test", 6000, (void *) nullptr,
                tskIDLE_PRIORITY + 1, nullptr);
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

void modbus_task(void *param) {

    const uint led_pin = 22;
    const uint button = 9;

    // Initialize LED pin
    gpio_init(led_pin);
    gpio_set_dir(led_pin, GPIO_OUT);

    gpio_init(button);
    gpio_set_dir(button, GPIO_IN);
    gpio_pull_up(button);

    // Initialize chosen serial port
    //stdio_init_all();

    //printf("\nBoot\n");

#ifdef USE_MODBUS
    auto uart{std::make_shared<PicoOsUart>(UART_NR, UART_TX_PIN, UART_RX_PIN, BAUD_RATE, STOP_BITS)};
    auto rtu_client{std::make_shared<ModbusClient>(uart)};
    ModbusRegister rh(rtu_client, 241, 256);
    ModbusRegister t(rtu_client, 241, 257);
    ModbusRegister produal(rtu_client, 1, 0);
    produal.write(100);
    vTaskDelay((100));
    produal.write(100);
#endif

    while (true) {
#ifdef USE_MODBUS
        gpio_put(led_pin, !gpio_get(led_pin)); // toggle  led
        printf("RH=%5.1f%%\n", rh.read() / 10.0);
        vTaskDelay(5);
        printf("T =%5.1f%%\n", t.read() / 10.0);
        vTaskDelay(3000);
#endif
    }


}

#include "ssd1306os.h"
void display_task(void *param)
{
    auto i2cbus{std::make_shared<PicoI2C>(1, 400000)};
    ssd1306os display(i2cbus);
    display.fill(0);
    display.text("Boot", 0, 0);
    display.text("Sampo on homo", 0,25);
    display.show();
    while(true) {
        vTaskDelay(100);
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

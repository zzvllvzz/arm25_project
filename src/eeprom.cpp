#include "eeprom.h"
#include <cstdio>

#include "queue.h"

extern QueueHandle_t user_queue;

EEPROM_24C256::EEPROM_24C256(i2c_inst_t *i2c_instance, uint8_t addr)
    : i2c(i2c_instance), device_addr(addr) {
}

// bool EEPROM_24C256::test_communication() {
//     uint8_t addr_buf[2] = {0x00, 0x00};
//     int result = i2c_write_blocking(i2c, device_addr, addr_buf, 2, true);
//     return (result == 2);
// }
//
// void EEPROM_24C256::scan_bus() {
//     printf("Scanning I2C bus...\n");
//     for(uint8_t addr = 1; addr < 127; addr++) {
//         if(addr % 16 == 0) printf("\n");
//
//         uint8_t dummy[1] = {0};
//         int result = i2c_write_blocking(i2c, addr, dummy, 1, true);
//
//         if(result == 1) {
//             printf(" [0x%02X] ", addr);
//         } else {
//             printf(" . ");
//         }
//     }
//     printf("\n\n");
// }

bool EEPROM_24C256::write_byte(uint16_t mem_addr, uint8_t data) {
    uint8_t buf[3];
    buf[0] = (mem_addr >> 8) & 0xFF;
    buf[1] = mem_addr & 0xFF;
    buf[2] = data;

    int result = i2c_write_blocking(i2c, device_addr, buf, 3, false);
    vTaskDelay(pdMS_TO_TICKS(5)); // FreeRTOS delay
    return (result == 3);
}

bool EEPROM_24C256::write_page(uint16_t mem_addr, const uint8_t *data, uint8_t len) {
    if(len > 64) return false;

    uint8_t buf[66];
    buf[0] = (mem_addr >> 8) & 0xFF;
    buf[1] = mem_addr & 0xFF;

    for(uint8_t i = 0; i < len; i++) {
        buf[i + 2] = data[i];
    }

    int result = i2c_write_blocking(i2c, device_addr, buf, len + 2, false);
    vTaskDelay(pdMS_TO_TICKS(5));
    return (result == (len + 2));
}

bool EEPROM_24C256::write_data(uint16_t mem_addr, const uint8_t *data, uint16_t len) {
    uint16_t bytes_written = 0;

    while(bytes_written < len) {
        uint16_t page_start = (mem_addr + bytes_written) & 0xFFC0;
        uint16_t page_offset = (mem_addr + bytes_written) - page_start;
        uint16_t bytes_to_write = 64 - page_offset;

        if(bytes_to_write > (len - bytes_written)) {
            bytes_to_write = len - bytes_written;
        }

        if(!write_page(mem_addr + bytes_written, data + bytes_written, bytes_to_write)) {
            return false;
        }

        bytes_written += bytes_to_write;
    }

    return true;
}

uint8_t EEPROM_24C256::read_byte(uint16_t mem_addr) {
    uint8_t addr_buf[2] = {
        static_cast<uint8_t>((mem_addr >> 8) & 0xFF),
        static_cast<uint8_t>(mem_addr & 0xFF)
    };

    if(i2c_write_blocking(i2c, device_addr, addr_buf, 2, true) != 2) {
        return 0xFF;
    }

    uint8_t data;
    if(i2c_read_blocking(i2c, device_addr, &data, 1, false) == 1) {
        return data;
    }

    return 0xFF;
}

bool EEPROM_24C256::read_data(uint16_t mem_addr, uint8_t *data, uint16_t len) {
    uint8_t addr_buf[2] = {
        static_cast<uint8_t>((mem_addr >> 8) & 0xFF),
        static_cast<uint8_t>(mem_addr & 0xFF)
    };

    if(i2c_write_blocking(i2c, device_addr, addr_buf, 2, true) != 2) {
        return false;
    }

    return (i2c_read_blocking(i2c, device_addr, data, len, false) == len);
}

// CO2 setpoint functions
bool EEPROM_24C256::write_co2_setpoint(uint16_t ppm) {
    uint8_t data[2] = {
        static_cast<uint8_t>(ppm >> 8),
        static_cast<uint8_t>(ppm & 0xFF)
    };
    return write_data(0x0000, data, 2);
}

bool EEPROM_24C256::read_co2_setpoint(uint16_t &ppm) {
    uint8_t data[2];
    if(read_data(0x0000, data, 2)) {
        ppm = (data[0] << 8) | data[1];
        return true;
    }
    return false;
}

void eepromTask(void *param) {
    (void)param;

    printf("EEPROM FreeRTOS Task Started!\n");

    // Initialize I2C
    i2c_init(i2c0, 100000);
    gpio_set_function(16, GPIO_FUNC_I2C);
    gpio_set_function(17, GPIO_FUNC_I2C);
    gpio_pull_up(16);
    gpio_pull_up(17);

    EEPROM_24C256 eeprom(i2c0);

    // Test communication
    // if(!eeprom.test_communication()) {
    //     printf("EEPROM communication FAILED\n");
    //     eeprom.scan_bus();
    //     vTaskDelete(NULL);
    // }
    // printf("EEPROM communication OK\n");

    // Read existing setpoint on startup
    uint16_t current_ppm;
    if(eeprom.read_co2_setpoint(current_ppm)) {
        printf("Stored CO2 setpoint: %u ppm\n", current_ppm);
        // Send to command queue for system use
        float setpoint_float = (float)current_ppm;
        xQueueOverwrite(user_queue, &setpoint_float);
    } else {
        printf("No stored setpoint, initializing with 400 ppm\n");
        eeprom.write_co2_setpoint(400);
    }

    // Main loop - store new setpoints from queue
    float new_setpoint;
    while(true) {
        if(xQueueReceive(user_queue, &new_setpoint, portMAX_DELAY) == pdPASS) {
            uint16_t ppm_to_store = (uint16_t)new_setpoint;

            printf("EEPROM: Storing %u ppm... ", ppm_to_store);

            if(eeprom.write_co2_setpoint(ppm_to_store)) {
                printf("Success\n");

                // Optional verification
                // uint16_t verify_ppm;
                // if(eeprom.read_co2_setpoint(verify_ppm) && verify_ppm == ppm_to_store) {
                //     printf("EEPROM: Verified OK\n");
                // }
            } else {
                printf("Failed!\n");
            }
        }
    }
}
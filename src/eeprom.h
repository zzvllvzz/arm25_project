#ifndef EEPROM_H
#define EEPROM_H

#include <cstdint>
#include "hardware/i2c.h"
#include "pico/stdlib.h"
#include "FreeRTOS.h"
#include "task.h"

class EEPROM_24C256 {
private:
    i2c_inst_t *i2c;
    uint8_t device_addr;

public:
    EEPROM_24C256(i2c_inst_t *i2c_instance, uint8_t addr = 0x50);

    bool write_byte(uint16_t mem_addr, uint8_t data);
    bool write_page(uint16_t mem_addr, const uint8_t *data, uint8_t len);
    bool write_data(uint16_t mem_addr, const uint8_t *data, uint16_t len);

    uint8_t read_byte(uint16_t mem_addr);
    bool read_data(uint16_t mem_addr, uint8_t *data, uint16_t len);

    bool test_communication();
    void scan_bus();

    // CO2 setpoint functions
    bool write_co2_setpoint(uint16_t ppm);
    bool read_co2_setpoint(uint16_t &ppm);
};

// FreeRTOS task
void eeprom_test_task(void *param);

#endif
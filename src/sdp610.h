#ifndef SDP610_H
#define SDP610_H
#pragma once
#include <memory>
#include "PicoI2C.h"

// ==========================
// SDP610 driver interface
// ==========================

class SDP610 {
public:
    explicit SDP610(std::shared_ptr<PicoI2C> i2c);

    // Low-level API (optional if you want manual reads instead of the FreeRTOS task):
    bool sdp610_read_raw(int16_t &raw_out);
    float sdp610_raw_to_pa(int16_t raw);
    float apply_altitude_correction(float dp_pa, float ambient_mbar);

    // Task to periodically read SDP610 and print values.
    // Call xTaskCreate(sdp610_task, "SDP610", 512, nullptr, tskIDLE_PRIORITY + 1, nullptr);
    static void sdp610_task(void *param);


private:
    std::shared_ptr<PicoI2C> i2c;

    // ===== Constants from datasheet =====
    static constexpr uint8_t SDP610_ADDR = 0x40; // I2C 7-bit address
    static constexpr uint8_t CMD_MEASURE = 0xF1; // Trigger measurement
    static constexpr uint8_t CMD_SOFT_RST = 0xFE; // Soft reset
    static constexpr float SCALE_125PA = 240.0f; // Scale factor for 125Pa sensor
    static constexpr float AMBIENT_PRESSURE = 1013.0f; // Ambient pressure at 0 meters

    static uint8_t sdp_crc8(const uint8_t *data, size_t len);
};
#endif //SDP610_H

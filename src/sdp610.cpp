#include "sdp610.h"
#include "FreeRTOS.h"
#include "task.h"
#include <cstdio>


SDP610::SDP610(std::shared_ptr<PicoI2C> i2c) : i2c(std::move(i2c)) {}

// ===== CRC8 helper (Sensirion polynomial 0x31) =====
uint8_t SDP610::sdp_crc8(const uint8_t *data, size_t len) {
    uint8_t crc = 0x00;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (uint8_t b = 0; b < 8; ++b) {
            if (crc & 0x80) crc = (uint8_t) ((crc << 1) ^ 0x31);
            else crc <<= 1;
        }
    }
    return crc;
}

// ===== Read raw data =====
bool SDP610::sdp610_read_raw(int16_t &raw_out) {
    uint8_t cmd = CMD_MEASURE;
    uint8_t rx[3] = {0, 0, 0};

    // Send command, then read up to 3 bytes (MSB, LSB, CRC)
    int rv = i2c->transaction(SDP610_ADDR, &cmd, 1, rx, 3);
    if (rv < 2) return false;

    // If CRC byte present, check
    if (rv >= 3) {
        uint8_t calc = sdp_crc8(rx, 2);
        if (calc != rx[2]) {
            // CRC failed -> soft reset
            uint8_t rst = CMD_SOFT_RST;
            i2c->write(SDP610_ADDR, &rst, 1);
            return false;
        }
    }

    raw_out = static_cast<int16_t>((rx[0] << 8) | rx[1]);
    return true;
}

// ===== Convert to Pascals =====
float SDP610::sdp610_raw_to_pa(int16_t raw) {
    return static_cast<float>(raw) / SCALE_125PA;
}

// ==== Datasheet: DPeff = DPsensor * (Pcal / Pamb), Pcal = 966 mbar =====
// ==== (sensor calibration pressure) =====
float SDP610::apply_altitude_correction(float dp_pa, float ambient_mbar) {
    const float Pcal = 966.0f;
    return dp_pa * (Pcal / ambient_mbar);
}

// ===== FreeRTOS task =====
void SDP610::sdp610_task(void *param) {
    auto sensor = static_cast<SDP610*>(param);
    int16_t raw;

    while (true) {
        if (sensor -> sdp610_read_raw(raw)) {
            float pa = sensor -> sdp610_raw_to_pa(raw);
            float dp_pa = sensor -> apply_altitude_correction(pa, AMBIENT_PRESSURE);
            printf("SDP610 raw=%d -> %.3f Pa. Altitude correction: %.3f\n", raw, pa, dp_pa);
        } else {
            printf("SDP610 read failed or CRC mismatch\n");
        }
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

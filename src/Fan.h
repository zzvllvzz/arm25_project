//
// Created by Sampo Hulkko on 30.9.2025.
//

#ifndef RP2040_FREERTOS_IRQ_FAN_H
#define RP2040_FREERTOS_IRQ_FAN_H
#include "ModbusRegister.h"

struct FanState {
    float percent;        // 0.0 ... 100.0 (%)
    bool  running;        // true if pulses observed
    uint16_t last_pulses; // last raw counter read
};

class Fan {
    public:

    explicit Fan(std::shared_ptr<ModbusClient> &client, const int slave_addr = 1):
    ao1(client, slave_addr, 0),
    ao1_counter(client,slave_addr,4)  {}


    float set_percent(float pct ) {
        if (pct < 0.f)   pct = 0.f;
        if (pct > 100.f) pct = 100.f;
        // 0..100 % -> 0..1000 rekisteriarvo
        uint16_t reg = static_cast<uint16_t>(pct * 10.f );
        ao1.write(reg);
        cached.percent = pct;
        return pct;
    }
    FanState poll_running() {
        uint16_t pulses = ao1_counter.read(); // nollaa laskurin samalla
        cached.last_pulses = pulses;
        cached.running = (pulses > 0);
        return cached;
    }



private:
    ModbusRegister ao1; // speed control
    ModbusRegister ao1_counter; //
    FanState cached{0.0f, false, 0};

};

#endif //RP2040_FREERTOS_IRQ_FAN_H
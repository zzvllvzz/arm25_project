#ifndef RP2040_FREERTOS_IRQ_DEVICES_H
#define RP2040_FREERTOS_IRQ_DEVICES_H

#include <memory>
#include "modbus/ModbusRegister.h"
#include "modbus/ModbusClient.h"

class FanControl {
public:
    explicit FanControl(const std::shared_ptr<ModbusClient>& rtu_client, int slave = 1)
        : do1(rtu_client, slave, 0),
          ao1(rtu_client, slave, 0)
    {}

    void setOnOff(bool on) {
        do1.write(on ? 1 : 0);
    }

    void setSpeed(float percent) {
        printf("input %f\n", percent);
        int value = static_cast<int>(percent * 10.0f);
        ao1.write(value);
    }

private:
    ModbusRegister do1;
    ModbusRegister ao1;
};

#endif // RP2040_FREERTOS_IRQ_DEVICES_H



#ifndef HMP60_H
#define HMP60_H



#include<memory>
#include"modbus/ModbusRegister.h"
#include "modbus/ModbusClient.h"

struct hmp60_data {
    float rh;
    float t;
    bool ok;
};
class Hmp60sensor {
public:
    explicit  Hmp60sensor(const std::shared_ptr<ModbusClient> &rtu_client ,const int slave=241):rh_reg(rtu_client,slave,256),t_reg(rtu_client,slave,257){}
    hmp60_data read() {
        hmp60_data data;
        int rh_raw=rh_reg.read();
        int t_raw=t_reg.read();
        if (rh_raw<0||t_raw<0) {
            data.ok=false;
        }
        data.rh=rh_raw/10.0f;
        data.t=t_raw/10.0f;
        data.ok=true;
        return data;
    }

private:
    ModbusRegister rh_reg;
    ModbusRegister  t_reg;
};

#endif // HMP60_H
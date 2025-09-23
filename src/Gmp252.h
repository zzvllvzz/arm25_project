//
// Created by Sampo Hulkko on 19.9.2025.
//

#include<memory>
#include"modbus/ModbusRegister.h"
#include "modbus/ModbusClient.h"

#ifndef GMP252_H
#define GMP252_H

#endif //GMP252_H


struct Gmp252_data {
    float co2_data;
    bool status;
} ;

class Gmp252_co2 {
    public:
    explicit Gmp252_co2(std::shared_ptr<ModbusClient> &client, const int slave_addr = 240)
        : co2_req(client,slave_addr, 258){}
// read function which returns  data in a struct
    Gmp252_data read_co2() {
        Gmp252_data data;
        float raw_data = co2_req.read();
        // to do add filter
        if (raw_data < 0  || raw_data > 32000  ) {
            data.status = false;
        }
       data.co2_data = raw_data;
        data.status = true;
        return data;
    }

private:
    ModbusRegister co2_req;

};
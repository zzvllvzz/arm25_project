//
// Created by Sampo Hulkko on 19.9.2025.
//

#include<memory>
#include"modbus/ModbusRegister.h"
#include "modbus/ModbusClient.h"

#ifndef GMP252_H
#define GMP252_H
#define valve_pin 27

// basic error codes
enum ErrorCode {None, Range};

struct Gmp252_data {
    float co2_data;
    bool status;
    ErrorCode err;
} ;

class Gmp252_co2 {
public:
    explicit Gmp252_co2(std::shared_ptr<ModbusClient> &client, const int slave_addr = 240)
        : co2_req(client,slave_addr, 256){}
    // read function which returns  data in a struct
    Gmp252_data read_co2() {
        Gmp252_data data;
        float raw_data = co2_req.read();
        //filtering data

        if (raw_data < 0) {
            data.status = false;
            data.err = ErrorCode::Range;
            data.co2_data = 0;
            return data;
        }

        data.co2_data = raw_data;
        data.status = true;
        data.err = None;
        return data;
    }

private:
    ModbusRegister co2_req;

    int co2_max = 2000; // set in the project documentation


};

#endif GMP252_H
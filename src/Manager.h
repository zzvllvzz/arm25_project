//
// Created by Sampo Hulkko on 23.9.2025.
//
#include"modbus/ModbusRegister.h"
#include "modbus/ModbusClient.h"
#include "Gmp252.h"
#include "hmp60.h"
#include "PicoOsUart.h"
#ifndef RP2040_FREERTOS_IRQ_MANGER_H
#define RP2040_FREERTOS_IRQ_MANGER_H




struct all_data {
   float co2_data;
    float hmp60_rh;
    float hmp60_t; 
    bool status;
    uint32_t timestamp;

};




class Manager {
public:

    Manager()
    : uart   (std::make_shared<PicoOsUart>(/*UART_NR*/1, /*TX*/4, /*RX*/5, /*BAUD*/9600, /*STOP*/2)),
      client (std::make_shared<ModbusClient>(uart)),
      co2    (client, 240),   // Gmp252_co2(std::shared_ptr<ModbusClient>&, slave)
      hmp    (client, 241)    // Hmp60sensor(const std::shared_ptr<ModbusClient>&, slave)
    {}


      // prototype read funtion
      all_data read_data() {
        for (;;) {
            auto c  = co2.read_co2(); // {co2_data, status, ...}
            auto ht = hmp.read();     // {rh, t, ok}


            //updating the data
            all_data data;
            data.co2_data  = c.co2_data;
            data.hmp60_rh  = ht.rh;
            data.hmp60_t   = ht.t;
            data.status    = (c.status && ht.ok);
            data.timestamp = xTaskGetTickCount();
            
            return data;

        }
    }

    // we can add the fan controlrs also here i think



private:

    std::shared_ptr<PicoOsUart>   uart;
    std::shared_ptr<ModbusClient> client;
    Gmp252_co2 co2;   // needs a client in the constructor
    Hmp60sensor hmp;   //needs a client in the constructor

    // Snapshot + suojaus
    all_data last{};
    SemaphoreHandle_t mtx{};
};
#endif //RP2040_FREERTOS_IRQ_MANGER_H
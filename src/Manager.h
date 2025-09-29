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
#define valve_pin 27




struct all_data {
   float co2_data;
    float hmp60_rh;
    float hmp60_t;
    float user_set_level = 1450;
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
    {
        gpio_init(valve_pin);
        gpio_set_dir(valve_pin, GPIO_OUT); // valve init
        gpio_put(valve_pin, 0);
    }


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



    void valve_control(float fan_speed) {

    }

    // prototype needs work
     all_data valve_open() {


       // check the co2 levels
        all_data data = read_data();
        // we keep opening the valve until we are above the user set level
        while (data.user_set_level > data.co2_data) {
            gpio_put(valve_pin, 1);
            vTaskDelay(2000);
            data = read_data();
            gpio_put(valve_pin, 0);

        }
        return data;

    }

private:

    std::shared_ptr<PicoOsUart>   uart;
    std::shared_ptr<ModbusClient> client;
    Gmp252_co2 co2;   // needs a client in the constructor
    Hmp60sensor hmp;   //needs a client in the constructor


};
#endif //RP2040_FREERTOS_IRQ_MANGER_H
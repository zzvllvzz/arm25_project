//
// Created by Sampo Hulkko on 23.9.2025.
//
#include"modbus/ModbusRegister.h"
#include "modbus/ModbusClient.h"
#include "Gmp252.h"
#include "hmp60.h"
#include "PicoOsUart.h"
#include "Fan.h"
#include "hardware/gpio.h"
#ifndef RP2040_FREERTOS_IRQ_MANGER_H
#define RP2040_FREERTOS_IRQ_MANGER_H
#define valve_pin 27




struct all_data {
   float co2_data;
    float hmp60_rh;
    float hmp60_t;
    bool status;
    uint32_t timestamp;
    bool fan_running;
    int last_pulses;
    float fan_percent;

};






class Manager {
public:

    Manager()
    : uart   (std::make_shared<PicoOsUart>(/*UART_NR*/1, /*TX*/4, /*RX*/5, /*BAUD*/9600, /*STOP*/2)),
      client (std::make_shared<ModbusClient>(uart)),
      co2    (client, 240),   // Gmp252_co2(std::shared_ptr<ModbusClient>&, slave)
      hmp    (client, 241),// Hmp60sensor(const std::shared_ptr<ModbusClient>&, slave)
        fan (client, 0)
    {
        gpio_init(valve_pin);
        gpio_set_dir(valve_pin, GPIO_OUT); // valve init
        gpio_put(valve_pin, 0);
        fan_percent_cached = 0.0f;
    }


      // prototype read funtion
      all_data read_data() {
            auto c  = co2.read_co2(); // {co2_data, status, ...}
            auto ht = hmp.read();// {rh, t, ok}
            // auto f=fan.poll_running();
            //updating the data
            all_data data;
            data.co2_data  = c.co2_data;
            data.hmp60_rh  = ht.rh;
            data.hmp60_t   = ht.t;
            data.status    = (c.status && ht.ok) ;
            data.timestamp = xTaskGetTickCount();
            data.fan_percent = fan_percent_cached;
            data.fan_running = (fan_percent_cached > 0.0f);
            return data;

    }



    void fan_on(float fan_speed) {

        fan.set_percent(fan_speed);
        fan_percent_cached = fan_speed;
    }
    void fan_off() {
        fan.set_percent(0);
        fan_percent_cached = 0.0f;
    }
    void valve_close() {
        gpio_put(valve_pin, 0);

    }


    // prototype needs work
    void valve_open(  int open_ms = 1000, int set_ms = 2000){
            // open the valve and keep it open while we wait
            gpio_put(valve_pin, 1);
            vTaskDelay(pdMS_TO_TICKS(open_ms));
            //let the co2 levels even out
            //close the valve
            gpio_put(valve_pin, 0);
        vTaskDelay(pdMS_TO_TICKS(set_ms));// to let the levels setle


    }





private:

    std::shared_ptr<PicoOsUart>   uart;
    std::shared_ptr<ModbusClient> client;
    Gmp252_co2 co2;   // needs a client in the constructor
    Hmp60sensor hmp;//needs a client in the constructor
    Fan fan;
    float fan_percent_cached{0.0f};


};
#endif //RP2040_FREERTOS_IRQ_MANGER_H
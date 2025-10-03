//
// Created by Sampo Hulkko on 1.10.2025.
//
#include <cstdio>

#include "FreeRTOS.h"
#include "task.h"
#include "pico/stdlib.h"
#include "queue.h"
#include "hardware/gpio.h"

#include "hardware/timer.h"
extern "C" QueueHandle_t rotary_queue;

#ifndef RP2040_FREERTOS_IRQ_ROTARY_H
#define RP2040_FREERTOS_IRQ_ROTARY_H

namespace Pins {
    constexpr uint ROT_A = 10;   // encoder A
    constexpr uint ROT_B = 11;   // encoder B
    constexpr uint ROT_BUTTON = 12;  //  encoderin nappi
    constexpr uint LED = 22;
}


typedef enum {
    EVENT_BUTTON,
    EVENT_ENCODER_CW,
    EVENT_ENCODER_CCW
} event_type_t;

typedef struct {
    event_type_t type;
    uint32_t timestamp;

} event_t;

static void gpio_callback(uint gpio, uint32_t events) {
    if (!rotary_queue) return;

    BaseType_t hpw = pdFALSE;

    // A:n nouseva reuna -> lue B ja päättele suunta
    if (gpio == Pins::ROT_A && (events & GPIO_IRQ_EDGE_RISE)) {
        bool b = gpio_get(Pins::ROT_B);
        event_t ev;
        ev.type = b ? EVENT_ENCODER_CCW : EVENT_ENCODER_CW; // suunnan määritys
        ev.timestamp = xTaskGetTickCountFromISR();   // aikaleima ms
        xQueueSendFromISR(rotary_queue, &ev, &hpw);
    }

    // nappi
    if (gpio == Pins::ROT_BUTTON && (events & GPIO_IRQ_EDGE_FALL)) {
        event_t ev;
        ev.type = EVENT_BUTTON;
        ev.timestamp = xTaskGetTickCountFromISR(); // aikaleima ms
        xQueueSendFromISR(rotary_queue, &ev, &hpw);
    }

    portYIELD_FROM_ISR(hpw);
}

static void setup_rotary() {
    // --- Encoder A
    gpio_init(Pins::ROT_A);
    gpio_set_dir(Pins::ROT_A, GPIO_IN);
    gpio_set_irq_enabled_with_callback(Pins::ROT_A,GPIO_IRQ_EDGE_RISE,true,gpio_callback);

    // --- Encoder B
    gpio_init(Pins::ROT_B);
    gpio_set_dir(Pins::ROT_B, GPIO_IN);
    // Ei keskeytystä ; luetaan tila A keskeytyksessä

    // --- Nappi -
    gpio_init(Pins::ROT_BUTTON);
    gpio_set_dir(Pins::ROT_BUTTON, GPIO_IN);
    gpio_pull_up(Pins::ROT_BUTTON);

    gpio_set_irq_enabled(Pins::ROT_BUTTON,GPIO_IRQ_EDGE_FALL,true);
}


#endif //RP2040_FREERTOS_IRQ_ROTARY_H
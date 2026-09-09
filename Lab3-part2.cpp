//
// Created by Konstantin Kovalev on 8.9.2026.
//

#include <cstdio>
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"
#include "pico/stdio.h"
#include "hardware/gpio.h"
#include <stdatomic.h>

// this is needed for runtime statistics
#include <atomic>
#include <cstdlib>

#include "queue.h"
#include "hardware/iuart.h"
#include "hardware/structs/timer.h"
#include "pico/util/queue.h"

#define STR_LEN 32
#define DELAY 1
#define LED_DELAY 100
#define LED 20

#define ROT_A 10
#define ROT_B 11
#define ROT_SW 12

void *sh;

extern "C"
{
    uint32_t read_runtime_ctr(void)
    {
        return timer_hw->timerawl;
    }
}

// stack overflow check
extern "C"
{
    void vApplicationStackOverflowHook( TaskHandle_t xTask, char * pcTaskName )
    {
        if (pcTaskName != NULL) panic("Stack overflow: %s",pcTaskName);
        else panic("Stack overflow of unnamed task");
    }
}

/** Receiving and filtering gpio events from the queue **/
void gpio_events(void *param)
{

}

void blink_led(void *param)
{

}

void gpio_callback(uint gpio, uint32_t events)
{
    uint event=0;

    if (gpio==ROT_A)
    {
        event = !gpio_get(ROT_B) ? -1 : 1;
    }
    queue_try_add((queue_t*)events, &event);

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    // signal task that a button was pressed
    xSemaphoreGiveFromISR(sh, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

int main()
{
    stdio_init_all();

    printf("\nBoot\n");

    queue_t events;

    gpio_set_irq_enabled_with_callback (ROT_A, GPIO_IRQ_EDGE_RISE, true, gpio_callback); // set callback function
    queue_init (&events, sizeof(std::atomic_int), 128);


    for (int i = 0; i < 3; ++i)
    {
        gpio_init(ROT_A + i);
        gpio_set_dir(ROT_A + i, GPIO_IN);
    }
    gpio_pull_up(ROT_SW);

    sh = xSemaphoreCreateBinary();

    vQueueAddToRegistry(events, "rot_events");

    xTaskCreate(gpio_events, "producer", 512, (void *) &events, tskIDLE_PRIORITY + 1, nullptr);
    xTaskCreate(blink_led, "consumer", 512, (void *) &sh, tskIDLE_PRIORITY + 1, nullptr);

    vTaskStartScheduler();

    while(true){};

    return 0;
}
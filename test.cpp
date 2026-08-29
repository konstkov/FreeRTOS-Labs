//
// Created by Konstantin Kovalev on 29.8.2026.
//

#include <cstdio>
#include "FreeRTOS.h"
#include "task.h"
#include "pico/stdio.h"
#include "hardware/gpio.h"

// this is needed for runtime statistics
#include <cstdlib>

#include "queue.h"
#include "hardware/timer.h"
#include "pico/time.h"


#define BUTTON_TIMEOUT 5000
#define BUF_SIZE 128
#define LED 20
#define SW 7

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

typedef struct
{
    QueueHandle_t xQueue;
    int *pvItemToQueue;
    int pvBuffer[BUF_SIZE];
    TickType_t xTicksToWait;
} data;

void producer_task(void *param)
{
    gpio_init(SW);
    gpio_set_dir(SW, GPIO_IN);
    gpio_pull_up(SW);

    const auto d = (data*) param;

    while (true)
    {
        if (!gpio_get(SW)) *d->pvItemToQueue = 1;
        //*d->pvItemToQueue = 1;
        xQueueSendToFront(d->xQueue, d->pvItemToQueue, d->xTicksToWait);
        vTaskDelay(pdMS_TO_TICKS(1000));
        *d->pvItemToQueue = 0;
        xQueueSendToFront(d->xQueue, d->pvItemToQueue, d->xTicksToWait);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void consumer_task(void *param)
{
    gpio_init(LED);
    gpio_set_dir(LED, GPIO_OUT);
    const auto d = (data*) param;

    while (true)
    {   /*If a button press is received the corresponding LED is lit for 200ms and
         *the press is processed as shown in the state diagram*/
        xQueueReceive(d->xQueue, d->pvBuffer, d->xTicksToWait);
        gpio_put(LED, *d->pvBuffer);
        //vTaskDelay(pdMS_TO_TICKS(100));
    }
}

int main()
{
    data d;

    d.xQueue = xQueueCreate(BUF_SIZE, sizeof(int));
    d.xTicksToWait = pdMS_TO_TICKS(1000);

    xTaskCreate(producer_task, "producer", 512, (void *) &d, tskIDLE_PRIORITY + 1, nullptr);
    xTaskCreate(consumer_task, "consumer", 512, (void *) &d, tskIDLE_PRIORITY + 1, nullptr);

    vTaskStartScheduler();

    while(true){};

    return 0;
}

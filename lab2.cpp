//
// Created by Konstantin Kovalev on 27.8.2026.
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
#define BUF_SIZE 32

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

/*struct led_params
{
    uint pin;
    TickType_t delay;
    uint button;
    absolute_time_t last_press;
    bool last_pressed;
};*/

struct queue_data
{
    QueueHandle_t xQueue;
    int *pvItemToQueue;
    int pvBuffer[BUF_SIZE];
    uint led;
    uint button;
    absolute_time_t last_press;
    bool last_pressed;
    TickType_t xTicksToWait;
};

bool wasPressed(queue_data *qd)
{
    bool pressed = !gpio_get(qd->button); // returns true if pressed (has pullup)
       if (pressed && !qd->last_pressed && time_reached(qd->last_press)) // adjust timeout according to needs
    {
        qd->last_press = get_absolute_time();
        qd->last_pressed = pressed;
        return true;
    }
    qd->last_pressed = pressed;
    return false;
}

void button_task(void *param)
{
    const auto qd = (queue_data*) param;

    const uint led_pin = qd->led;
    const uint button = qd->button;

    gpio_init(led_pin);
    gpio_set_dir(led_pin, GPIO_OUT);
    gpio_init(button);
    gpio_set_dir(button, GPIO_IN);
    gpio_pull_up(button);

    //if (wasPressed(qd))
    //if (!gpio_get(button))
    {
        *qd->pvItemToQueue = 1;
        xQueueSendToFront(qd->xQueue, qd->pvItemToQueue, qd->xTicksToWait);
    }
}

void process_task(void *param)
{
    const auto qd = (queue_data*) param;

    /* The size of each data item that the queue holds is set when the queue is created. The memory pointed
to by pvBuffer must be at least large enough to hold that many bytes. */
    xQueueReceive(qd->xQueue, qd->pvBuffer, qd->xTicksToWait);
    for (const auto x : qd->pvBuffer)
    {
        if (x == 1)
        {
            gpio_put(qd->led, true);
            vTaskDelay(pdMS_TO_TICKS(200));
            gpio_put(qd->led, false);
        }
    }
}

/*BaseType_t xQueueSendToFront( QueueHandle_t xQueue,
const void * pvItemToQueue,
TickType_t xTicksToWait );*/

int main()
{
    stdio_init_all();

    queue_data qd[3];

    printf("\nBoot\n");

    // all three instances of struct share the same queue handle ( there should be only one queue)

    qd[0].xQueue = xQueueCreate(BUF_SIZE, sizeof(int));
    for (int i = 1; i < 3; ++i)
    {
        qd[i].xQueue = qd[0].xQueue;
    }

    for (int i = 0; i < 3; ++i)
    {
        qd[i].xTicksToWait = pdMS_TO_TICKS(5000);
        qd[i].button = 9 - i; // GPIO pins 7-9 for all the buttons
        qd[i].led = 20 + i; // GPIO pins 20-22 for all the LEDs
    }

    xTaskCreate(button_task, "SW0", 512, (void *) &qd[0], tskIDLE_PRIORITY + 1, nullptr);
    xTaskCreate(button_task, "SW1", 512, (void *) &qd[1], tskIDLE_PRIORITY + 1, nullptr);
    xTaskCreate(button_task, "SW2", 512, (void *) &qd[2], tskIDLE_PRIORITY + 1, nullptr);

    xTaskCreate(process_task, "Processing", 256, (void *) &qd[0], tskIDLE_PRIORITY + 1, nullptr);
    vTaskStartScheduler();

    while(true){};
}

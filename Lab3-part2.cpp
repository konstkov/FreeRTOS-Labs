//
// Created by Konstantin Kovalev on 8.9.2026.
//

#include <cstdio>
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"
#include "pico/stdio.h"
#include "hardware/gpio.h"

// this is needed for runtime statistics
#include <atomic>
#include <cstdlib>

#include "queue.h"
#include "hardware/iuart.h"
#include "hardware/pwm.h"

#include "hardware/structs/timer.h"
#include "pico/util/queue.h"
//buf size and delays
#define BUF_SIZE 32
#define DELAY 1
//GPIO pins
#define LED 20
#define ROT_A 10
#define ROT_B 11
#define ROT_SW 12
//Minimum frequency is 2 Hz and maximum frequency is 200 Hz.
#define MAX_DELAY 250
#define MIN_DELAY 2 // not accurate
#define STEP 1


SemaphoreHandle_t qh;

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

typedef struct rot
{
    absolute_time_t last_press;
    bool last_pressed;
    uint button;
    int delay;
    bool on_state;
} rot;

bool wasPressed(rot *sw)
{
    //const bool pressed = true; // returns true if pressed (has pullup)
    //if (pressed && !sw->last_pressed && time_reached(sw->last_press)) // adjust timeout according to needs
    if (time_reached(sw->last_press)) // adjust timeout according to needs
    {
        sw->last_press = get_absolute_time();
        //sw->last_pressed = pressed;
        return true;
    }
    //sw->last_pressed = pressed;
    return false;
}

/** Receiving and filtering gpio events from the queue **/

/** Rotary encoder is used to control blinking frequency of the LED. Turning the knob clockwise
increases frequency and turning counterclockwise reduces frequency. If the LED is in OFF state
turning the knob has no effect. Minimum frequency is 2 Hz and maximum frequency is 200 Hz. **/

void gpio_events(void *param)
{
    auto r = (rot*) param;

    int buf[BUF_SIZE];
    r->on_state = false;
    r->last_press = nil_time;
    r->last_pressed = (*buf == ROT_SW); // 0 (false) if no events in the queue
    int temp = 0;

    while (true)
    {
        if (xQueueReceive(qh, buf, 0) == pdPASS)
        {
            printf("Received from the queue!\n");
            if (*buf==ROT_SW) //means the button was pressed
            {
                printf("Buf equals rot_sw!\n");
                //if (wasPressed(r))
                {
                    printf("Button pressed!\n");
                    while (!gpio_get(ROT_SW)); // wait until the release
                    if (r->on_state==false)
                    {
                        r->delay=temp;
                        r->on_state=true;
                        printf("The state is ON!\n");
                    }
                    else   // on_state==true and SW_1 pressed
                    {
                        printf("The state is OFF!\n");
                        r->on_state=false;
                        r->delay=MIN_DELAY;
                    }
                }
            }
            if (r->on_state==true)
            {
                if (*buf == 1)
                { // when user rotates knob clockwise brightness smoothly increases
                    if (r->delay < MAX_DELAY)
                    {
                        r->delay += STEP;
                        printf("Delay: %d ms\n", r->delay);
                    }
                }
                if (*buf == -1)
                { // when user rotates knob counter-clockwise brightness smoothly decreases
                    if (r->delay>MIN_DELAY)
                    {
                        r->delay-=STEP;
                        printf("Delay: %d\n", r->delay);
                    }
                    if (r->delay<MIN_DELAY) //since the step size might be bigger than 1, it might go to negative value and the wrap up going to plus very big number
                    {
                        r->delay=MIN_DELAY;
                    }
                }
                temp = r->delay;
            }
        }
    }
}

void blink_led(void *param)
{
    // typecast rot struct to an appropriate return type
    auto r = (rot*) param;

    TickType_t xLastWakeTime = xTaskGetTickCount();

    while (true)
    {
        gpio_put(LED, true);
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(r->delay));
        gpio_put(LED, false);
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(r->delay));
    }
}

void gpio_callback(uint gpio, uint32_t events)
{
    int event = 0;

    if (gpio == ROT_A)
    {
        event = !gpio_get(ROT_B) ? -1 : 1; // -1: counterclockwise, 1: clockwise
    }
    if (gpio == ROT_SW) event = ROT_SW;

    xQueueSendToFrontFromISR(qh, &event, NULL); // change higher priority task woken if needed

    //queue_try_add((queue_t*)events, &event);

    //BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    // signal task that a button was pressed
    //xSemaphoreGiveFromISR(sh, &xHigherPriorityTaskWoken);
    //portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

int main()
{
    stdio_init_all();

    printf("\nBoot\n");

    //queue_t events;
    qh = xQueueCreate(10, sizeof(int));;

    //rot data
    rot r;

    vQueueAddToRegistry(qh, "rot_events");

    // setting callback function to each GPIO pin
    gpio_set_irq_enabled_with_callback (ROT_SW, GPIO_IRQ_EDGE_RISE, true, gpio_callback);
    gpio_set_irq_enabled_with_callback (ROT_A, GPIO_IRQ_EDGE_RISE, true, gpio_callback); // set callback function
    //queue_init ((queue_t*)events, sizeof(int), 10);

    for (int i = 0; i < 3; ++i)
    {
        gpio_init(ROT_A + i);
        gpio_set_dir(ROT_A + i, GPIO_IN);
    }
    gpio_pull_up(ROT_SW);

    gpio_init(LED);
    gpio_set_dir(LED, GPIO_OUT);

    xTaskCreate(gpio_events, "producer", 512, (void *) &r, tskIDLE_PRIORITY + 1, nullptr);
    xTaskCreate(blink_led, "consumer", 512, (void *) &r, tskIDLE_PRIORITY + 1, nullptr);

    vTaskStartScheduler();

    while(true){};

    return 0;
}
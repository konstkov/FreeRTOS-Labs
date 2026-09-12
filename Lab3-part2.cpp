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
#include <algorithm>
#include <atomic>
#include <cstdlib>

#include "queue.h"

#include "hardware/structs/timer.h"
#include "pico/util/queue.h"
#include <stdatomic.h>

//buf size and delays
#define BUF_SIZE 32
#define DELAY 1
#define BUTTON_TIMEOUT 250000
//GPIO pins
#define LED 20
#define ROT_A 10
#define ROT_B 11
#define ROT_SW 12
//Minimum frequency is 2 Hz and maximum frequency is 200 Hz.
#define MAX_DELAY 250
#define SEC_TO_MS 1000
#define MIN_DELAY 2 // not accurate
#define STEP 2

SemaphoreHandle_t qh; // global variable

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
    absolute_time_t stamp{};
    uint button{};
    int count{};
    std::atomic_int delay;
    bool on_state{};
    int temp{};
} rot;

bool rotPressed(rot *sw, const int *buf)
{
    if (*buf == ROT_SW)
    {
        if (sw->count==0)
        {
            sw->stamp=get_absolute_time();
            ++sw->count;
        } // button presses that are closer than 250 ms are ignored
        if (absolute_time_diff_us (sw->stamp, get_absolute_time())>=BUTTON_TIMEOUT)
        {
            sw->count = 0; // reset the count
            return true;
        }
    }
    return false;
}

void toggle_led_state(rot *sw)
{
    printf("Button pressed!\n");
    while (!gpio_get(ROT_SW)); // wait until the release
    if (sw->on_state==false)
    {
        sw->delay=sw->temp;
        sw->on_state=true;
        printf("The state is ON!\n");
    }
    else   // on_state==true and SW_1 pressed
    {
        printf("The state is OFF!\n");
        sw->on_state=false;
        sw->delay=MIN_DELAY;
    }
}

int count_freq(const std::atomic_int &delay)
{
    const auto period = delay * 2; // because there are two delays in one period (one when led is on, one when it is off)
    const auto frequency = (1 * SEC_TO_MS) / period;
    return frequency;
}

void adjust_freq(rot *r, const int *buf)
{
    if (*buf == 1)
    { // when user rotates knob clockwise blinking frequency smoothly increases
        if (r->delay < MAX_DELAY)
        {
            r->delay += STEP;
            printf("Frequency: %d Hz\n", count_freq(r->delay));
        }
    }
    if (*buf == -1)
    { // when user rotates knob counter-clockwise brightness smoothly decreases
        if (r->delay>MIN_DELAY)
        {
            r->delay-=STEP;
            printf("Frequency: %d hz\n", count_freq(r->delay));
        }
        if (r->delay<MIN_DELAY) //since the step size might be bigger than 1, it might go to negative value and the wrap up going to plus very big number
        {
            r->delay=MIN_DELAY;
        }
    }
    r->temp = r->delay;
}

/** Receiving and filtering gpio events from the queue **/

/** Rotary encoder is used to control blinking frequency of the LED. Turning the knob clockwise
increases frequency and turning counterclockwise reduces frequency. If the LED is in OFF state
turning the knob has no effect. Minimum frequency is 2 Hz and maximum frequency is 200 Hz. **/

void gpio_events(void *param)
{
    // typecast rot struct to an appropriate return type
    auto r = (rot*) param;

    // init rotary encoder-related variables
    int buf[BUF_SIZE];
    r->button = ROT_SW;
    r->on_state = false;
    r->temp = 0;
    r->count = 0;
    r->delay = 250;

    while (true) // main loop
    {
        if (xQueueReceive(qh, buf, 0) == pdPASS) // if receiving from the queue was successful
        {
            if (rotPressed(r, buf)) // if ROT_SW button pressed toggle led state
            {
                toggle_led_state(r);
            }
            if (r->on_state==true) // if LED is on, detect encoder turns and adjust delay and update/print frequency
            {
                adjust_freq(r, buf);
            }
        }
    }
}

void blink_led(void *param)
{
    // typecast rot struct to an appropriate return type
    auto r = (rot*) param;

    while (true) // main loop
    {
        gpio_put(LED, r->on_state); // if state is ON, LED will blink, otherwise it will be off
        vTaskDelay(pdMS_TO_TICKS(r->delay));
        gpio_put(LED, false);
        vTaskDelay(pdMS_TO_TICKS(r->delay));
        // One period (T)
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
}
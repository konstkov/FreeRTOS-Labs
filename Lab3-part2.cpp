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
#define LED_DELAY 100
//GPIO pins
#define LED 20
#define ROT_A 10
#define ROT_B 11
#define ROT_SW 12
//PWM-related
#define WRAP_VALUE 999
#define CC_HIGH 1000
#define CC_LOW 0
//
#define STEP 32


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
    int duty;
} rot;

bool wasPressed(rot *sw)
{
    const bool pressed = !gpio_get(sw->button); // returns true if pressed (has pullup)
    if (pressed && !sw->last_pressed && time_reached(sw->last_press)) // adjust timeout according to needs
    {
        sw->last_press = get_absolute_time();
        sw->last_pressed = pressed;
        return true;
    }
    sw->last_pressed = pressed;
    return false;
}

/** Receiving and filtering gpio events from the queue **/

/** Rotary encoder is used to control blinking frequency of the LED. Turning the knob clockwise
increases frequency and turning counterclockwise reduces frequency. If the LED is in OFF state
turning the knob has no effect. Minimum frequency is 2 Hz and maximum frequency is 200 Hz. **/

void gpio_events(void *param)
{
    int buf[BUF_SIZE];
    bool on_state = false;
    int temp = 0;

    auto r = (rot*) param;

    while (true)
    {
        if (xQueueReceive(qh, buf, 0) ==pdPASS)
        {
            printf("Received from the queue!\n");
            if (*buf==ROT_SW) //means the button was pressed
            {
                //if (wasPressed(r))
                {
                    printf("Button pressed!\n");
                    while (!gpio_get(ROT_SW)); // wait until the release
                    if (on_state==false)
                    {
                        r->duty=temp;
                        on_state=true;
                    }
                    else   // on_state==true and SW_1 pressed
                    {
                        on_state=false;
                        r->duty=CC_LOW;
                    }
                }
            }
            if (on_state==true)
            {
                if (*buf == 1)
                { // when user rotates knob clockwise brightness smoothly increases
                    if (r->duty<CC_HIGH)
                    {
                        r->duty+=STEP;
                        printf("Freq increased!\n");
                    }
                }
                if (*buf == -1)
                { // when user rotates knob counter-clockwise brightness smoothly decreases
                    if (r->duty>CC_LOW)
                    {
                        r->duty-=STEP;
                        printf("Freq decreased!\n");
                    }
                    if (r->duty<CC_LOW) //since the step size might be bigger than 1, it might go to negative value and the wrap up going to plus very big number
                    {
                        r->duty=CC_LOW;
                    }
                }
                temp = r->duty;
            }
        }
    }

}

void blink_led(void *param)
{
    // typecast rot struct to an appropriate return type
    auto r = (rot*) param;

    //set the function of LEDS to PWM
    gpio_set_function(LED, GPIO_FUNC_PWM);

    //assign slices to LED pins
    uint slice_num = pwm_gpio_to_slice_num(LED);

    //assign the wrap(top) value after which the value wraps to zero
    pwm_set_wrap(slice_num, WRAP_VALUE);

    //assign channels to LED pins
    uint channel_num = pwm_gpio_to_channel(LED);

    //call pwm_set_enabled to start PWM
    pwm_set_enabled(slice_num, true);

    while (true)
    {
        pwm_set_chan_level (slice_num, channel_num, r->duty); //set the level of the channel
        vTaskDelay(pdMS_TO_TICKS(LED_DELAY)); //small delay to adjust the smoothness
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

    xTaskCreate(gpio_events, "producer", 512, (void *) &r, tskIDLE_PRIORITY + 1, nullptr);
    xTaskCreate(blink_led, "consumer", 512, (void *) &r, tskIDLE_PRIORITY + 1, nullptr);

    vTaskStartScheduler();

    while(true){};

    return 0;
}
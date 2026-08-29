#include <cstdio>
#include "FreeRTOS.h"
#include "task.h"
#include "pico/stdio.h"
#include "hardware/gpio.h"

// this is needed for runtime statistics
#include <vector>

#include "hardware/timer.h"
#include "pico/time.h"


#define BUTTON_TIMEOUT 5000

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

struct led_params
{
    uint pin;
    TickType_t delay;
    uint button;
    absolute_time_t last_press;
    bool last_pressed;
};

bool wasPressed(led_params *lpr)
{
    bool pressed = !gpio_get(lpr->button); // returns true if pressed (has pullup)
       if (pressed && !lpr->last_pressed && time_reached(lpr->last_press)) // adjust timeout according to needs
    {
        lpr->last_press = get_absolute_time();
        lpr->last_pressed = pressed;
        return true;
    }
    lpr->last_pressed = pressed;
    return false;
}

void blink_task(void *param)
{
    auto lpr = (led_params *) param;
    const uint led_pin = lpr->pin;
    const uint button = lpr->button;
    gpio_init(led_pin);
    gpio_set_dir(led_pin, GPIO_OUT);

    gpio_init(button);
    gpio_set_dir(button, GPIO_IN);
    gpio_pull_up(button);

    TickType_t xLastWakeTime = xTaskGetTickCount();

    while (true)
    {
        //if (!gpio_get(button))
        if (wasPressed(lpr))
        {
            lpr->delay = (lpr->delay + 100) % 500;
        }
        if (lpr->delay == 0)
        {
            gpio_put(led_pin, false);
            vTaskDelay(50);
        }
        else
        {
            gpio_put(led_pin,  true);
            vTaskDelayUntil(&xLastWakeTime, lpr->delay);
            gpio_put(led_pin, false);
            vTaskDelayUntil(&xLastWakeTime, lpr->delay);
        }
    }
}

int main()
{
    static led_params lp1 = { .pin = 20, .delay = 100, .button = 9, .last_press = nil_time, .last_pressed = false };
    static led_params lp2 = { .pin = 21, .delay = 300, .button = 8, .last_press = nil_time, .last_pressed = false };
    static led_params lp3 = { .pin = 22, .delay = 500, .button = 7, .last_press = nil_time, .last_pressed = false };

    stdio_init_all();

    printf("\nBoot\n");

    xTaskCreate(blink_task, "LED_1", 256, (void *) &lp1, tskIDLE_PRIORITY + 1, nullptr);
    xTaskCreate(blink_task, "LED_2", 256, (void *) &lp2, tskIDLE_PRIORITY + 1, nullptr);
    xTaskCreate(blink_task, "LED_3", 256, (void *) &lp3, tskIDLE_PRIORITY + 1, nullptr);
    vTaskStartScheduler();

    while(true){};
}

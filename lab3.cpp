//
// Created by Konstantin Kovalev on 4.9.2026.
//

#include <cstdio>
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"
#include "pico/stdio.h"
#include "hardware/gpio.h"

// this is needed for runtime statistics
#include <cstdlib>

#include "queue.h"
#include "hardware/iuart.h"
#include "hardware/structs/timer.h"

#define STR_LEN 32
#define DELAY 100
#define LED_DELAY 100
#define LED 20

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

/** Task reads characters from debug serial port using getchar_timeout_us and echoes them back to the serial
port. **/
void read_char(void *param)
{
    const auto sh = (QueueHandle_t *) param;
    char c;

    fflush(stdout);

    do
    {
        c = getchar_timeout_us(0); // read one char with 0 timeout
        if (c != 0) // if some valid char was received
        {
            printf("Received char:%c\n", c);
            printf("ASCII code of received char:%d\n", c);
            // if( xSemaphoreGive( sh ) != pdTRUE ) // send an indication (= give the binary semaphore) to blinker task
            // {
            //             // We would expect this call to fail because we cannot give
            //             // a semaphore without first "taking" it!
            // }
        }
        else // Use vTaskDelay to release CPU time to other tasks when no characters are received
        {
            vTaskDelay(DELAY);
        }
    } while (c != '\0'); // if a character was received just loop back


}
/** This task blinks the led once (100 ms on, 100 ms off) when it receives activity indication (= takes the binary
semaphore). **/
void blink_led(void *param)
{
    gpio_init(LED);
    gpio_set_dir(LED, GPIO_OUT);

    const auto sh = (QueueHandle_t) param;

    while (true)
    {
        //if (xSemaphoreTake(sh, pdMS_TO_TICKS(0)) == pdPASS)
        {
            gpio_put(LED,  true);
            vTaskDelay(pdMS_TO_TICKS(LED_DELAY));
            gpio_put(LED, false);
        }
    }
}

/** Write a program that creates two tasks: one for reading characters from the serial port and the other for
indicating received characters on the serial port. **/
int main()
{
    stdio_init_all();

    printf("\nBoot\n");

    auto sh = xSemaphoreCreateBinary();

    xTaskCreate(blink_led, "consumer", 512, (void *) &sh, tskIDLE_PRIORITY + 1, nullptr);
    xTaskCreate(read_char, "consumer", 512, (void *) &sh, tskIDLE_PRIORITY + 1, nullptr);

    vTaskStartScheduler();

    while(true){};

    return 0;
}
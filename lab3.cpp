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
#define DELAY 1
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
    auto sh = *(SemaphoreHandle_t*) param;
    
    while (true) // loop indefinitely while there is data waiting in RX queue
    {
        int c = getchar_timeout_us(0); // read one char with 0 timeout

        if (sh !=NULL)
        {
            if (c == PICO_ERROR_TIMEOUT) // if no valid char was received
            {
                // Use vTaskDelay to release CPU time to other tasks when no characters are received
                vTaskDelay(pdMS_TO_TICKS(DELAY));
                xSemaphoreTake(sh, pdMS_TO_TICKS(0)); // take the semaphore so
                //that the other process cannot obtain it
            }
            else
            {
                xSemaphoreGive(sh); // send an indication (= give the binary semaphore) to blinker task
            }
        }
    }
}

/** This task blinks the led once (100 ms on, 100 ms off) when it receives activity indication (= takes the binary
semaphore). **/
void blink_led(void *param)
{
    gpio_init(LED);
    gpio_set_dir(LED, GPIO_OUT);

    auto sh = *(SemaphoreHandle_t*) param;

    while (true)
    {
        if (sh != NULL) // see if we can obtain the semaphore
        {
            if (xSemaphoreTake(sh, pdMS_TO_TICKS(0)) == pdPASS) // if semaphore is available blink led
            {
                gpio_put(LED,  true);
                vTaskDelay(pdMS_TO_TICKS(LED_DELAY));
                gpio_put(LED, false);

                if  (xSemaphoreGive(sh) == pdPASS) // we have finished using shared resource, release the semaphore
                {
                    vTaskDelay(pdMS_TO_TICKS(DELAY)); // delay a bit to allow read_char task to run
                }
            }
        }
        else // We could not obtain the semaphore and can therefore not access the shared resource safely.
        {
            perror("Could not access the semaphore.\n");
        }
    }
}

/** Write a program that creates two tasks: one for reading characters from the serial port and the other for
indicating received characters on the serial port. **/
int main()
{
    stdio_init_all();

    printf("\nBoot\n");

    SemaphoreHandle_t sh = xSemaphoreCreateBinary();

    xTaskCreate(read_char, "producer", 512, (void *) &sh, tskIDLE_PRIORITY + 1, nullptr);
    xTaskCreate(blink_led, "consumer", 512, (void *) &sh, tskIDLE_PRIORITY + 1, nullptr);

    vTaskStartScheduler();

    while(true){};

    return 0;
}
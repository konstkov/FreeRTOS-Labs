#include "PicoOsUart.h"
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
#include <cstring>
#include <limits.h>

#include "timers.h"
#include "queue.h"

#include "hardware/structs/timer.h"
#include "pico/util/queue.h"
#include <stdatomic.h>

#define S_TO_MS 1000
#define INACTIVITY_TIMER_PERIOD pdMS_TO_TICKS( 500 )
#define LED_TIMER_PERIOD pdMS_TO_TICKS( 5 * S_TO_MS )

bool flush_flag = false;

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
/** If no characters are received in 30 seconds all the characters received so far
 * are discarded and the program prints “[Inactive]” **/
static void prvInactivityTimerCallback(TimerHandle_t xTimer )
{
    flush_flag = true;
    printf("[Inactive]\n");
}

static void prvLedTimerCallback(TimerHandle_t xTimer )
{
    gpio_put(LED, true);
}

void command_interpreter(const char *buf, TimerHandle_t xTimer)
{
    if (strncmp(buf, "help", 4) == 0)
    {
        printf("Display usage instructions\n");
    }
    else if (strncmp(buf, "interval", 8) == 0)
    { // Set the LED toggle interval (default is 5 seconds)
        auto interval = strtol (buf, NULL, 10);
        if (interval != 0 && interval != LONG_MIN && interval != LONG_MAX) // if command has a valid integer value
        {
            xTimerChangePeriod(xTimer, pdMS_TO_TICKS(interval * S_TO_MS), 0);
        }
        else
        {
            printf("Invalid command.\n");
        }
    }
    else if (strncmp(buf, "time", 4) == 0)
    { // prints the number of seconds with 0.1s accuracy since the last led toggle
        auto last_toggle = xTimerGetPeriod(xTimer) - xTimerGetExpiryTime(xTimer); // prints the number of seconds with 0.1s accuracy since the last led toggle
        printf("Last LED toggle: %lu ticks\n", last_toggle);
    }
    else
    {
        printf("Invalid command.\n");
    }
}

void serial_task(void *param)
{
    auto xInactivityTimer = *(TimerHandle_t*) param;
    PicoOsUart u(0, 0, 1, 115200);
    uint8_t buffer[64];
    while (true)
    {
        if (int count = u.read(buffer, 64, 30); count > 0)
        {
            u.write(buffer, count);
            /* When a character is received the inactivity timer is started/reset. */
            if( xTimerStart( xInactivityTimer, 0 ) != pdPASS )
            {
                printf("The timer could not be set into the Active state.\n");
            } // When enter is pressed the received character are processed in a command interpreter
            if (buffer[count] == '\r' || buffer[count] == '\n')
            {
                command_interpreter((const char*)buffer, xInactivityTimer);
            }

        } // If no characters are received in 30 seconds all the characters received so far are discarded
        if (flush_flag)
        {
            u.flush();
            flush_flag = false;
        }
    }
}

/** Implement a program that reads commands from the serial port using the provided interrupt driven
FreeRTOS uart driver. **/
int main()
{
    /* The program creates two timers */
    TimerHandle_t xTimers[2];

    /* One for inactivity monitoring */
    xTimers[0] = xTimerCreate(
    "Inactivity Monitoring", INACTIVITY_TIMER_PERIOD, pdTRUE ,
    nullptr, prvInactivityTimerCallback);

    /* One for toggling the green LED */
    xTimers[1] = xTimerCreate(
    "Led Toggle", LED_TIMER_PERIOD, pdTRUE ,
    nullptr, prvLedTimerCallback);

    for (auto & x: xTimers)
    {
        if (x  == nullptr )
        {
            printf("The timer was not created.\n");
            return 1;
        }
        // else
        // {
        //     /* Start the timer. No block time is specified, and
        //        even if one was it would be ignored because the RTOS
        //        scheduler has not yet been started. */
        //     if( xTimerStart( x, 0 ) != pdPASS )
        //     {
        //         printf("The timer could not be set into the Active state.\n");
        //     }
        // }
    }

    xTaskCreate(serial_task, "producer", 512, xTimers[0], tskIDLE_PRIORITY + 1, nullptr);
    //xTaskCreate(serial_task, "producer", 512, nullptr, tskIDLE_PRIORITY + 1, nullptr);

    vTaskStartScheduler();

    while(true){};
}

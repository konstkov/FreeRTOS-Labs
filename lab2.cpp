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
#define BUF_SIZE 5
#define LED1 20
#define SW1 7
#define SW2 8
#define SW3 9

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

typedef struct data
{
    QueueHandle_t xQueue;
    int *pvItemToQueue;
    int pvBuffer[BUF_SIZE];
    TickType_t xTicksToWait;
    absolute_time_t last_press;
    bool last_pressed;
    uint button;
    bool flag;
} data;

/** The sequence to open the lock is 0-2-1-0-2 **/
enum class button_states
{
    START,
    W1,
    W2,
    W3,
    W4,
    OPEN
};

bool wasPressed(data *d)
{
    const bool pressed = !gpio_get(d->button); // returns true if pressed (has pullup)
    if (pressed && !d->last_pressed && time_reached(d->last_press)) // adjust timeout according to needs
    {
        d->last_press = get_absolute_time();
        d->last_pressed = pressed;
        return true;
    }
    d->last_pressed = pressed;
    return false;
}

void producer_task(void *param)
{
    auto d = (data*) param;

    /* init */
    gpio_init(d->button);
    gpio_set_dir(d->button, GPIO_IN);
    gpio_pull_up(d->button);
    d->last_press = nil_time;
    d->last_pressed = !gpio_get(d->button);

    while (true)
    {
        //*d->pvItemToQueue = 0; // default value
        if (wasPressed(d))
        {
            printf("Button pressed!\n");
            auto val = (int)d->button - SW1;
            xQueueSendToFront(d->xQueue, &val, d->xTicksToWait);
        }
    }
}

/** The sequence to open the lock is 0-2-1-0-2 **/

void sm (button_states &bs, data *d)
{
    printf("The value in the queue:%d\n", *d->pvBuffer);

    switch (bs)
    {
        case button_states::START:
        {
            for (int i = 0; i < 3; ++i)
            {
                gpio_put(LED1 + i, false);
            }
            printf("Currently in the state START!\n");
            if (*d->pvBuffer == 0)
            {
                bs = button_states::W1;
                printf("Transferred to state W1!\n");
            }
            else
            {
                break;
            }
        }
        case button_states::W1:
        {
            printf("Currently in the state W1!\n");

            if (*d->pvBuffer == 2  && d->flag)
            {
                bs = button_states::W2;
                printf("Transferred to state W2\n");
                d->flag = false;
            }
            else if (*d->pvBuffer != 2  && d->flag)
            {
                bs = button_states::START;
                printf("Sequence interrupted. Transferred to state START\n");
                d->flag = false;
                break;
            }
            else
            {
                d->flag = true;
                break;
            }
        }
        case button_states::W2:
        {
            printf("Currently in the state W2!\n");
            if (*d->pvBuffer == 1  && d->flag)
            {
                bs = button_states::W3;
                printf("Transferred to state W3\n");
                d->flag = false;
            }
            else if (*d->pvBuffer != 1  && d->flag)
            {
                bs = button_states::START;
                printf("Sequence interrupted. Transferred to state START\n");
                d->flag = false;
                break;
            }
            else
            {
                d->flag = true;
                break;
            }
        }
        case button_states::W3:
        {
            printf("Currently in the state W3!\n");
            if (*d->pvBuffer == 0  && d->flag)
            {
                bs = button_states::W4;
                printf("Transferred to state W4\n");
                d->flag = false;
            }
            else if (*d->pvBuffer != 0  && d->flag)
            {
                bs = button_states::START;
                printf("Sequence interrupted. Transferred to state START\n");
                d->flag = false;
                break;
            }
            else
            {
                d->flag = true;
                break;
            }
        }
        case button_states::W4:
        {
            printf("Currently in the state W4!\n");
            if (*d->pvBuffer == 2  && d->flag)
            {
                bs = button_states::OPEN;
                printf("Transferred to state OPEN\n");
                d->flag = false;
            }
            else if (*d->pvBuffer != 2  && d->flag)
            {
                bs = button_states::START;
                printf("Sequence interrupted. Transferred to state START\n");
                d->flag = false;
                break;
            }
            else
            {
                d->flag = true;
                break;
            }
        }
        case button_states::OPEN:
        {
            printf("Currently in the state OPEN!\n");
            printf("The lock is open!\n");
            // The part below needs to be fixed (or maybe not)
            for (int i = 0; i < 3; ++i)
            {
                gpio_put(LED1 + i, true);
            }
        }
    }
}

void consumer_task(void *param)
{
    const auto d = (data*) param;

    d->flag = false; // default value

    for (int i = 0; i < 3; ++i)
    {
        gpio_init(LED1 + i);
        gpio_set_dir(LED1 + i, GPIO_OUT);
    }

    auto bs = button_states::START;

    while (true)
    {   /*If a button press is received the corresponding LED is lit for 200ms and
         *the press is processed as shown in the state diagram*/
        const auto rv = xQueueReceive(d->xQueue, d->pvBuffer, d->xTicksToWait);
        const uint led = LED1 + *d->pvBuffer;
        if (rv == pdPASS)
        {
            gpio_put(led, true);
            vTaskDelay(pdMS_TO_TICKS(200));
            gpio_put(led, false);
        }

        if (rv == errQUEUE_EMPTY || bs == button_states::OPEN && *d->pvBuffer <= 2 && *d->pvBuffer >= 0)
        {
            *d->pvBuffer = -1;
            bs = button_states::START;
        }

        // call state machine
        sm(bs, d);
    }
}

int main()
{
    stdio_init_all();

    printf("\nBoot\n");

    data d[3];

    for (int i = 0; i < 3; ++i)
    {
        d[i].button = SW1 + i;
    }

    auto qh = xQueueCreate(BUF_SIZE, sizeof(int));

    for (auto &x : d)
    {
        x.xQueue = qh; // same queue handle for every button task since they are writing to the same queue
        x.xTicksToWait = pdMS_TO_TICKS(5000); // 5 second timeout
        for (auto &y : x.pvBuffer)
        {
            y = -1; // init all the values in all the queues to zero
        }
    }

    xTaskCreate(producer_task, "producer1", 512, (void *) &d[0], tskIDLE_PRIORITY + 1, nullptr);
    xTaskCreate(producer_task, "producer2", 512, (void *) &d[1], tskIDLE_PRIORITY + 1, nullptr);
    xTaskCreate(producer_task, "producer3", 512, (void *) &d[2], tskIDLE_PRIORITY + 1, nullptr);

    xTaskCreate(consumer_task, "consumer", 512, (void *) &d, tskIDLE_PRIORITY + 1, nullptr);

    vTaskStartScheduler();

    while(true){};

    return 0;
}

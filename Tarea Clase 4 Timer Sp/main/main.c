#include <stdio.h>
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "freertos/task.h"

TimerHandle_t xTimer;
int timer_ID = 1;
int interval = 100;


void vTimerCallback(TimerHandle_t xTimer)
{
    ESP_LOGW("Main", "Hello world");
}

void app_main(void * args){

    xTimer = xTimerCreate("Timer",                     
                          (pdMS_TO_TICKS(interval)), 
                          pdTRUE,                     
                          (void *)timer_ID,              
                          &vTimerCallback               
    );
    if (xTimer == NULL)
    {
        ESP_LOGE("Main", "No se pudo crear el timer");
    }
    else
    {
        if (xTimerStart(xTimer, 0) != pdPASS)
        {
            ESP_LOGE("Main", "No se pudo activar el timer");
        }
    }
    ESP_LOGI("Main", "Se pudo crear el timer");
}
 
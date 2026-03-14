/**
 * @file hardware_timer.c
 * @brief 硬件定时器模块实现
 * @details 使用 ESP32-S3 Timer Group 进行高精度时间管理
 */

#include "hardware_timer.h"
#include "esp_log.h"
#include "driver/timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/soc.h"
#include <time.h>

static const char *TAG = "HW_TIMER";

#define TIMER_GROUP TIMER_GROUP_0
#define TIMER_INDEX TIMER_0
#define TIMER_DIVIDER 80
#define TIMER_SCALE (APB_CLK_FREQ / TIMER_DIVIDER)

static volatile time_t g_unix_sec = 0;
static volatile uint32_t g_unix_us = 0;
static bool g_timer_initialized = false;
static portMUX_TYPE g_timer_mux = portMUX_INITIALIZER_UNLOCKED;

static bool IRAM_ATTR timer_isr_callback(void *args)
{
    portENTER_CRITICAL_ISR(&g_timer_mux);
    g_unix_sec++;
    g_unix_us = 0;
    portEXIT_CRITICAL_ISR(&g_timer_mux);
    
    return true;
}

esp_err_t hardware_timer_init(void)
{
    if (g_timer_initialized) {
        return ESP_OK;
    }

    timer_config_t config = {
        .divider = TIMER_DIVIDER,
        .counter_dir = TIMER_COUNT_UP,
        .counter_en = TIMER_PAUSE,
        .alarm_en = TIMER_ALARM_EN,
        .auto_reload = TIMER_AUTORELOAD_EN,
    };

    ESP_LOGI(TAG, "Initializing hardware timer");

    timer_init(TIMER_GROUP, TIMER_INDEX, &config);
    timer_set_counter_value(TIMER_GROUP, TIMER_INDEX, 0);
    timer_set_alarm_value(TIMER_GROUP, TIMER_INDEX, TIMER_SCALE);
    timer_enable_intr(TIMER_GROUP, TIMER_INDEX);
    timer_isr_callback_add(TIMER_GROUP, TIMER_INDEX, timer_isr_callback, NULL, 0);
    timer_start(TIMER_GROUP, TIMER_INDEX);

    g_timer_initialized = true;
    ESP_LOGI(TAG, "Hardware timer initialized");

    return ESP_OK;
}

esp_err_t hardware_timer_set_time(time_t sec, uint32_t us)
{
    portENTER_CRITICAL(&g_timer_mux);
    g_unix_sec = sec;
    g_unix_us = us;
    portEXIT_CRITICAL(&g_timer_mux);
    
    ESP_LOGI(TAG, "Time set: %ld.%06ld", sec, us);
    return ESP_OK;
}

esp_err_t hardware_timer_get_time(time_t* sec, uint32_t* us)
{
    uint64_t timer_value;
    
    timer_get_counter_value(TIMER_GROUP, TIMER_INDEX, &timer_value);
    
    portENTER_CRITICAL(&g_timer_mux);
    time_t curr_sec = g_unix_sec;
    uint32_t curr_us = g_unix_us + (uint32_t)timer_value;
    portEXIT_CRITICAL(&g_timer_mux);
    
    while (curr_us >= 1000000) {
        curr_sec++;
        curr_us -= 1000000;
    }
    
    if (sec) *sec = curr_sec;
    if (us) *us = curr_us;
    
    return ESP_OK;
}

uint64_t hardware_timer_get_us(void)
{
    uint64_t timer_value;
    timer_get_counter_value(TIMER_GROUP, TIMER_INDEX, &timer_value);
    
    portENTER_CRITICAL(&g_timer_mux);
    time_t curr_sec = g_unix_sec;
    uint32_t curr_us = g_unix_us + (uint32_t)timer_value;
    portEXIT_CRITICAL(&g_timer_mux);
    
    while (curr_us >= 1000000) {
        curr_sec++;
        curr_us -= 1000000;
    }
    
    return (uint64_t)curr_sec * 1000000 + curr_us;
}

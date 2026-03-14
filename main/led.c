/**
 * @file led.c
 * @brief LED 指示模块实现
 * @details 负责 LED 状态指示和模式切换指示
 */

#include "led.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "LED";

// LED 配置
static led_config_t g_led_config = {
    .gpio = GPIO_NUM_NC,
    .state = LED_STATE_OFF,
    .blink_interval = 500
};

// LED 任务句柄
static TaskHandle_t g_led_task_handle = NULL;

/**
 * @brief LED 控制任务 - 控制 LED 闪烁和状态显示
 * @param pvParameters 任务参数
 */
static void led_task(void *pvParameters)
{
    while (1) {
        switch (g_led_config.state) {
            case LED_STATE_OFF:
                gpio_set_level(g_led_config.gpio, 0);
                vTaskDelay(pdMS_TO_TICKS(100));
                break;

            case LED_STATE_ON:
                gpio_set_level(g_led_config.gpio, 1);
                vTaskDelay(pdMS_TO_TICKS(100));
                break;

            case LED_STATE_BLINK_SLOW:
            case LED_STATE_BLINK_FAST:
                gpio_set_level(g_led_config.gpio, 1);
                vTaskDelay(pdMS_TO_TICKS(g_led_config.blink_interval));
                gpio_set_level(g_led_config.gpio, 0);
                vTaskDelay(pdMS_TO_TICKS(g_led_config.blink_interval));
                break;

            default:
                vTaskDelay(pdMS_TO_TICKS(100));
                break;
        }
    }
}

/**
 * @brief 初始化 LED 模块 - 配置 GPIO 引脚和创建 LED 控制任务
 * @param gpio LED GPIO 引脚
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t led_init(gpio_num_t gpio)
{
    // 检查 GPIO 引脚是否有效
    if (gpio < GPIO_NUM_0 || gpio >= GPIO_NUM_MAX) {
        ESP_LOGE(TAG, "Invalid GPIO pin: %d", gpio);
        return ESP_ERR_INVALID_ARG;
    }

    // 保存 GPIO 引脚
    g_led_config.gpio = gpio;

    // 配置 GPIO 为输出模式
    gpio_reset_pin(gpio);
    gpio_set_direction(gpio, GPIO_MODE_OUTPUT);
    gpio_set_level(gpio, 0);

    ESP_LOGI(TAG, "LED initialized on GPIO %d", gpio);

    return ESP_OK;
}

/**
 * @brief 设置 LED 状态 - 更新 LED 显示状态并启动/停止任务
 * @param state LED 状态
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t led_set_state(led_state_t state)
{
    // 检查 LED 是否已初始化
    if (g_led_config.gpio == GPIO_NUM_NC) {
        ESP_LOGE(TAG, "LED not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // 保存 LED 状态
    g_led_config.state = state;

    // 如果需要闪烁，启动 LED 任务
    if (state == LED_STATE_BLINK_SLOW || state == LED_STATE_BLINK_FAST) {
        if (g_led_task_handle == NULL) {
            xTaskCreate(led_task, "led_task", 2048, NULL, 2, &g_led_task_handle);
            ESP_LOGI(TAG, "LED task started");
        }
    } else {
        // 关闭 LED 任务
        if (g_led_task_handle != NULL) {
            vTaskDelete(g_led_task_handle);
            g_led_task_handle = NULL;
            ESP_LOGI(TAG, "LED task stopped");
        }

        // 直接设置 LED 状态
        gpio_set_level(g_led_config.gpio, (state == LED_STATE_ON) ? 1 : 0);
    }

    return ESP_OK;
}

/**
 * @brief 设置 LED 闪烁间隔 - 配置闪烁的时间间隔
 * @param interval 闪烁间隔（毫秒）
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t led_set_blink_interval(uint32_t interval)
{
    // 检查间隔是否有效
    if (interval < 10 || interval > 10000) {
        ESP_LOGE(TAG, "Invalid blink interval: %lu", interval);
        return ESP_ERR_INVALID_ARG;
    }

    // 保存闪烁间隔
    g_led_config.blink_interval = interval;

    ESP_LOGI(TAG, "LED blink interval set to %lu ms", interval);

    return ESP_OK;
}

/**
 * @brief 设置 AP 模式指示 - 快闪 100ms 表示 AP 模式
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t led_set_ap_mode(void)
{
    return led_set_state(LED_STATE_BLINK_FAST);
}

/**
 * @brief 设置 STA 模式指示 - 常亮表示 STA 模式已连接
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t led_set_sta_mode(void)
{
    return led_set_state(LED_STATE_ON);
}

/**
 * @brief 设置 STA 异常模式指示 - 常亮表示连接异常
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t led_set_sta_error_mode(void)
{
    return led_set_state(LED_STATE_ON);
}

/**
 * @brief 关闭 LED - 设置 LED 为关闭状态
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t led_off(void)
{
    return led_set_state(LED_STATE_OFF);
}

/**
 * @brief 打开 LED - 设置 LED 为常亮状态
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t led_on(void)
{
    return led_set_state(LED_STATE_ON);
}

/**
 * @brief 获取 LED 状态 - 返回当前 LED 的状态
 * @return LED 状态
 */
led_state_t led_get_state(void)
{
    return g_led_config.state;
}

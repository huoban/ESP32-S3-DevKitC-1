/**
 * @file led.h
 * @brief LED 指示模块头文件
 * @details 负责 LED 状态指示和模式切换指示
 */

#ifndef LED_H
#define LED_H

#include "esp_err.h"
#include "driver/gpio.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ==================== LED 状态枚举 ====================
/**
 * @brief LED 状态枚举
 */
typedef enum {
    LED_STATE_OFF = 0,               // 关闭
    LED_STATE_ON,                    // 常亮
    LED_STATE_BLINK_SLOW,            // 慢闪 (500ms)
    LED_STATE_BLINK_FAST             // 快闪 (100ms)
} led_state_t;

// ==================== LED 配置结构 ====================
/**
 * @brief LED 配置结构
 */
typedef struct {
    gpio_num_t gpio;                 // GPIO 引脚
    led_state_t state;               // 当前状态
    uint32_t blink_interval;         // 闪烁间隔
} led_config_t;

// ==================== LED 管理接口 ====================

/**
 * @brief 初始化 LED 模块
 * @param gpio LED GPIO 引脚
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t led_init(gpio_num_t gpio);

/**
 * @brief 设置 LED 状态
 * @param state LED 状态
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t led_set_state(led_state_t state);

/**
 * @brief 设置 LED 闪烁间隔
 * @param interval 闪烁间隔（毫秒）
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t led_set_blink_interval(uint32_t interval);

/**
 * @brief 设置 AP 模式指示（快闪 100ms）
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t led_set_ap_mode(void);

/**
 * @brief 设置 STA 模式指示（常亮）
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t led_set_sta_mode(void);

/**
 * @brief 设置 STA 异常模式指示（常亮）
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t led_set_sta_error_mode(void);

/**
 * @brief 关闭 LED
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t led_off(void);

/**
 * @brief 打开 LED
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t led_on(void);

/**
 * @brief 获取 LED 状态
 * @return LED 状态
 */
led_state_t led_get_state(void);

#ifdef __cplusplus
}
#endif

#endif // LED_H

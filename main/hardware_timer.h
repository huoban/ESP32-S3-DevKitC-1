/**
 * @file hardware_timer.h
 * @brief 硬件定时器模块头文件
 * @details 用于高精度时间管理
 */

#ifndef HARDWARE_TIMER_H
#define HARDWARE_TIMER_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

// ==================== 硬件定时器接口 ====================

/**
 * @brief 初始化硬件定时器
 */
esp_err_t hardware_timer_init(void);

/**
 * @brief 设置当前时间
 */
esp_err_t hardware_timer_set_time(time_t sec, uint32_t us);

/**
 * @brief 获取当前高精度时间
 */
esp_err_t hardware_timer_get_time(time_t* sec, uint32_t* us);

/**
 * @brief 获取当前微秒级时间戳
 */
uint64_t hardware_timer_get_us(void);

#ifdef __cplusplus
}
#endif

#endif

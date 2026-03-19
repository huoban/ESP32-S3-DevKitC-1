/**
 * @file sensors.h
 * @brief 传感器管理模块头文件
 * @details 负责各类传感器的接口管理和数据采集（占位接口，后续开发）
 */

#ifndef SENSORS_H
#define SENSORS_H

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ==================== 常量定义 ====================
#define SENSORS_MAX_COUNT 16         // 最大支持传感器数量

// ==================== 传感器类型枚举 ====================
typedef enum {
    SENSOR_TYPE_NONE = 0,
    SENSOR_TYPE_TEMPERATURE,
    SENSOR_TYPE_HUMIDITY,
    SENSOR_TYPE_PRESSURE,
    SENSOR_TYPE_LIGHT,
    SENSOR_TYPE_GPIO,
    SENSOR_TYPE_ANALOG,
    SENSOR_TYPE_CUSTOM
} sensor_type_t;

// ==================== 传感器数据结构 ====================
typedef struct {
    float value;                      // 传感器数值
    char unit[16];                    // 单位
    uint32_t timestamp;               // 时间戳
    bool is_valid;                    // 数据是否有效
} sensor_data_t;

// ==================== 传感器信息结构 ====================
typedef struct {
    uint8_t instance;                 // 实例编号 (0-15)
    sensor_type_t type;               // 传感器类型
    char name[64];                    // 传感器名称
    char description[128];            // 描述
    bool is_enabled;                  // 是否启用
    sensor_data_t last_data;          // 最新数据
} sensor_info_t;

// ==================== 传感器管理接口 ====================

/**
 * @brief 初始化传感器管理模块
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t sensors_init(void);

/**
 * @brief 启动传感器监控
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t sensors_start(void);

/**
 * @brief 停止传感器监控
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t sensors_stop(void);

/**
 * @brief 获取传感器信息
 * @param instance 传感器实例 (0-15)
 * @param info 传感器信息指针
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t sensors_get_info(uint8_t instance, sensor_info_t* info);

/**
 * @brief 获取传感器数据
 * @param instance 传感器实例 (0-15)
 * @param data 传感器数据指针
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t sensors_get_data(uint8_t instance, sensor_data_t* data);

/**
 * @brief 启用传感器
 * @param instance 传感器实例 (0-15)
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t sensors_enable(uint8_t instance);

/**
 * @brief 禁用传感器
 * @param instance 传感器实例 (0-15)
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t sensors_disable(uint8_t instance);

/**
 * @brief 获取已连接的传感器数量
 * @return 传感器数量
 */
uint8_t sensors_get_count(void);

#ifdef __cplusplus
}
#endif

#endif // SENSORS_H

/**
 * @file sensors.c
 * @brief 传感器管理模块
 * @details 负责各类传感器的接口管理和数据采集（占位实现，后续开发）
 */

#include "sensors.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "SENSORS";

// 传感器数组
static sensor_info_t sensors[SENSORS_MAX_COUNT] = {0};

// 模块运行状态
static bool sensors_initialized = false;
static bool sensors_running = false;

// 初始化传感器模块 - 初始化传感器数组，设置默认状态
esp_err_t sensors_init(void)
{
    ESP_LOGI(TAG, "Initializing sensors module (placeholder)...");

    memset(sensors, 0, sizeof(sensors));

    for (int i = 0; i < SENSORS_MAX_COUNT; i++) {
        sensors[i].instance = i;
        sensors[i].type = SENSOR_TYPE_NONE;
        sensors[i].is_enabled = false;
        snprintf(sensors[i].name, sizeof(sensors[i].name), "Sensor %d", i);
        snprintf(sensors[i].description, sizeof(sensors[i].description), "Reserved");
    }

    sensors_initialized = true;
    ESP_LOGI(TAG, "Sensors module initialized (placeholder)");
    return ESP_OK;
}

// 启动传感器监控 - 启动传感器监控任务（占位实现）
esp_err_t sensors_start(void)
{
    if (!sensors_initialized) {
        ESP_LOGE(TAG, "Sensors module not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Starting sensors module (placeholder)...");
    sensors_running = true;
    ESP_LOGI(TAG, "Sensors module started (placeholder)");
    return ESP_OK;
}

// 停止传感器监控 - 停止传感器监控任务（占位实现）
esp_err_t sensors_stop(void)
{
    ESP_LOGI(TAG, "Stopping sensors module (placeholder)...");
    sensors_running = false;
    ESP_LOGI(TAG, "Sensors module stopped (placeholder)");
    return ESP_OK;
}

// 获取传感器信息 - 获取指定传感器实例的信息
esp_err_t sensors_get_info(uint8_t instance, sensor_info_t* info)
{
    if (instance >= SENSORS_MAX_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    if (info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(info, &sensors[instance], sizeof(sensor_info_t));
    return ESP_OK;
}

// 获取传感器数据 - 获取指定传感器实例的最新数据
esp_err_t sensors_get_data(uint8_t instance, sensor_data_t* data)
{
    if (instance >= SENSORS_MAX_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    if (data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(data, &sensors[instance].last_data, sizeof(sensor_data_t));
    return ESP_OK;
}

// 启用传感器 - 启用指定传感器实例
esp_err_t sensors_enable(uint8_t instance)
{
    if (instance >= SENSORS_MAX_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    sensors[instance].is_enabled = true;
    ESP_LOGI(TAG, "Sensor %d enabled (placeholder)", instance);
    return ESP_OK;
}

// 禁用传感器 - 禁用指定传感器实例
esp_err_t sensors_disable(uint8_t instance)
{
    if (instance >= SENSORS_MAX_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    sensors[instance].is_enabled = false;
    ESP_LOGI(TAG, "Sensor %d disabled (placeholder)", instance);
    return ESP_OK;
}

// 获取已连接的传感器数量 - 统计已配置的传感器数量
uint8_t sensors_get_count(void)
{
    uint8_t count = 0;
    for (int i = 0; i < SENSORS_MAX_COUNT; i++) {
        if (sensors[i].type != SENSOR_TYPE_NONE) {
            count++;
        }
    }
    return count;
}

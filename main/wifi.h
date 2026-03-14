/**
 * @file wifi.h
 * @brief WiFi 管理模块头文件
 * @details 负责 WiFi 连接管理、AP/STA 模式切换和 WiFi 配置持久化
 */

#ifndef WIFI_H
#define WIFI_H

#include "esp_err.h"
#include "esp_wifi.h"
#include "config.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ==================== WiFi 配置结构 ====================
// 注意：wifi_config_t_custom 已在 config.h 中定义

// ==================== WiFi 状态结构 ====================
/**
 * @brief WiFi 状态结构
 */
typedef struct {
    wifi_mode_t mode;                // 当前模式
    bool is_connected;               // 是否已连接
    char ip_address[16];             // IP 地址
    char gateway[16];                // 网关地址
    char subnet[16];                 // 子网掩码
    uint8_t rssi;                    // 信号强度
    char ssid[32];                   // 当前连接的 SSID
} wifi_status_t;

// ==================== WiFi 管理接口 ====================

/**
 * @brief 初始化 WiFi 管理模块
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t wifi_init(void);

/**
 * @brief 启动 AP 模式
 * @param ssid AP SSID
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t wifi_start_ap(const char* ssid);

/**
 * @brief 启动 STA 模式
 * @param ssid WiFi SSID
 * @param password WiFi 密码
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t wifi_start_sta(const char* ssid, const char* password);

/**
 * @brief 停止 WiFi
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t wifi_stop(void);

/**
 * @brief 加载 WiFi 配置
 * @param config WiFi 配置指针
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t wifi_load_config(wifi_config_t_custom* config);

/**
 * @brief 保存 WiFi 配置
 * @param config WiFi 配置指针
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t wifi_save_config(const wifi_config_t_custom* config);

/**
 * @brief 获取 WiFi 状态
 * @param status WiFi 状态指针
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t wifi_get_status(wifi_status_t* status);

/**
 * @brief 检查 WiFi 是否已连接
 * @return true 已连接，false 未连接
 */
bool wifi_is_connected(void);

/**
 * @brief 获取当前 WiFi 模式
 * @return WiFi 模式
 */
wifi_mode_t wifi_get_mode(void);

/**
 * @brief WiFi 连接事件回调
 * @param arg 参数
 * @param event_base 事件基础
 * @param event_id 事件 ID
 * @param event_data 事件数据
 */
void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);

/**
 * @brief 自动启动 WiFi（根据配置选择 AP 或 STA 模式）
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t wifi_auto_start(void);

/**
 * @brief 扫描 WiFi 网络
 * @param[out] networks 网络列表
 * @param[in] max_count 最大数量
 * @param[out] count 实际数量
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t wifi_scan_networks(wifi_ap_record_t *networks, uint16_t max_count, uint16_t *count);

#ifdef __cplusplus
}
#endif

#endif // WIFI_H

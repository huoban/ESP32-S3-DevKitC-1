/**
 * @file monitor.h
 * @brief 网站健康监控模块头文件
 * @details 负责网站状态检测、多线程并发检测和离线/恢复通知
 */

#ifndef MONITOR_H
#define MONITOR_H

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

// ==================== 网站检测状态结构 ====================
/**
 * @brief 网站检测状态结构（PSRAM）
 */
typedef struct {
    char name[64];                   // 网站名称
    bool is_online;                  // 在线状态
    uint32_t fail_count;             // 失败计次
    uint32_t last_check_time;        // 最后检测时间
    uint32_t last_notify_time;       // 最后通知时间
    bool offline_notified;           // 离线通知已发送
} monitor_state_t;

// ==================== 监控管理接口 ====================

/**
 * @brief 初始化监控管理模块
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t monitor_init(void);

/**
 * @brief 启动监控
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t monitor_start(void);

/**
 * @brief 停止监控
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t monitor_stop(void);

/**
 * @brief 添加监控网站
 * @param site 网站配置指针
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t monitor_add_site(const monitor_site_t* site);

/**
 * @brief 移除监控网站
 * @param name 网站名称
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t monitor_remove_site(const char* name);

/**
 * @brief 获取网站配置
 * @param name 网站名称
 * @param site 网站配置指针
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t monitor_get_site(const char* name, monitor_site_t* site);

/**
 * @brief 获取所有网站配置
 * @param sites 网站配置数组
 * @param count 网站数量指针
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t monitor_get_all_sites(monitor_site_t* sites, size_t* count);

/**
 * @brief 检测网站状态
 * @param name 网站名称
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t monitor_check_site(const char* name);

/**
 * @brief 发送通知
 * @param site_name 网站名称
 * @param is_online 在线状态
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t monitor_send_notification(const char* site_name, bool is_online);

/**
 * @brief 发送测试通知
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t monitor_send_test_notification(void);

/**
 * @brief 加载监控配置
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t monitor_load_config(void);

/**
 * @brief 保存监控配置
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t monitor_save_config(void);

/**
 * @brief 检查监控是否运行
 * @return true 运行中，false 已停止
 */
bool monitor_is_running(void);

/**
 * @brief 获取网站检测状态
 * @param name 网站名称
 * @param state 状态指针
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t monitor_get_site_state(const char* name, monitor_state_t* state);

#ifdef __cplusplus
}
#endif

#endif // MONITOR_H

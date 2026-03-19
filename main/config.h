/**
 * @file config.h
 * @brief 配置管理模块头文件
 * @details 负责NVS配置的读写、持久化存储和恢复出厂设置功能
 */

#ifndef CONFIG_H
#define CONFIG_H

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ==================== 配置键定义 ====================
#define NVS_NAMESPACE "print_server"
#define NVS_KEY_WIFI_SSID "wifi_ssid"
#define NVS_KEY_WIFI_PASSWORD "wifi_password"
#define NVS_KEY_WIFI_MODE "wifi_mode"
#define NVS_KEY_NTP_CONFIG "ntp_config"
#define NVS_KEY_MONITOR_SITES "monitor_sites"
#define NVS_KEY_WECHAT_CONFIG "wechat_config"
#define NVS_KEY_DEVICE_NAME "device_name"
#define NVS_KEY_WEBHOOK_CONFIG "webhook_config"
#define NVS_KEY_MONITOR_SYSTEM "monitor_system"

// ==================== WiFi 配置结构 ====================
/**
 * @brief WiFi 配置结构
 */
typedef struct {
    char ssid[32];                   // SSID
    char password[64];               // 密码
    bool is_configured;              // 是否已配置
    char ip_address[16];             // 静态 IP 地址（可选）
    char gateway[16];                // 网关（可选）
    char netmask[16];                // 子网掩码（可选）
    char dns[16];                    // DNS 服务器（可选）
    bool use_static_ip;              // 是否使用静态 IP
} wifi_config_t_custom;

// ==================== NTP 配置结构 ====================
/**
 * @brief NTP 配置结构
 */
typedef struct {
    char server1[64];                // 主服务器
    char server2[64];                // 备服务器
    uint32_t sync_interval;          // 同步间隔（分钟）
    bool enable_client;              // 启用客户端
    bool enable_server;              // 启用服务器
} ntp_config_t;

// ==================== 监控网站配置结构 ====================
/**
 * @brief 监控网站配置结构
 */
typedef struct {
    char name[64];                   // 网站名称
    char url[256];                   // 网站地址
    char custom_host[128];           // 自定义 Host
    char user_agent[128];            // User-Agent
    uint32_t interval;               // 检测间隔（分钟）
    uint32_t timeout;                // 超时时间（秒）
    uint32_t offline_count;          // 离线确认次数
    bool status_code_1xx;            // 接受 1xx 状态码
    bool status_code_2xx;            // 接受 2xx 状态码
    bool status_code_3xx;            // 接受 3xx 状态码
    bool status_code_4xx;            // 接受 4xx 状态码
    bool status_code_5xx;            // 接受 5xx 状态码
    uint32_t webhook_interval;       // Webhook 通知间隔（分钟）
    bool enabled;                    // 是否启用监控
    bool paused;                     // 是否暂停检测
} monitor_site_t;

// ==================== 系统监控配置结构 ====================
/**
 * @brief 系统监控配置结构
 */
typedef struct {
    uint32_t global_check_interval;  // 全局检测间隔（分钟）
    uint32_t global_notify_interval; // 全局通知间隔（分钟）
    uint32_t global_timeout;         // 全局超时时间（秒）
    uint32_t global_offline_count;   // 全局离线确认次数
    char global_user_agent[128];     // 全局 User-Agent
    bool global_enabled;             // 全局监控启用
} monitor_system_config_t;



// ==================== 企业微信配置结构 ====================
/**
 * @brief 企业微信配置结构
 */
typedef struct {
    char corpid[64];                 // 企业 ID
    char corpsecret[64];             // 应用密钥
    char agentid[32];                // 应用 ID
    char touser[64];                 // 接收用户
} wechat_config_t;

// ==================== 系统配置结构 ====================
/**
 * @brief 系统配置结构
 */
typedef struct {
    char device_name[64];            // 设备名称
    wifi_config_t_custom wifi;       // WiFi 配置
    ntp_config_t ntp;                // NTP 配置
    wechat_config_t wechat;          // 企业微信配置
    monitor_site_t monitor_sites[10]; // 监控网站列表
    size_t monitor_site_count;       // 监控网站数量
} system_config_t;

// ==================== 配置管理接口 ====================

/**
 * @brief 初始化配置管理模块
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t config_init(void);

/**
 * @brief 保存 WiFi 配置
 * @param config WiFi 配置指针
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t config_save_wifi(const wifi_config_t_custom* config);

/**
 * @brief 加载 WiFi 配置
 * @param config WiFi 配置指针
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t config_load_wifi(wifi_config_t_custom* config);

/**
 * @brief 保存 NTP 配置
 * @param config NTP 配置指针
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t config_save_ntp(const ntp_config_t* config);

/**
 * @brief 加载 NTP 配置
 * @param config NTP 配置指针
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t config_load_ntp(ntp_config_t* config);

/**
 * @brief 保存监控配置
 * @param sites 监控网站列表
 * @param count 网站数量
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t config_save_monitor(const monitor_site_t* sites, size_t count);

/**
 * @brief 加载监控配置
 * @param sites 监控网站列表
 * @param count 网站数量指针
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t config_load_monitor(monitor_site_t* sites, size_t* count);

/**
 * @brief 保存企业微信配置
 * @param config 企业微信配置指针
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t config_save_wechat(const wechat_config_t* config);

/**
 * @brief 加载企业微信配置
 * @param config 企业微信配置指针
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t config_load_wechat(wechat_config_t* config);

/**
 * @brief 恢复出厂设置
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t config_factory_reset(void);

/**
 * @brief 检查 WiFi 配置是否存在
 * @return true 存在，false 不存在
 */
bool config_has_wifi(void);

/**
 * @brief 保存设备名称
 * @param name 设备名称
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t config_save_device_name(const char* name);

/**
 * @brief 加载设备名称
 * @param name 设备名称缓冲区
 * @param len 缓冲区长度
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t config_load_device_name(char* name, size_t len);

/**
 * @brief 保存系统监控配置
 * @param config 系统监控配置指针
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t config_save_monitor_system(const monitor_system_config_t* config);

/**
 * @brief 加载系统监控配置
 * @param config 系统监控配置指针
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t config_load_monitor_system(monitor_system_config_t* config);

// ==================== WebHook 配置结构 ====================
/**
 * @brief SMTP 配置结构
 */
typedef struct {
    bool enabled;                     // 是否启用
    char smtp_server[128];           // SMTP 服务器地址
    uint16_t smtp_port;              // SMTP 端口 (25/465/587)
    char username[128];               // 账号
    char password[128];               // 密码
    char from_email[128];             // 发件人邮箱
    char to_email[256];               // 收件人邮箱
} smtp_config_t;

/**
 * @brief 企业微信配置结构
 */
typedef struct {
    bool enabled;                     // 是否启用
    char corpid[128];                 // 企业 ID
    char corpsecret[128];             // 应用密钥
    char agentid[64];                 // 应用 ID
    char touser[128];                 // 接收用户
} wechat_webhook_config_t;

/**
 * @brief 自定义 WebHook 配置结构
 */
typedef struct {
    bool enabled;                     // 是否启用
    char url[512];                    // WebHook 地址
    char method[8];                   // 请求方法 (GET/POST)
    char content_type[64];            // Content-Type
    char body_template[1024];         // 请求体模板
} custom_webhook_config_t;

/**
 * @brief WebHook 完整配置结构
 */
typedef struct {
    smtp_config_t smtp;               // SMTP 配置
    wechat_webhook_config_t wechat;   // 企业微信配置
    custom_webhook_config_t custom;   // 自定义 WebHook 配置
} webhook_config_t;

/**
 * @brief 保存 WebHook 配置
 * @param config WebHook 配置指针
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t config_save_webhook(const webhook_config_t* config);

/**
 * @brief 加载 WebHook 配置
 * @param config WebHook 配置指针
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t config_load_webhook(webhook_config_t* config);

#ifdef __cplusplus
}
#endif

#endif // CONFIG_H

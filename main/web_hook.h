/**
 * @file web_hook.h
 * @brief WebHook 通知模块头文件
 * @details 负责邮件、企业微信、自定义 WebHook 通知发送
 */

#ifndef WEB_HOOK_H
#define WEB_HOOK_H

#include "esp_err.h"
#include "config.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

// ==================== 常量定义 ====================
#define WEBHOOK_TITLE_MAX_LEN 256
#define WEBHOOK_CONTENT_MAX_LEN 2048
#define WEBHOOK_URL_MAX_LEN 512
#define WECHAT_ACCESS_TOKEN_MAX_LEN 512

// ==================== WebHook 通知结构 ====================
typedef struct {
    char title[WEBHOOK_TITLE_MAX_LEN];     // 通知标题
    char content[WEBHOOK_CONTENT_MAX_LEN]; // 通知内容
} webhook_notification_t;

// ==================== 企业微信 access_token 管理结构 ====================
typedef struct {
    char access_token[WECHAT_ACCESS_TOKEN_MAX_LEN];  // access_token 字符串
    time_t expire_time;                              // 过期时间戳（秒）
} wechat_access_token_t;

// ==================== WebHook 管理接口 ====================

/**
 * @brief 初始化 WebHook 模块
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t web_hook_init(void);

/**
 * @brief 发送通知
 * @param notification 通知内容
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t web_hook_send_notification(const webhook_notification_t* notification);

/**
 * @brief 测试 SMTP 配置
 * @param config SMTP 配置
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t web_hook_test_smtp(const smtp_config_t* config);

/**
 * @brief 测试企业微信配置
 * @param config 企业微信配置
 * @param title 消息标题
 * @param content 消息内容
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t web_hook_test_wechat(const wechat_webhook_config_t* config, const char* title, const char* content);

/**
 * @brief 测试自定义 WebHook
 * @param config WebHook 配置
 * @param title 通知标题
 * @param content 通知内容
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t web_hook_test_custom(const custom_webhook_config_t* config, const char* title, const char* content);

/**
 * @brief 测试所有启用的 WebHook 通知方式
 * @param config 完整 WebHook 配置
 * @param title 通知标题
 * @param content 通知内容
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t web_hook_test_all(const webhook_config_t* config, const char* title, const char* content);

// ==================== 新增接口 ====================

/**
 * @brief 顺序发送所有启用的 WebHook 通知（单线程，依次执行）
 * @param title 通知标题
 * @param content 通知内容
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t web_hook_send_all_sequential(const char* title, const char* content);

/**
 * @brief 创建任务并顺序发送所有启用的 WebHook 通知
 * @param title 通知标题
 * @param content 通知内容
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t web_hook_start_send_task(const char* title, const char* content);

/**
 * @brief 获取企业微信 access_token
 * @param config 企业微信配置
 * @param token_out 输出 access_token
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t web_hook_get_wechat_access_token(const wechat_webhook_config_t* config, wechat_access_token_t* token_out);

/**
 * @brief 发送企业微信消息
 * @param config 企业微信配置
 * @param title 消息标题
 * @param content 消息内容
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t web_hook_send_wechat_message(const wechat_webhook_config_t* config, const char* title, const char* content);

/**
 * @brief 发送自定义 WebHook 通知
 * @param config WebHook 配置
 * @param title 通知标题
 * @param content 通知内容
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t web_hook_send_custom_webhook(const custom_webhook_config_t* config, const char* title, const char* content);

#ifdef __cplusplus
}
#endif

#endif // WEB_HOOK_H

/**
 * @file ntp_server.h
 * @brief NTP 服务器模块头文件
 * @details 负责 NTP 时间服务器功能
 */

#ifndef NTP_SERVER_H
#define NTP_SERVER_H

#include "esp_err.h"
#include <stdbool.h>
#include <time.h>
#include "ntp_client.h"

#ifdef __cplusplus
extern "C" {
#endif

// ==================== NTP 服务器接口 ====================

/**
 * @brief 初始化 NTP 服务器
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t ntp_server_init(void);

/**
 * @brief 启动 NTP 服务器
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t ntp_server_start(void);

/**
 * @brief 停止 NTP 服务器
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t ntp_server_stop(void);

/**
 * @brief 设置服务器时间
 * @param sec 秒数
 * @param us 微秒数
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t ntp_server_set_time(time_t sec, uint32_t us);

/**
 * @brief 获取服务器时间
 * @param sec 秒数指针
 * @param us 微秒数指针
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t ntp_server_get_time(time_t* sec, uint32_t* us);

/**
 * @brief 检查 NTP 服务器是否运行
 * @return true 运行中，false 已停止
 */
bool ntp_server_is_running(void);

#ifdef __cplusplus
}
#endif

#endif // NTP_SERVER_H

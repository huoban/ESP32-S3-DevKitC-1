/**
 * @file ntp_client.h
 * @brief NTP 客户端模块头文件
 * @details 负责完整 NTPv4 客户端功能
 */

#ifndef NTP_CLIENT_H
#define NTP_CLIENT_H

#include "esp_err.h"
#include <stdbool.h>
#include <time.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ==================== NTP 配置结构 ====================
/**
 * @brief NTP 配置结构
 */
typedef struct {
    char server1[64];                
    char server2[64];                
    uint32_t sync_interval;          
    bool enable_client;            
} ntp_client_config_t;

// ==================== NTP 时间戳结构 ====================
typedef struct {
    uint32_t seconds;                
    uint32_t fraction;              
} ntp_timestamp_t;

// ==================== NTP 客户端接口 ====================

/**
 * @brief 初始化 NTP 客户端
 */
esp_err_t ntp_client_init(const ntp_client_config_t* config);

/**
 * @brief 启动 NTP 客户端
 */
esp_err_t ntp_client_start(void);

/**
 * @brief 停止 NTP 客户端
 */
esp_err_t ntp_client_stop(void);

/**
 * @brief 手动触发时间同步（完整 NTPv4 算法）
 */
esp_err_t ntp_client_sync_time(void);

/**
 * @brief 获取当前时间
 */
esp_err_t ntp_client_get_time(time_t* sec, uint32_t* us);

/**
 * @brief 检查 NTP 客户端是否运行
 */
bool ntp_client_is_running(void);

#ifdef __cplusplus
}
#endif

#endif

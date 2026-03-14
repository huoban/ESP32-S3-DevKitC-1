#ifndef NTP_STORAGE_H
#define NTP_STORAGE_H

#include <time.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 NTP 存储（使用 PSRAM）
 */
void ntp_storage_init(void);

/**
 * @brief 保存 NTP 最后同步时间
 * @param timestamp Unix 时间戳
 */
void ntp_storage_save_sync_time(time_t timestamp);

/**
 * @brief 获取 NTP 最后同步时间
 * @return time_t 最后同步时间，如果从未同步返回 0
 */
time_t ntp_storage_get_sync_time(void);

#ifdef __cplusplus
}
#endif

#endif // NTP_STORAGE_H

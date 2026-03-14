#include "ntp_storage.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

#define TAG "NTP_STORAGE"

// PSRAM 中存储 NTP 同步时间的地址（使用最后 1KB 的 PSRAM）
static time_t *s_ntp_sync_time = NULL;

void ntp_storage_init(void)
{
    // 从 PSRAM 分配内存
    s_ntp_sync_time = heap_caps_malloc(sizeof(time_t), MALLOC_CAP_SPIRAM);
    if (s_ntp_sync_time == NULL) {
        ESP_LOGE(TAG, "Failed to allocate PSRAM for NTP storage");
        return;
    }
    
    // 初始化为 0（表示从未同步）
    *s_ntp_sync_time = 0;
    ESP_LOGI(TAG, "NTP storage initialized in PSRAM");
}

void ntp_storage_save_sync_time(time_t timestamp)
{
    if (s_ntp_sync_time != NULL) {
        *s_ntp_sync_time = timestamp;
        ESP_LOGI(TAG, "NTP sync time saved: %ld", (long)timestamp);
    }
}

time_t ntp_storage_get_sync_time(void)
{
    if (s_ntp_sync_time != NULL) {
        return *s_ntp_sync_time;
    }
    return 0;
}

/**
 * @file ntp_client.c
 * @brief NTP 客户端模块实现
 * @details 完整 NTPv4 客户端，包含 RTT、offset、多次采样滤波
 */

#include "ntp_client.h"
#include "hardware_timer.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>

static const char *TAG = "NTP_CLIENT";

#define NTP_PORT 123
#define NTP_PACKET_SIZE 48
#define UNIX_OFFSET 2208988800UL
#define NTP_TIMEOUT_MS 800
#define SAMPLE_COUNT 6
#define SAMPLE_DELAY_MS 150

static bool g_ntp_running = false;
static TaskHandle_t g_ntp_task_handle = NULL;
static char g_server1[64] = "ntp.ntsc.ac.cn";
static char g_server2[64] = "ntp1.aliyun.com";
static uint32_t g_sync_interval = 1800;

static int compare_int64(const void *a, const void *b)
{
    int64_t x = *(const int64_t *)a;
    int64_t y = *(const int64_t *)b;
    if (x < y) return -1;
    if (x > y) return 1;
    return 0;
}

static bool ntp_get_single_time(const char *server, uint32_t *out_sec, uint32_t *out_us, int64_t *out_offset)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Failed to create socket");
        return false;
    }

    struct timeval timeout;
    timeout.tv_sec = NTP_TIMEOUT_MS / 1000;
    timeout.tv_usec = (NTP_TIMEOUT_MS % 1000) * 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    struct hostent *host = gethostbyname(server);
    if (host == NULL) {
        ESP_LOGE(TAG, "Failed to resolve %s", server);
        close(sock);
        return false;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(NTP_PORT);
    memcpy(&server_addr.sin_addr, host->h_addr_list[0], host->h_length);

    uint8_t buf[NTP_PACKET_SIZE] = {0};
    buf[0] = 0xE3;

    uint64_t t1 = hardware_timer_get_us();
    sendto(sock, buf, NTP_PACKET_SIZE, 0, (struct sockaddr *)&server_addr, sizeof(server_addr));

    struct sockaddr_in resp_addr;
    socklen_t resp_len = sizeof(resp_addr);
    int len = recvfrom(sock, buf, NTP_PACKET_SIZE, 0, (struct sockaddr *)&resp_addr, &resp_len);
    uint64_t t4 = hardware_timer_get_us();
    close(sock);

    if (len < NTP_PACKET_SIZE) {
        return false;
    }

    uint32_t T2s = ntohl(*(uint32_t *)(buf + 32));
    uint32_t T2f = ntohl(*(uint32_t *)(buf + 36));
    uint32_t T3s = ntohl(*(uint32_t *)(buf + 40));
    uint32_t T3f = ntohl(*(uint32_t *)(buf + 44));

    uint64_t T2 = (uint64_t)(T2s - UNIX_OFFSET) * 1000000 + (uint64_t)T2f * 1000000 / 0xFFFFFFFF;
    uint64_t T3 = (uint64_t)(T3s - UNIX_OFFSET) * 1000000 + (uint64_t)T3f * 1000000 / 0xFFFFFFFF;

    int64_t offset = ((int64_t)T2 - (int64_t)t1 + (int64_t)T3 - (int64_t)t4) / 2;
    uint64_t real_time_us = t4 + offset;

    *out_sec = (uint32_t)(real_time_us / 1000000);
    *out_us = (uint32_t)(real_time_us % 1000000);
    
    if (out_offset) {
        *out_offset = offset;
    }

    return true;
}

static bool ntp_get_precise_time(uint32_t *best_sec, uint32_t *best_us)
{
    int64_t offsets[SAMPLE_COUNT];
    uint32_t times[SAMPLE_COUNT][2];
    int ok_count = 0;

    for (int i = 0; i < SAMPLE_COUNT; i++) {
        uint32_t s, u;
        int64_t offset;
        
        const char *server = (i % 2 == 0) ? g_server1 : g_server2;
        
        if (ntp_get_single_time(server, &s, &u, &offset)) {
            times[ok_count][0] = s;
            times[ok_count][1] = u;
            offsets[ok_count] = offset;
            ok_count++;
            ESP_LOGI(TAG, "Sample %d: %lu.%06lu (offset=%lld)", ok_count, s, u, offset);
        }
        
        if (i < SAMPLE_COUNT - 1) {
            vTaskDelay(pdMS_TO_TICKS(SAMPLE_DELAY_MS));
        }
    }

    if (ok_count < 3) {
        ESP_LOGE(TAG, "Not enough valid samples: %d", ok_count);
        return false;
    }

    qsort(offsets, ok_count, sizeof(int64_t), compare_int64);
    
    int mid_index = ok_count / 2;
    int64_t best_offset = offsets[mid_index];
    
    for (int i = 0; i < ok_count; i++) {
        uint64_t sample_us = (uint64_t)times[i][0] * 1000000 + times[i][1];
        uint64_t current_us = hardware_timer_get_us();
        int64_t sample_offset = (int64_t)sample_us - (int64_t)current_us;
        
        if (sample_offset == best_offset || (sample_offset - best_offset >= -1000 && sample_offset - best_offset <= 1000)) {
            *best_sec = times[i][0];
            *best_us = times[i][1];
            ESP_LOGI(TAG, "Selected sample %d: %lu.%06lu", i, *best_sec, *best_us);
            break;
        }
    }

    return true;
}

static void ntp_sync_task(void *arg)
{
    ESP_LOGI(TAG, "NTP sync task started");
    
    while (g_ntp_running) {
        uint32_t sec, us;
        
        ESP_LOGI(TAG, "Starting NTP sync...");
        if (ntp_get_precise_time(&sec, &us)) {
            ESP_LOGI(TAG, "NTP sync successful: %lu.%06lu", sec, us);
            
            struct timeval tv;
            tv.tv_sec = sec;
            tv.tv_usec = us;
            settimeofday(&tv, NULL);
            
            hardware_timer_set_time(sec, us);
            
            extern void ntp_storage_save_sync_time(time_t timestamp);
            ntp_storage_save_sync_time(sec);
        } else {
            ESP_LOGE(TAG, "NTP sync failed, using hardware timer time");
            
            time_t hw_sec;
            uint32_t hw_us;
            hardware_timer_get_time(&hw_sec, &hw_us);
            
            struct timeval tv;
            tv.tv_sec = hw_sec;
            tv.tv_usec = hw_us;
            settimeofday(&tv, NULL);
            ESP_LOGI(TAG, "System time updated from hardware timer: %ld.%06ld", hw_sec, hw_us);
        }
        
        for (uint32_t i = 0; i < g_sync_interval && g_ntp_running; i++) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
    
    ESP_LOGI(TAG, "NTP sync task stopped");
    vTaskDelete(NULL);
}

esp_err_t ntp_client_init(const ntp_client_config_t* config)
{
    if (config) {
        if (config->server1[0]) {
            strlcpy(g_server1, config->server1, sizeof(g_server1));
        }
        if (config->server2[0]) {
            strlcpy(g_server2, config->server2, sizeof(g_server2));
        }
        if (config->sync_interval > 0) {
            g_sync_interval = config->sync_interval;
        }
    }
    
    hardware_timer_init();
    
    time_t sec;
    uint32_t us;
    hardware_timer_get_time(&sec, &us);
    
    struct timeval tv;
    tv.tv_sec = sec;
    tv.tv_usec = us;
    settimeofday(&tv, NULL);
    ESP_LOGI(TAG, "System time initialized from hardware timer: %ld.%06ld", sec, us);
    
    ESP_LOGI(TAG, "NTP client initialized (servers: %s, %s)", g_server1, g_server2);
    return ESP_OK;
}

esp_err_t ntp_client_start(void)
{
    if (g_ntp_running) {
        ESP_LOGW(TAG, "NTP client already running");
        return ESP_OK;
    }

    g_ntp_running = true;
    
    BaseType_t ret = xTaskCreate(ntp_sync_task, "ntp_sync_task", 8192, NULL, 5, &g_ntp_task_handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create NTP sync task");
        g_ntp_running = false;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "NTP client started");
    return ESP_OK;
}

esp_err_t ntp_client_stop(void)
{
    g_ntp_running = false;
    
    if (g_ntp_task_handle != NULL) {
        vTaskDelete(g_ntp_task_handle);
        g_ntp_task_handle = NULL;
    }
    
    ESP_LOGI(TAG, "NTP client stopped");
    return ESP_OK;
}

esp_err_t ntp_client_sync_time(void)
{
    uint32_t sec, us;
    
    ESP_LOGI(TAG, "Manual NTP sync...");
    if (ntp_get_precise_time(&sec, &us)) {
        ESP_LOGI(TAG, "Manual NTP sync successful: %lu.%06lu", sec, us);
        
        struct timeval tv;
        tv.tv_sec = sec;
        tv.tv_usec = us;
        settimeofday(&tv, NULL);
        
        hardware_timer_set_time(sec, us);
        
        extern void ntp_storage_save_sync_time(time_t timestamp);
        ntp_storage_save_sync_time(sec);
        return ESP_OK;
    }
    
    ESP_LOGE(TAG, "Manual NTP sync failed");
    return ESP_FAIL;
}

esp_err_t ntp_client_get_time(time_t* sec, uint32_t* us)
{
    return hardware_timer_get_time(sec, us);
}

bool ntp_client_is_running(void)
{
    return g_ntp_running;
}

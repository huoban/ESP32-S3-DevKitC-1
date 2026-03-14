/**
 * @file ntp_server.c
 * @brief NTP 服务器模块实现
 * @details 负责 NTP 时间服务器功能，使用硬件定时器
 */

#include "ntp_server.h"
#include "hardware_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include <string.h>

static const char *TAG = "NTP_SERVER";
#define UNIX_OFFSET 2208988800UL

static bool g_ntp_server_running = false;
static TaskHandle_t g_ntp_server_task_handle = NULL;

static void ntp_server_task(void *arg)
{
    ESP_LOGI(TAG, "NTP server task started");

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Failed to create socket");
        vTaskDelete(NULL);
        return;
    }

    int optval = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(123);

    int ret = bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if (ret < 0) {
        ESP_LOGE(TAG, "Failed to bind socket to port 123");
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "NTP server listening on port 123");

    uint8_t buffer[48];
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);

    while (g_ntp_server_running) {
        int len = recvfrom(sock, buffer, sizeof(buffer), 0, (struct sockaddr *)&client_addr, &addr_len);
        if (len < 0) {
            if (g_ntp_server_running) {
                ESP_LOGE(TAG, "Failed to receive NTP request");
            }
            continue;
        }

        if (len >= 48) {
            uint8_t response[48];
            memset(response, 0, 48);

            uint8_t li_vn_mode = buffer[0];
            uint8_t vn = (li_vn_mode >> 3) & 0x07;
            if (vn < 1 || vn > 4) {
                vn = 4;
            }

            response[0] = (0 << 6) | (vn << 3) | 4;
            response[1] = 2;
            response[2] = buffer[2];
            if (response[2] < 4 || response[2] > 17) {
                response[2] = 4;
            }
            response[3] = 0xFA;
            response[4] = 0x00;
            response[5] = 0x00;
            response[6] = 0x00;
            response[7] = 0x00;
            response[8] = 0x00;
            response[9] = 0x00;
            response[10] = 0x02;
            response[11] = 0x8F;
            response[12] = 'L';
            response[13] = 'O';
            response[14] = 'C';
            response[15] = 'L';

            time_t curr_sec;
            uint32_t curr_us;
            hardware_timer_get_time(&curr_sec, &curr_us);

            uint32_t ntp_sec = curr_sec + UNIX_OFFSET;
            uint32_t ntp_frac = (curr_us * 0xFFFFFFFFUL) / 1000000;
            uint32_t ntp_sec_be = htonl(ntp_sec);
            uint32_t ntp_frac_be = htonl(ntp_frac);

            memcpy(&response[16], &ntp_sec_be, 4);
            memcpy(&response[20], &ntp_frac_be, 4);
            memcpy(&response[24], &buffer[40], 8);
            memcpy(&response[32], &ntp_sec_be, 4);
            memcpy(&response[36], &ntp_frac_be, 4);
            memcpy(&response[40], &ntp_sec_be, 4);
            memcpy(&response[44], &ntp_frac_be, 4);

            int send_len = sendto(sock, response, 48, 0, (struct sockaddr *)&client_addr, addr_len);
            if (send_len == 48) {
                ESP_LOGI(TAG, "NTP response sent to %s (VN=%d, time=%lu.%06lu)", 
                    inet_ntoa(client_addr.sin_addr), vn, curr_sec, curr_us);
            } else {
                ESP_LOGE(TAG, "Failed to send NTP response, len=%d", send_len);
            }
        }
    }

    close(sock);
    ESP_LOGI(TAG, "NTP server task ended");
    vTaskDelete(NULL);
}

esp_err_t ntp_server_init(void) {
    ESP_LOGI(TAG, "NTP server initialized");
    return ESP_OK;
}

esp_err_t ntp_server_start(void) {
    if (g_ntp_server_running) {
        ESP_LOGW(TAG, "NTP server already running");
        return ESP_OK;
    }

    g_ntp_server_running = true;
    
    // 创建 NTP 服务器任务
    BaseType_t ret = xTaskCreate(ntp_server_task, "ntp_server_task", 4096, NULL, 4, &g_ntp_server_task_handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create NTP server task");
        g_ntp_server_running = false;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "NTP server started");
    return ESP_OK;
}

esp_err_t ntp_server_stop(void) {
    g_ntp_server_running = false;
    
    if (g_ntp_server_task_handle != NULL) {
        vTaskDelete(g_ntp_server_task_handle);
        g_ntp_server_task_handle = NULL;
    }
    
    ESP_LOGI(TAG, "NTP server stopped");
    return ESP_OK;
}

esp_err_t ntp_server_set_time(time_t sec, uint32_t us) {
    struct timeval tv = { .tv_sec = sec, .tv_usec = us };
    settimeofday(&tv, NULL);
    return ESP_OK;
}

esp_err_t ntp_server_get_time(time_t* sec, uint32_t* us) {
    if (sec) {
        time(sec);
    }
    if (us) {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        *us = tv.tv_usec;
    }
    return ESP_OK;
}

bool ntp_server_is_running(void) {
    return g_ntp_server_running;
}

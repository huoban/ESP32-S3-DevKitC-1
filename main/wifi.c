/**
 * @file wifi.c
 * @brief WiFi 管理模块实现
 * @details 负责 WiFi 连接管理、AP/STA 模式切换和 WiFi 配置持久化
 */

#include "wifi.h"
#include "config.h"
#include "ntp_client.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "string.h"
#include "esp_mac.h"

static const char *TAG = "WIFI";

// WiFi 事件组
static EventGroupHandle_t s_wifi_event_group;

// WiFi 事件位
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

// 重试次数
static int s_retry_num = 0;
#define MAX_RETRY 5

/**
 * @brief WiFi 连接后 NTP 校时任务 - 等待几秒后执行一次 NTP 校时
 * @param pvParameters 任务参数
 */
static void wifi_ntp_sync_task(void *pvParameters)
{
    ESP_LOGI(TAG, "WiFi connected, waiting 3 seconds before NTP sync...");
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    ESP_LOGI(TAG, "Starting initial NTP sync...");
    esp_err_t err = ntp_client_sync_time();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Initial NTP sync successful");
    } else {
        ESP_LOGW(TAG, "Initial NTP sync failed: %s", esp_err_to_name(err));
    }
    
    vTaskDelete(NULL);
}

/**
 * @brief WiFi 事件处理函数 - 处理 WiFi 连接、断开等事件
 * @param arg 参数
 * @param event_base 事件基础
 * @param event_id 事件 ID
 * @param event_data 事件数据
 */
void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        ESP_LOGI(TAG, "WiFi station started");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < MAX_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "Retry connecting to the AP (%d/%d)", s_retry_num, MAX_RETRY);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            ESP_LOGE(TAG, "Failed to connect to AP");
        }
        ESP_LOGI(TAG, "Connection to the AP failed");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        
        // 创建任务执行一次 NTP 校时
        BaseType_t ret = xTaskCreate(wifi_ntp_sync_task, "wifi_ntp_sync", 4096, NULL, 5, NULL);
        if (ret != pdPASS) {
            ESP_LOGE(TAG, "Failed to create WiFi NTP sync task");
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "Station "MACSTR" joined, AID=%d", MAC2STR(event->mac), event->aid);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "Station "MACSTR" left, AID=%d", MAC2STR(event->mac), event->aid);
    }
}

/**
 * @brief 初始化 WiFi 管理模块 - 初始化 WiFi 驱动和事件处理
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t wifi_init(void)
{
    // 创建事件组
    s_wifi_event_group = xEventGroupCreate();

    // 初始化 TCP/IP 协议栈
    ESP_ERROR_CHECK(esp_netif_init());

    // 创建默认事件循环
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // 初始化 WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // 注册事件处理程序
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    ESP_LOGI(TAG, "WiFi initialized");

    return ESP_OK;
}

/**
 * @brief 扫描 WiFi 网络
 * @param[out] networks 网络列表
 * @param[in] max_count 最大数量
 * @param[out] count 实际数量
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t wifi_scan_networks(wifi_ap_record_t *networks, uint16_t max_count, uint16_t *count)
{
    if (!networks || !count) {
        return ESP_ERR_INVALID_ARG;
    }

    // 确保 WiFi 已初始化
    esp_err_t ret = esp_wifi_scan_start(NULL, true);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start scan: %s", esp_err_to_name(ret));
        return ret;
    }

    // 获取扫描结果
    ret = esp_wifi_scan_get_ap_records(&max_count, networks);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get scan results: %s", esp_err_to_name(ret));
        return ret;
    }

    *count = max_count;
    ESP_LOGI(TAG, "Scan done, found %d networks", max_count);

    return ESP_OK;
}

/**
 * @brief 启动 AP 模式 - 启动 WiFi 热点模式
 * @param ssid AP SSID
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t wifi_start_ap(const char* ssid)
{
    // 创建 AP 网络接口
    esp_netif_create_default_wifi_ap();

    // 配置 AP 参数
    wifi_config_t wifi_config = {
        .ap = {
            .ssid_len = strlen(ssid),
            .channel = 1,
            .max_connection = 4,
            .authmode = WIFI_AUTH_OPEN,
            .pmf_cfg = {
                .required = false,
            },
        },
    };

    strlcpy((char*)wifi_config.ap.ssid, ssid, sizeof(wifi_config.ap.ssid));

    // 设置 WiFi 模式
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));

    // 设置 WiFi 配置
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));

    // 启动 WiFi
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi AP started: SSID=%s", ssid);

    return ESP_OK;
}

/**
 * @brief 启动 STA 模式 - 启动 WiFi 客户端模式
 * @param ssid WiFi SSID
 * @param password WiFi 密码
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t wifi_start_sta(const char* ssid, const char* password)
{
    // 创建 STA 网络接口
    esp_netif_create_default_wifi_sta();

    // 配置 STA 参数
    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };

    strlcpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    strlcpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password));

    // 设置 WiFi 模式
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    // 设置 WiFi 配置
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    // 启动 WiFi
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi STA started: SSID=%s", ssid);

    // 等待连接
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            pdMS_TO_TICKS(10000));

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to AP");
        return ESP_OK;
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "Failed to connect to AP");
        return ESP_FAIL;
    } else {
        ESP_LOGE(TAG, "Connection timeout");
        return ESP_ERR_TIMEOUT;
    }
}

/**
 * @brief 停止 WiFi - 停止 WiFi 功能
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t wifi_stop(void)
{
    ESP_ERROR_CHECK(esp_wifi_stop());
    ESP_LOGI(TAG, "WiFi stopped");
    return ESP_OK;
}

/**
 * @brief 加载 WiFi 配置 - 从 NVS 读取 WiFi 配置
 * @param config WiFi 配置指针
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t wifi_load_config(wifi_config_t_custom* config)
{
    return config_load_wifi(config);
}

/**
 * @brief 保存 WiFi 配置 - 将 WiFi 配置保存到 NVS
 * @param config WiFi 配置指针
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t wifi_save_config(const wifi_config_t_custom* config)
{
    return config_save_wifi(config);
}

/**
 * @brief 获取 WiFi 状态 - 获取当前 WiFi 连接状态
 * @param status WiFi 状态指针
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t wifi_get_status(wifi_status_t* status)
{
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    wifi_mode_t mode;
    esp_err_t err = esp_wifi_get_mode(&mode);
    if (err != ESP_OK) {
        return err;
    }

    status->mode = mode;

    if (mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA) {
        wifi_ap_record_t ap_info;
        err = esp_wifi_sta_get_ap_info(&ap_info);
        if (err == ESP_OK) {
            status->is_connected = true;
            status->rssi = ap_info.rssi;
            strlcpy(status->ssid, (char*)ap_info.ssid, sizeof(status->ssid));

            // 获取 IP 地址
            esp_netif_ip_info_t ip_info;
            esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
            if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
                snprintf(status->ip_address, sizeof(status->ip_address), IPSTR, IP2STR(&ip_info.ip));
            }
        } else {
            status->is_connected = false;
        }
    } else if (mode == WIFI_MODE_AP) {
        status->is_connected = true;
        strlcpy(status->ip_address, "192.168.4.1", sizeof(status->ip_address));
        strlcpy(status->ssid, "AP_MODE", sizeof(status->ssid));
    }

    return ESP_OK;
}

/**
 * @brief 检查 WiFi 是否已连接 - 返回 WiFi 连接状态
 * @return true 已连接，false 未连接
 */
bool wifi_is_connected(void)
{
    EventBits_t bits = xEventGroupGetBits(s_wifi_event_group);
    return (bits & WIFI_CONNECTED_BIT) != 0;
}

/**
 * @brief 获取当前 WiFi 模式 - 返回 WiFi 工作模式
 * @return WiFi 模式
 */
wifi_mode_t wifi_get_mode(void)
{
    wifi_mode_t mode;
    esp_wifi_get_mode(&mode);
    return mode;
}

/**
 * @brief 自动启动 WiFi - 根据配置选择 AP 或 STA 模式
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t wifi_auto_start(void)
{
    wifi_config_t_custom wifi_config;
    esp_err_t err = wifi_load_config(&wifi_config);

    if (err == ESP_OK && wifi_config.is_configured) {
        // 有配置，启动 STA 模式
        ESP_LOGI(TAG, "Starting WiFi STA mode");
        return wifi_start_sta(wifi_config.ssid, wifi_config.password);
    } else {
        // 无配置，启动 AP 模式
        char device_name[64];
        err = config_load_device_name(device_name, sizeof(device_name));
        if (err != ESP_OK) {
            strlcpy(device_name, "ESP32-S3_Printer", sizeof(device_name));
        }

        ESP_LOGI(TAG, "Starting WiFi AP mode");
        return wifi_start_ap(device_name);
    }
}

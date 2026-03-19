/**
 * @file monitor.c
 * @brief 网站健康监控模块实现
 * @details 负责网站状态检测、多线程并发检测和离线/恢复通知
 */

#include "monitor.h"
#include "printer.h"
#include "web_hook.h"
#include "config.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "ntp_client.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>

static const char *TAG = "MONITOR";

// ==================== 全局变量 ====================
static bool g_monitor_running = false;
static bool g_check_task_running = false;
static TaskHandle_t g_main_task_handle = NULL;
static TaskHandle_t g_check_task_handle = NULL;
static SemaphoreHandle_t g_data_mutex = NULL;
static uint32_t g_last_check_time = 0;

// 监控配置（存储在 RAM）
static monitor_site_t g_sites[MONITOR_MAX_SITES];
static size_t g_site_count = 0;
static monitor_state_t g_states[MONITOR_MAX_SITES];
static monitor_system_config_t g_system_config = {
    .global_check_interval = 3,
    .global_notify_interval = 20,
    .global_timeout = 3,
    .global_offline_count = 5,
    .global_enabled = true
};

// ==================== 工具函数 ====================

// 检查状态码是否有效
static bool is_status_code_valid(int status_code, const monitor_site_t* site) {
    // 默认接受 2xx
    if (!site->status_code_1xx && !site->status_code_2xx && 
        !site->status_code_3xx && !site->status_code_4xx && 
        !site->status_code_5xx) {
        return status_code >= 200 && status_code < 300;
    }
    
    // 检查各个区间
    if (site->status_code_1xx && status_code >= 100 && status_code < 200) {
        return true;
    }
    if (site->status_code_2xx && status_code >= 200 && status_code < 300) {
        return true;
    }
    if (site->status_code_3xx && status_code >= 300 && status_code < 400) {
        return true;
    }
    if (site->status_code_4xx && status_code >= 400 && status_code < 500) {
        return true;
    }
    if (site->status_code_5xx && status_code >= 500 && status_code < 600) {
        return true;
    }
    
    return false;
}

// 获取当前系统时间（秒）
static uint32_t get_current_time(void) {
    return esp_timer_get_time() / 1000000;
}

// 获取系统时间（Unix 时间戳），优先使用 NTP 同步后的时间
static time_t get_system_time(void) {
    time_t sec = 0;
    uint32_t us = 0;
    
    // 尝试从 NTP 获取时间
    if (ntp_client_get_time(&sec, &us) == ESP_OK && sec > 0) {
        return sec;
    }
    
    // NTP 未同步，使用标准 time() 函数
    time(&sec);
    return sec > 0 ? sec : 0;
}

// ==================== HTTP 检测函数 ====================

static esp_err_t check_single_site(const monitor_site_t* site, bool* is_online, int* http_status) {
    *is_online = false;
    *http_status = 0;
    
    // 检查是否有打印机在忙碌，若有则跳过
    if (printer_any_busy()) {
        ESP_LOGW(TAG, "Printer is busy, skipping check for: %s", site->name);
        return ESP_OK;
    }
    
    char url[512];
    strlcpy(url, site->url, sizeof(url));
    
    // 如果 URL 没有协议，默认添加 http://
    if (strstr(url, "http://") == NULL && strstr(url, "https://") == NULL) {
        memmove(url + 7, url, strlen(url) + 1);
        memcpy(url, "http://", 7);
    }
    
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,  // 改用 GET 方法，兼容性更好
        .timeout_ms = (site->timeout > 0 ? site->timeout : g_system_config.global_timeout) * 1000,
        .keep_alive_enable = false,
        .skip_cert_common_name_check = true,
        .use_global_ca_store = false,
        .cert_pem = NULL,
        .cert_len = 0,
        .client_cert_pem = NULL,
        .client_cert_len = 0,
        .client_key_pem = NULL,
        .client_key_len = 0,
        .disable_auto_redirect = false,
        .max_redirection_count = 5,
        .buffer_size = 2048,  // 增加缓冲区
        .buffer_size_tx = 1024,
    };
    
    // 设置 User-Agent
    char user_agent[256];
    if (strlen(site->user_agent) > 0) {
        strlcpy(user_agent, site->user_agent, sizeof(user_agent));
    } else if (strlen(g_system_config.global_user_agent) > 0) {
        strlcpy(user_agent, g_system_config.global_user_agent, sizeof(user_agent));
    } else {
        strlcpy(user_agent, "ESP32-Monitor/1.0", sizeof(user_agent));
    }
    config.user_agent = user_agent;
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to create HTTP client for %s", site->name);
        return ESP_FAIL;
    }
    
    // 设置自定义 Host（如果有）
    if (strlen(site->custom_host) > 0) {
        esp_http_client_set_header(client, "Host", site->custom_host);
    }
    
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        *http_status = esp_http_client_get_status_code(client);
        *is_online = is_status_code_valid(*http_status, site);
        ESP_LOGI(TAG, "Site %s: HTTP %d, online=%d", site->name, *http_status, *is_online);
    } else {
        ESP_LOGW(TAG, "Site %s check failed: %s", site->name, esp_err_to_name(err));
    }
    
    esp_http_client_cleanup(client);
    return ESP_OK;
}

// ==================== 检测任务函数 ====================

static void check_sites_task(void* pvParameters) {
    ESP_LOGI(TAG, "Check task started");
    
    if (g_data_mutex == NULL) {
        ESP_LOGE(TAG, "Mutex not initialized");
        vTaskDelete(NULL);
        return;
    }
    
    for (size_t i = 0; i < g_site_count; i++) {
        // 检查是否需要终止（有打印任务）
        if (printer_any_busy()) {
            ESP_LOGW(TAG, "Printer busy, aborting check task");
            break;
        }
        
        // 获取互斥锁访问数据
        if (xSemaphoreTake(g_data_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
            continue;
        }
        
        monitor_site_t site = g_sites[i];
        xSemaphoreGive(g_data_mutex);
        
        // 跳过未启用或暂停的站点
        if (!site.enabled || site.paused) {
            continue;
        }
        
        bool is_online = false;
        int http_status = 0;
        
        ESP_LOGI(TAG, "Checking site: %s", site.name);
        
        // 检测站点
        check_single_site(&site, &is_online, &http_status);
        
        // 更新状态
        if (xSemaphoreTake(g_data_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
            continue;
        }
        
        // 获取系统时间（Unix 时间戳）用于所有时间比较和记录
        time_t system_time = get_system_time();
        
        monitor_state_t* state = &g_states[i];
        uint32_t offline_threshold = (site.offline_count > 0 ? site.offline_count : g_system_config.global_offline_count);
        uint32_t notify_interval = (site.webhook_interval > 0 ? site.webhook_interval : g_system_config.global_notify_interval) * 60;
        
        // 使用系统时间（Unix 时间戳）记录站点最后检测时间
        state->last_check_time = (uint32_t)system_time;
        
        if (is_online) {
            // 在线
            if (!state->is_online) {
                // 从离线恢复
                ESP_LOGI(TAG, "Site %s recovered online", site.name);
                
                if (system_time - state->last_notify_time >= notify_interval || state->last_notify_time == 0) {
                    monitor_send_notification(site.name, true, site.url);
                    // 使用系统时间（Unix 时间戳）记录通知时间
                    state->last_notify_time = (uint32_t)system_time;
                }
                state->offline_notified = false;
            }
            state->is_online = true;
            state->fail_count = 0;
        } else {
            // 离线
            if (state->fail_count < offline_threshold) {
                state->fail_count++;
                ESP_LOGI(TAG, "Site %s fail count: %d/%d", site.name, state->fail_count, offline_threshold);
            }
            
            if (state->fail_count >= offline_threshold) {
                ESP_LOGW(TAG, "Site %s confirmed offline (fail count=%d)", site.name, state->fail_count);
                
                // 每次检测到离线且满足间隔就发送通知
                if (system_time - state->last_notify_time >= notify_interval || state->last_notify_time == 0) {
                    ESP_LOGI(TAG, "Sending offline notification for %s", site.name);
                    monitor_send_notification(site.name, false, site.url);
                    // 使用系统时间（Unix 时间戳）记录通知时间
                    state->last_notify_time = (uint32_t)system_time;
                }
                state->is_online = false;
                state->offline_notified = true;
            }
        }
        
        xSemaphoreGive(g_data_mutex);
        
        // 站点间延迟
        vTaskDelay(pdMS_TO_TICKS(MONITOR_CHECK_DELAY_MS));
    }
    
    // 更新最后检测时间（使用 NTP 同步后的 Unix 时间戳）
    g_last_check_time = (uint32_t)get_system_time();
    
    g_check_task_running = false;
    ESP_LOGI(TAG, "Check task completed");
    vTaskDelete(NULL);
}

// ==================== 主监控任务 ====================

static void monitor_main_task(void* pvParameters) {
    ESP_LOGI(TAG, "Monitor main task started");
    
    while (g_monitor_running) {
        uint32_t check_interval = g_system_config.global_check_interval * 60;
        // 获取系统时间（NTP 同步后的 Unix 时间戳）用于时间比较
        time_t now = get_system_time();
        
        if (now - g_last_check_time >= check_interval && g_system_config.global_enabled) {
            if (!g_check_task_running) {
                ESP_LOGI(TAG, "Starting scheduled check");
                g_check_task_running = true;
                BaseType_t ret = xTaskCreate(
                    check_sites_task,
                    "check_sites",
                    MONITOR_TASK_STACK_SIZE,
                    NULL,
                    MONITOR_TASK_PRIORITY,
                    &g_check_task_handle
                );
                if (ret != pdPASS) {
                    ESP_LOGE(TAG, "Failed to create check task");
                    g_check_task_running = false;
                }
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    
    ESP_LOGI(TAG, "Monitor main task stopped");
    vTaskDelete(NULL);
}

// ==================== 公共接口实现 ====================

// 初始化监控模块
esp_err_t monitor_init(void) {
    ESP_LOGI(TAG, "Initializing monitor module");
    
    // 创建互斥锁
    g_data_mutex = xSemaphoreCreateMutex();
    if (g_data_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_FAIL;
    }
    
    // 清零数据
    memset(g_sites, 0, sizeof(g_sites));
    memset(g_states, 0, sizeof(g_states));
    g_site_count = 0;
    
    // 初始化最后检测时间（使用 NTP 同步后的 Unix 时间戳）
    g_last_check_time = (uint32_t)get_system_time();
    
    // 加载系统配置
    config_load_monitor_system(&g_system_config);
    
    // 加载网站配置
    monitor_load_config();
    
    ESP_LOGI(TAG, "Monitor initialized");
    return ESP_OK;
}

// 启动监控
esp_err_t monitor_start(void) {
    if (g_monitor_running) {
        ESP_LOGW(TAG, "Monitor already running");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Starting monitor");
    g_monitor_running = true;
    
    // 初始化最后检测时间，让倒计时立即开始（使用 NTP 同步后的 Unix 时间戳）
    if (g_last_check_time == 0) {
        g_last_check_time = (uint32_t)get_system_time();
    }
    
    // 创建主监控任务
    BaseType_t ret = xTaskCreate(
        monitor_main_task,
        "monitor_main",
        4096,
        NULL,
        MONITOR_TASK_PRIORITY - 1,
        &g_main_task_handle
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create main task");
        g_monitor_running = false;
        return ESP_FAIL;
    }
    
    return ESP_OK;
}

// 停止监控
esp_err_t monitor_stop(void) {
    if (!g_monitor_running) {
        ESP_LOGW(TAG, "Monitor not running");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Stopping monitor");
    g_monitor_running = false;
    
    // 等待主任务结束
    if (g_main_task_handle != NULL) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        g_main_task_handle = NULL;
    }
    
    return ESP_OK;
}

// 添加监控网站
esp_err_t monitor_add_site(const monitor_site_t* site) {
    if (site == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(g_data_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    if (g_site_count >= MONITOR_MAX_SITES) {
        xSemaphoreGive(g_data_mutex);
        return ESP_ERR_NO_MEM;
    }
    
    // 检查名称是否已存在
    for (size_t i = 0; i < g_site_count; i++) {
        if (strcmp(g_sites[i].name, site->name) == 0) {
            xSemaphoreGive(g_data_mutex);
            return ESP_ERR_INVALID_STATE;
        }
    }
    
    // 复制配置
    memcpy(&g_sites[g_site_count], site, sizeof(monitor_site_t));
    
    // 初始化状态
    memset(&g_states[g_site_count], 0, sizeof(monitor_state_t));
    strlcpy(g_states[g_site_count].name, site->name, sizeof(g_states[g_site_count].name));
    g_states[g_site_count].is_online = false;
    
    g_site_count++;
    
    xSemaphoreGive(g_data_mutex);
    
    monitor_save_config();
    ESP_LOGI(TAG, "Added site: %s", site->name);
    
    return ESP_OK;
}

// 移除监控网站
esp_err_t monitor_remove_site(const char* name) {
    if (name == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(g_data_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    int found_index = -1;
    for (size_t i = 0; i < g_site_count; i++) {
        if (strcmp(g_sites[i].name, name) == 0) {
            found_index = i;
            break;
        }
    }
    
    if (found_index == -1) {
        xSemaphoreGive(g_data_mutex);
        return ESP_ERR_NOT_FOUND;
    }
    
    // 移除并移动后续元素
    for (size_t i = found_index; i < g_site_count - 1; i++) {
        memcpy(&g_sites[i], &g_sites[i + 1], sizeof(monitor_site_t));
        memcpy(&g_states[i], &g_states[i + 1], sizeof(monitor_state_t));
    }
    
    g_site_count--;
    memset(&g_sites[g_site_count], 0, sizeof(monitor_site_t));
    memset(&g_states[g_site_count], 0, sizeof(monitor_state_t));
    
    xSemaphoreGive(g_data_mutex);
    
    monitor_save_config();
    ESP_LOGI(TAG, "Removed site: %s", name);
    
    return ESP_OK;
}

// 更新监控网站
esp_err_t monitor_update_site(const monitor_site_t* site) {
    if (site == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(g_data_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    int found_index = -1;
    for (size_t i = 0; i < g_site_count; i++) {
        if (strcmp(g_sites[i].name, site->name) == 0) {
            found_index = i;
            break;
        }
    }
    
    if (found_index == -1) {
        xSemaphoreGive(g_data_mutex);
        return ESP_ERR_NOT_FOUND;
    }
    
    memcpy(&g_sites[found_index], site, sizeof(monitor_site_t));
    
    xSemaphoreGive(g_data_mutex);
    
    monitor_save_config();
    ESP_LOGI(TAG, "Updated site: %s", site->name);
    
    return ESP_OK;
}

// 获取单个网站配置
esp_err_t monitor_get_site(const char* name, monitor_site_t* site) {
    if (name == NULL || site == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(g_data_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    int found_index = -1;
    for (size_t i = 0; i < g_site_count; i++) {
        if (strcmp(g_sites[i].name, name) == 0) {
            found_index = i;
            break;
        }
    }
    
    if (found_index == -1) {
        xSemaphoreGive(g_data_mutex);
        return ESP_ERR_NOT_FOUND;
    }
    
    memcpy(site, &g_sites[found_index], sizeof(monitor_site_t));
    
    xSemaphoreGive(g_data_mutex);
    
    return ESP_OK;
}

// 获取所有网站配置
esp_err_t monitor_get_all_sites(monitor_site_t* sites, size_t* count) {
    if (sites == NULL || count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (g_data_mutex == NULL) {
        *count = 0;
        return ESP_ERR_INVALID_STATE;
    }
    
    if (xSemaphoreTake(g_data_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    *count = g_site_count;
    for (size_t i = 0; i < g_site_count; i++) {
        memcpy(&sites[i], &g_sites[i], sizeof(monitor_site_t));
    }
    
    xSemaphoreGive(g_data_mutex);
    
    return ESP_OK;
}

// 获取所有网站状态
esp_err_t monitor_get_all_states(monitor_state_t* states, size_t* count) {
    if (states == NULL || count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (g_data_mutex == NULL) {
        *count = 0;
        return ESP_ERR_INVALID_STATE;
    }
    
    if (xSemaphoreTake(g_data_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    *count = g_site_count;
    for (size_t i = 0; i < g_site_count; i++) {
        memcpy(&states[i], &g_states[i], sizeof(monitor_state_t));
    }
    
    xSemaphoreGive(g_data_mutex);
    
    return ESP_OK;
}

// 立即检测所有网站
esp_err_t monitor_check_all_sites(void) {
    if (g_check_task_running) {
        ESP_LOGW(TAG, "Check task already running");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Starting manual check");
    g_check_task_running = true;
    
    // 手动开始检测时也更新最后检测时间，确保倒计时正确（使用 NTP 同步后的 Unix 时间戳）
    g_last_check_time = (uint32_t)get_system_time();
    
    BaseType_t ret = xTaskCreate(
        check_sites_task,
        "check_sites",
        MONITOR_TASK_STACK_SIZE,
        NULL,
        MONITOR_TASK_PRIORITY,
        &g_check_task_handle
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create check task");
        g_check_task_running = false;
        return ESP_FAIL;
    }
    
    return ESP_OK;
}

// 检测单个网站
esp_err_t monitor_check_site(const char* name) {
    if (name == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    monitor_site_t site;
    esp_err_t err = monitor_get_site(name, &site);
    if (err != ESP_OK) {
        return err;
    }
    
    bool is_online = false;
    int http_status = 0;
    
    return check_single_site(&site, &is_online, &http_status);
}

// 发送通知
esp_err_t monitor_send_notification(const char* site_name, bool is_online, const char* site_url) {
    ESP_LOGI(TAG, "Sending notification: %s is %s", site_name, is_online ? "online" : "offline");
    
    char title[128];
    char content[512];
    char time_str[64];
    
    // 获取当前时间（使用 NTP 同步后的系统时间）
    time_t now = get_system_time();
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &timeinfo);
    
    if (is_online) {
        snprintf(title, sizeof(title), "上线%s", site_name);
        snprintf(content, sizeof(content), "%s\n离线时间：%s", site_url, time_str);
    } else {
        snprintf(title, sizeof(title), "离线%s", site_name);
        snprintf(content, sizeof(content), "%s\n离线时间：%s", site_url, time_str);
    }
    
    ESP_LOGI(TAG, "WebHook: Title='%s', Content='%s'", title, content);
    
    // 调用 WebHook 发送通知（异步，不阻塞）
    esp_err_t err = web_hook_start_send_task(title, content);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send WebHook notification: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "WebHook notification task started successfully");
    }
    
    return err;
}

// 发送测试通知
esp_err_t monitor_send_test_notification(void) {
    ESP_LOGI(TAG, "Sending test notification");
    
    const char *title = "测试通知";
    const char *content = "这是一条来自监控模块的测试消息";
    
    // 调用 WebHook 发送测试通知
    esp_err_t err = web_hook_start_send_task(title, content);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send test WebHook notification: %s", esp_err_to_name(err));
    }
    
    return err;
}

// 加载监控配置
esp_err_t monitor_load_config(void) {
    monitor_site_t *sites = (monitor_site_t *)heap_caps_malloc(
        MONITOR_MAX_SITES * sizeof(monitor_site_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );
    
    if (sites == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for monitor sites");
        return ESP_ERR_NO_MEM;
    }
    
    size_t count = 0;
    
    esp_err_t err = config_load_monitor(sites, &count);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No existing monitor config found, using defaults");
        heap_caps_free(sites);
        return ESP_OK;
    }
    
    if (xSemaphoreTake(g_data_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        heap_caps_free(sites);
        return ESP_ERR_TIMEOUT;
    }
    
    g_site_count = count;
    for (size_t i = 0; i < count; i++) {
        memcpy(&g_sites[i], &sites[i], sizeof(monitor_site_t));
        memset(&g_states[i], 0, sizeof(monitor_state_t));
        strlcpy(g_states[i].name, sites[i].name, sizeof(g_states[i].name));
    }
    
    xSemaphoreGive(g_data_mutex);
    heap_caps_free(sites);
    
    ESP_LOGI(TAG, "Loaded %zu monitor sites", count);
    return ESP_OK;
}

// 保存监控配置
esp_err_t monitor_save_config(void) {
    if (xSemaphoreTake(g_data_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    esp_err_t err = config_save_monitor(g_sites, g_site_count);
    
    xSemaphoreGive(g_data_mutex);
    
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Saved %zu monitor sites", g_site_count);
    }
    
    return err;
}

// 获取系统配置
esp_err_t monitor_get_system_config(monitor_system_config_t* config) {
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memcpy(config, &g_system_config, sizeof(monitor_system_config_t));
    return ESP_OK;
}

// 设置系统配置
esp_err_t monitor_set_system_config(const monitor_system_config_t* config) {
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memcpy(&g_system_config, config, sizeof(monitor_system_config_t));
    
    // 保存到 NVS
    config_save_monitor_system(&g_system_config);
    
    ESP_LOGI(TAG, "System config updated");
    return ESP_OK;
}

// 检查监控是否运行
bool monitor_is_running(void) {
    return g_monitor_running;
}

// 获取网站检测状态
esp_err_t monitor_get_site_state(const char* name, monitor_state_t* state) {
    if (name == NULL || state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(g_data_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    int found_index = -1;
    for (size_t i = 0; i < g_site_count; i++) {
        if (strcmp(g_states[i].name, name) == 0) {
            found_index = i;
            break;
        }
    }
    
    if (found_index == -1) {
        xSemaphoreGive(g_data_mutex);
        return ESP_ERR_NOT_FOUND;
    }
    
    memcpy(state, &g_states[found_index], sizeof(monitor_state_t));
    
    xSemaphoreGive(g_data_mutex);
    
    return ESP_OK;
}

// 获取最后检测时间（返回 Unix 时间戳）
uint32_t monitor_get_last_check_time(void) {
    return g_last_check_time;
}

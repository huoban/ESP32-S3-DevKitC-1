/**
 * @file web_server.c
 * @brief Web 管理界面模块实现
 * @details 负责 HTTP 请求处理、Web 资源服务和 API 接口处理
 */

#include "web_server.h"
#include "config.h"
#include "wifi.h"
#include "printer.h"
#include "monitor.h"
#include "web_hook.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "cJSON.h"
#include "api_controller.h"
#include "ntp_storage.h"
#include "nvs_manager.h"
#include "web_resources.h"
#include "esp_timer.h"
#include "esp_https_ota.h"
#include "esp_ota_ops.h"
#include "freertos/task.h"
#include <string.h>
#include <time.h>
#include "esp_heap_caps.h"
#include "driver/temperature_sensor.h"
#include "temp_sensor.h"

static const char *TAG = "WEB_SERVER";

static esp_err_t ota_url_handler(httpd_req_t* req);

static esp_err_t web_server_handle_monitor_site_add(httpd_req_t* req);
static esp_err_t web_server_handle_monitor_site_update(httpd_req_t* req);
static esp_err_t web_server_handle_monitor_site_delete(httpd_req_t* req);
static esp_err_t web_server_handle_monitor_system_config(httpd_req_t* req);
static esp_err_t web_server_handle_monitor_toggle(httpd_req_t* req);
static esp_err_t web_server_handle_monitor_check_all(httpd_req_t* req);

static httpd_handle_t g_server = NULL;
static int64_t g_system_start_time = 0;

/**
 * @brief 获取系统运行时间（秒）
 */
static uint32_t get_system_uptime(void)
{
    if (g_system_start_time == 0) {
        g_system_start_time = esp_timer_get_time();
    }
    return (esp_timer_get_time() - g_system_start_time) / 1000000;
}

/**
 * @brief 获取芯片温度
 */
static float get_chip_temperature(void)
{
    if (temp_sensor == NULL) {
        return 0.0f;
    }
    
    float temp_celsius = 0;
    esp_err_t ret = temperature_sensor_get_celsius(temp_sensor, &temp_celsius);
    if (ret == ESP_OK) {
        return temp_celsius;
    }
    return 0.0f;
}

/**
 * @brief 查询系统状态处理器 - GET /api/status
 * @param req HTTP 请求对象
 * @return ESP_OK 成功，其他值失败
 */
#include "esp_sntp.h"

esp_err_t web_server_handle_get_status(httpd_req_t* req)
{
    httpd_resp_set_type(req, "application/json");

    cJSON *root = cJSON_CreateObject();
    cJSON *wifi = cJSON_CreateObject();
    cJSON *printers = cJSON_CreateArray();
    cJSON *system = cJSON_CreateObject();

    // WiFi 状态
    wifi_status_t wifi_status;
    wifi_get_status(&wifi_status);
    cJSON_AddStringToObject(wifi, "mode", (wifi_status.mode == WIFI_MODE_AP) ? "AP" : "STA");
    cJSON_AddBoolToObject(wifi, "connected", wifi_status.is_connected);
    cJSON_AddStringToObject(wifi, "ip", wifi_status.ip_address);
    cJSON_AddStringToObject(wifi, "ssid", wifi_status.ssid);
    cJSON_AddNumberToObject(wifi, "rssi", wifi_status.rssi);
    cJSON_AddItemToObject(root, "wifi", wifi);

    // 打印机状态
    for (int i = 0; i < 4; i++) {
        usb_printer_t printer;
        if (printer_get_info(i, &printer) == ESP_OK) {
            cJSON *p = cJSON_CreateObject();
            cJSON_AddNumberToObject(p, "instance", i);
            cJSON_AddNumberToObject(p, "status", printer.status);
            cJSON_AddStringToObject(p, "name", printer.device_name);
            cJSON_AddStringToObject(p, "serial_number", printer.serial_number);
            cJSON_AddBoolToObject(p, "busy", printer.is_busy);
            cJSON_AddNumberToObject(p, "last_active_time", printer.last_active_time);
            cJSON_AddItemToArray(printers, p);
        }
    }
    cJSON_AddItemToObject(root, "printers", printers);

    // 系统状态
    cJSON_AddStringToObject(system, "device_name", "ESP32-S3_Printer");
    cJSON_AddNumberToObject(system, "uptime", get_system_uptime());
    cJSON_AddNumberToObject(system, "temperature", get_chip_temperature());
    cJSON_AddNumberToObject(system, "free_memory", esp_get_free_heap_size());
    
    // PSRAM 信息
    cJSON_AddNumberToObject(system, "psram_total", (double)heap_caps_get_total_size(MALLOC_CAP_SPIRAM));
    cJSON_AddNumberToObject(system, "psram_used", (double)(heap_caps_get_total_size(MALLOC_CAP_SPIRAM) - heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
    
    // 获取系统时间 (Unix 时间戳)
    time_t now;
    time(&now);
    cJSON_AddNumberToObject(system, "system_time", (double)now);
    
    // 获取 NTP 最后同步时间 (从 PSRAM 读取)
    time_t ntp_sync_time = ntp_storage_get_sync_time();
    cJSON_AddNumberToObject(system, "ntp_last_sync", (double)ntp_sync_time);
    
    cJSON_AddItemToObject(root, "system", system);

    // 监控相关数据
    cJSON *monitor_sites = cJSON_CreateArray();
    cJSON *monitor_states = cJSON_CreateArray();
    monitor_site_t *sites = (monitor_site_t *)heap_caps_malloc(
        MONITOR_MAX_SITES * sizeof(monitor_site_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );
    monitor_state_t *states = (monitor_state_t *)heap_caps_malloc(
        MONITOR_MAX_SITES * sizeof(monitor_state_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );
    
    if (sites == NULL || states == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for monitor data");
        if (sites != NULL) heap_caps_free(sites);
        if (states != NULL) heap_caps_free(states);
        cJSON_Delete(root);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    
    size_t count = 0;
    
    if (monitor_get_all_sites(sites, &count) == ESP_OK) {
        for (size_t i = 0; i < count; i++) {
            cJSON *site_obj = cJSON_CreateObject();
            cJSON_AddStringToObject(site_obj, "name", sites[i].name);
            cJSON_AddStringToObject(site_obj, "url", sites[i].url);
            cJSON_AddStringToObject(site_obj, "custom_host", sites[i].custom_host);
            cJSON_AddNumberToObject(site_obj, "interval", sites[i].interval);
            cJSON_AddNumberToObject(site_obj, "timeout", sites[i].timeout);
            cJSON_AddNumberToObject(site_obj, "offline_count", sites[i].offline_count);
            cJSON_AddBoolToObject(site_obj, "status_code_1xx", sites[i].status_code_1xx);
            cJSON_AddBoolToObject(site_obj, "status_code_2xx", sites[i].status_code_2xx);
            cJSON_AddBoolToObject(site_obj, "status_code_3xx", sites[i].status_code_3xx);
            cJSON_AddBoolToObject(site_obj, "status_code_4xx", sites[i].status_code_4xx);
            cJSON_AddBoolToObject(site_obj, "status_code_5xx", sites[i].status_code_5xx);
            cJSON_AddNumberToObject(site_obj, "webhook_interval", sites[i].webhook_interval);
            cJSON_AddBoolToObject(site_obj, "enabled", sites[i].enabled);
            cJSON_AddBoolToObject(site_obj, "paused", sites[i].paused);
            cJSON_AddItemToArray(monitor_sites, site_obj);
        }
    }
    
    if (monitor_get_all_states(states, &count) == ESP_OK) {
        for (size_t i = 0; i < count; i++) {
            cJSON *state_obj = cJSON_CreateObject();
            cJSON_AddStringToObject(state_obj, "name", states[i].name);
            cJSON_AddBoolToObject(state_obj, "is_online", states[i].is_online);
            cJSON_AddNumberToObject(state_obj, "fail_count", states[i].fail_count);
            cJSON_AddNumberToObject(state_obj, "last_check_time", states[i].last_check_time);
            cJSON_AddNumberToObject(state_obj, "last_notify_time", states[i].last_notify_time);
            cJSON_AddItemToArray(monitor_states, state_obj);
        }
    }
    
    cJSON_AddItemToObject(root, "monitor_sites", monitor_sites);
    cJSON_AddItemToObject(root, "monitor_states", monitor_states);
    
    // 监控系统配置
    cJSON *monitor_system_config = cJSON_CreateObject();
    monitor_system_config_t sys_config;
    if (monitor_get_system_config(&sys_config) == ESP_OK) {
        cJSON_AddNumberToObject(monitor_system_config, "global_check_interval", sys_config.global_check_interval);
        cJSON_AddNumberToObject(monitor_system_config, "global_notify_interval", sys_config.global_notify_interval);
        cJSON_AddNumberToObject(monitor_system_config, "global_timeout", sys_config.global_timeout);
        cJSON_AddNumberToObject(monitor_system_config, "global_offline_count", sys_config.global_offline_count);
        cJSON_AddStringToObject(monitor_system_config, "global_user_agent", sys_config.global_user_agent);
        cJSON_AddBoolToObject(monitor_system_config, "global_enabled", sys_config.global_enabled);
    }
    cJSON_AddItemToObject(root, "monitor_system_config", monitor_system_config);
    
    // 监控运行状态
    cJSON_AddBoolToObject(root, "monitor_running", monitor_is_running());
    cJSON_AddNumberToObject(root, "monitor_last_check", monitor_get_last_check_time());

    char *json_str = cJSON_PrintUnformatted(root);
    if (json_str != NULL) {
        httpd_resp_send(req, json_str, strlen(json_str));
        cJSON_free(json_str);
    } else {
        httpd_resp_send_500(req);
    }

    cJSON_Delete(root);
    heap_caps_free(sites);
    heap_caps_free(states);

    return ESP_OK;
}

/**
 * @brief 保存 WiFi 配置处理器 - POST /api/config/wifi
 * @param req HTTP 请求对象
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t web_server_handle_post_config_wifi(httpd_req_t* req)
{
    // 调用新的 API 处理函数，支持静态 IP
    return save_wifi_config_api_handler(req);
}

/**
 * @brief 静态文件处理器 - 处理 HTML、CSS、JS 文件
 * @param req HTTP 请求对象
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t web_server_handle_static_file(httpd_req_t* req)
{
    const char* uri = req->uri;
    char filepath[512];
    
    // 去掉查询字符串
    char* query = strchr(uri, '?');
    if (query) {
        size_t path_len = query - uri;
        if (path_len >= sizeof(filepath)) path_len = sizeof(filepath) - 1;
        strncpy(filepath, uri, path_len);
        filepath[path_len] = '\0';
        uri = filepath;
    }
    
    ESP_LOGI(TAG, "Serving file: %s", uri);
    
    // 安全检查：防止路径遍历攻击
    if (strstr(uri, "..") != NULL) {
        ESP_LOGE(TAG, "Path traversal attempt detected: %s", uri);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid path");
        return ESP_FAIL;
    }
    
    // 构建文件路径 - 直接使用 URI，不添加 web/ 前缀
    if (strcmp(uri, "/") == 0) {
        strlcpy(filepath, "/index.html", sizeof(filepath));
    } else {
        // 直接使用 URI
        strlcpy(filepath, uri, sizeof(filepath));
    }
    
    ESP_LOGI(TAG, "Looking for resource: %s", filepath);
    
    // 从 PSRAM 获取资源
    const web_resource_t* resource = web_resources_get(filepath);
    if (resource == NULL) {
        ESP_LOGW(TAG, "File not found in PSRAM: %s", filepath);
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Found resource: %s (%zu bytes)", filepath, resource->size);
    
    // 根据文件扩展名设置 MIME 类型
    const char* content_type = "text/plain";
    if (strstr(filepath, ".html")) {
        content_type = "text/html";
    } else if (strstr(filepath, ".css")) {
        content_type = "text/css";
    } else if (strstr(filepath, ".js")) {
        content_type = "application/javascript";
    } else if (strstr(filepath, ".png")) {
        content_type = "image/png";
    } else if (strstr(filepath, ".jpg") || strstr(filepath, ".jpeg")) {
        content_type = "image/jpeg";
    }
    
    httpd_resp_set_type(req, content_type);
    
    // 添加缓存控制头 - 防止浏览器缓存
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    httpd_resp_set_hdr(req, "Expires", "0");
    
    // 直接从 PSRAM 发送数据
    esp_err_t result = httpd_resp_send(req, (const char*)resource->data, resource->size);
    
    return result;
}

esp_err_t web_server_init(void) {
    // Web 资源已在 main.c 中通过 web_resources_init() 初始化并复制到 PSRAM
    // 这里不需要再挂载 SPIFFS
    
    ESP_LOGI(TAG, "Web server initialized (using PSRAM resources)");
    return ESP_OK;
}

// 404 错误处理器 - 处理所有未匹配的 URI 请求
static esp_err_t web_server_handle_404(httpd_req_t* req, httpd_err_code_t error_code)
{
    (void)error_code;
    ESP_LOGI(TAG, "404 handler called for: %s", req->uri);
    web_server_handle_static_file(req);
    // 返回 ESP_OK 表示我们已经处理了这个请求
    return ESP_OK;
}

esp_err_t web_server_start(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.max_uri_handlers = 64; // 增加最大 URI 处理程序数量
    config.stack_size = 8192; // 增加任务栈大小，防止溢出

    if (httpd_start(&g_server, &config) == ESP_OK) {
        ESP_LOGI(TAG, "Web server started");

        // 注册 URI 处理器
        httpd_uri_t status_uri = {
            .uri = "/api/status",
            .method = HTTP_GET,
            .handler = web_server_handle_get_status,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(g_server, &status_uri);

        httpd_uri_t wifi_uri = {
            .uri = "/api/config/wifi",
            .method = HTTP_POST,
            .handler = web_server_handle_post_config_wifi,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(g_server, &wifi_uri);

        // 注册监控配置接口
        httpd_uri_t monitor_uri = {
            .uri = "/api/config/monitor",
            .method = HTTP_POST,
            .handler = web_server_handle_post_config_monitor,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(g_server, &monitor_uri);

        // 注册企业微信配置接口
        httpd_uri_t wechat_uri = {
            .uri = "/api/config/wechat",
            .method = HTTP_POST,
            .handler = web_server_handle_post_config_wechat,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(g_server, &wechat_uri);

        // 注册完整 WebHook 配置接口（POST - 保存）
        httpd_uri_t webhook_post_uri = {
            .uri = "/api/config/webhook",
            .method = HTTP_POST,
            .handler = web_server_handle_post_config_webhook,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(g_server, &webhook_post_uri);

        // 注册获取 WebHook 配置接口（GET - 读取）
        httpd_uri_t webhook_get_uri = {
            .uri = "/api/config/webhook",
            .method = HTTP_GET,
            .handler = get_webhook_config_api_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(g_server, &webhook_get_uri);

        // 注册恢复出厂设置接口
        httpd_uri_t reset_uri = {
            .uri = "/api/system/reset",
            .method = HTTP_POST,
            .handler = web_server_handle_post_reset,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(g_server, &reset_uri);

        // 注册重启系统接口
        httpd_uri_t reboot_uri = {
            .uri = "/api/system/reboot",
            .method = HTTP_POST,
            .handler = web_server_handle_post_reboot,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(g_server, &reboot_uri);

        // 注册测试通知接口
        httpd_uri_t test_uri = {
            .uri = "/api/monitor/test",
            .method = HTTP_POST,
            .handler = web_server_handle_post_monitor_test,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(g_server, &test_uri);

        // 注册 WebHook 通用钩子接口 API
        httpd_uri_t webhook_hook_uri = {
            .uri = "/api/webhook/hook",
            .method = HTTP_POST,
            .handler = web_server_handle_webhook_hook,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(g_server, &webhook_hook_uri);
        
        // 注册测试SMTP配置API
        httpd_uri_t webhook_test_smtp_uri = {
            .uri = "/api/webhook/test/smtp",
            .method = HTTP_POST,
            .handler = web_server_handle_webhook_test_smtp,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(g_server, &webhook_test_smtp_uri);
        
        // 注册测试企业微信API
        httpd_uri_t webhook_test_wechat_uri = {
            .uri = "/api/webhook/test/wechat",
            .method = HTTP_POST,
            .handler = web_server_handle_webhook_test_wechat,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(g_server, &webhook_test_wechat_uri);
        
        // 注册测试自定义WebHook API
        httpd_uri_t webhook_test_custom_uri = {
            .uri = "/api/webhook/test/custom",
            .method = HTTP_POST,
            .handler = web_server_handle_webhook_test_custom,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(g_server, &webhook_test_custom_uri);
        
        // 注册测试所有WebHook API
        httpd_uri_t webhook_test_all_uri = {
            .uri = "/api/webhook/test/all",
            .method = HTTP_POST,
            .handler = web_server_handle_webhook_test_all,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(g_server, &webhook_test_all_uri);

        httpd_uri_t ota_uri = {
            .uri = "/api/system/ota",
            .method = HTTP_POST,
            .handler = ota_url_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(g_server, &ota_uri);

        httpd_uri_t root_uri = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = web_server_handle_static_file,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(g_server, &root_uri);

        // 注册打印机绑定 API 处理器
        httpd_uri_t bind_uri = {
            .uri = "/bind",
            .method = HTTP_POST,
            .handler = bind_printer_api_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(g_server, &bind_uri);

        httpd_uri_t unbind_uri = {
            .uri = "/unbind",
            .method = HTTP_POST,
            .handler = unbind_printer_api_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(g_server, &unbind_uri);

        // 注册绑定列表查询 API
        httpd_uri_t bindings_uri = {
            .uri = "/api/bindings",
            .method = HTTP_GET,
            .handler = get_bindings_api_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(g_server, &bindings_uri);

        // 注册 WiFi 扫描 API
        httpd_uri_t wifi_scan_uri = {
            .uri = "/api/wifi/scan",
            .method = HTTP_GET,
            .handler = wifi_scan_api_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(g_server, &wifi_scan_uri);

        // 注册监控站点添加 API
        httpd_uri_t monitor_site_add_uri = {
            .uri = "/api/monitor/site/add",
            .method = HTTP_POST,
            .handler = web_server_handle_monitor_site_add,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(g_server, &monitor_site_add_uri);
        
        // 注册监控站点更新 API
        httpd_uri_t monitor_site_update_uri = {
            .uri = "/api/monitor/site/update",
            .method = HTTP_POST,
            .handler = web_server_handle_monitor_site_update,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(g_server, &monitor_site_update_uri);
        
        // 注册监控站点删除 API
        httpd_uri_t monitor_site_delete_uri = {
            .uri = "/api/monitor/site/delete",
            .method = HTTP_POST,
            .handler = web_server_handle_monitor_site_delete,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(g_server, &monitor_site_delete_uri);
        
        // 注册监控系统配置 API (两种路径兼容)
        httpd_uri_t monitor_system_config_uri = {
            .uri = "/api/monitor/system/config",
            .method = HTTP_POST,
            .handler = web_server_handle_monitor_system_config,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(g_server, &monitor_system_config_uri);
        
        // 兼容前端调用的另一种路径
        httpd_uri_t monitor_system_config_uri2 = {
            .uri = "/api/monitor/system_config",
            .method = HTTP_POST,
            .handler = web_server_handle_monitor_system_config,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(g_server, &monitor_system_config_uri2);
        
        // 注册监控启停 API
        httpd_uri_t monitor_toggle_uri = {
            .uri = "/api/monitor/toggle",
            .method = HTTP_POST,
            .handler = web_server_handle_monitor_toggle,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(g_server, &monitor_toggle_uri);
        
        // 注册立即检测 API
        httpd_uri_t monitor_check_all_uri = {
            .uri = "/api/monitor/check_all",
            .method = HTTP_POST,
            .handler = web_server_handle_monitor_check_all,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(g_server, &monitor_check_all_uri);

        // 注册 404 错误处理器 - 处理所有未匹配的 URI 请求
        httpd_register_err_handler(g_server, HTTPD_404_NOT_FOUND, web_server_handle_404);

        return ESP_OK;
    }

    return ESP_FAIL;
}

esp_err_t web_server_stop(void) {
    if (g_server) {
        httpd_stop(g_server);
        g_server = NULL;
    }
    
    // 不需要卸载 SPIFFS（已改用 PSRAM 存储 Web 资源）
    
    return ESP_OK;
}

// 以下函数已在其他地方实现，此处保留声明以便未来扩展
// esp_err_t web_server_register_handler(const char* uri, httpd_method_t method, esp_err_t (*handler)(httpd_req_t* req));
// esp_err_t web_server_serve_file(httpd_req_t* req, const char* filepath);
esp_err_t web_server_handle_post_config_monitor(httpd_req_t* req)
{
    char buf[1024];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        ESP_LOGE(TAG, "Failed to receive request data");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    buf[ret] = '\0';

    // 解析监控配置
    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    // 保存监控配置
    monitor_site_t sites[10];
    size_t count = 0;
    
    cJSON *sites_array = cJSON_GetObjectItem(root, "sites");
    if (sites_array && cJSON_IsArray(sites_array)) {
        int array_size = cJSON_GetArraySize(sites_array);
        for (int i = 0; i < array_size && i < 10; i++) {
            cJSON *site = cJSON_GetArrayItem(sites_array, i);
            if (site) {
                cJSON *name = cJSON_GetObjectItem(site, "name");
                cJSON *url = cJSON_GetObjectItem(site, "url");
                
                if (name && url) {
                    strlcpy(sites[count].name, cJSON_GetStringValue(name), sizeof(sites[count].name));
                    strlcpy(sites[count].url, cJSON_GetStringValue(url), sizeof(sites[count].url));
                    
                    // 其他参数
                    cJSON *interval = cJSON_GetObjectItem(site, "interval");
                    if (interval) sites[count].interval = cJSON_GetNumberValue(interval);
                    
                    cJSON *timeout = cJSON_GetObjectItem(site, "timeout");
                    if (timeout) sites[count].timeout = cJSON_GetNumberValue(timeout);
                    
                    cJSON *offline_count = cJSON_GetObjectItem(site, "offline_count");
                    if (offline_count) sites[count].offline_count = cJSON_GetNumberValue(offline_count);
                    
                    count++;
                }
            }
        }
    }

    esp_err_t err = config_save_monitor(sites, count);
    cJSON_Delete(root);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save monitor config: %s", esp_err_to_name(err));
        httpd_resp_send_500(req);
        return err;
    }

    httpd_resp_set_type(req, "application/json");
    const char *response = "{\"success\":true,\"message\":\"Monitor config saved\"}";
    httpd_resp_send(req, response, strlen(response));

    return ESP_OK;
}

/**
 * @brief 保存完整 WebHook 配置处理器 - POST /api/config/webhook
 * @param req HTTP 请求对象
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t web_server_handle_post_config_webhook(httpd_req_t* req)
{
    char buf[2048];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        ESP_LOGE(TAG, "Failed to receive request data");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    buf[ret] = '\0';
    ESP_LOGI(TAG, "Webhook config: %s", buf);

    // 解析 JSON
    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    webhook_config_t config = {0};

    // 解析 SMTP 配置
    cJSON *smtp = cJSON_GetObjectItem(root, "smtp");
    if (smtp) {
        cJSON *enabled = cJSON_GetObjectItem(smtp, "enabled");
        cJSON *smtp_server = cJSON_GetObjectItem(smtp, "smtp_server");
        cJSON *smtp_port = cJSON_GetObjectItem(smtp, "smtp_port");
        cJSON *username = cJSON_GetObjectItem(smtp, "username");
        cJSON *password = cJSON_GetObjectItem(smtp, "password");
        cJSON *from_email = cJSON_GetObjectItem(smtp, "from_email");
        cJSON *to_email = cJSON_GetObjectItem(smtp, "to_email");

        config.smtp.enabled = (enabled && cJSON_IsTrue(enabled));
        if (smtp_server) strlcpy(config.smtp.smtp_server, cJSON_GetStringValue(smtp_server), sizeof(config.smtp.smtp_server));
        if (smtp_port) config.smtp.smtp_port = (uint16_t)smtp_port->valueint;
        if (username) strlcpy(config.smtp.username, cJSON_GetStringValue(username), sizeof(config.smtp.username));
        if (password) strlcpy(config.smtp.password, cJSON_GetStringValue(password), sizeof(config.smtp.password));
        if (from_email) strlcpy(config.smtp.from_email, cJSON_GetStringValue(from_email), sizeof(config.smtp.from_email));
        if (to_email) strlcpy(config.smtp.to_email, cJSON_GetStringValue(to_email), sizeof(config.smtp.to_email));
    }

    // 解析企业微信配置
    cJSON *wechat = cJSON_GetObjectItem(root, "wechat");
    if (wechat) {
        cJSON *enabled = cJSON_GetObjectItem(wechat, "enabled");
        cJSON *corpid = cJSON_GetObjectItem(wechat, "corpid");
        cJSON *corpsecret = cJSON_GetObjectItem(wechat, "corpsecret");
        cJSON *agentid = cJSON_GetObjectItem(wechat, "agentid");
        cJSON *touser = cJSON_GetObjectItem(wechat, "touser");

        config.wechat.enabled = (enabled && cJSON_IsTrue(enabled));
        if (corpid) strlcpy(config.wechat.corpid, cJSON_GetStringValue(corpid), sizeof(config.wechat.corpid));
        if (corpsecret) strlcpy(config.wechat.corpsecret, cJSON_GetStringValue(corpsecret), sizeof(config.wechat.corpsecret));
        if (agentid) strlcpy(config.wechat.agentid, cJSON_GetStringValue(agentid), sizeof(config.wechat.agentid));
        if (touser) strlcpy(config.wechat.touser, cJSON_GetStringValue(touser), sizeof(config.wechat.touser));
    }

    // 解析自定义 WebHook 配置
    cJSON *custom = cJSON_GetObjectItem(root, "custom");
    if (custom) {
        cJSON *enabled = cJSON_GetObjectItem(custom, "enabled");
        cJSON *url = cJSON_GetObjectItem(custom, "url");
        cJSON *method = cJSON_GetObjectItem(custom, "method");
        cJSON *content_type = cJSON_GetObjectItem(custom, "content_type");
        cJSON *body_template = cJSON_GetObjectItem(custom, "body_template");

        config.custom.enabled = (enabled && cJSON_IsTrue(enabled));
        if (url) strlcpy(config.custom.url, cJSON_GetStringValue(url), sizeof(config.custom.url));
        if (method) strlcpy(config.custom.method, cJSON_GetStringValue(method), sizeof(config.custom.method));
        if (content_type) strlcpy(config.custom.content_type, cJSON_GetStringValue(content_type), sizeof(config.custom.content_type));
        if (body_template) strlcpy(config.custom.body_template, cJSON_GetStringValue(body_template), sizeof(config.custom.body_template));
    }

    cJSON_Delete(root);

    // 保存到 NVS
    esp_err_t err = config_save_webhook(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save webhook config: %s", esp_err_to_name(err));
        httpd_resp_send_500(req);
        return err;
    }

    ESP_LOGI(TAG, "Webhook config saved to NVS");

    httpd_resp_set_type(req, "application/json");
    const char *response = "{\"success\":true,\"message\":\"Webhook config saved\"}";
    httpd_resp_send(req, response, strlen(response));

    return ESP_OK;
}

esp_err_t web_server_handle_post_config_wechat(httpd_req_t* req)
{
    char buf[512];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        ESP_LOGE(TAG, "Failed to receive request data");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    buf[ret] = '\0';

    // 解析企业微信配置
    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    wechat_config_t config = {0};
    
    cJSON *corpid = cJSON_GetObjectItem(root, "corpid");
    cJSON *corpsecret = cJSON_GetObjectItem(root, "corpsecret");
    cJSON *agentid = cJSON_GetObjectItem(root, "agentid");
    cJSON *touser = cJSON_GetObjectItem(root, "touser");

    if (corpid) strlcpy(config.corpid, cJSON_GetStringValue(corpid), sizeof(config.corpid));
    if (corpsecret) strlcpy(config.corpsecret, cJSON_GetStringValue(corpsecret), sizeof(config.corpsecret));
    if (agentid) strlcpy(config.agentid, cJSON_GetStringValue(agentid), sizeof(config.agentid));
    if (touser) strlcpy(config.touser, cJSON_GetStringValue(touser), sizeof(config.touser));

    cJSON_Delete(root);

    esp_err_t err = config_save_wechat(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save wechat config: %s", esp_err_to_name(err));
        httpd_resp_send_500(req);
        return err;
    }

    httpd_resp_set_type(req, "application/json");
    const char *response = "{\"success\":true,\"message\":\"Wechat config saved\"}";
    httpd_resp_send(req, response, strlen(response));

    return ESP_OK;
}

esp_err_t web_server_handle_post_reset(httpd_req_t* req)
{
    esp_err_t err = config_factory_reset();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to factory reset: %s", esp_err_to_name(err));
        httpd_resp_send_500(req);
        return err;
    }

    httpd_resp_set_type(req, "application/json");
    const char *response = "{\"success\":true,\"message\":\"Factory reset completed. Rebooting...\"}";
    httpd_resp_send(req, response, strlen(response));

    // 重启系统
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();

    return ESP_OK;
}

esp_err_t web_server_handle_post_reboot(httpd_req_t* req)
{
    httpd_resp_set_type(req, "application/json");
    const char *response = "{\"success\":true,\"message\":\"Rebooting...\"}";
    httpd_resp_send(req, response, strlen(response));

    // 重启系统
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();

    return ESP_OK;
}

esp_err_t web_server_handle_post_monitor_test(httpd_req_t* req)
{
    esp_err_t err = monitor_send_test_notification();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send test notification: %s", esp_err_to_name(err));
        httpd_resp_send_500(req);
        return err;
    }

    httpd_resp_set_type(req, "application/json");
    const char *response = "{\"success\":true,\"message\":\"Test notification sent\"}";
    httpd_resp_send(req, response, strlen(response));

    return ESP_OK;
}

esp_err_t web_server_handle_webhook_hook(httpd_req_t* req)
{
    return webhook_hook_api_handler(req);
}

// 测试SMTP配置API处理 - 测试SMTP邮件发送功能
esp_err_t web_server_handle_webhook_test_smtp(httpd_req_t* req)
{
    char buf[1024];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No data received");
        return ESP_FAIL;
    }
    buf[ret] = '\0';
    
    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    webhook_config_t config = {0};
    config_load_webhook(&config);
    
    cJSON_Delete(root);
    
    esp_err_t err = web_hook_test_smtp(&config.smtp);
    
    httpd_resp_set_type(req, "application/json");
    if (err == ESP_OK) {
        cJSON *response = cJSON_CreateObject();
        cJSON_AddBoolToObject(response, "success", true);
        cJSON_AddStringToObject(response, "message", "SMTP test sent");
        
        char *response_str = cJSON_PrintUnformatted(response);
        cJSON_Delete(response);
        
        httpd_resp_sendstr(req, response_str);
    } else {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "SMTP test failed");
    }
    
    return ESP_OK;
}

// 测试企业微信API处理 - 测试企业微信消息发送
esp_err_t web_server_handle_webhook_test_wechat(httpd_req_t* req)
{
    char buf[1024];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No data received");
        return ESP_FAIL;
    }
    buf[ret] = '\0';
    
    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    webhook_config_t config = {0};
    config_load_webhook(&config);
    
    const char *title = "测试通知";
    const char *content = "这是一条来自ESP32-S3打印服务器的测试消息";
    
    cJSON_Delete(root);
    
    esp_err_t err = web_hook_test_wechat(&config.wechat, title, content);
    
    httpd_resp_set_type(req, "application/json");
    if (err == ESP_OK) {
        cJSON *response = cJSON_CreateObject();
        cJSON_AddBoolToObject(response, "success", true);
        cJSON_AddStringToObject(response, "message", "WeChat test started");
        
        char *response_str = cJSON_PrintUnformatted(response);
        cJSON_Delete(response);
        
        httpd_resp_sendstr(req, response_str);
    } else {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to start WeChat test");
    }
    
    return ESP_OK;
}

// 测试自定义WebHook API处理 - 测试自定义WebHook发送
esp_err_t web_server_handle_webhook_test_custom(httpd_req_t* req)
{
    char buf[1024];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No data received");
        return ESP_FAIL;
    }
    buf[ret] = '\0';
    
    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    webhook_config_t config = {0};
    config_load_webhook(&config);
    
    const char *title = "测试通知";
    const char *content = "这是一条来自ESP32-S3打印服务器的测试消息";
    
    cJSON_Delete(root);
    
    esp_err_t err = web_hook_test_custom(&config.custom, title, content);
    
    httpd_resp_set_type(req, "application/json");
    if (err == ESP_OK) {
        cJSON *response = cJSON_CreateObject();
        cJSON_AddBoolToObject(response, "success", true);
        cJSON_AddStringToObject(response, "message", "Custom webhook test sent");
        
        char *response_str = cJSON_PrintUnformatted(response);
        cJSON_Delete(response);
        
        httpd_resp_sendstr(req, response_str);
    } else {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Custom webhook test failed");
    }
    
    return ESP_OK;
}

// 测试所有WebHook API处理 - 测试所有启用的WebHook通知方式
esp_err_t web_server_handle_webhook_test_all(httpd_req_t* req)
{
    char buf[1024];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No data received");
        return ESP_FAIL;
    }
    buf[ret] = '\0';
    
    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    const char *title = "测试通知";
    const char *content = "这是一条来自ESP32-S3打印服务器的测试消息";
    
    cJSON_Delete(root);
    
    esp_err_t err = web_hook_start_send_task(title, content);
    
    httpd_resp_set_type(req, "application/json");
    if (err == ESP_OK) {
        cJSON *response = cJSON_CreateObject();
        cJSON_AddBoolToObject(response, "success", true);
        cJSON_AddStringToObject(response, "message", "WebHook send task started");
        
        char *response_str = cJSON_PrintUnformatted(response);
        cJSON_Delete(response);
        
        httpd_resp_sendstr(req, response_str);
    } else {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to start WebHook send task");
    }
    
    return ESP_OK;
}

esp_err_t web_server_get_embedded_resource(const char* filepath, const char** data, size_t* len) {
    return ESP_OK;
}

static esp_err_t ota_url_handler(httpd_req_t* req)
{
    char buf[512];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No data received");
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *url = cJSON_GetObjectItem(root, "url");
    if (!url || !cJSON_IsString(url)) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing 'url' field");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Starting OTA from URL: %s", url->valuestring);

    esp_http_client_config_t config = {
        .url = url->valuestring,
        .timeout_ms = 60000,
        .skip_cert_common_name_check = true,
        .keep_alive_enable = true,
    };

    esp_https_ota_config_t ota_config = {
        .http_config = &config
    };

    ESP_LOGI(TAG, "OTA config prepared, starting OTA...");

    esp_err_t err = esp_https_ota(&ota_config);
    cJSON_Delete(root);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ESP HTTPS OTA failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA failed");
        return err;
    }

    ESP_LOGI(TAG, "OTA update successful, rebooting...");

    httpd_resp_set_type(req, "application/json");
    const char *resp = "{\"success\":true,\"message\":\"OTA update complete. Rebooting...\"}";
    httpd_resp_send(req, resp, strlen(resp));

    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();

    return ESP_OK;
}

// ==================== 监控模块新 API ====================

// 添加监控站点
static esp_err_t web_server_handle_monitor_site_add(httpd_req_t* req)
{
    char buf[2048];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No data");
        return ESP_FAIL;
    }
    buf[ret] = '\0';
    
    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    cJSON *site_json = cJSON_GetObjectItem(root, "site");
    if (!site_json) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing site");
        return ESP_FAIL;
    }
    
    monitor_site_t site = {0};
    cJSON *name = cJSON_GetObjectItem(site_json, "name");
    cJSON *url = cJSON_GetObjectItem(site_json, "url");
    cJSON *custom_host = cJSON_GetObjectItem(site_json, "custom_host");
    cJSON *interval = cJSON_GetObjectItem(site_json, "interval");
    cJSON *timeout = cJSON_GetObjectItem(site_json, "timeout");
    cJSON *offline_count = cJSON_GetObjectItem(site_json, "offline_count");
    cJSON *status_code_1xx = cJSON_GetObjectItem(site_json, "status_code_1xx");
    cJSON *status_code_2xx = cJSON_GetObjectItem(site_json, "status_code_2xx");
    cJSON *status_code_3xx = cJSON_GetObjectItem(site_json, "status_code_3xx");
    cJSON *status_code_4xx = cJSON_GetObjectItem(site_json, "status_code_4xx");
    cJSON *status_code_5xx = cJSON_GetObjectItem(site_json, "status_code_5xx");
    cJSON *webhook_interval = cJSON_GetObjectItem(site_json, "webhook_interval");
    cJSON *enabled = cJSON_GetObjectItem(site_json, "enabled");
    cJSON *paused = cJSON_GetObjectItem(site_json, "paused");
    
    if (name && url) {
        strlcpy(site.name, cJSON_GetStringValue(name), sizeof(site.name));
        strlcpy(site.url, cJSON_GetStringValue(url), sizeof(site.url));
        if (custom_host) strlcpy(site.custom_host, cJSON_GetStringValue(custom_host), sizeof(site.custom_host));
        if (interval) site.interval = cJSON_GetNumberValue(interval);
        if (timeout) site.timeout = cJSON_GetNumberValue(timeout);
        if (offline_count) site.offline_count = cJSON_GetNumberValue(offline_count);
        site.status_code_1xx = status_code_1xx ? cJSON_IsTrue(status_code_1xx) : false;
        site.status_code_2xx = status_code_2xx ? cJSON_IsTrue(status_code_2xx) : true;
        site.status_code_3xx = status_code_3xx ? cJSON_IsTrue(status_code_3xx) : false;
        site.status_code_4xx = status_code_4xx ? cJSON_IsTrue(status_code_4xx) : false;
        site.status_code_5xx = status_code_5xx ? cJSON_IsTrue(status_code_5xx) : false;
        if (webhook_interval) site.webhook_interval = cJSON_GetNumberValue(webhook_interval);
        site.enabled = enabled ? cJSON_IsTrue(enabled) : true;
        site.paused = paused ? cJSON_IsTrue(paused) : false;
    } else {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing name or url");
        return ESP_FAIL;
    }
    
    cJSON_Delete(root);
    
    esp_err_t err = monitor_add_site(&site);
    
    httpd_resp_set_type(req, "application/json");
    if (err == ESP_OK) {
        httpd_resp_sendstr(req, "{\"success\":true}");
    } else {
        httpd_resp_sendstr(req, "{\"success\":false,\"message\":\"Add failed\"}");
    }
    
    return ESP_OK;
}

// 更新监控站点
static esp_err_t web_server_handle_monitor_site_update(httpd_req_t* req)
{
    char buf[2048];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No data");
        return ESP_FAIL;
    }
    buf[ret] = '\0';
    
    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    cJSON *site_json = cJSON_GetObjectItem(root, "site");
    cJSON *old_name_json = cJSON_GetObjectItem(root, "old_name");
    if (!site_json) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing site");
        return ESP_FAIL;
    }
    
    // 先获取现有配置作为基础
    const char *old_name = old_name_json ? cJSON_GetStringValue(old_name_json) : NULL;
    monitor_site_t site = {0};
    bool site_loaded = false;
    
    // 如果有 old_name，优先从旧名称中获取 name
    const char *update_name = NULL;
    cJSON *name_json = cJSON_GetObjectItem(site_json, "name");
    if (name_json) {
        update_name = cJSON_GetStringValue(name_json);
    } else if (old_name) {
        update_name = old_name;
    }
    
    if (old_name) {
        esp_err_t load_err = monitor_get_site(old_name, &site);
        site_loaded = (load_err == ESP_OK);
        if (!site_loaded) {
            ESP_LOGW(TAG, "Site not found for update: %s, creating new", old_name);
            // 如果找不到旧站点，至少设置 name
            if (update_name) {
                strlcpy(site.name, update_name, sizeof(site.name));
            }
        }
    }
    
    cJSON *name = cJSON_GetObjectItem(site_json, "name");
    cJSON *url = cJSON_GetObjectItem(site_json, "url");
    cJSON *custom_host = cJSON_GetObjectItem(site_json, "custom_host");
    cJSON *interval = cJSON_GetObjectItem(site_json, "interval");
    cJSON *timeout = cJSON_GetObjectItem(site_json, "timeout");
    cJSON *offline_count = cJSON_GetObjectItem(site_json, "offline_count");
    cJSON *status_code_1xx = cJSON_GetObjectItem(site_json, "status_code_1xx");
    cJSON *status_code_2xx = cJSON_GetObjectItem(site_json, "status_code_2xx");
    cJSON *status_code_3xx = cJSON_GetObjectItem(site_json, "status_code_3xx");
    cJSON *status_code_4xx = cJSON_GetObjectItem(site_json, "status_code_4xx");
    cJSON *status_code_5xx = cJSON_GetObjectItem(site_json, "status_code_5xx");
    cJSON *webhook_interval = cJSON_GetObjectItem(site_json, "webhook_interval");
    cJSON *enabled = cJSON_GetObjectItem(site_json, "enabled");
    cJSON *paused = cJSON_GetObjectItem(site_json, "paused");
    
    if (name) strlcpy(site.name, cJSON_GetStringValue(name), sizeof(site.name));
    if (url) strlcpy(site.url, cJSON_GetStringValue(url), sizeof(site.url));
    if (custom_host) strlcpy(site.custom_host, cJSON_GetStringValue(custom_host), sizeof(site.custom_host));
    if (interval) site.interval = cJSON_GetNumberValue(interval);
    if (timeout) site.timeout = cJSON_GetNumberValue(timeout);
    if (offline_count) site.offline_count = cJSON_GetNumberValue(offline_count);
    if (status_code_1xx) site.status_code_1xx = cJSON_IsTrue(status_code_1xx);
    if (status_code_2xx) site.status_code_2xx = cJSON_IsTrue(status_code_2xx);
    if (status_code_3xx) site.status_code_3xx = cJSON_IsTrue(status_code_3xx);
    if (status_code_4xx) site.status_code_4xx = cJSON_IsTrue(status_code_4xx);
    if (status_code_5xx) site.status_code_5xx = cJSON_IsTrue(status_code_5xx);
    if (webhook_interval) site.webhook_interval = cJSON_GetNumberValue(webhook_interval);
    if (enabled) site.enabled = cJSON_IsTrue(enabled);
    if (paused) site.paused = cJSON_IsTrue(paused);
    
    cJSON_Delete(root);
    
    esp_err_t err = monitor_update_site(&site);
    
    httpd_resp_set_type(req, "application/json");
    if (err == ESP_OK) {
        httpd_resp_sendstr(req, "{\"success\":true}");
    } else {
        httpd_resp_sendstr(req, "{\"success\":false,\"message\":\"Update failed\"}");
    }
    
    return ESP_OK;
}

// 删除监控站点
static esp_err_t web_server_handle_monitor_site_delete(httpd_req_t* req)
{
    char buf[256];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No data");
        return ESP_FAIL;
    }
    buf[ret] = '\0';
    
    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    cJSON *name = cJSON_GetObjectItem(root, "name");
    if (!name) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing name");
        return ESP_FAIL;
    }
    
    esp_err_t err = monitor_remove_site(cJSON_GetStringValue(name));
    cJSON_Delete(root);
    
    httpd_resp_set_type(req, "application/json");
    if (err == ESP_OK) {
        httpd_resp_sendstr(req, "{\"success\":true}");
    } else {
        httpd_resp_sendstr(req, "{\"success\":false,\"message\":\"Delete failed\"}");
    }
    
    return ESP_OK;
}

// 保存监控系统配置
static esp_err_t web_server_handle_monitor_system_config(httpd_req_t* req)
{
    char buf[512];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No data");
        return ESP_FAIL;
    }
    buf[ret] = '\0';
    
    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    monitor_system_config_t config = {0};
    monitor_get_system_config(&config);
    
    cJSON *check_interval = cJSON_GetObjectItem(root, "global_check_interval");
    cJSON *notify_interval = cJSON_GetObjectItem(root, "global_notify_interval");
    cJSON *timeout = cJSON_GetObjectItem(root, "global_timeout");
    cJSON *offline_count = cJSON_GetObjectItem(root, "global_offline_count");
    cJSON *user_agent = cJSON_GetObjectItem(root, "global_user_agent");
    
    if (check_interval) config.global_check_interval = cJSON_GetNumberValue(check_interval);
    if (notify_interval) config.global_notify_interval = cJSON_GetNumberValue(notify_interval);
    if (timeout) config.global_timeout = cJSON_GetNumberValue(timeout);
    if (offline_count) config.global_offline_count = cJSON_GetNumberValue(offline_count);
    if (user_agent) strlcpy(config.global_user_agent, cJSON_GetStringValue(user_agent), sizeof(config.global_user_agent));
    
    cJSON_Delete(root);
    
    monitor_set_system_config(&config);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"success\":true}");
    
    return ESP_OK;
}

// 切换监控状态
static esp_err_t web_server_handle_monitor_toggle(httpd_req_t* req)
{
    char buf[128];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No data");
        return ESP_FAIL;
    }
    buf[ret] = '\0';
    
    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    cJSON *enabled = cJSON_GetObjectItem(root, "enabled");
    bool new_enabled = false;
    if (enabled != NULL) {
        if (cJSON_IsTrue(enabled)) {
            new_enabled = true;
        } else if (cJSON_IsFalse(enabled)) {
            new_enabled = false;
        }
    }
    cJSON_Delete(root);
    
    monitor_system_config_t config = {0};
    monitor_get_system_config(&config);
    config.global_enabled = new_enabled;
    monitor_set_system_config(&config);
    
    if (new_enabled && !monitor_is_running()) {
        monitor_start();
    } else if (!new_enabled && monitor_is_running()) {
        monitor_stop();
    }
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"success\":true}");
    
    return ESP_OK;
}

// 立即检测所有站点
static esp_err_t web_server_handle_monitor_check_all(httpd_req_t* req)
{
    esp_err_t err = monitor_check_all_sites();
    
    httpd_resp_set_type(req, "application/json");
    if (err == ESP_OK) {
        httpd_resp_sendstr(req, "{\"success\":true}");
    } else {
        httpd_resp_sendstr(req, "{\"success\":false,\"message\":\"Check failed\"}");
    }
    
    return ESP_OK;
}
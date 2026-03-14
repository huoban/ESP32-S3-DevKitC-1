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

    char *json_str = cJSON_PrintUnformatted(root);
    httpd_resp_send(req, json_str, strlen(json_str));

    cJSON_Delete(root);
    free(json_str);

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
    esp_err_t ret = web_server_handle_static_file(req);
    // 返回 ESP_OK 表示我们已经处理了这个请求
    return ESP_OK;
}

esp_err_t web_server_start(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.max_uri_handlers = 16; // 增加最大 URI 处理程序数量
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

esp_err_t web_server_register_handler(const char* uri, httpd_method_t method, esp_err_t (*handler)(httpd_req_t* req)) { return ESP_OK; }
esp_err_t web_server_serve_file(httpd_req_t* req, const char* filepath) { return ESP_OK; }
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

    wechat_config_t config;
    
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
/**
 * @file web_server.h
 * @brief Web 管理界面模块头文件
 * @details 负责 HTTP 请求处理、Web 资源服务和 API 接口处理
 */

#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include "esp_err.h"
#include "esp_http_server.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ==================== HTTP 请求处理上下文结构 ====================
/**
 * @brief HTTP 请求处理上下文结构
 */
typedef struct {
    httpd_req_t* req;                // HTTP 请求对象
    char url[128];                   // 请求 URL
    char method[16];                 // 请求方法 (GET/POST)
    char query_string[256];          // 查询字符串
    char post_data[512];             // POST 数据
} http_context_t;

// ==================== API 响应结构 ====================
/**
 * @brief API 响应结构
 */
typedef struct {
    int status_code;                 // HTTP 状态码
    char content_type[64];           // Content-Type
    char body[2048];                 // 响应体
} api_response_t;

// ==================== Web 服务器接口 ====================

/**
 * @brief 初始化 Web 服务器
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t web_server_init(void);

/**
 * @brief 启动 Web 服务器
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t web_server_start(void);

/**
 * @brief 停止 Web 服务器
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t web_server_stop(void);

/**
 * @brief 注册 URI 处理器
 * @param uri URI 路径
 * @param method HTTP 方法
 * @param handler 处理函数
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t web_server_register_handler(const char* uri, httpd_method_t method, esp_err_t (*handler)(httpd_req_t* req));

/**
 * @brief 服务文件
 * @param req HTTP 请求对象
 * @param filepath 文件路径
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t web_server_serve_file(httpd_req_t* req, const char* filepath);

/**
 * @brief 处理 GET /api/status 请求
 * @param req HTTP 请求对象
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t web_server_handle_get_status(httpd_req_t* req);

/**
 * @brief 处理 POST /api/config/wifi 请求
 * @param req HTTP 请求对象
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t web_server_handle_post_config_wifi(httpd_req_t* req);

/**
 * @brief 处理 POST /api/config/monitor 请求
 * @param req HTTP 请求对象
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t web_server_handle_post_config_monitor(httpd_req_t* req);

/**
 * @brief 处理 POST /api/config/wechat 请求
 * @param req HTTP 请求对象
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t web_server_handle_post_config_wechat(httpd_req_t* req);

/**
 * @brief 处理 POST /api/system/reset 请求
 * @param req HTTP 请求对象
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t web_server_handle_post_reset(httpd_req_t* req);

/**
 * @brief 处理 POST /api/system/reboot 请求
 * @param req HTTP 请求对象
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t web_server_handle_post_reboot(httpd_req_t* req);

/**
 * @brief 处理 POST /api/monitor/test 请求
 * @param req HTTP 请求对象
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t web_server_handle_post_monitor_test(httpd_req_t* req);

/**
 * @brief 发送 JSON 响应
 * @param req HTTP 请求对象
 * @param json_str JSON 字符串
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t web_server_send_json_response(httpd_req_t* req, const char* json_str);

/**
 * @brief 获取嵌入的 Web 资源
 * @param filepath 文件路径
 * @param data 数据指针
 * @param len 数据长度指针
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t web_server_get_embedded_resource(const char* filepath, const char** data, size_t* len);

#ifdef __cplusplus
}
#endif

#endif // WEB_SERVER_H

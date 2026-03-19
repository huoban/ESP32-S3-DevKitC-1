#ifndef API_CONTROLLER_H
#define API_CONTROLLER_H

#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 绑定打印机序列号到 TCP 端口
 * @param req HTTP 请求
 * @return esp_err_t 错误代码
 */
esp_err_t bind_printer_api_handler(httpd_req_t *req);

/**
 * @brief 解绑打印机序列号
 * @param req HTTP 请求
 * @return esp_err_t 错误代码
 */
esp_err_t unbind_printer_api_handler(httpd_req_t *req);

/**
 * @brief 获取绑定列表
 * @param req HTTP 请求
 * @return esp_err_t 错误代码
 */
esp_err_t get_bindings_api_handler(httpd_req_t *req);

/**
 * @brief WiFi 扫描 API
 * @param req HTTP 请求
 * @return esp_err_t 错误代码
 */
esp_err_t wifi_scan_api_handler(httpd_req_t *req);

/**
 * @brief 保存 WiFi 配置 API（支持静态 IP）
 * @param req HTTP 请求
 * @return esp_err_t 错误代码
 */
esp_err_t save_wifi_config_api_handler(httpd_req_t *req);

/**
 * @brief URL 解码辅助函数
 * @param src 源字符串
 * @param dest 目标字符串
 * @param max_len 最大长度
 */
void url_decode(const char *src, char *dest, size_t max_len);

/**
 * @brief WebHook 通用钩子接口 - 接收 title 和 content 并依次发送到所有启用的通知方式
 * @param req HTTP 请求
 * @return esp_err_t 错误代码
 */
esp_err_t webhook_hook_api_handler(httpd_req_t *req);

/**
 * @brief 获取 WebHook 配置 API - GET /api/config/webhook
 * @param req HTTP 请求
 * @return esp_err_t 错误代码
 */
esp_err_t get_webhook_config_api_handler(httpd_req_t *req);

#ifdef __cplusplus
}
#endif

#endif // API_CONTROLLER_H

/**
 * @file web_hook.c
 * @brief WebHook 通知模块实现
 * @details 负责邮件、企业微信、自定义 WebHook 通知发送
 */

#include "web_hook.h"
#include "config.h"
#include "wifi.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "mbedtls/platform.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/esp_debug.h"
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/error.h"
#include <mbedtls/base64.h>
#include <sys/param.h>

static const char *TAG = "WEBHOOK";

// SMTP 相关常量
#define SMTP_TASK_STACK_SIZE     (16 * 1024)
#define SMTP_BUF_SIZE            512
#define SERVER_USES_STARTSSL     1

// WebHook 顺序发送任务参数结构（前向声明）
typedef struct {
    char title[256];
    char content[2048];
} webhook_send_task_params_t;

// WebHook 顺序发送任务函数（前向声明）
static void webhook_send_sequential_task(void* pvParameters);

// 验证 mbedtls 返回值的宏
#define VALIDATE_MBEDTLS_RETURN(ret, min_valid_ret, max_valid_ret, goto_label)  \
    do {                                                                        \
        if (ret < min_valid_ret || ret > max_valid_ret) {                       \
            goto goto_label;                                                    \
        }                                                                       \
    } while (0)

// 模块运行状态
static bool webhook_initialized = false;

// SMTP 发送函数前向声明
static esp_err_t web_hook_send_smtp_message(const smtp_config_t* config, const char* title, const char* content);

// 企业微信 access_token 缓存
static wechat_access_token_t g_wechat_token = {0};

// WeChat 测试任务句柄
static TaskHandle_t wechat_test_task_handle = NULL;



// HTTP 事件处理器
static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGW(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
            break;
        case HTTP_EVENT_REDIRECT:
            ESP_LOGD(TAG, "HTTP_EVENT_REDIRECT");
            break;
    }
    return ESP_OK;
}

// SMTP: 写入数据并获取响应（非 SSL）
static int smtp_write_and_get_response(mbedtls_net_context *sock_fd, unsigned char *buf, size_t len)
{
    int ret;
    const size_t DATA_SIZE = 256;
    unsigned char data[DATA_SIZE];
    char code[4];
    size_t i, idx = 0;

    if (len) {
        ESP_LOGI(TAG, "SMTP send: %s", buf);
    }

    if (len && (ret = mbedtls_net_send(sock_fd, buf, len)) <= 0) {
        ESP_LOGE(TAG, "mbedtls_net_send failed with error -0x%x", -ret);
        return ret;
    }

    do {
        len = DATA_SIZE - 1;
        memset(data, 0, DATA_SIZE);
        ret = mbedtls_net_recv(sock_fd, data, len);

        if (ret <= 0) {
            ESP_LOGE(TAG, "mbedtls_net_recv failed with error -0x%x", -ret);
            goto exit;
        }

        data[len] = '\0';
        ESP_LOGI(TAG, "SMTP recv: %s", data);
        len = ret;
        for (i = 0; i < len; i++) {
            if (data[i] != '\n') {
                if (idx < 4) {
                    code[idx++] = data[i];
                }
                continue;
            }

            if (idx >= 3 && code[0] >= '0' && code[0] <= '9') {
                if (idx == 4 && code[3] == ' ') {
                    code[3] = '\0';
                } else if (idx == 3) {
                    code[3] = '\0';
                }
                ret = atoi(code);
                ESP_LOGI(TAG, "SMTP response code: %d", ret);
                goto exit;
            }

            idx = 0;
        }
    } while (1);

exit:
    return ret;
}

// SMTP: 写入数据并获取响应（SSL）
static int smtp_write_ssl_and_get_response(mbedtls_ssl_context *ssl, unsigned char *buf, size_t len)
{
    int ret;
    const size_t DATA_SIZE = 256;
    unsigned char data[DATA_SIZE];
    char code[4];
    size_t i, idx = 0;

    if (len) {
        ESP_LOGI(TAG, "SMTP send: %s", buf);
    }

    while (len && (ret = mbedtls_ssl_write(ssl, buf, len)) <= 0) {
        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            ESP_LOGE(TAG, "mbedtls_ssl_write failed with error -0x%x", -ret);
            goto exit;
        }
    }

    do {
        len = DATA_SIZE - 1;
        memset(data, 0, DATA_SIZE);
        ret = mbedtls_ssl_read(ssl, data, len);

        if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
            continue;
        }

        if (ret <= 0) {
            ESP_LOGE(TAG, "mbedtls_ssl_read failed with error -0x%x", -ret);
            goto exit;
        }

        data[len] = '\0';
        ESP_LOGI(TAG, "SMTP recv: %s", data);

        len = ret;
        for (i = 0; i < len; i++) {
            if (data[i] != '\n') {
                if (idx < 4) {
                    code[idx++] = data[i];
                }
                continue;
            }

            if (idx >= 3 && code[0] >= '0' && code[0] <= '9') {
                if (idx == 4 && code[3] == ' ') {
                    code[3] = '\0';
                } else if (idx == 3) {
                    code[3] = '\0';
                }
                ret = atoi(code);
                ESP_LOGI(TAG, "SMTP response code: %d", ret);
                goto exit;
            }

            idx = 0;
        }
    } while (1);

exit:
    return ret;
}

// SMTP: 写入 SSL 数据
static int smtp_write_ssl_data(mbedtls_ssl_context *ssl, unsigned char *buf, size_t len)
{
    int ret;

    if (len) {
        ESP_LOGD(TAG, "%s", buf);
    }

    while (len && (ret = mbedtls_ssl_write(ssl, buf, len)) <= 0) {
        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            ESP_LOGE(TAG, "mbedtls_ssl_write failed with error -0x%x", -ret);
            return ret;
        }
    }

    return 0;
}

// SMTP: 执行 TLS 握手
static int smtp_perform_tls_handshake(mbedtls_ssl_context *ssl)
{
    int ret = -1;
    uint32_t flags;
    char *buf = NULL;
    buf = (char *) heap_caps_calloc(1, SMTP_BUF_SIZE, MALLOC_CAP_SPIRAM);
    if (buf == NULL) {
        ESP_LOGE(TAG, "calloc failed for size %d", SMTP_BUF_SIZE);
        goto exit;
    }

    ESP_LOGI(TAG, "Performing the SSL/TLS handshake...");

    fflush(stdout);
    while ((ret = mbedtls_ssl_handshake(ssl)) != 0) {
        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            ESP_LOGE(TAG, "mbedtls_ssl_handshake returned -0x%x", -ret);
            goto exit;
        }
    }

    ESP_LOGI(TAG, "Verifying peer X.509 certificate...");

    if ((flags = mbedtls_ssl_get_verify_result(ssl)) != 0) {
        ESP_LOGW(TAG, "Failed to verify peer certificate!");
        mbedtls_x509_crt_verify_info(buf, SMTP_BUF_SIZE, "  ! ", flags);
        ESP_LOGW(TAG, "verification info: %s", buf);
    } else {
        ESP_LOGI(TAG, "Certificate verified.");
    }

    ESP_LOGI(TAG, "Cipher suite is %s", mbedtls_ssl_get_ciphersuite(ssl));
    ret = 0;

exit:
    if (buf) {
        heap_caps_free(buf);
    }
    return ret;
}

// WebHook模块初始化 - 初始化模块状态和企业微信access_token缓存
esp_err_t web_hook_init(void)
{
    ESP_LOGI(TAG, "Initializing webhook module...");
    webhook_initialized = true;
    memset(&g_wechat_token, 0, sizeof(g_wechat_token));
    ESP_LOGI(TAG, "Webhook module initialized");
    return ESP_OK;
}

// 发送通知 - 依次发送通知到所有启用的通知方式（SMTP、企业微信、自定义WebHook）
esp_err_t web_hook_send_notification(const webhook_notification_t* notification)
{
    if (!webhook_initialized) {
        ESP_LOGE(TAG, "Webhook module not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    if (notification == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Sending notification...");
    ESP_LOGI(TAG, "Title: %s", notification->title);
    ESP_LOGI(TAG, "Content: %s", notification->content);

    // 加载配置
    webhook_config_t config = {0};
    esp_err_t err = config_load_webhook(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load webhook config");
        return err;
    }

    // 依次发送到启用的通知方式
    if (config.smtp.enabled) {
        web_hook_send_smtp_message(&config.smtp, notification->title, notification->content);
    }

    if (config.wechat.enabled) {
        web_hook_send_wechat_message(&config.wechat, notification->title, notification->content);
    }

    if (config.custom.enabled) {
        web_hook_send_custom_webhook(&config.custom, notification->title, notification->content);
    }

    return ESP_OK;
}

// SMTP 测试任务参数结构
typedef struct {
    smtp_config_t config;
    char title[128];
    char content[512];
} smtp_test_task_params_t;

// 发送 SMTP 邮件 - 发送邮件到指定收件人
static esp_err_t web_hook_send_smtp_message(const smtp_config_t* config, const char* title, const char* content)
{
    if (config == NULL || title == NULL || content == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // 检查 WiFi 连接状态
    if (!wifi_is_connected()) {
        ESP_LOGE(TAG, "WiFi not connected, cannot send SMTP email");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Sending SMTP email...");
    ESP_LOGI(TAG, "SMTP Server: %s:%d", config->smtp_server, config->smtp_port);
    ESP_LOGI(TAG, "From: %s", config->from_email);
    ESP_LOGI(TAG, "To: %s", config->to_email);

    char *buf = NULL;
    unsigned char base64_buffer[128];
    int ret, len;
    size_t base64_len;
    char port_str[8];

    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_ssl_context ssl;
    mbedtls_x509_crt cacert;
    mbedtls_ssl_config conf;
    mbedtls_net_context server_fd;

    mbedtls_ssl_init(&ssl);
    mbedtls_x509_crt_init(&cacert);
    mbedtls_ctr_drbg_init(&ctr_drbg);
    ESP_LOGI(TAG, "Seeding the random number generator");

    mbedtls_ssl_config_init(&conf);

    mbedtls_entropy_init(&entropy);
    if ((ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                                     NULL, 0)) != 0) {
        ESP_LOGE(TAG, "mbedtls_ctr_drbg_seed returned -0x%x", -ret);
        goto exit;
    }

    // 不验证证书（简化实现）
    ESP_LOGI(TAG, "Setting hostname for TLS session...");
    if ((ret = mbedtls_ssl_set_hostname(&ssl, config->smtp_server)) != 0) {
        ESP_LOGE(TAG, "mbedtls_ssl_set_hostname returned -0x%x", -ret);
        goto exit;
    }

    ESP_LOGI(TAG, "Setting up the SSL/TLS structure...");
    if ((ret = mbedtls_ssl_config_defaults(&conf,
                                           MBEDTLS_SSL_IS_CLIENT,
                                           MBEDTLS_SSL_TRANSPORT_STREAM,
                                           MBEDTLS_SSL_PRESET_DEFAULT)) != 0) {
        ESP_LOGE(TAG, "mbedtls_ssl_config_defaults returned -0x%x", -ret);
        goto exit;
    }

    mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_NONE);
    mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &ctr_drbg);

    if ((ret = mbedtls_ssl_setup(&ssl, &conf)) != 0) {
        ESP_LOGE(TAG, "mbedtls_ssl_setup returned -0x%x", -ret);
        goto exit;
    }

    mbedtls_net_init(&server_fd);

    snprintf(port_str, sizeof(port_str), "%d", config->smtp_port);
    ESP_LOGI(TAG, "Connecting to %s:%s...", config->smtp_server, port_str);

    if ((ret = mbedtls_net_connect(&server_fd, config->smtp_server,
                                   port_str, MBEDTLS_NET_PROTO_TCP)) != 0) {
        ESP_LOGE(TAG, "mbedtls_net_connect returned -0x%x", -ret);
        goto exit;
    }

    ESP_LOGI(TAG, "Connected.");

    mbedtls_ssl_set_bio(&ssl, &server_fd, mbedtls_net_send, mbedtls_net_recv, NULL);

    buf = (char *) heap_caps_calloc(1, SMTP_BUF_SIZE, MALLOC_CAP_SPIRAM);
    if (buf == NULL) {
        ESP_LOGE(TAG, "calloc failed for size %d", SMTP_BUF_SIZE);
        goto exit;
    }

#if SERVER_USES_STARTSSL
    ret = smtp_write_and_get_response(&server_fd, (unsigned char *) buf, 0);
    if (ret < 200 || ret > 299) {
        ESP_LOGW(TAG, "Unexpected response code %d for initial banner, continuing anyway", ret);
    }

    ESP_LOGI(TAG, "Writing EHLO to server...");
    len = snprintf((char *) buf, SMTP_BUF_SIZE, "EHLO %s\r\n", "ESP32");
    ret = smtp_write_and_get_response(&server_fd, (unsigned char *) buf, len);
    if (ret < 200 || ret > 299) {
        ESP_LOGW(TAG, "Unexpected response code %d for EHLO, continuing anyway", ret);
    }

    ESP_LOGI(TAG, "Writing STARTTLS to server...");
    len = snprintf((char *) buf, SMTP_BUF_SIZE, "STARTTLS\r\n");
    ret = smtp_write_and_get_response(&server_fd, (unsigned char *) buf, len);
    if (ret < 200 || ret > 299) {
        ESP_LOGW(TAG, "Unexpected response code %d for STARTTLS, continuing anyway", ret);
    }

    ret = smtp_perform_tls_handshake(&ssl);
    if (ret != 0) {
        goto exit;
    }

    ESP_LOGI(TAG, "Re-sending EHLO after STARTTLS...");
    len = snprintf((char *) buf, SMTP_BUF_SIZE, "EHLO %s\r\n", "ESP32");
    ret = smtp_write_ssl_and_get_response(&ssl, (unsigned char *) buf, len);
    if (ret < 200 || ret > 299) {
        ESP_LOGW(TAG, "Unexpected response code %d for EHLO after STARTTLS, continuing anyway", ret);
    }
#else
    ret = smtp_perform_tls_handshake(&ssl);
    if (ret != 0) {
        goto exit;
    }

    ret = smtp_write_ssl_and_get_response(&ssl, (unsigned char *) buf, 0);
    if (ret < 200 || ret > 299) {
        ESP_LOGW(TAG, "Unexpected response code %d for initial banner, continuing anyway", ret);
    }
    ESP_LOGI(TAG, "Writing EHLO to server...");

    len = snprintf((char *) buf, SMTP_BUF_SIZE, "EHLO %s\r\n", "ESP32");
    ret = smtp_write_ssl_and_get_response(&ssl, (unsigned char *) buf, len);
    if (ret < 200 || ret > 299) {
        ESP_LOGW(TAG, "Unexpected response code %d for EHLO, continuing anyway", ret);
    }
#endif

    ESP_LOGI(TAG, "Authentication...");
    ESP_LOGI(TAG, "Write AUTH LOGIN");
    len = snprintf( (char *) buf, SMTP_BUF_SIZE, "AUTH LOGIN\r\n" );
    ret = smtp_write_ssl_and_get_response(&ssl, (unsigned char *) buf, len);
    if (ret < 200 || ret > 399) {
        ESP_LOGW(TAG, "Unexpected response code %d for AUTH LOGIN, continuing anyway", ret);
    }

    ESP_LOGI(TAG, "Write USER NAME");
    ret = mbedtls_base64_encode((unsigned char *) base64_buffer, sizeof(base64_buffer),
                                &base64_len, (unsigned char *) config->username, strlen(config->username));
    if (ret != 0) {
        ESP_LOGE(TAG, "Error in mbedtls encode! ret = -0x%x", -ret);
        goto exit;
    }
    len = snprintf((char *) buf, SMTP_BUF_SIZE, "%s\r\n", base64_buffer);
    ret = smtp_write_ssl_and_get_response(&ssl, (unsigned char *) buf, len);
    if (ret < 300 || ret > 399) {
        ESP_LOGW(TAG, "Unexpected response code %d for username, continuing anyway", ret);
    }

    ESP_LOGI(TAG, "Write PASSWORD");
    ret = mbedtls_base64_encode((unsigned char *) base64_buffer, sizeof(base64_buffer),
                                &base64_len, (unsigned char *) config->password, strlen(config->password));
    if (ret != 0) {
        ESP_LOGE(TAG, "Error in mbedtls encode! ret = -0x%x", -ret);
        goto exit;
    }
    len = snprintf((char *) buf, SMTP_BUF_SIZE, "%s\r\n", base64_buffer);
    ret = smtp_write_ssl_and_get_response(&ssl, (unsigned char *) buf, len);
    if (ret < 200 || ret > 399) {
        ESP_LOGW(TAG, "Unexpected response code %d for password, continuing anyway", ret);
    }

    ESP_LOGI(TAG, "Write MAIL FROM");
    len = snprintf((char *) buf, SMTP_BUF_SIZE, "MAIL FROM:<%s>\r\n", config->from_email);
    ret = smtp_write_ssl_and_get_response(&ssl, (unsigned char *) buf, len);
    if (ret < 200 || ret > 299) {
        ESP_LOGW(TAG, "Unexpected response code %d for MAIL FROM, continuing anyway", ret);
    }

    ESP_LOGI(TAG, "Write RCPT");
    len = snprintf((char *) buf, SMTP_BUF_SIZE, "RCPT TO:<%s>\r\n", config->to_email);
    ret = smtp_write_ssl_and_get_response(&ssl, (unsigned char *) buf, len);
    if (ret < 200 || ret > 299) {
        ESP_LOGW(TAG, "Unexpected response code %d for RCPT TO, continuing anyway", ret);
    }

    ESP_LOGI(TAG, "Write DATA");
    len = snprintf((char *) buf, SMTP_BUF_SIZE, "DATA\r\n");
    ret = smtp_write_ssl_and_get_response(&ssl, (unsigned char *) buf, len);
    if (ret < 300 || ret > 399) {
        ESP_LOGW(TAG, "Unexpected response code %d for DATA, continuing anyway", ret);
    }

    ESP_LOGI(TAG, "Write Content");
    len = snprintf((char *) buf, SMTP_BUF_SIZE,
                   "From: %s\r\nSubject: %s\r\n"
                   "To: %s\r\n"
                   "MIME-Version: 1.0\r\n"
                   "Content-Type: text/plain; charset=UTF-8\r\n\r\n"
                   "%s\r\n",
                   config->from_email, title, config->to_email, content);
    ret = smtp_write_ssl_data(&ssl, (unsigned char *) buf, len);

    len = snprintf((char *) buf, SMTP_BUF_SIZE, "\r\n.\r\n");
    ret = smtp_write_ssl_and_get_response(&ssl, (unsigned char *) buf, len);
    if (ret < 200 || ret > 299) {
        ESP_LOGW(TAG, "Unexpected response code %d for end of DATA, continuing anyway", ret);
    }
    ESP_LOGI(TAG, "Email sent!");

    mbedtls_ssl_close_notify(&ssl);
    ret = 0;

exit:
    mbedtls_net_free(&server_fd);
    mbedtls_x509_crt_free(&cacert);
    mbedtls_ssl_free(&ssl);
    mbedtls_ssl_config_free(&conf);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);

    if (ret != 0) {
        char err_buf[100];
        mbedtls_strerror(ret, err_buf, 100);
        ESP_LOGE(TAG, "Last error was: -0x%x - %s", -ret, err_buf);
    }

    if (buf) {
        heap_caps_free(buf);
    }

    return (ret == 0) ? ESP_OK : ESP_FAIL;
}

// SMTP 测试任务函数
static void smtp_test_task(void* pvParameters)
{
    smtp_test_task_params_t* params = (smtp_test_task_params_t*)pvParameters;
    
    if (params == NULL) {
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI(TAG, "SMTP test task started");
    
    // 调用发送邮件函数
    esp_err_t err = web_hook_send_smtp_message(&params->config, params->title, params->content);
    
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "SMTP test succeeded");
    } else {
        ESP_LOGE(TAG, "SMTP test failed: %s", esp_err_to_name(err));
    }
    
    // 释放参数内存
    heap_caps_free(params);
    
    // 删除任务
    vTaskDelete(NULL);
}

// 测试所有启用的 WebHook 通知方式 - 依次检测并发送到所有启用的通知方式
esp_err_t web_hook_test_all(const webhook_config_t* config, const char* title, const char* content)
{
    if (config == NULL || title == NULL || content == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Testing all webhook notifications...");

    // 依次检测并发送到所有启用的通知方式

    // 1. 如果 SMTP 启用，发送 SMTP 通知
    if (config->smtp.enabled) {
        ESP_LOGI(TAG, "SMTP enabled, sending SMTP notification...");
        esp_err_t err = web_hook_test_smtp(&config->smtp);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "SMTP notification failed");
        } else {
            ESP_LOGI(TAG, "SMTP notification sent");
        }
    }

    // 2. 如果企业微信启用，发送企业微信通知
    if (config->wechat.enabled) {
        ESP_LOGI(TAG, "WeChat enabled, sending WeChat notification...");
        esp_err_t err = web_hook_test_wechat(&config->wechat, title, content);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "WeChat notification failed");
        } else {
            ESP_LOGI(TAG, "WeChat notification sent");
        }
    }

    // 3. 如果自定义 WebHook 启用，发送自定义 WebHook 通知
    if (config->custom.enabled) {
        if (config->custom.url[0] == '\0') {
            ESP_LOGW(TAG, "Custom webhook enabled but URL is empty, skipping...");
        } else {
            ESP_LOGI(TAG, "Custom webhook enabled, sending custom webhook notification...");
            esp_err_t err = web_hook_test_custom(&config->custom, title, content);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Custom webhook notification failed");
            } else {
                ESP_LOGI(TAG, "Custom webhook notification sent");
            }
        }
    }

    ESP_LOGI(TAG, "All webhook notifications processed");
    return ESP_OK;
}

// 顺序发送所有启用的 WebHook 通知（单线程，依次执行）
esp_err_t web_hook_send_all_sequential(const char* title, const char* content)
{
    if (!webhook_initialized) {
        ESP_LOGE(TAG, "Webhook module not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    if (title == NULL || content == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "开始顺序发送 WebHook 通知流程");

    // 步骤1：获取 /api/config/webhook 配置
    webhook_config_t config = {0};
    esp_err_t ret = config_load_webhook(&config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "获取 WebHook 配置失败");
        return ret;
    }
    ESP_LOGI(TAG, "获取 WebHook 配置成功，开始依次执行发送流程");

    // 步骤2：依次检查并执行每个模块（单线程、等待完成）
    
    // 2.1 执行 SMTP 发送（仅 enabled=true 时执行）
    if (config.smtp.enabled) {
        ESP_LOGI(TAG, "SMTP 已启用，开始发送...");
        ret = web_hook_send_smtp_message(&config.smtp, title, content);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "SMTP 发送成功");
        } else {
            ESP_LOGE(TAG, "SMTP 发送失败: %s", esp_err_to_name(ret));
        }
    } else {
        ESP_LOGI(TAG, "SMTP 未启用，跳过");
    }

    // 2.2 执行企业微信发送（仅 enabled=true 时执行）
    if (config.wechat.enabled) {
        ESP_LOGI(TAG, "企业微信已启用，开始发送...");
        ret = web_hook_send_wechat_message(&config.wechat, title, content);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "企业微信发送成功");
        } else {
            ESP_LOGE(TAG, "企业微信发送失败: %s", esp_err_to_name(ret));
        }
    } else {
        ESP_LOGI(TAG, "企业微信未启用，跳过");
    }

    // 2.3 执行自定义 WebHook 发送（仅 enabled=true 时执行）
    if (config.custom.enabled) {
        if (config.custom.url[0] == '\0') {
            ESP_LOGW(TAG, "自定义 WebHook 已启用但 URL 为空，跳过");
        } else {
            ESP_LOGI(TAG, "自定义 WebHook 已启用，开始发送...");
            ret = web_hook_send_custom_webhook(&config.custom, title, content);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "自定义 WebHook 发送成功");
            } else {
                ESP_LOGE(TAG, "自定义 WebHook 发送失败: %s", esp_err_to_name(ret));
            }
        }
    } else {
        ESP_LOGI(TAG, "自定义 WebHook 未启用，跳过");
    }

    // 步骤3：所有模块执行完成
    ESP_LOGI(TAG, "WebHook 所有发送流程执行完毕");
    return ESP_OK;
}

// 创建任务并顺序发送所有启用的 WebHook 通知
esp_err_t web_hook_start_send_task(const char* title, const char* content)
{
    if (title == NULL || content == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "/test/wechat 接口收到请求，启动 WebHook 发送流程");

    // 分配任务参数
    webhook_send_task_params_t* params = (webhook_send_task_params_t*)heap_caps_malloc(
        sizeof(webhook_send_task_params_t), MALLOC_CAP_SPIRAM);
    if (params == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for WebHook send task");
        return ESP_ERR_NO_MEM;
    }

    // 复制参数
    memset(params, 0, sizeof(webhook_send_task_params_t));
    strlcpy(params->title, title, sizeof(params->title));
    strlcpy(params->content, content, sizeof(params->content));

    // 创建任务
    BaseType_t ret = xTaskCreate(
        webhook_send_sequential_task,
        "webhook_send",
        16384,
        params,
        5,
        NULL
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create WebHook send task");
        heap_caps_free(params);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "WebHook send task created");
    return ESP_OK;
}

// WebHook 顺序发送任务函数
static void webhook_send_sequential_task(void* pvParameters)
{
    webhook_send_task_params_t* params = (webhook_send_task_params_t*)pvParameters;
    
    if (params == NULL) {
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI(TAG, "WebHook 顺序发送任务启动");
    
    // 调用顺序发送函数
    esp_err_t err = web_hook_send_all_sequential(params->title, params->content);
    
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "WebHook 顺序发送任务完成");
    } else {
        ESP_LOGE(TAG, "WebHook 顺序发送任务失败: %s", esp_err_to_name(err));
    }
    
    // 释放参数内存
    heap_caps_free(params);
    
    // 删除任务
    vTaskDelete(NULL);
}

// 测试SMTP配置 - 测试SMTP邮件发送功能
esp_err_t web_hook_test_smtp(const smtp_config_t* config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Testing SMTP config...");
    ESP_LOGI(TAG, "SMTP Server: %s:%d", config->smtp_server, config->smtp_port);
    ESP_LOGI(TAG, "Username: %s", config->username);
    
    // 分配任务参数
    smtp_test_task_params_t* params = (smtp_test_task_params_t*)heap_caps_malloc(sizeof(smtp_test_task_params_t), MALLOC_CAP_SPIRAM);
    if (params == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for SMTP test");
        return ESP_ERR_NO_MEM;
    }
    
    // 复制配置和参数
    memset(params, 0, sizeof(smtp_test_task_params_t));
    memcpy(&params->config, config, sizeof(smtp_config_t));
    strlcpy(params->title, "测试邮件", sizeof(params->title));
    strlcpy(params->content, "这是一封来自 ESP32-S3 打印服务器的测试邮件！", sizeof(params->content));
    
    // 创建测试任务
    BaseType_t ret = xTaskCreate(
        smtp_test_task,
        "smtp_test",
        SMTP_TASK_STACK_SIZE,
        params,
        5,
        NULL
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create SMTP test task");
        heap_caps_free(params);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "SMTP test task created");
    return ESP_OK;
}

// WeChat 测试任务参数结构
typedef struct {
    wechat_webhook_config_t config;
    char title[128];
    char content[512];
} wechat_test_task_params_t;

// Custom WebHook 测试任务参数结构
typedef struct {
    custom_webhook_config_t config;
    char title[128];
    char content[512];
} custom_test_task_params_t;

// Custom WebHook 测试任务句柄
static TaskHandle_t custom_test_task_handle = NULL;

// WeChat 测试任务函数
static void wechat_test_task(void* pvParameters)
{
    wechat_test_task_params_t* params = (wechat_test_task_params_t*)pvParameters;
    
    if (params == NULL) {
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI(TAG, "WeChat test task started");
    
    // 调用发送消息函数
    esp_err_t err = web_hook_send_wechat_message(&params->config, params->title, params->content);
    
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "WeChat test succeeded");
    } else {
        ESP_LOGE(TAG, "WeChat test failed: %s", esp_err_to_name(err));
    }
    
    // 释放参数内存
    heap_caps_free(params);
    
    // 删除任务
    wechat_test_task_handle = NULL;
    vTaskDelete(NULL);
}

// Custom WebHook 测试任务函数
static void custom_test_task(void* pvParameters)
{
    custom_test_task_params_t* params = (custom_test_task_params_t*)pvParameters;
    
    if (params == NULL) {
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI(TAG, "Custom WebHook test task started");
    
    // 调用发送消息函数
    esp_err_t err = web_hook_send_custom_webhook(&params->config, params->title, params->content);
    
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Custom WebHook test succeeded");
    } else {
        ESP_LOGE(TAG, "Custom WebHook test failed: %s", esp_err_to_name(err));
    }
    
    // 释放参数内存
    heap_caps_free(params);
    
    // 删除任务
    custom_test_task_handle = NULL;
    vTaskDelete(NULL);
}

esp_err_t web_hook_test_wechat(const wechat_webhook_config_t* config, const char* title, const char* content)
{
    if (config == NULL || title == NULL || content == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Testing WeChat config...");
    
    // 检查是否已有任务在运行
    if (wechat_test_task_handle != NULL) {
        ESP_LOGW(TAG, "WeChat test already in progress");
        return ESP_ERR_INVALID_STATE;
    }
    
    // 分配任务参数
    wechat_test_task_params_t* params = (wechat_test_task_params_t*)heap_caps_malloc(sizeof(wechat_test_task_params_t), MALLOC_CAP_SPIRAM);
    if (params == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for WeChat test");
        return ESP_ERR_NO_MEM;
    }
    
    // 复制配置和参数
    memset(params, 0, sizeof(wechat_test_task_params_t));
    memcpy(&params->config, config, sizeof(wechat_webhook_config_t));
    strlcpy(params->title, title, sizeof(params->title));
    strlcpy(params->content, content, sizeof(params->content));
    
    // 创建测试任务
    BaseType_t ret = xTaskCreate(
        wechat_test_task,
        "wechat_test",
        8192,
        params,
        5,
        &wechat_test_task_handle
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create WeChat test task");
        heap_caps_free(params);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "WeChat test task created");
    return ESP_OK;
}

esp_err_t web_hook_test_custom(const custom_webhook_config_t* config, const char* title, const char* content)
{
    if (config == NULL || title == NULL || content == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Testing custom webhook...");
    
    // 检查是否已有任务在运行
    if (custom_test_task_handle != NULL) {
        ESP_LOGW(TAG, "Custom WebHook test already in progress");
        return ESP_ERR_INVALID_STATE;
    }
    
    // 分配任务参数
    custom_test_task_params_t* params = (custom_test_task_params_t*)heap_caps_malloc(sizeof(custom_test_task_params_t), MALLOC_CAP_SPIRAM);
    if (params == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for Custom WebHook test");
        return ESP_ERR_NO_MEM;
    }
    
    // 复制配置和参数
    memset(params, 0, sizeof(custom_test_task_params_t));
    memcpy(&params->config, config, sizeof(custom_webhook_config_t));
    strlcpy(params->title, title, sizeof(params->title));
    strlcpy(params->content, content, sizeof(params->content));
    
    // 创建测试任务
    BaseType_t ret = xTaskCreate(
        custom_test_task,
        "custom_test",
        8192,
        params,
        5,
        &custom_test_task_handle
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create Custom WebHook test task");
        heap_caps_free(params);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Custom WebHook test task created");
    return ESP_OK;
}

// 获取企业微信access_token - 从企业微信API获取access_token并缓存
esp_err_t web_hook_get_wechat_access_token(const wechat_webhook_config_t* config, wechat_access_token_t* token_out)
{
    // 参数检查
    if (config == NULL || token_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // 零初始化输出
    memset(token_out, 0, sizeof(wechat_access_token_t));

    // 检查 WiFi 连接状态
    if (!wifi_is_connected()) {
        return ESP_ERR_INVALID_STATE;
    }

    // 检查缓存的 token 是否有效
    time_t now;
    time(&now);
    
    if (g_wechat_token.access_token[0] != '\0' && g_wechat_token.expire_time > now + 60) {
        memcpy(token_out, &g_wechat_token, sizeof(wechat_access_token_t));
        return ESP_OK;
    }

    // 验证配置参数
    if (config->corpid[0] == '\0' || config->corpsecret[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    
    char url[384] = {0};
    int ret = snprintf(url, sizeof(url), 
             "https://qyapi.weixin.qq.com/cgi-bin/gettoken?corpid=%s&corpsecret=%s",
             config->corpid, config->corpsecret);
    
    if (ret < 0 || ret >= sizeof(url)) {
        return ESP_ERR_NO_MEM;
    }

    // 创建 HTTP 客户端
    esp_http_client_config_t http_config = {
        .url = url,
        .timeout_ms = 10000,
        .skip_cert_common_name_check = true,
        .use_global_ca_store = false,
        .buffer_size = 4096,
        .is_async = false,
    };

    esp_http_client_handle_t client = esp_http_client_init(&http_config);
    if (client == NULL) {
        return ESP_FAIL;
    }

    // 打开 HTTP 连接
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        return err;
    }

    // 读取 HTTP 响应
    int content_length = esp_http_client_fetch_headers(client);
    if (content_length < 0) {
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    int status_code = esp_http_client_get_status_code(client);
    if (status_code != 200) {
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    // 分配缓冲区
    int buf_size = (content_length > 0 && content_length < 4096) ? content_length : 4096;
    char* response_data = (char*)heap_caps_malloc(buf_size + 1, MALLOC_CAP_SPIRAM);
    if (response_data == NULL) {
        esp_http_client_cleanup(client);
        return ESP_ERR_NO_MEM;
    }
    memset(response_data, 0, buf_size + 1);

    // 读取响应
    int read_len = esp_http_client_read(client, response_data, buf_size);
    esp_http_client_cleanup(client);
    
    if (read_len <= 0) {
        heap_caps_free(response_data);
        return ESP_FAIL;
    }
    response_data[read_len] = '\0';

    // 解析 JSON
    cJSON* root = cJSON_Parse(response_data);
    heap_caps_free(response_data);

    if (root == NULL) {
        return ESP_FAIL;
    }

    // 检查 API 错误码
    cJSON* errcode_item = cJSON_GetObjectItem(root, "errcode");
    if (errcode_item != NULL && cJSON_IsNumber(errcode_item) && errcode_item->valueint != 0) {
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    // 获取 token
    cJSON* token_item = cJSON_GetObjectItem(root, "access_token");
    cJSON* expires_item = cJSON_GetObjectItem(root, "expires_in");

    if (token_item == NULL || !cJSON_IsString(token_item) ||
        expires_item == NULL || !cJSON_IsNumber(expires_item)) {
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    // 保存到缓存
    memset(&g_wechat_token, 0, sizeof(g_wechat_token));
    strlcpy(g_wechat_token.access_token, token_item->valuestring, sizeof(g_wechat_token.access_token));
    g_wechat_token.expire_time = now + expires_item->valueint;

    // 复制到输出
    memcpy(token_out, &g_wechat_token, sizeof(wechat_access_token_t));

    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t web_hook_send_wechat_message(const wechat_webhook_config_t* config, const char* title, const char* content)
{
    if (config == NULL || title == NULL || content == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Sending WeChat message...");

    // 验证配置参数
    if (config->corpid[0] == '\0' || config->corpsecret[0] == '\0' || 
        config->agentid[0] == '\0' || config->touser[0] == '\0') {
        ESP_LOGE(TAG, "WeChat message config incomplete");
        return ESP_ERR_INVALID_ARG;
    }

    // 获取 access_token
    wechat_access_token_t token = {0};  // 必须初始化！
    esp_err_t err = web_hook_get_wechat_access_token(config, &token);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get access_token");
        return err;
    }

    // 验证 access_token
    if (token.access_token[0] == '\0') {
        ESP_LOGE(TAG, "Access token is empty");
        return ESP_ERR_INVALID_STATE;
    }

    // 构建请求 URL
    char url[512] = {0};
    int url_len = snprintf(url, sizeof(url), 
             "https://qyapi.weixin.qq.com/cgi-bin/message/send?access_token=%s",
             token.access_token);
    
    if (url_len < 0 || url_len >= sizeof(url)) {
        ESP_LOGE(TAG, "Failed to build message URL");
        return ESP_ERR_NO_MEM;
    }
    
    ESP_LOGI(TAG, "Message URL length: %d", url_len);
    
    // 构建消息内容 - 将 title 和 content 用空格连接
    char wechat_content[WEBHOOK_TITLE_MAX_LEN + WEBHOOK_CONTENT_MAX_LEN + 16] = {0};
    snprintf(wechat_content, sizeof(wechat_content), 
             "%s %s", title, content);
    
    // 构建 JSON 请求体
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "touser", config->touser);
    cJSON_AddStringToObject(root, "msgtype", "text");
    cJSON_AddNumberToObject(root, "agentid", atoi(config->agentid));
    
    cJSON* text = cJSON_CreateObject();
    cJSON_AddStringToObject(text, "content", wechat_content);
    cJSON_AddItemToObject(root, "text", text);

    cJSON_AddNumberToObject(root, "safe", 0);
    cJSON_AddNumberToObject(root, "enable_id_trans", 0);
    cJSON_AddNumberToObject(root, "enable_duplicate_check", 0);
    cJSON_AddNumberToObject(root, "duplicate_check_interval", 0);

    char* post_data = cJSON_PrintUnformatted(root);
    if (!post_data) {
        cJSON_Delete(root);
        ESP_LOGE(TAG, "Failed to format JSON");
        return ESP_FAIL;
    }
    cJSON_Delete(root);

    ESP_LOGI(TAG, "WeChat message payload: %s", post_data);

    // 发送 HTTP 请求
    esp_http_client_config_t http_config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 10000,
        .skip_cert_common_name_check = true,
        .use_global_ca_store = false,
        .buffer_size = 4096,
    };

    esp_http_client_handle_t client = esp_http_client_init(&http_config);
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to create HTTP client");
        cJSON_free(post_data);
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");

    // 打开 HTTP 连接
    err = esp_http_client_open(client, strlen(post_data));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP open failed: %s", esp_err_to_name(err));
        cJSON_free(post_data);
        esp_http_client_cleanup(client);
        return err;
    }

    // 写 POST 数据
    int write_len = esp_http_client_write(client, post_data, strlen(post_data));
    cJSON_free(post_data);
    if (write_len < 0) {
        ESP_LOGE(TAG, "Failed to write POST data");
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    // 读取 HTTP 头部
    int content_length = esp_http_client_fetch_headers(client);
    if (content_length < 0) {
        ESP_LOGE(TAG, "Failed to fetch headers");
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    int status_code = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "WeChat message sent, status code: %d", status_code);
    ESP_LOGI(TAG, "Content length: %d", content_length);

    // 读取响应
    if (content_length <= 0) {
        content_length = 4096;
        ESP_LOGI(TAG, "Using default buffer size: %d", content_length);
    }
    char* response_data = (char*)heap_caps_malloc(content_length + 1, MALLOC_CAP_SPIRAM);
    if (response_data) {
        int read_len = esp_http_client_read(client, response_data, content_length);
        if (read_len >= 0) {
            response_data[read_len] = '\0';
            ESP_LOGI(TAG, "WeChat response length: %d", read_len);
            if (read_len > 0) {
                ESP_LOGI(TAG, "WeChat response: %s", response_data);
            }
        } else {
            ESP_LOGE(TAG, "Failed to read response");
        }
        heap_caps_free(response_data);
    }

    esp_http_client_cleanup(client);

    return (status_code == 200) ? ESP_OK : ESP_FAIL;
}

// 发送自定义WebHook请求 - 发送HTTP请求到自定义WebHook URL
esp_err_t web_hook_send_custom_webhook(const custom_webhook_config_t* config, const char* title, const char* content)
{
    if (config == NULL || title == NULL || content == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // 检查 WiFi 连接状态
    if (!wifi_is_connected()) {
        ESP_LOGE(TAG, "WiFi not connected, cannot send custom webhook");
        return ESP_ERR_INVALID_STATE;
    }

    // 验证 URL 配置
    if (config->url[0] == '\0') {
        ESP_LOGE(TAG, "Custom webhook URL is empty");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Sending custom webhook to: %s", config->url);

    // 构建请求体 - 替换占位符
    char body[2048];
    const char* template_body = config->body_template[0] ? config->body_template : "{\"title\":\"\",\"content\":\"\"}";
    
    // 简单的占位符替换
    const char* src = template_body;
    char* dst = body;
    size_t dst_len = 0;
    const size_t max_len = sizeof(body) - 1;

    while (*src && dst_len < max_len) {
        if (strncmp(src, "{title}", 7) == 0) {
            strncpy(dst, title, max_len - dst_len);
            dst += strlen(title);
            dst_len += strlen(title);
            src += 7;
        } else if (strncmp(src, "{content}", 9) == 0) {
            strncpy(dst, content, max_len - dst_len);
            dst += strlen(content);
            dst_len += strlen(content);
            src += 9;
        } else {
            *dst++ = *src++;
            dst_len++;
        }
    }
    *dst = '\0';

    ESP_LOGI(TAG, "Webhook request body: %s", body);

    // 发送 HTTP 请求
    esp_http_client_config_t http_config = {
        .url = config->url,
        .event_handler = http_event_handler,
        .timeout_ms = 10000,
        .skip_cert_common_name_check = true,
    };

    esp_http_client_handle_t client = esp_http_client_init(&http_config);
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to create HTTP client");
        return ESP_FAIL;
    }

    // 设置 HTTP 方法
    if (strcmp(config->method, "GET") == 0) {
        esp_http_client_set_method(client, HTTP_METHOD_GET);
    } else {
        esp_http_client_set_method(client, HTTP_METHOD_POST);
        esp_http_client_set_header(client, "Content-Type", config->content_type);
        esp_http_client_set_post_field(client, body, strlen(body));
    }

    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }

    int status_code = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "Custom webhook sent, status code: %d", status_code);

    esp_http_client_cleanup(client);

    return (status_code >= 200 && status_code < 300) ? ESP_OK : ESP_FAIL;
}

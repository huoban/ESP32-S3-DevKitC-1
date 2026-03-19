/**
 * @file tcp_server.c
 * @brief TCP 打印服务模块实现
 * @details 负责 TCP 9100-9103 端口监听，根据绑定关系动态查找对应打印机
 */

#include "tcp_server.h"
#include "printer.h"
#include "nvs_manager.h"
#include "common_defs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "esp_heap_caps.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "TCP_SERVER";

#define PRINTER_COUNT 4
#define BASE_PORT 9100

typedef struct {
    uint8_t printer_instance;
    uint16_t port;
    int listen_sock;
    bool is_running;
    TaskHandle_t task_handle;
} printer_tcp_server_t;

typedef struct {
    printer_tcp_server_t *printer_server;
    int client_sock;
} client_task_args_t;

static printer_tcp_server_t g_printer_servers[PRINTER_COUNT];
static bool g_tcp_servers_initialized = false;

/**
 * @brief TCP 客户端处理任务 - 处理客户端连接和打印数据
 * @param arg 参数 (client_task_args_t*)
 */
static void tcp_client_handle_task(void *arg)
{
    client_task_args_t *args = (client_task_args_t *)arg;
    printer_tcp_server_t *printer_server = args->printer_server;
    int client_sock = args->client_sock;
    
    // 释放动态分配的参数结构
    heap_caps_free(args);
    
    ESP_LOGI(TAG, "Client task started with socket %d on port %d", 
             client_sock, printer_server->port);

    uint8_t buffer[4096];
    int total_bytes = 0;
    int target_printer_instance = -1;
    
    // 根据端口查找绑定的序列号，再根据序列号查找打印机实例
    // 先获取所有绑定关系，找到当前端口对应的序列号
    printer_binding_t bindings[MAX_BINDINGS];
    size_t binding_count = 0;
    char target_serial[32] = {0};
    
    if (get_all_bindings(bindings, MAX_BINDINGS, &binding_count) == ESP_OK) {
        for (size_t i = 0; i < binding_count; i++) {
            if (bindings[i].port == printer_server->port) {
                strlcpy(target_serial, bindings[i].serial, sizeof(target_serial));
                ESP_LOGI(TAG, "Port %d bound to serial: %s", printer_server->port, target_serial);
                break;
            }
        }
    }
    
    // 如果找到了绑定的序列号，查找对应的打印机实例
    if (strlen(target_serial) > 0) {
        target_printer_instance = printer_find_instance_by_serial(target_serial);
        if (target_printer_instance >= 0) {
            ESP_LOGI(TAG, "Found printer instance %d for serial %s", target_printer_instance, target_serial);
        } else {
            ESP_LOGW(TAG, "Printer with serial %s not connected", target_serial);
        }
    } else {
        ESP_LOGW(TAG, "No binding found for port %d, using default instance %d", 
                 printer_server->port, printer_server->printer_instance);
        target_printer_instance = printer_server->printer_instance;
    }

    while (1) {
        // 接收客户端数据，设置 100ms 超时
        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000; // 100ms
        int ret_sockopt = setsockopt(client_sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        if (ret_sockopt != 0) {
            ESP_LOGW(TAG, "Failed to set SO_RCVTIMEO: errno=%d", errno);
        }

        int bytes_read = recv(client_sock, buffer, sizeof(buffer), 0);
        if (bytes_read < 0) {
            // 超时或错误
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                ESP_LOGE(TAG, "Failed to receive data from port %d: %d", 
                         printer_server->port, errno);
                break;
            }
            // 超时，继续等待
            continue;
        } else if (bytes_read == 0) {
            ESP_LOGI(TAG, "Client disconnected from port %d", printer_server->port);
            break;
        }

        total_bytes += bytes_read;
        ESP_LOGI(TAG, "Received %d bytes from client for port %d (total: %d)", 
                 bytes_read, printer_server->port, total_bytes);

        // 转发数据到目标打印机队列
        if (target_printer_instance >= 0) {
            esp_err_t err = printer_enqueue_data(target_printer_instance, buffer, bytes_read, 5000);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to enqueue data to printer %d: %s", 
                         target_printer_instance, esp_err_to_name(err));
            } else {
                ESP_LOGI(TAG, "Data enqueued to printer %d (serial %s)", target_printer_instance, target_serial);
            }
        } else {
            ESP_LOGE(TAG, "No target printer available for port %d", printer_server->port);
        }
    }

    ESP_LOGI(TAG, "Client task ended for port %d. Total received: %d bytes", 
             printer_server->port, total_bytes);
    close(client_sock);
    vTaskDelete(NULL);
}

/**
 * @brief 单个打印机的 TCP 服务器任务 - 监听指定端口
 * @param arg printer_tcp_server_t 结构
 */
static void printer_tcp_server_task(void *arg)
{
    printer_tcp_server_t *printer_server = (printer_tcp_server_t *)arg;
    
    ESP_LOGI(TAG, "TCP server task started for printer %d on port %d", 
             printer_server->printer_instance, printer_server->port);

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(printer_server->port);

    // 创建监听 Socket
    printer_server->listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (printer_server->listen_sock < 0) {
        ESP_LOGE(TAG, "Failed to create socket for printer %d", printer_server->printer_instance);
        printer_server->is_running = false;
        vTaskDelete(NULL);
        return;
    }

    // 设置 Socket 选项允许端口复用
    int opt = 1;
    setsockopt(printer_server->listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // 绑定 Socket
    int ret = bind(printer_server->listen_sock, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if (ret < 0) {
        ESP_LOGE(TAG, "Failed to bind socket for printer %d on port %d: errno=%d", 
                 printer_server->printer_instance, printer_server->port, errno);
        close(printer_server->listen_sock);
        printer_server->is_running = false;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Socket bound successfully for printer %d on port %d", 
             printer_server->printer_instance, printer_server->port);

    // 监听 Socket
    ret = listen(printer_server->listen_sock, 4);
    if (ret < 0) {
        ESP_LOGE(TAG, "Failed to listen socket for printer %d", printer_server->printer_instance);
        close(printer_server->listen_sock);
        printer_server->is_running = false;
        vTaskDelete(NULL);
        return;
    }

    printer_server->is_running = true;
    ESP_LOGI(TAG, "TCP server for printer %d listening on port %d", 
             printer_server->printer_instance, printer_server->port);

    // 接受客户端连接
    while (printer_server->is_running) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);

        int client_sock = accept(printer_server->listen_sock, (struct sockaddr *)&client_addr, &addr_len);
        if (client_sock < 0) {
            if (printer_server->is_running) {
                ESP_LOGE(TAG, "Failed to accept connection for printer %d", printer_server->printer_instance);
            }
            continue;
        }

        ESP_LOGI(TAG, "Client connected from %s:%d to printer %d (port %d)", 
                 inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port),
                 printer_server->printer_instance, printer_server->port);

        // 创建客户端处理任务，动态分配参数结构（使用PSRAM）
        client_task_args_t *args = (client_task_args_t *)heap_caps_malloc(sizeof(client_task_args_t), MALLOC_CAP_SPIRAM);
        if (!args) {
            ESP_LOGE(TAG, "Failed to allocate memory for client task args");
            close(client_sock);
            continue;
        }
        args->printer_server = printer_server;
        args->client_sock = client_sock;
        
        BaseType_t task_ret = xTaskCreate(tcp_client_handle_task, "tcp_client_task", 16384, args, 5, NULL);
        if (task_ret != pdPASS) {
            ESP_LOGE(TAG, "Failed to create client task for printer %d", printer_server->printer_instance);
            heap_caps_free(args);
            close(client_sock);
        }
    }

    // 关闭监听 Socket
    close(printer_server->listen_sock);
    printer_server->listen_sock = -1;
    printer_server->is_running = false;

    vTaskDelete(NULL);
}

esp_err_t tcp_server_init(void) {
    ESP_LOGI(TAG, "Initializing TCP servers for %d printers", PRINTER_COUNT);
    
    // 初始化每台打印机的 TCP 服务器配置
    for (int i = 0; i < PRINTER_COUNT; i++) {
        g_printer_servers[i].printer_instance = i;
        g_printer_servers[i].port = BASE_PORT + i;  // 9100, 9101, 9102, 9103
        g_printer_servers[i].listen_sock = -1;
        g_printer_servers[i].is_running = false;
        g_printer_servers[i].task_handle = NULL;
    }
    
    g_tcp_servers_initialized = true;
    ESP_LOGI(TAG, "TCP servers initialized: Printer 1->9100, Printer 2->9101, Printer 3->9102, Printer 4->9103");
    return ESP_OK;
}

esp_err_t tcp_server_start(void) {
    if (!g_tcp_servers_initialized) {
        ESP_LOGE(TAG, "TCP servers not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Starting TCP servers for %d printers", PRINTER_COUNT);
    
    // 为每台打印机创建独立的 TCP 服务器任务
    for (int i = 0; i < PRINTER_COUNT; i++) {
        BaseType_t ret = xTaskCreate(printer_tcp_server_task, "tcp_server_task", 16384, &g_printer_servers[i], 6, &g_printer_servers[i].task_handle);
        if (ret != pdPASS) {
            ESP_LOGE(TAG, "Failed to create TCP server task for printer %d", i);
            // 清理已创建的任务
            for (int j = 0; j < i; j++) {
                if (g_printer_servers[j].task_handle != NULL) {
                    vTaskDelete(g_printer_servers[j].task_handle);
                    g_printer_servers[j].task_handle = NULL;
                }
            }
            return ESP_ERR_NO_MEM;
        }
        vTaskDelay(pdMS_TO_TICKS(100)); // 延迟启动，避免端口冲突
    }
    
    ESP_LOGI(TAG, "All TCP servers started");
    return ESP_OK;
}

esp_err_t tcp_server_stop(void) {
    for (int i = 0; i < PRINTER_COUNT; i++) {
        g_printer_servers[i].is_running = false;
        if (g_printer_servers[i].task_handle != NULL) {
            vTaskDelete(g_printer_servers[i].task_handle);
            g_printer_servers[i].task_handle = NULL;
        }
    }
    g_tcp_servers_initialized = false;
    return ESP_OK;
}

esp_err_t tcp_server_accept_client(int* client_sock, struct sockaddr_in* client_addr) { return ESP_OK; }
esp_err_t tcp_server_close_client(int client_sock) { return ESP_OK; }
esp_err_t tcp_server_receive_data(int sock, uint8_t* buffer, size_t* len, uint32_t timeout_ms) { return ESP_OK; }
esp_err_t tcp_server_send_to_printer(uint8_t printer_instance, const uint8_t* data, size_t len) {
    return printer_send_data(printer_instance, data, len, 30000);
}

bool tcp_server_is_running(void) {
    for (int i = 0; i < PRINTER_COUNT; i++) {
        if (g_printer_servers[i].is_running) return true;
    }
    return false;
}

int tcp_server_get_client_count(void) { return 0; }

esp_err_t tcp_server_get_config(tcp_server_config_t* config) {
    if (config) {
        config->port = BASE_PORT;
        config->max_clients = PRINTER_COUNT;
        config->is_running = tcp_server_is_running();
    }
    return ESP_OK;
}

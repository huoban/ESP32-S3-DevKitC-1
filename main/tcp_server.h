/**
 * @file tcp_server.h
 * @brief TCP 打印服务模块头文件
 * @details 负责 TCP 9100 端口监听、打印数据接收和转发到 USB 打印机
 */

#ifndef TCP_SERVER_H
#define TCP_SERVER_H

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <sys/socket.h>
#include <netinet/in.h>

#ifdef __cplusplus
extern "C" {
#endif

// ==================== TCP 客户端连接信息结构 ====================
/**
 * @brief TCP 客户端连接信息结构
 */
typedef struct {
    int sock_fd;                     // Socket 文件描述符
    struct sockaddr_in client_addr;  // 客户端地址
    uint8_t printer_instance;        // 关联的打印机实例
    bool is_active;                  // 连接是否活跃
    uint32_t connect_time;           // 连接时间
    uint32_t bytes_sent;             // 发送字节数
    uint32_t bytes_received;         // 接收字节数
} tcp_client_t;

// ==================== TCP 服务器配置结构 ====================
/**
 * @brief TCP 服务器配置结构
 */
typedef struct {
    uint16_t port;                   // 监听端口 (9100)
    int max_clients;                 // 最大客户端数
    int listen_sock;                 // 监听 Socket
    bool is_running;                 // 运行状态
} tcp_server_config_t;

// ==================== TCP 服务器接口 ====================

/**
 * @brief 初始化 TCP 服务器
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t tcp_server_init(void);

/**
 * @brief 启动 TCP 服务器
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t tcp_server_start(void);

/**
 * @brief 停止 TCP 服务器
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t tcp_server_stop(void);

/**
 * @brief 接受客户端连接
 * @param client_sock 客户端 Socket 指针
 * @param client_addr 客户端地址指针
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t tcp_server_accept_client(int* client_sock, struct sockaddr_in* client_addr);

/**
 * @brief 关闭客户端连接
 * @param client_sock 客户端 Socket
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t tcp_server_close_client(int client_sock);

/**
 * @brief 从客户端接收数据
 * @param sock 客户端 Socket
 * @param buffer 数据缓冲区
 * @param len 数据长度指针
 * @param timeout_ms 超时时间（毫秒）
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t tcp_server_receive_data(int sock, uint8_t* buffer, size_t* len, uint32_t timeout_ms);

/**
 * @brief 向打印机发送数据
 * @param printer_instance 打印机实例 (0-3)
 * @param data 数据指针
 * @param len 数据长度
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t tcp_server_send_to_printer(uint8_t printer_instance, const uint8_t* data, size_t len);

/**
 * @brief 检查服务器是否运行
 * @return true 运行中，false 已停止
 */
bool tcp_server_is_running(void);

/**
 * @brief 获取客户端连接数
 * @return 客户端连接数
 */
int tcp_server_get_client_count(void);

/**
 * @brief 获取服务器配置
 * @param config 配置结构指针
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t tcp_server_get_config(tcp_server_config_t* config);

#ifdef __cplusplus
}
#endif

#endif // TCP_SERVER_H

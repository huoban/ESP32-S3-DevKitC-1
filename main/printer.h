/**
 * @file printer.h
 * @brief USB 打印机管理模块头文件
 * @details 负责 USB 打印机的枚举、状态监控和数据传输管理
 */

#ifndef PRINTER_H
#define PRINTER_H

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include "usb/usb_host.h"

#ifdef __cplusplus
extern "C" {
#endif

// ==================== 常量定义 ====================
#define PRINTER_MAX_COUNT 4         // 最大支持打印机数量
#define PRINTER_BUFFER_SIZE (1024 * 1024)  // 打印机缓冲区大小 (1MB)
#define PRINTER_TRANSFER_CHUNK_SIZE 4096    // USB传输块大小 (4KB)

// ==================== 打印机状态枚举 ====================
/**
 * @brief 打印机状态枚举
 */
typedef enum {
    PRINTER_STATUS_DISCONNECTED = 0,  // 断开
    PRINTER_STATUS_READY = 1,         // 就绪
    PRINTER_STATUS_ERROR = 2,         // 异常
    PRINTER_STATUS_BUSY = 3           // 忙碌
} printer_status_t;

// ==================== USB 打印机信息结构 ====================
/**
 * @brief USB 打印机信息结构
 */
typedef struct {
    uint8_t dev_addr;                 // 设备地址
    uint8_t instance;                 // 实例编号 (0-3)
    printer_status_t status;          // 打印机状态
    char device_name[64];             // 设备名称
    char vendor_name[64];             // 厂商名称
    char serial_number[64];           // 序列号
    uint8_t dev_desc[256];            // 设备描述符缓冲区
    bool is_busy;                     // 是否正在打印
    uint32_t last_active_time;        // 最后活动时间
    uint8_t ep_in;                    // 输入端点
    uint8_t ep_out;                   // 输出端点
    uint16_t max_packet_size;         // 最大包大小
    usb_device_handle_t dev_hdl;      // USB 设备句柄
} usb_printer_t;

// ==================== 打印机缓冲区结构 ====================
/**
 * @brief 打印机缓冲区结构（环形缓冲）
 */
typedef struct {
    uint8_t* buffer;                  // 缓冲区指针（PSRAM）
    size_t buffer_size;               // 缓冲区大小 (1MB)
    size_t head;                      // 写指针
    size_t tail;                      // 读指针
    SemaphoreHandle_t sem_read;       // 读信号量
    SemaphoreHandle_t sem_write;      // 写信号量
} printer_buffer_t;

// ==================== 打印机管理接口 ====================

/**
 * @brief 初始化 USB 打印机管理模块
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t printer_init(void);

/**
 * @brief 启动 USB 打印机管理
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t printer_start(void);

/**
 * @brief 停止 USB 打印机管理
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t printer_stop(void);

/**
 * @brief 获取打印机信息
 * @param instance 打印机实例 (0-3)
 * @param info 打印机信息指针
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t printer_get_info(uint8_t instance, usb_printer_t* info);

/**
 * @brief 向打印机发送数据
 * @param instance 打印机实例 (0-3)
 * @param data 数据指针
 * @param len 数据长度
 * @param timeout_ms 超时时间（毫秒）
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t printer_send_data(uint8_t instance, const uint8_t* data, size_t len, uint32_t timeout_ms);

/**
 * @brief 从打印机接收数据
 * @param instance 打印机实例 (0-3)
 * @param data 数据缓冲区
 * @param len 数据长度指针
 * @param timeout_ms 超时时间（毫秒）
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t printer_receive_data(uint8_t instance, uint8_t* data, size_t* len, uint32_t timeout_ms);

/**
 * @brief 获取打印机状态
 * @param instance 打印机实例 (0-3)
 * @return 打印机状态
 */
printer_status_t printer_get_status(uint8_t instance);

/**
 * @brief 检查打印机是否已连接
 * @param instance 打印机实例 (0-3)
 * @return true 已连接，false 未连接
 */
bool printer_is_connected(uint8_t instance);

/**
 * @brief 检查打印机是否忙碌
 * @param instance 打印机实例 (0-3)
 * @return true 忙碌，false 空闲
 */
bool printer_is_busy(uint8_t instance);

/**
 * @brief 设置打印机忙碌状态
 * @param instance 打印机实例 (0-3)
 * @param busy 忙碌状态
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t printer_set_busy(uint8_t instance, bool busy);

/**
 * @brief 初始化打印机缓冲区
 * @param instance 打印机实例 (0-3)
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t printer_buffer_init(uint8_t instance);

/**
 * @brief 向缓冲区写入数据
 * @param instance 打印机实例 (0-3)
 * @param data 数据指针
 * @param len 数据长度
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t printer_buffer_write(uint8_t instance, const uint8_t* data, size_t len);

/**
 * @brief 从缓冲区读取数据
 * @param instance 打印机实例 (0-3)
 * @param data 数据缓冲区
 * @param len 数据长度指针
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t printer_buffer_read(uint8_t instance, uint8_t* data, size_t* len);

/**
 * @brief 获取缓冲区可用空间
 * @param instance 打印机实例 (0-3)
 * @return 可用空间大小
 */
size_t printer_buffer_available(uint8_t instance);

/**
 * @brief 获取缓冲区数据长度
 * @param instance 打印机实例 (0-3)
 * @return 数据长度
 */
size_t printer_buffer_length(uint8_t instance);

/**
 * @brief 清空缓冲区
 * @param instance 打印机实例 (0-3)
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t printer_buffer_clear(uint8_t instance);

/**
 * @brief USB 设备连接回调
 * @param dev_addr 设备地址
 * @param dev_desc 设备描述符
 * @param desc_len 描述符长度
 */
void printer_on_device_connected(uint8_t dev_addr, const uint8_t* dev_desc, size_t desc_len);

/**
 * @brief USB 设备断开回调
 * @param dev_addr 设备地址
 */
void printer_on_device_disconnected(uint8_t dev_addr);

/**
 * @brief 根据序列号查找打印机实例
 * @param serial_number 打印机序列号
 * @return 找到的打印机实例，如果未找到返回-1
 */
int printer_find_instance_by_serial(const char* serial_number);

#ifdef __cplusplus
}
#endif

#endif // PRINTER_H

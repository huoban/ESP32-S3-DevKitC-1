#ifndef NVS_MANAGER_H
#define NVS_MANAGER_H

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// 绑定信息结构
typedef struct {
    char serial[32];    // 打印机序列号
    uint16_t port;      // 绑定的TCP端口
    char remark[64];    // 备注信息（最少16个字符）
} printer_binding_t;

/**
 * @brief 初始化 NVS 存储
 * @return esp_err_t 错误代码
 */
esp_err_t init_nvs(void);

/**
 * @brief 保存打印机序列号到 TCP 端口的绑定关系（带备注）
 * @param serial 打印机序列号
 * @param port TCP 端口号
 * @param remark 备注信息（最少16个字符，可选）
 * @return esp_err_t 错误代码
 */
esp_err_t save_binding_with_remark(const char *serial, uint16_t port, const char *remark);

/**
 * @brief 保存打印机序列号到 TCP 端口的绑定关系
 * @param serial 打印机序列号
 * @param port TCP 端口号
 * @return esp_err_t 错误代码
 */
esp_err_t save_binding(const char *serial, uint16_t port);

/**
 * @brief 删除打印机序列号的绑定关系
 * @param serial 打印机序列号
 * @return esp_err_t 错误代码
 */
esp_err_t remove_binding(const char *serial);

/**
 * @brief 获取打印机序列号绑定的 TCP 端口
 * @param serial 打印机序列号
 * @return uint16_t 绑定的 TCP 端口，如果未绑定返回 0
 */
uint16_t get_binding_port(const char *serial);

/**
 * @brief 获取所有绑定的打印机信息
 * @param bindings 绑定信息数组
 * @param max_bindings 最大绑定数量
 * @param count 实际返回的绑定数量
 * @return esp_err_t 错误代码
 */
esp_err_t get_all_bindings(printer_binding_t* bindings, size_t max_bindings, size_t* count);

#ifdef __cplusplus
}
#endif

#endif // NVS_MANAGER_H

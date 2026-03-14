#ifndef WEB_RESOURCES_H
#define WEB_RESOURCES_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Web 资源文件信息结构
 */
typedef struct {
    const char *filename;      // 文件名（路径）
    const uint8_t *data;       // 数据指针
    size_t size;               // 文件大小
} web_resource_t;

/**
 * @brief 初始化 Web 资源管理
 * @details 从 Flash 复制 Web 资源到 PSRAM
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t web_resources_init(void);

/**
 * @brief 获取 Web 资源文件
 * @param filename 文件名（如 "/index.html"）
 * @return 资源文件指针，失败返回 NULL
 */
const web_resource_t* web_resources_get(const char *filename);

/**
 * @brief 获取 Web 资源文件数量
 * @return 资源文件数量
 */
size_t web_resources_get_count(void);

/**
 * @brief 获取所有 Web 资源列表
 * @return 资源列表指针
 */
const web_resource_t* web_resources_get_list(void);

/**
 * @brief 释放 Web 资源（释放 PSRAM 内存）
 */
void web_resources_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // WEB_RESOURCES_H

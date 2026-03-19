#include "nvs_manager.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "common_defs.h"
#include <stdlib.h>
#include <inttypes.h>

#define NVS_NAMESPACE "print_bind"
#define TAG "NVS_MANAGER"
#define BINDINGS_LIST_KEY "bindings_list"
#define REMARK_KEY_PREFIX "rmk_"

/**
 * @brief 计算序列号的哈希值
 * @param serial 序列号
 * @return 32位哈希值
 */
static uint32_t calculate_serial_hash(const char *serial) {
    uint32_t hash = 0;
    const char *p = serial;
    while (*p) {
        hash = hash * 31 + (unsigned char)(*p++);
    }
    return hash;
}

// 初始化NVS存储 - 初始化NVS Flash并处理分区损坏情况
esp_err_t init_nvs(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    return ret;
}

// 保存打印机绑定关系（带备注） - 将打印机序列号绑定到指定端口并保存备注
esp_err_t save_binding_with_remark(const char *serial, uint16_t port, const char *remark) {
    if (!serial || *serial == '\0' || port < 1024) {
        ESP_LOGE(TAG, "Invalid argument: serial=%s, port=%d", serial ? serial : "(null)", port);
        return ESP_ERR_INVALID_ARG;
    }
    
    // 备注最多 16 个字符（UTF-8 编码，支持中文）
    if (remark && strlen(remark) > 16) {
        ESP_LOGE(TAG, "Remark too long: %d bytes", strlen(remark));
        return ESP_ERR_INVALID_ARG;
    }
    
    // NVS 键名最大长度为 15 字符，使用"bind_"前缀 + 序列号（截断）
    char key[16];
    snprintf(key, sizeof(key), "bind_%.9s", serial);
    
    // 备注键名：使用"rmk_"前缀 + 序列号哈希值（确保键名不超过 15 字符）
    char remark_key[16];
    uint32_t hash = calculate_serial_hash(serial);
    snprintf(remark_key, sizeof(remark_key), "rmk_%08" PRIX32, hash);
    
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return err;
    }
    
    // 保存绑定
    err = nvs_set_u16(handle, key, port);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set port: %s", esp_err_to_name(err));
    }
    
    // 保存备注（如果有）
    if (err == ESP_OK && remark && strlen(remark) > 0) {
        err = nvs_set_str(handle, remark_key, remark);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set remark: %s", esp_err_to_name(err));
        }
    }
    
    // 更新绑定列表
    if (err == ESP_OK) {
        // 获取当前绑定列表
        char bindings_list[512] = {0};
        size_t required_len = sizeof(bindings_list);
        esp_err_t list_err = nvs_get_str(handle, BINDINGS_LIST_KEY, bindings_list, &required_len);
        if (list_err != ESP_OK && list_err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to get bindings list: %s", esp_err_to_name(list_err));
        }
        
        // 检查序列号是否已在列表中
        if (strstr(bindings_list, serial) == NULL) {
            // 添加到列表
            if (strlen(bindings_list) > 0) {
                strlcat(bindings_list, ",", sizeof(bindings_list));
            }
            strlcat(bindings_list, serial, sizeof(bindings_list));
            err = nvs_set_str(handle, BINDINGS_LIST_KEY, bindings_list);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to set bindings list: %s", esp_err_to_name(err));
            }
        }
        
        if (err == ESP_OK) {
            err = nvs_commit(handle);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to commit: %s", esp_err_to_name(err));
            }
        }
    }
    
    nvs_close(handle);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Saved binding: %s -> %d, remark: %s", serial, port, remark ? remark : "(none)");
    }
    return err;
}

// 向后兼容的函数
esp_err_t save_binding(const char *serial, uint16_t port) {
    return save_binding_with_remark(serial, port, NULL);
}

// 移除打印机绑定关系 - 删除指定序列号的绑定关系和备注
esp_err_t remove_binding(const char *serial) {
    if (!serial || *serial == '\0') return ESP_ERR_INVALID_ARG;
    
    // NVS 键名最大长度为 15 字符，使用"bind_"前缀 + 序列号（截断）
    char key[16];
    snprintf(key, sizeof(key), "bind_%.9s", serial);
    
    // 备注键名：使用哈希值
    char remark_key[16];
    uint32_t hash = calculate_serial_hash(serial);
    snprintf(remark_key, sizeof(remark_key), "rmk_%08" PRIX32, hash);
    
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return err;
    }
    
    // 删除绑定
    err = nvs_erase_key(handle, key);
    
    // 删除备注
    if (err == ESP_OK) {
        nvs_erase_key(handle, remark_key);
        err = ESP_OK;
    }
    
    // 更新绑定列表
    if (err == ESP_OK) {
        char bindings_list[512] = {0};
        size_t required_len = sizeof(bindings_list);
        if (nvs_get_str(handle, BINDINGS_LIST_KEY, bindings_list, &required_len) == ESP_OK) {
            // 从列表中删除该序列号
            char new_list[512] = {0};
            char *token = strtok(bindings_list, ",");
            bool first = true;
            
            while (token != NULL) {
                if (strcmp(token, serial) != 0) {
                    if (!first) {
                        strlcat(new_list, ",", sizeof(new_list));
                    }
                    strlcat(new_list, token, sizeof(new_list));
                    first = false;
                }
                token = strtok(NULL, ",");
            }
            
            if (strlen(new_list) > 0) {
                nvs_set_str(handle, BINDINGS_LIST_KEY, new_list);
            } else {
                nvs_erase_key(handle, BINDINGS_LIST_KEY);
            }
        }
        err = nvs_commit(handle);
        ESP_LOGI(TAG, "Removed binding: %s", serial);
    }
    
    nvs_close(handle);
    return err;
}

// 获取所有绑定关系 - 读取NVS中保存的所有打印机绑定关系
esp_err_t get_all_bindings(printer_binding_t* bindings, size_t max_bindings, size_t* count) {
    if (!bindings || !count) {
        return ESP_ERR_INVALID_ARG;
    }
    
    *count = 0;
    
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return err;
    }
    
    // 获取绑定列表
    char bindings_list[512] = {0};
    size_t required_len = sizeof(bindings_list);
    if (nvs_get_str(handle, BINDINGS_LIST_KEY, bindings_list, &required_len) != ESP_OK) {
        nvs_close(handle);
        return ESP_OK; // 没有绑定，返回空列表
    }
    
    // 解析列表并获取每个绑定
    char temp_list[512];
    strlcpy(temp_list, bindings_list, sizeof(temp_list));
    
    char *token = strtok(temp_list, ",");
    while (token != NULL && *count < max_bindings) {
        uint16_t port = get_binding_port(token);
        if (port > 0) {
            strlcpy(bindings[*count].serial, token, sizeof(bindings[*count].serial));
            bindings[*count].port = port;
            
            // 读取备注（使用哈希键名）
            char remark_key[16];
            uint32_t hash = calculate_serial_hash(token);
            snprintf(remark_key, sizeof(remark_key), "rmk_%08" PRIX32, hash);
            
            char remark[64] = {0};
            size_t remark_len = sizeof(remark);
            if (nvs_get_str(handle, remark_key, remark, &remark_len) == ESP_OK) {
                strlcpy(bindings[*count].remark, remark, sizeof(bindings[*count].remark));
            } else {
                bindings[*count].remark[0] = '\0';
            }
            
            (*count)++;
        }
        token = strtok(NULL, ",");
    }
    
    nvs_close(handle);
    return ESP_OK;
}

// 获取绑定端口 - 根据打印机序列号查找绑定的端口号
uint16_t get_binding_port(const char *serial) {
    if (!serial || *serial == '\0') return 0;
    
    // NVS 键名最大长度为 15 字符，使用"bind_"前缀 + 序列号（截断）
    char key[16];
    snprintf(key, sizeof(key), "bind_%.9s", serial);
    
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) 
        return 0;
    
    uint16_t port = 0;
    esp_err_t err = nvs_get_u16(handle, key, &port);
    nvs_close(handle);
    
    return (err == ESP_OK) ? port : 0;
}

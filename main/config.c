/**
 * @file config.c
 * @brief 配置管理模块实现
 * @details 负责 NVS 配置的读写、持久化存储和恢复出厂设置功能
 */

#include "config.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "CONFIG";

/**
 * @brief 初始化配置管理模块 - 初始化 NVS 存储并打开命名空间
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t config_init(void)
{
    // 初始化 NVS
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS 分区已损坏，擦除并重试
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_LOGI(TAG, "Config manager initialized");

    return ESP_OK;
}

/**
 * @brief 保存 WiFi 配置 - 将 WiFi SSID 和密码保存到 NVS
 * @param config WiFi 配置指针
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t config_save_wifi(const wifi_config_t_custom* config)
{
    nvs_handle_t nvs_handle;
    esp_err_t err;

    // 参数检查
    if (config == NULL) {
        ESP_LOGE(TAG, "Invalid argument: config is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    // 打开 NVS
    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS namespace!", esp_err_to_name(err));
        return err;
    }

    // 保存 SSID
    err = nvs_set_str(nvs_handle, NVS_KEY_WIFI_SSID, config->ssid);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error saving WiFi SSID!");
        nvs_close(nvs_handle);
        return err;
    }

    // 保存密码
    err = nvs_set_str(nvs_handle, NVS_KEY_WIFI_PASSWORD, config->password);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error saving WiFi password!");
        nvs_close(nvs_handle);
        return err;
    }

    // 保存配置标记
    uint8_t configured = config->is_configured ? 1 : 0;
    err = nvs_set_u8(nvs_handle, NVS_KEY_WIFI_MODE, configured);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error saving WiFi configured flag!");
        nvs_close(nvs_handle);
        return err;
    }

    // 保存静态 IP 配置
    uint8_t use_static = config->use_static_ip ? 1 : 0;
    if (config->use_static_ip) {
        err = nvs_set_str(nvs_handle, "wifi_ip", config->ip_address);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Error saving WiFi IP!");
            nvs_close(nvs_handle);
            return err;
        }
        
        err = nvs_set_str(nvs_handle, "wifi_gateway", config->gateway);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Error saving WiFi gateway!");
            nvs_close(nvs_handle);
            return err;
        }
        
        err = nvs_set_str(nvs_handle, "wifi_netmask", config->netmask);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Error saving WiFi netmask!");
            nvs_close(nvs_handle);
            return err;
        }
        
        err = nvs_set_str(nvs_handle, "wifi_dns", config->dns);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Error saving WiFi DNS!");
            nvs_close(nvs_handle);
            return err;
        }
    }
    
    err = nvs_set_u8(nvs_handle, "wifi_use_static", use_static);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error saving WiFi static IP flag!");
        nvs_close(nvs_handle);
        return err;
    }

    // 提交更改
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error committing NVS changes!");
    }

    nvs_close(nvs_handle);

    ESP_LOGI(TAG, "WiFi config saved: SSID=%s, static_ip=%d", config->ssid, config->use_static_ip);

    return err;
}

/**
 * @brief 加载 WiFi 配置 - 从 NVS 读取 WiFi SSID 和密码
 * @param config WiFi 配置指针
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t config_load_wifi(wifi_config_t_custom* config)
{
    nvs_handle_t nvs_handle;
    esp_err_t err;

    // 参数检查
    if (config == NULL) {
        ESP_LOGE(TAG, "Invalid argument: config is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    // 打开 NVS
    err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS namespace!", esp_err_to_name(err));
        return err;
    }

    // 读取 SSID
    size_t required_size = sizeof(config->ssid);
    err = nvs_get_str(nvs_handle, NVS_KEY_WIFI_SSID, config->ssid, &required_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error reading WiFi SSID!");
        nvs_close(nvs_handle);
        return err;
    }

    // 读取密码
    required_size = sizeof(config->password);
    err = nvs_get_str(nvs_handle, NVS_KEY_WIFI_PASSWORD, config->password, &required_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error reading WiFi password!");
        nvs_close(nvs_handle);
        return err;
    }

    // 读取配置标记
    uint8_t configured = 0;
    err = nvs_get_u8(nvs_handle, NVS_KEY_WIFI_MODE, &configured);
    config->is_configured = (configured == 1);

    // 读取静态 IP 配置
    uint8_t use_static = 0;
    err = nvs_get_u8(nvs_handle, "wifi_use_static", &use_static);
    config->use_static_ip = (use_static == 1);
    
    if (config->use_static_ip) {
        required_size = sizeof(config->ip_address);
        err = nvs_get_str(nvs_handle, "wifi_ip", config->ip_address, &required_size);
        if (err != ESP_OK) {
            config->ip_address[0] = '\0';
        }
        
        required_size = sizeof(config->gateway);
        err = nvs_get_str(nvs_handle, "wifi_gateway", config->gateway, &required_size);
        if (err != ESP_OK) {
            config->gateway[0] = '\0';
        }
        
        required_size = sizeof(config->netmask);
        err = nvs_get_str(nvs_handle, "wifi_netmask", config->netmask, &required_size);
        if (err != ESP_OK) {
            config->netmask[0] = '\0';
        }
        
        required_size = sizeof(config->dns);
        err = nvs_get_str(nvs_handle, "wifi_dns", config->dns, &required_size);
        if (err != ESP_OK) {
            config->dns[0] = '\0';
        }
    } else {
        config->ip_address[0] = '\0';
        config->gateway[0] = '\0';
        config->netmask[0] = '\0';
        config->dns[0] = '\0';
    }

    nvs_close(nvs_handle);

    ESP_LOGI(TAG, "WiFi config loaded: SSID=%s, configured=%d, static_ip=%d", 
             config->ssid, config->is_configured, config->use_static_ip);

    return ESP_OK;
}

/**
 * @brief 保存 NTP 配置 - 将 NTP 服务器和同步间隔保存到 NVS
 * @param config NTP 配置指针
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t config_save_ntp(const ntp_config_t* config)
{
    nvs_handle_t nvs_handle;
    esp_err_t err;

    // 参数检查
    if (config == NULL) {
        ESP_LOGE(TAG, "Invalid argument: config is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    // 打开 NVS
    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS namespace!", esp_err_to_name(err));
        return err;
    }

    // 保存配置
    err = nvs_set_blob(nvs_handle, NVS_KEY_NTP_CONFIG, config, sizeof(ntp_config_t));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error saving NTP config!");
        nvs_close(nvs_handle);
        return err;
    }

    // 提交更改
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error committing NVS changes!");
    }

    nvs_close(nvs_handle);

    ESP_LOGI(TAG, "NTP config saved");

    return err;
}

/**
 * @brief 加载 NTP 配置 - 从 NVS 读取 NTP 配置
 * @param config NTP 配置指针
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t config_load_ntp(ntp_config_t* config)
{
    nvs_handle_t nvs_handle;
    esp_err_t err;

    // 参数检查
    if (config == NULL) {
        ESP_LOGE(TAG, "Invalid argument: config is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    // 打开 NVS
    err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS namespace!", esp_err_to_name(err));
        return err;
    }

    // 读取配置
    size_t required_size = sizeof(ntp_config_t);
    err = nvs_get_blob(nvs_handle, NVS_KEY_NTP_CONFIG, config, &required_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error reading NTP config!");
        nvs_close(nvs_handle);
        return err;
    }

    nvs_close(nvs_handle);

    ESP_LOGI(TAG, "NTP config loaded");

    return ESP_OK;
}

/**
 * @brief 保存监控配置 - 将监控网站列表保存到 NVS
 * @param sites 监控网站列表
 * @param count 网站数量
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t config_save_monitor(const monitor_site_t* sites, size_t count)
{
    nvs_handle_t nvs_handle;
    esp_err_t err;

    // 参数检查
    if (sites == NULL) {
        ESP_LOGE(TAG, "Invalid argument: sites is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    // 打开 NVS
    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS namespace!", esp_err_to_name(err));
        return err;
    }

    // 保存网站数量
    err = nvs_set_blob(nvs_handle, NVS_KEY_MONITOR_SITES, sites, count * sizeof(monitor_site_t));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error saving monitor config!");
        nvs_close(nvs_handle);
        return err;
    }

    // 提交更改
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error committing NVS changes!");
    }

    nvs_close(nvs_handle);

    ESP_LOGI(TAG, "Monitor config saved: %d sites", count);

    return err;
}

/**
 * @brief 加载监控配置 - 从 NVS 读取监控网站列表
 * @param sites 监控网站列表
 * @param count 网站数量指针
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t config_load_monitor(monitor_site_t* sites, size_t* count)
{
    nvs_handle_t nvs_handle;
    esp_err_t err;

    // 参数检查
    if (sites == NULL || count == NULL) {
        ESP_LOGE(TAG, "Invalid argument: sites or count is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    // 打开 NVS
    err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS namespace!", esp_err_to_name(err));
        return err;
    }

    // 读取配置
    size_t required_size = 10 * sizeof(monitor_site_t);
    err = nvs_get_blob(nvs_handle, NVS_KEY_MONITOR_SITES, sites, &required_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error reading monitor config!");
        nvs_close(nvs_handle);
        return err;
    }

    *count = required_size / sizeof(monitor_site_t);

    nvs_close(nvs_handle);

    ESP_LOGI(TAG, "Monitor config loaded: %d sites", *count);

    return ESP_OK;
}

/**
 * @brief 保存企业微信配置 - 将企业微信配置保存到 NVS
 * @param config 企业微信配置指针
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t config_save_wechat(const wechat_config_t* config)
{
    nvs_handle_t nvs_handle;
    esp_err_t err;

    // 参数检查
    if (config == NULL) {
        ESP_LOGE(TAG, "Invalid argument: config is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    // 打开 NVS
    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS namespace!", esp_err_to_name(err));
        return err;
    }

    // 保存配置
    err = nvs_set_blob(nvs_handle, NVS_KEY_WECHAT_CONFIG, config, sizeof(wechat_config_t));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error saving wechat config!");
        nvs_close(nvs_handle);
        return err;
    }

    // 提交更改
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error committing NVS changes!");
    }

    nvs_close(nvs_handle);

    ESP_LOGI(TAG, "WeChat config saved");

    return err;
}

/**
 * @brief 加载企业微信配置 - 从 NVS 读取企业微信配置
 * @param config 企业微信配置指针
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t config_load_wechat(wechat_config_t* config)
{
    nvs_handle_t nvs_handle;
    esp_err_t err;

    // 参数检查
    if (config == NULL) {
        ESP_LOGE(TAG, "Invalid argument: config is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    // 打开 NVS
    err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS namespace!", esp_err_to_name(err));
        return err;
    }

    // 读取配置
    size_t required_size = sizeof(wechat_config_t);
    err = nvs_get_blob(nvs_handle, NVS_KEY_WECHAT_CONFIG, config, &required_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error reading wechat config!");
        nvs_close(nvs_handle);
        return err;
    }

    nvs_close(nvs_handle);

    ESP_LOGI(TAG, "WeChat config loaded");

    return ESP_OK;
}

/**
 * @brief 恢复出厂设置 - 清空 NVS 中的所有配置
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t config_factory_reset(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err;

    // 打开 NVS
    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS namespace!", esp_err_to_name(err));
        return err;
    }

    // 擦除所有配置
    err = nvs_erase_all(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error erasing NVS data!");
        nvs_close(nvs_handle);
        return err;
    }

    // 提交更改
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error committing NVS changes!");
    }

    nvs_close(nvs_handle);
    
    // 擦除打印机绑定命名空间
    err = nvs_open("print_bind", NVS_READWRITE, &nvs_handle);
    if (err == ESP_OK) {
        esp_err_t bind_err = nvs_erase_all(nvs_handle);
        if (bind_err == ESP_OK) {
            nvs_commit(nvs_handle);
            ESP_LOGI(TAG, "Printer bindings cleared");
        }
        nvs_close(nvs_handle);
    }

    ESP_LOGI(TAG, "Factory reset completed");

    return ESP_OK;
}

/**
 * @brief 检查 WiFi 配置是否存在 - 检查 NVS 中是否有 WiFi 配置
 * @return true 存在，false 不存在
 */
bool config_has_wifi(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err;

    // 打开 NVS
    err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        return false;
    }

    // 检查配置标记
    uint8_t configured = 0;
    err = nvs_get_u8(nvs_handle, NVS_KEY_WIFI_MODE, &configured);

    nvs_close(nvs_handle);

    return (err == ESP_OK && configured == 1);
}

/**
 * @brief 保存设备名称 - 将设备名称保存到 NVS
 * @param name 设备名称
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t config_save_device_name(const char* name)
{
    nvs_handle_t nvs_handle;
    esp_err_t err;

    // 打开 NVS
    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS namespace!", esp_err_to_name(err));
        return err;
    }

    // 保存设备名称
    err = nvs_set_str(nvs_handle, NVS_KEY_DEVICE_NAME, name);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error saving device name!");
        nvs_close(nvs_handle);
        return err;
    }

    // 提交更改
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error committing NVS changes!");
    }

    nvs_close(nvs_handle);

    ESP_LOGI(TAG, "Device name saved: %s", name);

    return err;
}

/**
 * @brief 加载设备名称 - 从 NVS 读取设备名称
 * @param name 设备名称缓冲区
 * @param len 缓冲区长度
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t config_load_device_name(char* name, size_t len)
{
    nvs_handle_t nvs_handle;
    esp_err_t err;

    // 参数检查
    if (name == NULL || len == 0) {
        ESP_LOGE(TAG, "Invalid argument: name is NULL or len is 0");
        return ESP_ERR_INVALID_ARG;
    }

    // 打开 NVS
    err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        // 如果 NVS 打开失败，使用默认名称
        strlcpy(name, "ESP32-S3_Printer", len);
        ESP_LOGI(TAG, "NVS not available, using default device name: %s", name);
        return ESP_OK;
    }

    // 读取设备名称
    size_t required_size = len;
    err = nvs_get_str(nvs_handle, NVS_KEY_DEVICE_NAME, name, &required_size);
    if (err != ESP_OK) {
        // 如果没有保存设备名称，使用默认名称
        strlcpy(name, "ESP32-S3_Printer", len);
        ESP_LOGI(TAG, "Using default device name: %s", name);
    } else {
        ESP_LOGI(TAG, "Device name loaded: %s", name);
    }

    nvs_close(nvs_handle);

    return ESP_OK;
}

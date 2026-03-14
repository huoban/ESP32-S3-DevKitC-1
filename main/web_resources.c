/**
 * @file web_resources.c
 * @brief Web 资源管理模块实现
 * @details 负责将嵌入在固件中的 Web 资源复制到 PSRAM，并提供访问接口
 */

#include "web_resources.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <string.h>
#include <sys/param.h>

static const char *TAG = "WEB_RESOURCES";

// PSRAM 中存储 Web 资源的指针
static web_resource_t *s_resources = NULL;
static size_t s_resource_count = 0;

// 外部链接符号（由 CMake 自动生成）
extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[] asm("_binary_index_html_end");

extern const uint8_t wifi_html_start[] asm("_binary_wifi_html_start");
extern const uint8_t wifi_html_end[] asm("_binary_wifi_html_end");

extern const uint8_t usb_html_start[] asm("_binary_usb_html_start");
extern const uint8_t usb_html_end[] asm("_binary_usb_html_end");

extern const uint8_t monitor_html_start[] asm("_binary_monitor_html_start");
extern const uint8_t monitor_html_end[] asm("_binary_monitor_html_end");

extern const uint8_t app_js_start[] asm("_binary_app_js_start");
extern const uint8_t app_js_end[] asm("_binary_app_js_end");

extern const uint8_t style_css_start[] asm("_binary_style_css_start");
extern const uint8_t style_css_end[] asm("_binary_style_css_end");

// 资源列表（编译时嵌入）
static const struct {
    const char *filename;
    const uint8_t *data_start;
    const uint8_t *data_end;
} s_embedded_resources[] = {
    {"/", index_html_start, index_html_end},
    {"/index.html", index_html_start, index_html_end},
    {"/wifi.html", wifi_html_start, wifi_html_end},
    {"/usb.html", usb_html_start, usb_html_end},
    {"/monitor.html", monitor_html_start, monitor_html_end},
    {"/js/app.js", app_js_start, app_js_end},
    {"/css/style.css", style_css_start, style_css_end},
};

#define EMBEDDED_RESOURCE_COUNT (sizeof(s_embedded_resources) / sizeof(s_embedded_resources[0]))

/**
 * @brief 复制单个资源到 PSRAM - 自动剥离 UTF-8 BOM
 */
static esp_err_t copy_resource_to_psram(size_t index)
{
    const uint8_t *src = s_embedded_resources[index].data_start;
    size_t size = s_embedded_resources[index].data_end - s_embedded_resources[index].data_start;
    size_t offset = 0;
    
    // 检查并剥离 UTF-8 BOM (0xEF, 0xBB, 0xBF)
    if (size >= 3 && src[0] == 0xEF && src[1] == 0xBB && src[2] == 0xBF) {
        offset = 3;
        size -= 3;
        ESP_LOGI(TAG, "Stripped UTF-8 BOM from %s", s_embedded_resources[index].filename);
    }
    
    // 去除末尾的 NULL 字符 (0x00)
    while (size > 0 && src[offset + size - 1] == 0x00) {
        size--;
    }
    
    // 从 PSRAM 分配内存
    uint8_t *psram_data = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    if (psram_data == NULL) {
        ESP_LOGE(TAG, "Failed to allocate PSRAM for %s", s_embedded_resources[index].filename);
        return ESP_ERR_NO_MEM;
    }
    
    // 复制数据到 PSRAM（跳过 BOM）
    memcpy(psram_data, src + offset, size);
    
    // 保存到资源列表
    s_resources[index].filename = s_embedded_resources[index].filename;
    s_resources[index].data = psram_data;
    s_resources[index].size = size;
    
    ESP_LOGD(TAG, "Copied %s to PSRAM (%zu bytes)", s_embedded_resources[index].filename, size);
    
    return ESP_OK;
}

esp_err_t web_resources_init(void)
{
    if (s_resources != NULL) {
        ESP_LOGW(TAG, "Web resources already initialized");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Initializing web resources...");
    ESP_LOGI(TAG, "Found %d embedded web resources", EMBEDDED_RESOURCE_COUNT);
    
    // 从 PSRAM 分配资源列表
    s_resources = heap_caps_malloc(EMBEDDED_RESOURCE_COUNT * sizeof(web_resource_t), MALLOC_CAP_SPIRAM);
    if (s_resources == NULL) {
        ESP_LOGE(TAG, "Failed to allocate PSRAM for resource list");
        return ESP_ERR_NO_MEM;
    }
    
    memset(s_resources, 0, EMBEDDED_RESOURCE_COUNT * sizeof(web_resource_t));
    s_resource_count = 0;
    
    // 复制所有资源到 PSRAM
    esp_err_t err = ESP_OK;
    for (size_t i = 0; i < EMBEDDED_RESOURCE_COUNT; i++) {
        err = copy_resource_to_psram(i);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to copy resource %s", s_embedded_resources[i].filename);
            // 释放已分配的资源
            web_resources_deinit();
            return err;
        }
        s_resource_count++;
    }
    
    ESP_LOGI(TAG, "Web resources initialized successfully");
    ESP_LOGI(TAG, "Total size: %zu bytes", 
             heap_caps_get_total_size(MALLOC_CAP_SPIRAM) - heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    
    return ESP_OK;
}

const web_resource_t* web_resources_get(const char *filename)
{
    if (s_resources == NULL || filename == NULL) {
        return NULL;
    }
    
    ESP_LOGI(TAG, "Looking for resource: %s", filename);
    
    for (size_t i = 0; i < s_resource_count; i++) {
        ESP_LOGI(TAG, "  Comparing with: %s", s_resources[i].filename);
        if (strcmp(s_resources[i].filename, filename) == 0) {
            ESP_LOGI(TAG, "  Found: %s", filename);
            return &s_resources[i];
        }
    }
    
    ESP_LOGW(TAG, "Resource not found: %s", filename);
    return NULL;
}

size_t web_resources_get_count(void)
{
    return s_resource_count;
}

const web_resource_t* web_resources_get_list(void)
{
    return s_resources;
}

void web_resources_deinit(void)
{
    if (s_resources == NULL) {
        return;
    }
    
    // 释放所有 PSRAM 资源
    for (size_t i = 0; i < s_resource_count; i++) {
        if (s_resources[i].data != NULL) {
            heap_caps_free((void *)s_resources[i].data);
        }
    }
    
    // 释放资源列表
    heap_caps_free(s_resources);
    s_resources = NULL;
    s_resource_count = 0;
    
    ESP_LOGI(TAG, "Web resources deinitialized");
}

/**
 * @file main.c
 * @brief ESP32-S3 打印服务器主程序入口
 * @details 初始化所有模块并启动系统
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "driver/temperature_sensor.h"
#include "temp_sensor.h"
#include "esp_heap_caps.h"
#include "mbedtls/platform.h"

#include "config.h"
#include "wifi.h"
#include "led.h"
#include "web_server.h"
#include "printer.h"
#include "tcp_server.h"
#include "ntp_client.h"
#include "ntp_server.h"
#include "ntp_storage.h"
#include "hardware_timer.h"
#include "monitor.h"
#include "web_resources.h"
#include "web_hook.h"
#include "esp_timer.h"
#include "esp_chip_info.h"
#include "esp_flash.h"

static const char *TAG = "MAIN";

// ==================== mbedTLS 自定义内存分配器 ====================
// mbedTLS calloc - 使用 PSRAM 分配内存
static void *mbedtls_psram_calloc(size_t n, size_t size)
{
    size_t total_size = n * size;
    void *ptr = heap_caps_malloc(total_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ptr != NULL) {
        memset(ptr, 0, total_size);
    }
    return ptr;
}

// mbedTLS free - 使用 PSRAM 释放内存
static void mbedtls_psram_free(void *ptr)
{
    if (ptr != NULL) {
        heap_caps_free(ptr);
    }
}

// 初始化 mbedTLS 内存分配器
static void mbedtls_init_psram_allocator(void)
{
    ESP_LOGI(TAG, "Initializing mbedTLS with PSRAM allocator...");
    mbedtls_platform_set_calloc_free(mbedtls_psram_calloc, mbedtls_psram_free);
    ESP_LOGI(TAG, "mbedTLS PSRAM allocator initialized");
}

// 系统运行时间（秒）
static uint32_t Global_Uptime = 0;

// 温度传感器句柄（在 temp_sensor.h 中声明）
temperature_sensor_handle_t temp_sensor = NULL;

// Boot按钮配置
#define BOOT_BUTTON_GPIO GPIO_NUM_0
#define BOOT_BUTTON_LONG_PRESS_MS 5000

/**
 * @brief Boot按钮检测任务 - 检测Boot按钮长按5秒，执行恢复出厂设置
 * @param pvParameters 任务参数
 */
static void boot_button_monitor_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Boot button monitor task started");

    // 配置Boot按钮GPIO
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BOOT_BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    int press_start_time = 0;
    bool is_pressed = false;
    bool long_press_detected = false;

    while (1) {
        // 读取Boot按钮状态（低电平表示按下）
        int button_level = gpio_get_level(BOOT_BUTTON_GPIO);

        if (button_level == 0) {
            // 按钮按下
            if (!is_pressed) {
                // 开始计时
                press_start_time = xTaskGetTickCount();
                is_pressed = true;
                long_press_detected = false;
                ESP_LOGI(TAG, "Boot button pressed");
            } else {
                // 检查是否长按超过5秒
                int elapsed_ms = (xTaskGetTickCount() - press_start_time) * portTICK_PERIOD_MS;
                if (elapsed_ms >= BOOT_BUTTON_LONG_PRESS_MS && !long_press_detected) {
                    long_press_detected = true;
                    ESP_LOGI(TAG, "Boot button long press detected (5 seconds), performing factory reset...");

                    // 执行恢复出厂设置
                    esp_err_t err = config_factory_reset();
                    if (err == ESP_OK) {
                        ESP_LOGI(TAG, "Factory reset completed, rebooting...");
                        vTaskDelay(pdMS_TO_TICKS(1000));
                        esp_restart();
                    } else {
                        ESP_LOGE(TAG, "Factory reset failed: %s", esp_err_to_name(err));
                    }
                } else if (elapsed_ms % 1000 == 0 && !long_press_detected) {
                    // 每秒输出一次进度
                    ESP_LOGI(TAG, "Boot button pressed: %d/%d seconds", elapsed_ms / 1000, BOOT_BUTTON_LONG_PRESS_MS / 1000);
                    // 添加小延迟避免日志过于频繁
                    vTaskDelay(pdMS_TO_TICKS(10));
                }
            }
        } else {
            // 按钮释放
            if (is_pressed) {
                ESP_LOGI(TAG, "Boot button released");
                is_pressed = false;
                long_press_detected = false;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(50)); // 50ms检测一次
    }
}

/**
 * @brief 运行时间定时器回调 - 每秒更新系统运行时间
 * @param arg 参数
 */
static void uptime_timer_callback(void* arg)
{
    Global_Uptime++;
}

/**
 * @brief 获取系统温度 - 读取 CPU 温度传感器
 * @return 温度值（摄氏度），如果读取失败返回 -1
 */
static float get_system_temperature(void)
{
    if (temp_sensor == NULL) {
        return -1.0f;
    }
    
    float temp_celsius = 0;
    esp_err_t ret = temperature_sensor_get_celsius(temp_sensor, &temp_celsius);
    if (ret == ESP_OK) {
        return temp_celsius;
    }
    return -1.0f;
}

/**
 * @brief 系统监控任务 - 监控系统状态并输出日志
 * @param pvParameters 任务参数
 */
static void system_monitor_task(void *pvParameters)
{
    ESP_LOGI(TAG, "System monitor task started");

    while (1) {
        // 每 2 秒检查一次是否有打印任务在忙碌
        vTaskDelay(pdMS_TO_TICKS(2000));

        // 检查是否有打印机正在忙碌
        if (printer_any_busy()) {
            ESP_LOGI(TAG, "Printer is busy, skipping monitor tasks...");
            continue;
        }

        // 每 60 秒输出一次系统状态
        static uint32_t last_log_time = 0;
        if (Global_Uptime - last_log_time >= 60) {
            ESP_LOGI(TAG, "System uptime: %lu seconds", Global_Uptime);
            ESP_LOGI(TAG, "Free heap: %lu bytes", esp_get_free_heap_size());
            ESP_LOGI(TAG, "Temperature: %.1f C", get_system_temperature());
            last_log_time = Global_Uptime;
        }
    }
}

/**
 * @brief 应用程序主函数 - 初始化所有模块并启动系统
 */
void app_main(void)
{
    // 首先初始化 mbedTLS 内存分配器（必须在所有 mbedTLS 操作之前）
    mbedtls_init_psram_allocator();

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "ESP32-S3 Print Server v1.0");
    ESP_LOGI(TAG, "========================================");

    // 打印芯片信息
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    ESP_LOGI(TAG, "Chip: %s cores, %s WiFi, %s BLE",
             chip_info.cores == 2 ? "dual" : "single",
             chip_info.features & CHIP_FEATURE_WIFI_BGN ? "enabled" : "disabled",
             chip_info.features & CHIP_FEATURE_BT ? "enabled" : "disabled");

    // 打印 Flash 信息
    uint32_t flash_size;
    esp_flash_get_size(NULL, &flash_size);
    ESP_LOGI(TAG, "Flash size: %lu MB", flash_size / (1024 * 1024));

    // 初始化配置管理模块
    ESP_LOGI(TAG, "Initializing config manager...");
    ESP_ERROR_CHECK(config_init());

    // 加载设备名称
    char device_name[64];
    ESP_ERROR_CHECK(config_load_device_name(device_name, sizeof(device_name)));
    ESP_LOGI(TAG, "Device name: %s", device_name);

    // 初始化 LED 模块（使用 GPIO 2，ESP32-S3 开发板板载 LED）
    ESP_LOGI(TAG, "Initializing LED...");
    ESP_ERROR_CHECK(led_init(GPIO_NUM_2));

    // 初始化 WiFi 模块
    ESP_LOGI(TAG, "Initializing WiFi...");
    ESP_ERROR_CHECK(wifi_init());

    // 自动启动 WiFi（根据配置选择 AP 或 STA 模式）
    ESP_LOGI(TAG, "Starting WiFi...");
    esp_err_t err = wifi_auto_start();
    if (err == ESP_OK) {
        // WiFi 启动成功
        if (wifi_get_mode() == WIFI_MODE_AP) {
            ESP_LOGI(TAG, "WiFi AP mode started");
            ESP_LOGI(TAG, "SSID: %s", device_name);
            ESP_LOGI(TAG, "IP: 192.168.4.1");
            ESP_LOGI(TAG, "Please connect to AP and configure WiFi");
            ESP_LOGI(TAG, "Web interface: http://192.168.4.1");
            led_set_ap_mode();
        } else {
            ESP_LOGI(TAG, "WiFi STA mode started");
            ESP_LOGI(TAG, "Connected to WiFi");

            wifi_status_t status;
            wifi_get_status(&status);
            ESP_LOGI(TAG, "IP: %s", status.ip_address);
            led_set_sta_mode();
        }
    } else {
        ESP_LOGE(TAG, "WiFi start failed: %s", esp_err_to_name(err));
        led_set_sta_error_mode();
    }

    // 初始化 Web 资源（从 Flash 复制到 PSRAM）
    ESP_LOGI(TAG, "Initializing Web resources...");
    ESP_ERROR_CHECK(web_resources_init());

    // 初始化温度传感器
    ESP_LOGI(TAG, "Initializing temperature sensor...");
    temperature_sensor_config_t temp_sensor_config = {
        .range_min = -10,
        .range_max = 80,
    };
    esp_err_t temp_err = temperature_sensor_install(&temp_sensor_config, &temp_sensor);
    if (temp_err == ESP_OK) {
        temperature_sensor_enable(temp_sensor);
        ESP_LOGI(TAG, "Temperature sensor initialized");
    } else {
        ESP_LOGW(TAG, "Temperature sensor init failed: %s", esp_err_to_name(temp_err));
        temp_sensor = NULL;
    }
    
    // 初始化网站监控模块（必须在 Web 服务器启动前初始化）
    ESP_LOGI(TAG, "Initializing monitor module...");
    ESP_ERROR_CHECK(monitor_init());
    
    // 初始化 Web 服务器
    ESP_LOGI(TAG, "Initializing Web server...");
    ESP_ERROR_CHECK(web_server_init());
    ESP_ERROR_CHECK(web_server_start());

    // 初始化 WebHook 模块
    ESP_LOGI(TAG, "Initializing WebHook module...");
    ESP_ERROR_CHECK(web_hook_init());

    // 初始化 USB 打印机管理
    ESP_LOGI(TAG, "Initializing printer module...");
    ESP_ERROR_CHECK(printer_init());
    ESP_ERROR_CHECK(printer_start());

    // 初始化 TCP 打印服务
    ESP_LOGI(TAG, "Initializing TCP server...");
    ESP_ERROR_CHECK(tcp_server_init());
    ESP_ERROR_CHECK(tcp_server_start());

    // 初始化硬件定时器
    ESP_LOGI(TAG, "Initializing hardware timer...");
    ESP_ERROR_CHECK(hardware_timer_init());

    // 初始化 NTP 存储（PSRAM）
    ESP_LOGI(TAG, "Initializing NTP storage...");
    ntp_storage_init();

    // 初始化 NTP 客户端
    ESP_LOGI(TAG, "Initializing NTP client...");
    ntp_client_config_t ntp_config = {
        .server1 = "ntp.ntsc.ac.cn",
        .server2 = "ntp1.aliyun.com",
        .sync_interval = 1800,
        .enable_client = true
    };
    ESP_ERROR_CHECK(ntp_client_init(&ntp_config));
    ESP_ERROR_CHECK(ntp_client_start());

    // 初始化 NTP 服务器
    ESP_LOGI(TAG, "Initializing NTP server...");
    ESP_ERROR_CHECK(ntp_server_init());
    ESP_ERROR_CHECK(ntp_server_start());
    
    // 根据系统配置决定是否启动监控
    monitor_system_config_t sys_config;
    monitor_get_system_config(&sys_config);
    if (sys_config.global_enabled) {
        ESP_LOGI(TAG, "Starting monitor (enabled in config)");
        ESP_ERROR_CHECK(monitor_start());
    } else {
        ESP_LOGI(TAG, "Monitor is disabled in config, not starting");
    }

    // 创建系统运行时间定时器（保存句柄以便后续清理）
    ESP_LOGI(TAG, "Creating uptime timer...");
    static esp_timer_handle_t uptime_timer = NULL;
    const esp_timer_create_args_t uptime_timer_args = {
        .callback = &uptime_timer_callback,
        .name = "uptime_timer"
    };
    ESP_ERROR_CHECK(esp_timer_create(&uptime_timer_args, &uptime_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(uptime_timer, 1000000)); // 1 秒

    // 创建系统监控任务（检查返回值）
    ESP_LOGI(TAG, "Creating system monitor task...");
    BaseType_t ret = xTaskCreate(system_monitor_task, "system_monitor", 4096, NULL, 5, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create system monitor task");
    }

    // 创建 Boot 按钮检测任务（检查返回值）
    ESP_LOGI(TAG, "Creating boot button monitor task...");
    ret = xTaskCreate(boot_button_monitor_task, "boot_button_monitor", 4096, NULL, 5, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create boot button monitor task");
    }

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "System started successfully!");
    ESP_LOGI(TAG, "========================================");

    // 主循环
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

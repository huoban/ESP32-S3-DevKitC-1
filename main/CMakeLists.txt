# Main 组件 CMakeLists.txt

# 嵌入 Web 资源到固件
set(EMBED_TXTFILES
    "${CMAKE_CURRENT_SOURCE_DIR}/web/index.html"
    "${CMAKE_CURRENT_SOURCE_DIR}/web/wifi.html"
    "${CMAKE_CURRENT_SOURCE_DIR}/web/usb.html"
    "${CMAKE_CURRENT_SOURCE_DIR}/web/monitor.html"
    "${CMAKE_CURRENT_SOURCE_DIR}/web/webhook.html"
    "${CMAKE_CURRENT_SOURCE_DIR}/web/sensors.html"
    "${CMAKE_CURRENT_SOURCE_DIR}/web/css/style.css"
    "${CMAKE_CURRENT_SOURCE_DIR}/web/js/app.js"
)

# 将文件嵌入到固件（自动生成 _binary_web_xxx_start/_end 符号）
idf_component_register(
    SRCS
        "main.c"
        "printer.c"
        "tcp_server.c"
        "web_server.c"
        "wifi.c"
        "ntp_client.c"
        "ntp_server.c"
        "hardware_timer.c"
        "monitor.c"
        "config.c"
        "led.c"
        "api_controller.c"
        "nvs_manager.c"
        "ntp_storage.c"
        "web_resources.c"
        "sensors.c"
        "web_hook.c"
    INCLUDE_DIRS
        "."
        "include"
        "C:/esp/v5.5.3/esp-idf/components/usb/include"
    REQUIRES
        "nvs_flash"
        "esp_wifi"
        "esp_netif"
        "lwip"
        "usb"
        "esp_http_server"
        "esp_http_client"
        "driver"
        "esp_timer"
        "mbedtls"
        "esp_event"
        "json"
        "spi_flash"
        "app_update"
        "esp_https_ota"
    EMBED_TXTFILES ${EMBED_TXTFILES}
)

# 禁用 format-truncation 警告（对于 snprintf 截断检查）
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-format-truncation)

# 禁用 SPIFFS 分区镜像（Web 资源已嵌入固件）
# set(WEB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/web)
# message(STATUS "Web directory: ${WEB_DIR}")
# spiffs_create_partition_image(spiffs ${WEB_DIR} FLASH_IN_PROJECT)

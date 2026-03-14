#include "api_controller.h"
#include "nvs_manager.h"
#include "common_defs.h"
#include "esp_http_server.h"
#include "cJSON.h"
#include "esp_log.h"
#include "wifi.h"  // 包含 WiFi 相关定义
#include "config.h" // 包含配置结构定义
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define TAG "API"
#define MAX_JSON_SIZE 256

esp_err_t bind_printer_api_handler(httpd_req_t *req) {
    char buf[MAX_JSON_SIZE + 1];
    int ret = httpd_req_recv(req, buf, MAX_JSON_SIZE);
    
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No data received");
        return ESP_FAIL;
    }
    
    buf[ret] = '\0';
    
    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    cJSON *serial = cJSON_GetObjectItem(root, "serial");
    cJSON *port = cJSON_GetObjectItem(root, "port");
    cJSON *remark = cJSON_GetObjectItem(root, "remark");
    
    if (!cJSON_IsString(serial) || !cJSON_IsNumber(port)) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing parameters");
        return ESP_FAIL;
    }
    
    if (strlen(serial->valuestring) > 31) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Serial too long");
        return ESP_FAIL;
    }
    
    uint16_t port_val = port->valueint;
    if (port_val < 1024) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid port range");
        return ESP_FAIL;
    }
    
    const char *remark_val = NULL;
    if (remark && cJSON_IsString(remark)) {
        remark_val = remark->valuestring;
        // 备注最多16个字符（UTF-8编码，支持中文）
        if (strlen(remark_val) > 16) {
            cJSON_Delete(root);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Remark must be at most 16 characters");
            return ESP_FAIL;
        }
    }
    
    ESP_LOGI(TAG, "Binding printer: serial=%s, port=%d, remark=%s", 
           serial->valuestring, port_val, remark_val ? remark_val : "(none)");
    
    esp_err_t err = save_binding_with_remark(serial->valuestring, port_val, remark_val);
    cJSON_Delete(root);
    
    if (err == ESP_OK) {
        httpd_resp_sendstr(req, "{\"status\":\"success\"}");
    } else {
        ESP_LOGE(TAG, "Bind failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Bind failed");
    }
    return ESP_OK;
}

esp_err_t unbind_printer_api_handler(httpd_req_t *req) {
    char buf[MAX_JSON_SIZE + 1];
    int ret = httpd_req_recv(req, buf, MAX_JSON_SIZE);
    
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No data received");
        return ESP_FAIL;
    }
    
    buf[ret] = '\0';
    
    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    cJSON *serial = cJSON_GetObjectItem(root, "serial");
    if (!cJSON_IsString(serial)) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing serial");
        return ESP_FAIL;
    }
    
    esp_err_t err = remove_binding(serial->valuestring);
    cJSON_Delete(root);
    
    if (err == ESP_OK) {
        httpd_resp_sendstr(req, "{\"status\":\"success\"}");
    } else {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Unbind failed");
    }
    return ESP_OK;
}

esp_err_t get_bindings_api_handler(httpd_req_t *req) {
    cJSON *root = cJSON_CreateObject();
    cJSON *bindings = cJSON_CreateArray();
    
    // 获取所有实际绑定的设备
    printer_binding_t binding_list[MAX_BINDINGS];
    size_t count = 0;
    
    if (get_all_bindings(binding_list, MAX_BINDINGS, &count) == ESP_OK) {
        for (size_t i = 0; i < count; i++) {
            cJSON *binding = cJSON_CreateObject();
            cJSON_AddStringToObject(binding, "serial", binding_list[i].serial);
            cJSON_AddNumberToObject(binding, "port", binding_list[i].port);
            if (strlen(binding_list[i].remark) > 0) {
                cJSON_AddStringToObject(binding, "remark", binding_list[i].remark);
            }
            cJSON_AddItemToArray(bindings, binding);
        }
    }
    
    cJSON_AddItemToObject(root, "bindings", bindings);
    
    char *json_str = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, strlen(json_str));
    
    cJSON_Delete(root);
    free(json_str);
    
    return ESP_OK;
}

/**
 * @brief WiFi 扫描 API 处理函数
 */
esp_err_t wifi_scan_api_handler(httpd_req_t *req) {
    cJSON *root = cJSON_CreateObject();
    cJSON *networks = cJSON_CreateArray();
    
    // 扫描 WiFi 网络
    wifi_ap_record_t ap_records[20];
    uint16_t count = 0;
    
    esp_err_t ret = wifi_scan_networks(ap_records, 20, &count);
    if (ret == ESP_OK) {
        for (int i = 0; i < count; i++) {
            cJSON *network = cJSON_CreateObject();
            cJSON_AddStringToObject(network, "ssid", (char *)ap_records[i].ssid);
            cJSON_AddNumberToObject(network, "rssi", ap_records[i].rssi);
            cJSON_AddBoolToObject(network, "secure", ap_records[i].authmode != WIFI_AUTH_OPEN);
            cJSON_AddNumberToObject(network, "channel", ap_records[i].primary);
            cJSON_AddItemToArray(networks, network);
        }
        ESP_LOGI(TAG, "WiFi scan completed, found %d networks", count);
    } else {
        ESP_LOGE(TAG, "WiFi scan failed: %s", esp_err_to_name(ret));
    }
    
    cJSON_AddItemToObject(root, "networks", networks);
    cJSON_AddNumberToObject(root, "count", count);
    
    char *json_str = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, strlen(json_str));
    
    cJSON_Delete(root);
    free(json_str);
    
    return ESP_OK;
}

/**
 * @brief 保存 WiFi 配置 API 处理函数（支持静态 IP）
 */
esp_err_t save_wifi_config_api_handler(httpd_req_t *req) {
    char buf[512];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No data received");
        return ESP_FAIL;
    }
    
    buf[ret] = '\0';
    ESP_LOGI(TAG, "WiFi config: %s", buf);
    
    // 解析 URL 编码的数据
    char ssid[33] = {0};
    char password[65] = {0};
    char ip_address[16] = {0};
    char gateway[16] = {0};
    char netmask[16] = {0};
    char dns[16] = {0};
    
    // 简单的 URL 解码
    char *token = strtok(buf, "&");
    while (token != NULL) {
        char *value = strchr(token, '=');
        if (value) {
            *value = '\0';
            value++;
            
            if (strcmp(token, "ssid") == 0) {
                url_decode(value, ssid, sizeof(ssid));
            } else if (strcmp(token, "password") == 0) {
                url_decode(value, password, sizeof(password));
            } else if (strcmp(token, "ip_address") == 0) {
                url_decode(value, ip_address, sizeof(ip_address));
            } else if (strcmp(token, "gateway") == 0) {
                url_decode(value, gateway, sizeof(gateway));
            } else if (strcmp(token, "netmask") == 0) {
                url_decode(value, netmask, sizeof(netmask));
            } else if (strcmp(token, "dns") == 0) {
                url_decode(value, dns, sizeof(dns));
            }
        }
        token = strtok(NULL, "&");
    }
    
    ESP_LOGI(TAG, "WiFi SSID=%s, Password=%s, IP=%s, Gateway=%s, Netmask=%s, DNS=%s",
             ssid, password, ip_address, gateway, netmask, dns);
    
    // 保存 WiFi 配置
    wifi_config_t_custom config = {0};
    strlcpy(config.ssid, ssid, sizeof(config.ssid));
    strlcpy(config.password, password, sizeof(config.password));
    config.is_configured = true;
    
    // 如果提供了 IP 地址，则使用静态 IP
    if (strlen(ip_address) > 0) {
        config.use_static_ip = true;
        strlcpy(config.ip_address, ip_address, sizeof(config.ip_address));
        strlcpy(config.gateway, gateway, sizeof(config.gateway));
        strlcpy(config.netmask, netmask, sizeof(config.netmask));
        strlcpy(config.dns, dns, sizeof(config.dns));
    } else {
        config.use_static_ip = false;
    }
    
    esp_err_t err = config_save_wifi(&config);
    
    cJSON *root = cJSON_CreateObject();
    if (err == ESP_OK) {
        cJSON_AddBoolToObject(root, "success", true);
        cJSON_AddStringToObject(root, "message", "配置已保存，系统将重启");
        ESP_LOGI(TAG, "WiFi config saved successfully");
    } else {
        cJSON_AddBoolToObject(root, "success", false);
        cJSON_AddStringToObject(root, "message", "保存失败");
        ESP_LOGE(TAG, "Failed to save WiFi config: %s", esp_err_to_name(err));
    }
    
    char *json_str = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, strlen(json_str));
    
    cJSON_Delete(root);
    free(json_str);
    
    // 延迟重启，让响应发送完成
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    
    return ESP_OK;
}

/**
 * @brief URL 解码辅助函数
 */
void url_decode(const char *src, char *dest, size_t max_len) {
    if (!src || !dest || max_len == 0) {
        return;
    }
    
    size_t i = 0, j = 0;
    while (src[i] && j < max_len - 1) {
        if (src[i] == '%' && src[i+1] && src[i+2]) {
            int hex;
            if (sscanf(src + i + 1, "%2x", &hex) == 1) {
                dest[j++] = (char)hex;
                i += 3;
            } else {
                dest[j++] = src[i++];
            }
        } else if (src[i] == '+') {
            dest[j++] = ' ';
            i++;
        } else {
            dest[j++] = src[i++];
        }
    }
    dest[j] = '\0';
}

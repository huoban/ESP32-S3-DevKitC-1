/**
 * @file printer.c
 * @brief USB 打印机管理模块实现
 * @details 负责 USB 打印机的枚举、状态监控和数据传输管理
 */

#include "printer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "usb/usb_host.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "PRINTER";

#define CLIENT_NUM_EVENT_MSG        5

typedef enum {
    ACTION_OPEN_DEV         = (1 << 0),
    ACTION_GET_DEV_INFO     = (1 << 1),
    ACTION_GET_DEV_DESC     = (1 << 2),
    ACTION_GET_CONFIG_DESC  = (1 << 3),
    ACTION_GET_STR_DESC     = (1 << 4),
    ACTION_CLAIM_INTERFACE  = (1 << 5),
    ACTION_CLOSE_DEV        = (1 << 6),
} action_t;

typedef struct {
    usb_host_client_handle_t client_hdl;
    uint8_t dev_addr;
    usb_device_handle_t dev_hdl;
    uint8_t interface_num;
    uint8_t ep_in;
    uint8_t ep_out;
    action_t actions;
    bool is_printer;
} usb_device_t;

typedef struct {
    struct {
        union {
            struct {
                uint8_t unhandled_devices: 1;
                uint8_t shutdown: 1;
                uint8_t reserved6: 6;
            };
            uint8_t val;
        } flags;
        usb_device_t device[PRINTER_MAX_COUNT];
    } mux_protected;

    struct {
        usb_host_client_handle_t client_hdl;
        SemaphoreHandle_t mux_lock;
    } constant;
} class_driver_t;

static usb_printer_t g_printers[PRINTER_MAX_COUNT];
static printer_buffer_t g_printer_buffers[PRINTER_MAX_COUNT];
static TaskHandle_t g_usb_host_task_handle = NULL;
static TaskHandle_t g_class_driver_task_handle = NULL;
static SemaphoreHandle_t g_printer_mutex = NULL;
static class_driver_t *s_driver_obj = NULL;

static void client_event_cb(const usb_host_client_event_msg_t *event_msg, void *arg)
{
    class_driver_t *driver_obj = (class_driver_t *)arg;
    switch (event_msg->event) {
    case USB_HOST_CLIENT_EVENT_NEW_DEV:
        ESP_LOGI(TAG, "New USB device connected, address: %d", event_msg->new_dev.address);
        xSemaphoreTake(driver_obj->constant.mux_lock, portMAX_DELAY);
        driver_obj->mux_protected.device[event_msg->new_dev.address].dev_addr = event_msg->new_dev.address;
        driver_obj->mux_protected.device[event_msg->new_dev.address].dev_hdl = NULL;
        driver_obj->mux_protected.device[event_msg->new_dev.address].actions |= ACTION_OPEN_DEV;
        driver_obj->mux_protected.flags.unhandled_devices = 1;
        xSemaphoreGive(driver_obj->constant.mux_lock);
        break;
    case USB_HOST_CLIENT_EVENT_DEV_GONE:
        ESP_LOGI(TAG, "USB device disconnected");
        xSemaphoreTake(driver_obj->constant.mux_lock, portMAX_DELAY);
        for (uint8_t i = 0; i < PRINTER_MAX_COUNT; i++) {
            if (driver_obj->mux_protected.device[i].dev_hdl == event_msg->dev_gone.dev_hdl) {
                driver_obj->mux_protected.device[i].actions = ACTION_CLOSE_DEV;
                driver_obj->mux_protected.flags.unhandled_devices = 1;
                
                if (driver_obj->mux_protected.device[i].is_printer) {
                    for (int j = 0; j < PRINTER_MAX_COUNT; j++) {
                        if (g_printers[j].dev_addr == driver_obj->mux_protected.device[i].dev_addr) {
                            if (xSemaphoreTake(g_printer_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                                g_printers[j].status = PRINTER_STATUS_DISCONNECTED;
                                g_printers[j].is_busy = false;
                                
                                if (g_printer_buffers[j].buffer != NULL) {
                                    heap_caps_free(g_printer_buffers[j].buffer);
                                    g_printer_buffers[j].buffer = NULL;
                                }
                                
                                xSemaphoreGive(g_printer_mutex);
                            }
                            ESP_LOGI(TAG, "Printer %d disconnected", j);
                            break;
                        }
                    }
                }
            }
        }
        xSemaphoreGive(driver_obj->constant.mux_lock);
        break;
    default:
        break;
    }
}

static void action_open_dev(usb_device_t *device_obj)
{
    assert(device_obj->dev_addr != 0);
    ESP_LOGI(TAG, "Opening device at address %d", device_obj->dev_addr);
    ESP_ERROR_CHECK(usb_host_device_open(device_obj->client_hdl, device_obj->dev_addr, &device_obj->dev_hdl));
    device_obj->actions |= ACTION_GET_DEV_INFO;
}

static void action_get_info(usb_device_t *device_obj)
{
    assert(device_obj->dev_hdl != NULL);
    ESP_LOGI(TAG, "Getting device information");
    usb_device_info_t dev_info;
    ESP_ERROR_CHECK(usb_host_device_info(device_obj->dev_hdl, &dev_info));
    ESP_LOGI(TAG, "\t%s speed", (char *[]) {
        "Low", "Full", "High"
    }[dev_info.speed]);
    ESP_LOGI(TAG, "\tbConfigurationValue %d", dev_info.bConfigurationValue);
    device_obj->actions |= ACTION_GET_DEV_DESC;
}

static void action_get_dev_desc(usb_device_t *device_obj)
{
    assert(device_obj->dev_hdl != NULL);
    ESP_LOGI(TAG, "Getting device descriptor");
    const usb_device_desc_t *dev_desc;
    ESP_ERROR_CHECK(usb_host_get_device_descriptor(device_obj->dev_hdl, &dev_desc));
    usb_print_device_descriptor(dev_desc);
    device_obj->actions |= ACTION_GET_CONFIG_DESC;
}

static void action_get_config_desc(usb_device_t *device_obj)
{
    assert(device_obj->dev_hdl != NULL);
    ESP_LOGI(TAG, "Getting config descriptor");
    const usb_config_desc_t *config_desc;
    ESP_ERROR_CHECK(usb_host_get_active_config_descriptor(device_obj->dev_hdl, &config_desc));
    usb_print_config_descriptor(config_desc, NULL);
    
    device_obj->is_printer = false;
    
    for (int intf_num = 0; intf_num < config_desc->bNumInterfaces; intf_num++) {
        int offset = 0;
        const usb_intf_desc_t *intf = usb_parse_interface_descriptor(config_desc, intf_num, 0, &offset);
        if (intf != NULL) {
            ESP_LOGI(TAG, "Interface %d found: Class=0x%02x, Subclass=0x%02x, Protocol=0x%02x", 
                     intf_num, intf->bInterfaceClass, intf->bInterfaceSubClass, intf->bInterfaceProtocol);
            
            if (intf->bInterfaceClass == 0x07) {
                ESP_LOGI(TAG, "Printer class device detected!");
                device_obj->is_printer = true;
                device_obj->interface_num = intf->bInterfaceNumber;
                
                ESP_LOGI(TAG, "Starting endpoint parsing, starting from interface offset=%d", offset);
                
                // 手动遍历配置描述符中的当前接口部分，寻找端点
                // 只解析当前接口的端点，遇到下一个接口描述符或配置描述符结束就停止
                const uint8_t *desc_ptr = (const uint8_t *)config_desc + offset;
                const uint8_t *desc_end = (const uint8_t *)config_desc + config_desc->wTotalLength;
                int ep_idx = 0;
                
                // 跳过接口描述符本身，从接口描述符后面开始解析
                uint8_t intf_len = desc_ptr[0];
                ESP_LOGI(TAG, "Skipping interface descriptor (length=%d)", intf_len);
                desc_ptr += intf_len;
                
                while (desc_ptr < desc_end) {
                    uint8_t bDescriptorType = desc_ptr[1];
                    
                    // 如果遇到下一个接口描述符，停止解析
                    if (bDescriptorType == USB_B_DESCRIPTOR_TYPE_INTERFACE) {
                        ESP_LOGI(TAG, "Reached next interface, stopping endpoint parsing");
                        break;
                    }
                    
                    // 如果遇到端点描述符，解析它
                    if (bDescriptorType == USB_B_DESCRIPTOR_TYPE_ENDPOINT) {
                        const usb_ep_desc_t *ep = (const usb_ep_desc_t *)desc_ptr;
                        ESP_LOGI(TAG, "Found endpoint %d: address=0x%02x, bmAttributes=0x%02x", 
                                 ep_idx, ep->bEndpointAddress, ep->bmAttributes);
                        if (ep->bEndpointAddress & 0x80) {
                            device_obj->ep_in = ep->bEndpointAddress;
                            ESP_LOGI(TAG, "IN endpoint: 0x%02x", device_obj->ep_in);
                        } else {
                            device_obj->ep_out = ep->bEndpointAddress;
                            ESP_LOGI(TAG, "OUT endpoint: 0x%02x", device_obj->ep_out);
                        }
                        ep_idx++;
                    }
                    
                    // 移动到下一个描述符
                    desc_ptr += desc_ptr[0];
                }
                
                ESP_LOGI(TAG, "Endpoint parsing done. ep_in=0x%02x, ep_out=0x%02x", 
                         device_obj->ep_in, device_obj->ep_out);
                
                device_obj->actions |= ACTION_CLAIM_INTERFACE;
                break;
            }
        }
    }
    
    if (!device_obj->is_printer) {
        ESP_LOGI(TAG, "No printer class interface found");
    }
    
    if (device_obj->is_printer) {
        device_obj->actions |= ACTION_GET_STR_DESC;
    }
}

static void action_get_str_desc(usb_device_t *device_obj)
{
    assert(device_obj->dev_hdl != NULL);
    usb_device_info_t dev_info;
    ESP_ERROR_CHECK(usb_host_device_info(device_obj->dev_hdl, &dev_info));
    
    if (dev_info.str_desc_manufacturer) {
        ESP_LOGI(TAG, "Getting Manufacturer string descriptor");
        usb_print_string_descriptor(dev_info.str_desc_manufacturer);
    }
    if (dev_info.str_desc_product) {
        ESP_LOGI(TAG, "Getting Product string descriptor");
        usb_print_string_descriptor(dev_info.str_desc_product);
    }
    if (dev_info.str_desc_serial_num) {
        ESP_LOGI(TAG, "Getting Serial Number string descriptor");
        usb_print_string_descriptor(dev_info.str_desc_serial_num);
    }
}

static void action_claim_interface(usb_device_t *device_obj)
{
    ESP_LOGI(TAG, "Claiming interface %d", device_obj->interface_num);
    ESP_LOGI(TAG, "Before claim - device_obj->ep_in: 0x%02x, ep_out: 0x%02x", 
             device_obj->ep_in, device_obj->ep_out);
    ESP_ERROR_CHECK(usb_host_interface_claim(device_obj->client_hdl, device_obj->dev_hdl, device_obj->interface_num, 0));
    
    // 获取设备信息
    usb_device_info_t dev_info;
    ESP_ERROR_CHECK(usb_host_device_info(device_obj->dev_hdl, &dev_info));
    
    for (int i = 0; i < PRINTER_MAX_COUNT; i++) {
        if (g_printers[i].status == PRINTER_STATUS_DISCONNECTED) {
            if (xSemaphoreTake(g_printer_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                g_printers[i].dev_addr = device_obj->dev_addr;
                g_printers[i].dev_hdl = device_obj->dev_hdl;  // 保存设备句柄
                g_printers[i].ep_in = device_obj->ep_in;       // 保存输入端点
                g_printers[i].ep_out = device_obj->ep_out;     // 保存输出端点
                ESP_LOGI(TAG, "Saved to printer %d - ep_in: 0x%02x, ep_out: 0x%02x", 
                         i, g_printers[i].ep_in, g_printers[i].ep_out);
                g_printers[i].status = PRINTER_STATUS_READY;
                g_printers[i].last_active_time = xTaskGetTickCount();
                
                // 填充设备信息
                if (dev_info.str_desc_product) {
                    char product_str[64] = {0};
                    usb_print_string_descriptor(dev_info.str_desc_product);
                    for (int j = 0; j < dev_info.str_desc_product->bLength - 2 && j < 63; j += 2) {
                        product_str[j/2] = dev_info.str_desc_product->wData[j/2] & 0xFF;
                    }
                    strlcpy(g_printers[i].device_name, product_str, sizeof(g_printers[i].device_name));
                } else {
                    snprintf(g_printers[i].device_name, sizeof(g_printers[i].device_name), "Printer %d", i);
                }
                
                if (dev_info.str_desc_manufacturer) {
                    char mfg_str[64] = {0};
                    usb_print_string_descriptor(dev_info.str_desc_manufacturer);
                    for (int j = 0; j < dev_info.str_desc_manufacturer->bLength - 2 && j < 63; j += 2) {
                        mfg_str[j/2] = dev_info.str_desc_manufacturer->wData[j/2] & 0xFF;
                    }
                    strlcpy(g_printers[i].vendor_name, mfg_str, sizeof(g_printers[i].vendor_name));
                } else {
                    strlcpy(g_printers[i].vendor_name, "Unknown", sizeof(g_printers[i].vendor_name));
                }
                
                if (dev_info.str_desc_serial_num) {
                    char serial_str[64] = {0};
                    usb_print_string_descriptor(dev_info.str_desc_serial_num);
                    for (int j = 0; j < dev_info.str_desc_serial_num->bLength - 2 && j < 63; j += 2) {
                        serial_str[j/2] = dev_info.str_desc_serial_num->wData[j/2] & 0xFF;
                    }
                    strlcpy(g_printers[i].serial_number, serial_str, sizeof(g_printers[i].serial_number));
                } else {
                    snprintf(g_printers[i].serial_number, sizeof(g_printers[i].serial_number), "N/A");
                }
                
                esp_err_t err = printer_buffer_init(i);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to initialize buffer for printer %d", i);
                    g_printers[i].status = PRINTER_STATUS_ERROR;
                    xSemaphoreGive(g_printer_mutex);
                    continue;
                }
                
                xSemaphoreGive(g_printer_mutex);
                ESP_LOGI(TAG, "Printer %d connected and ready", i);
                break;
            }
        }
    }
}

static void action_close_dev(usb_device_t *device_obj)
{
    if (device_obj->dev_hdl != NULL) {
        ESP_ERROR_CHECK(usb_host_device_close(device_obj->client_hdl, device_obj->dev_hdl));
        device_obj->dev_hdl = NULL;
        device_obj->dev_addr = 0;
        device_obj->is_printer = false;
    }
}

static void class_driver_device_handle(usb_device_t *device_obj)
{
    uint8_t actions = device_obj->actions;
    device_obj->actions = 0;

    while (actions) {
        if (actions & ACTION_OPEN_DEV) {
            action_open_dev(device_obj);
        }
        if (actions & ACTION_GET_DEV_INFO) {
            action_get_info(device_obj);
        }
        if (actions & ACTION_GET_DEV_DESC) {
            action_get_dev_desc(device_obj);
        }
        if (actions & ACTION_GET_CONFIG_DESC) {
            action_get_config_desc(device_obj);
        }
        if (actions & ACTION_GET_STR_DESC) {
            action_get_str_desc(device_obj);
        }
        if (actions & ACTION_CLAIM_INTERFACE) {
            action_claim_interface(device_obj);
        }
        if (actions & ACTION_CLOSE_DEV) {
            action_close_dev(device_obj);
        }

        actions = device_obj->actions;
        device_obj->actions = 0;
    }
}

static void class_driver_task(void *arg)
{
    class_driver_t driver_obj = {0};
    usb_host_client_handle_t class_driver_client_hdl = NULL;

    ESP_LOGI(TAG, "Registering Client");

    SemaphoreHandle_t mux_lock = xSemaphoreCreateMutex();
    if (mux_lock == NULL) {
        ESP_LOGE(TAG, "Unable to create class driver mutex");
        vTaskDelete(NULL);
        return;
    }

    usb_host_client_config_t client_config = {
        .is_synchronous = false,
        .max_num_event_msg = CLIENT_NUM_EVENT_MSG,
        .async = {
            .client_event_callback = client_event_cb,
            .callback_arg = (void *) &driver_obj,
        },
    };
    ESP_ERROR_CHECK(usb_host_client_register(&client_config, &class_driver_client_hdl));

    driver_obj.constant.mux_lock = mux_lock;
    driver_obj.constant.client_hdl = class_driver_client_hdl;

    for (uint8_t i = 0; i < PRINTER_MAX_COUNT; i++) {
        driver_obj.mux_protected.device[i].client_hdl = class_driver_client_hdl;
    }

    s_driver_obj = &driver_obj;

    while (1) {
        if (driver_obj.mux_protected.flags.unhandled_devices) {
            xSemaphoreTake(driver_obj.constant.mux_lock, portMAX_DELAY);
            for (uint8_t i = 0; i < PRINTER_MAX_COUNT; i++) {
                if (driver_obj.mux_protected.device[i].actions) {
                    class_driver_device_handle(&driver_obj.mux_protected.device[i]);
                }
            }
            driver_obj.mux_protected.flags.unhandled_devices = 0;
            xSemaphoreGive(driver_obj.constant.mux_lock);
        } else {
            if (driver_obj.mux_protected.flags.shutdown == 0) {
                usb_host_client_handle_events(class_driver_client_hdl, portMAX_DELAY);
            } else {
                break;
            }
        }
    }

    ESP_LOGI(TAG, "Deregistering Class Client");
    ESP_ERROR_CHECK(usb_host_client_deregister(class_driver_client_hdl));
    if (mux_lock != NULL) {
        vSemaphoreDelete(mux_lock);
    }
    vTaskDelete(NULL);
}

static void usb_host_lib_task(void *arg)
{
    ESP_LOGI(TAG, "Installing USB Host Library");
    usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
        .peripheral_map = BIT0,
    };
    ESP_ERROR_CHECK(usb_host_install(&host_config));
    ESP_LOGI(TAG, "USB Host installed");

    xTaskNotifyGive(arg);

    bool has_clients = true;
    bool has_devices = false;
    while (has_clients) {
        uint32_t event_flags;
        ESP_ERROR_CHECK(usb_host_lib_handle_events(portMAX_DELAY, &event_flags));
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            ESP_LOGI(TAG, "Get FLAGS_NO_CLIENTS");
            if (ESP_OK == usb_host_device_free_all()) {
                ESP_LOGI(TAG, "All devices marked as free, no need to wait FLAGS_ALL_FREE event");
                has_clients = false;
            } else {
                ESP_LOGI(TAG, "Wait for the FLAGS_ALL_FREE");
                has_devices = true;
            }
        }
        if (has_devices && event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) {
            ESP_LOGI(TAG, "Get FLAGS_ALL_FREE");
            has_clients = false;
        }
    }
    ESP_LOGI(TAG, "No more clients and devices, uninstall USB Host library");

    ESP_ERROR_CHECK(usb_host_uninstall());
    vTaskDelete(NULL);
}

esp_err_t printer_init(void)
{
    ESP_LOGI(TAG, "Initializing printer module");

    memset(g_printers, 0, sizeof(g_printers));
    memset(g_printer_buffers, 0, sizeof(g_printer_buffers));

    for (int i = 0; i < PRINTER_MAX_COUNT; i++) {
        g_printers[i].instance = i;
        g_printers[i].status = PRINTER_STATUS_DISCONNECTED;
        g_printers[i].is_busy = false;
    }

    g_printer_mutex = xSemaphoreCreateMutex();
    if (g_printer_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create printer mutex");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Printer module initialized");

    return ESP_OK;
}

esp_err_t printer_start(void)
{
    ESP_LOGI(TAG, "Starting printer module");

    BaseType_t task_created = xTaskCreatePinnedToCore(usb_host_lib_task,
                                           "usb_host",
                                           4096,
                                           xTaskGetCurrentTaskHandle(),
                                           2,
                                           &g_usb_host_task_handle,
                                           0);
    assert(task_created == pdTRUE);

    ulTaskNotifyTake(false, 1000);

    task_created = xTaskCreatePinnedToCore(class_driver_task,
                                           "class_driver",
                                           8192,
                                           NULL,
                                           3,
                                           &g_class_driver_task_handle,
                                           0);
    assert(task_created == pdTRUE);

    ESP_LOGI(TAG, "Printer module started");

    return ESP_OK;
}

esp_err_t printer_stop(void)
{
    ESP_LOGI(TAG, "Stopping printer module");

    if (g_class_driver_task_handle != NULL) {
        vTaskDelete(g_class_driver_task_handle);
        g_class_driver_task_handle = NULL;
    }

    if (g_usb_host_task_handle != NULL) {
        vTaskDelete(g_usb_host_task_handle);
        g_usb_host_task_handle = NULL;
    }

    if (g_printer_mutex != NULL) {
        vSemaphoreDelete(g_printer_mutex);
        g_printer_mutex = NULL;
    }

    ESP_LOGI(TAG, "Printer module stopped");

    return ESP_OK;
}

esp_err_t printer_get_info(uint8_t instance, usb_printer_t* info)
{
    if (instance >= PRINTER_MAX_COUNT || info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(g_printer_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        memcpy(info, &g_printers[instance], sizeof(usb_printer_t));
        xSemaphoreGive(g_printer_mutex);
        return ESP_OK;
    }

    return ESP_ERR_TIMEOUT;
}

// USB 传输完成回调
static void usb_transfer_complete_cb(usb_transfer_t *transfer)
{
    ESP_LOGI(TAG, "USB transfer completed: status=%d, actual_num_bytes=%zu", 
             transfer->status, transfer->actual_num_bytes);
    // 通知等待的任务
    if (transfer->context) {
        xTaskNotifyGive((TaskHandle_t)transfer->context);
    }
}

esp_err_t printer_send_data(uint8_t instance, const uint8_t* data, size_t len, uint32_t timeout_ms)
{
    if (instance >= PRINTER_MAX_COUNT || data == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!printer_is_connected(instance)) {
        ESP_LOGE(TAG, "Printer %d not connected", instance);
        return ESP_ERR_INVALID_STATE;
    }

    if (printer_is_busy(instance)) {
        ESP_LOGE(TAG, "Printer %d is busy", instance);
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Sending %zu bytes to printer %d", len, instance);
    
    // 获取打印机设备句柄和端点
    usb_device_handle_t dev_hdl;
    uint8_t ep_out;
    uint8_t ep_in;
    if (xSemaphoreTake(g_printer_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        dev_hdl = g_printers[instance].dev_hdl;
        ep_out = g_printers[instance].ep_out;
        ep_in = g_printers[instance].ep_in;
        ESP_LOGI(TAG, "Reading from printer %d - dev_hdl: %p, ep_in: 0x%02x, ep_out: 0x%02x", 
                 instance, dev_hdl, ep_in, ep_out);
        xSemaphoreGive(g_printer_mutex);
    } else {
        return ESP_ERR_TIMEOUT;
    }

    if (dev_hdl == NULL) {
        ESP_LOGE(TAG, "Printer %d device handle is NULL", instance);
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Device handle: %p, EP out: 0x%02x", dev_hdl, ep_out);

    printer_set_busy(instance, true);
    
    esp_err_t ret = ESP_OK;
    size_t offset = 0;
    const size_t max_chunk_size = PRINTER_TRANSFER_CHUNK_SIZE;
    
    while (offset < len) {
        size_t chunk_size = (len - offset) > max_chunk_size ? max_chunk_size : (len - offset);
        
        // 分配USB传输结构
        usb_transfer_t *transfer = NULL;
        ret = usb_host_transfer_alloc(chunk_size, 0, &transfer);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to allocate transfer: %s", esp_err_to_name(ret));
            break;
        }
        
        // 填充传输数据
        memcpy(transfer->data_buffer, data + offset, chunk_size);
        transfer->num_bytes = chunk_size;
        transfer->bEndpointAddress = ep_out;
        transfer->device_handle = dev_hdl;
        transfer->callback = usb_transfer_complete_cb;
        transfer->context = xTaskGetCurrentTaskHandle();
        transfer->timeout_ms = timeout_ms;
        
        ESP_LOGI(TAG, "Submitting transfer: offset=%zu, chunk_size=%zu", offset, chunk_size);
        
        // 提交传输
        ret = usb_host_transfer_submit(transfer);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to submit transfer: %s", esp_err_to_name(ret));
            usb_host_transfer_free(transfer);
            break;
        }
        
        // 等待传输完成
        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(timeout_ms)) == 0) {
            ESP_LOGE(TAG, "Transfer timeout");
            ret = ESP_ERR_TIMEOUT;
            // 注意：超时情况下我们不释放transfer，因为回调可能还在运行
            // 这是一个简化处理，实际应用中需要更完善的超时处理
            break;
        }
        
        // 检查传输状态
        if (transfer->status != USB_TRANSFER_STATUS_COMPLETED) {
            ESP_LOGE(TAG, "Transfer failed with status: %d", transfer->status);
            ret = ESP_FAIL;
        }
        
        // 释放传输结构
        usb_host_transfer_free(transfer);
        
        if (ret != ESP_OK) {
            break;
        }
        
        offset += chunk_size;
    }
    
    printer_set_busy(instance, false);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Successfully sent all %zu bytes", len);
    }
    
    return ret;
}

esp_err_t printer_receive_data(uint8_t instance, uint8_t* data, size_t* len, uint32_t timeout_ms)
{
    if (instance >= PRINTER_MAX_COUNT || data == NULL || len == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!printer_is_connected(instance)) {
        return ESP_ERR_INVALID_STATE;
    }

    *len = 0;
    
    ESP_LOGI(TAG, "Receiving data from printer %d", instance);

    return ESP_OK;
}

printer_status_t printer_get_status(uint8_t instance)
{
    if (instance >= PRINTER_MAX_COUNT) {
        return PRINTER_STATUS_DISCONNECTED;
    }

    printer_status_t status = PRINTER_STATUS_DISCONNECTED;

    if (xSemaphoreTake(g_printer_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        status = g_printers[instance].status;
        xSemaphoreGive(g_printer_mutex);
    }

    return status;
}

bool printer_is_connected(uint8_t instance)
{
    return printer_get_status(instance) != PRINTER_STATUS_DISCONNECTED;
}

bool printer_is_busy(uint8_t instance)
{
    if (instance >= PRINTER_MAX_COUNT) {
        return false;
    }

    bool busy = false;

    if (xSemaphoreTake(g_printer_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        busy = g_printers[instance].is_busy;
        xSemaphoreGive(g_printer_mutex);
    }

    return busy;
}

esp_err_t printer_set_busy(uint8_t instance, bool busy)
{
    if (instance >= PRINTER_MAX_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(g_printer_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        g_printers[instance].is_busy = busy;
        xSemaphoreGive(g_printer_mutex);
        return ESP_OK;
    }

    return ESP_ERR_TIMEOUT;
}

esp_err_t printer_buffer_init(uint8_t instance)
{
    if (instance >= PRINTER_MAX_COUNT) {
        ESP_LOGE(TAG, "Invalid printer instance: %d", instance);
        return ESP_ERR_INVALID_ARG;
    }

    const size_t buffer_size = PRINTER_BUFFER_SIZE;

    uint8_t* buffer = (uint8_t*)heap_caps_malloc(buffer_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate buffer for printer %d", instance);
        return ESP_ERR_NO_MEM;
    }

    SemaphoreHandle_t sem_read = xSemaphoreCreateBinary();
    if (sem_read == NULL) {
        heap_caps_free(buffer);
        ESP_LOGE(TAG, "Failed to create read semaphore for printer %d", instance);
        return ESP_ERR_NO_MEM;
    }

    SemaphoreHandle_t sem_write = xSemaphoreCreateBinary();
    if (sem_write == NULL) {
        vSemaphoreDelete(sem_read);
        heap_caps_free(buffer);
        ESP_LOGE(TAG, "Failed to create write semaphore for printer %d", instance);
        return ESP_ERR_NO_MEM;
    }

    g_printer_buffers[instance].buffer = buffer;
    g_printer_buffers[instance].buffer_size = buffer_size;
    g_printer_buffers[instance].head = 0;
    g_printer_buffers[instance].tail = 0;
    g_printer_buffers[instance].sem_read = sem_read;
    g_printer_buffers[instance].sem_write = sem_write;

    ESP_LOGI(TAG, "Printer %d buffer initialized", instance);

    return ESP_OK;
}

esp_err_t printer_buffer_write(uint8_t instance, const uint8_t* data, size_t len)
{
    if (instance >= PRINTER_MAX_COUNT || data == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    printer_buffer_t* buf = &g_printer_buffers[instance];

    size_t available = (buf->buffer_size - 1 - (buf->head - buf->tail)) % buf->buffer_size;
    if (len > available) {
        ESP_LOGE(TAG, "Buffer overflow for printer %d", instance);
        return ESP_ERR_NO_MEM;
    }

    for (size_t i = 0; i < len; i++) {
        buf->buffer[buf->head] = data[i];
        buf->head = (buf->head + 1) % buf->buffer_size;
    }

    xSemaphoreGive(buf->sem_read);

    return ESP_OK;
}

esp_err_t printer_buffer_read(uint8_t instance, uint8_t* data, size_t* len)
{
    if (instance >= PRINTER_MAX_COUNT || data == NULL || len == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    printer_buffer_t* buf = &g_printer_buffers[instance];

    if (xSemaphoreTake(buf->sem_read, pdMS_TO_TICKS(100)) != pdTRUE) {
        *len = 0;
        return ESP_ERR_TIMEOUT;
    }

    size_t available = (buf->head - buf->tail) % buf->buffer_size;
    size_t read_len = (available < *len) ? available : *len;

    for (size_t i = 0; i < read_len; i++) {
        data[i] = buf->buffer[buf->tail];
        buf->tail = (buf->tail + 1) % buf->buffer_size;
    }

    *len = read_len;

    return ESP_OK;
}

size_t printer_buffer_available(uint8_t instance)
{
    if (instance >= PRINTER_MAX_COUNT) {
        return 0;
    }

    printer_buffer_t* buf = &g_printer_buffers[instance];
    return (buf->buffer_size - 1 - (buf->head - buf->tail)) % buf->buffer_size;
}

size_t printer_buffer_length(uint8_t instance)
{
    if (instance >= PRINTER_MAX_COUNT) {
        return 0;
    }

    printer_buffer_t* buf = &g_printer_buffers[instance];
    return (buf->head - buf->tail) % buf->buffer_size;
}

esp_err_t printer_buffer_clear(uint8_t instance)
{
    if (instance >= PRINTER_MAX_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(g_printer_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        g_printer_buffers[instance].head = 0;
        g_printer_buffers[instance].tail = 0;
        xSemaphoreGive(g_printer_mutex);
        return ESP_OK;
    }

    return ESP_ERR_TIMEOUT;
}

void printer_on_device_connected(uint8_t dev_addr, const uint8_t* dev_desc, size_t desc_len)
{
}

void printer_on_device_disconnected(uint8_t dev_addr)
{
}

int printer_find_instance_by_serial(const char* serial_number) {
    if (!serial_number || *serial_number == '\0') {
        return -1;
    }
    
    if (xSemaphoreTake(g_printer_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        for (int i = 0; i < PRINTER_MAX_COUNT; i++) {
            if (g_printers[i].status != PRINTER_STATUS_DISCONNECTED && 
                strcmp(g_printers[i].serial_number, serial_number) == 0) {
                xSemaphoreGive(g_printer_mutex);
                return i;
            }
        }
        xSemaphoreGive(g_printer_mutex);
    }
    
    return -1;
}

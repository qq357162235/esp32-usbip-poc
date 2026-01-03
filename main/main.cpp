#include <stdio.h>
#include "string.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "usb/usb_host.h"
#include "usb/cdc_acm_host.h"
#include "usb/vcp_ch34x.hpp"
#include "usb/vcp_cp210x.hpp"
#include "usb/vcp_ftdi.hpp"
#include "usb/vcp.hpp"

#include <stdio.h>
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <sys/types.h>
#include <esp_vfs_fat.h>
#include "nvs_flash.h"
#include "usbip.hpp"
#include "usbip_config.h"

using namespace esp_usb;

// Global configuration
static usbip_config_t g_usbip_config;

extern "C" void start_server();
USBhost* host = nullptr;
USBipDevice* device = nullptr;
static bool is_ready = false;
static USBIP usbip;

void client_event_callback(const usb_host_client_event_msg_t *event_msg, void *arg)
{
    ESP_LOGW("USB_HOST", "usb_host_client_event_msg_t event: %d", event_msg->event);
    
    if (event_msg->event == USB_HOST_CLIENT_EVENT_NEW_DEV)
    {
        // Open the USB device
        esp_err_t err = host->open(event_msg);
        if (err != ESP_OK) {
            ESP_LOGE("USB_HOST", "Failed to open USB device: %s", esp_err_to_name(err));
            return;
        }

        // Get device information
        usb_device_info_t info = host->getDeviceInfo();
        ESP_LOGI("USB_HOST", "Device connected - speed: %s, address: %d, max ep_ctrl size: %d, config: %d", 
                 info.speed ? "USB_SPEED_FULL" : "USB_SPEED_LOW", 
                 info.dev_addr, 
                 info.bMaxPacketSize0, 
                 info.bConfigurationValue);
        
        const usb_device_desc_t* dev_desc = host->getDeviceDescriptor();
        if (dev_desc == nullptr) {
            ESP_LOGE("USB_HOST", "Failed to get device descriptor");
            return;
        }
        
        // Create and initialize USBipDevice
        device = new (std::nothrow) USBipDevice();
        if (device == nullptr) {
            ESP_LOGE("USB_HOST", "Failed to allocate USBipDevice");
            return;
        }
        
        bool init_result = device->init(host);
        if (!init_result) {
            ESP_LOGE("USB_HOST", "Failed to initialize USBipDevice");
            delete device;
            device = nullptr;
            return;
        }

        is_ready = true;
        ESP_LOGI("USB_HOST", "USB device ready for USBIP");
    }
    else
    {
        // Release all interfaces and clean up
        ESP_LOGI("USB_HOST", "USB device disconnected");
        is_ready = false;
        
        if (device != nullptr) {
            device->deinit();
            delete device;
            device = nullptr;
        }
    }
}

esp_err_t init_usbip()
{
    esp_err_t err;
    
    // Create USBhost object
    host = new (std::nothrow) USBhost();
    if (host == nullptr) {
        ESP_LOGE("USB_HOST", "Failed to allocate USBhost object");
        return ESP_ERR_NO_MEM;
    }
    
    // Register client callback
    host->registerClientCb(client_event_callback);
    
    // Initialize USB host
    err = host->init();
    if (err != ESP_OK) {
        ESP_LOGE("USB_HOST", "Failed to initialize USB host: %s", esp_err_to_name(err));
        delete host;
        host = nullptr;
        return err;
    }
    
    // Install CDC ACM driver
    ESP_LOGI("VCP", "Installing CDC-ACM driver");
    err = cdc_acm_host_install(NULL);
    if (err != ESP_OK) {
        ESP_LOGE("VCP", "Failed to install CDC-ACM driver: %s", esp_err_to_name(err));
        return err;
    }
    
    // Register all supported VCP drivers
    ESP_LOGI("VCP", "Registering VCP drivers");
    
    VCP::register_driver<FT23x>();
    VCP::register_driver<CP210x>();
    VCP::register_driver<CH34x>();
    
    ESP_LOGI("USB_HOST", "USBIP initialized successfully");
    return ESP_OK;
}

extern "C" void app_main(void)
{
    esp_err_t err;
    
    // Set log level to INFO to see USB device enumeration and identification
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("USB_HOST", ESP_LOG_DEBUG);
    esp_log_level_set("USBIP", ESP_LOG_DEBUG);
    
    // Initialize USBIP configuration
    ESP_LOGI("MAIN", "Initializing USBIP configuration");
    usbip_config_init(&g_usbip_config);
    
    // Validate configuration
    int validate_result = usbip_config_validate(&g_usbip_config);
    if (validate_result != 0) {
        ESP_LOGE("MAIN", "Configuration validation failed with error code: %d", validate_result);
        return;
    }
    
    ESP_LOGI("MAIN", "Configuration validated successfully");
    ESP_LOGI("MAIN", "  TCP Port: %u", g_usbip_config.tcp_port);
    ESP_LOGI("MAIN", "  Max Clients: %u", g_usbip_config.max_clients);
    ESP_LOGI("MAIN", "  AP SSID: %s", g_usbip_config.ap_ssid);
    
    // Initialize NVS (required for WiFi)
    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW("NVS", "NVS partition needs to be erased, erasing...");
        err = nvs_flash_erase();
        if (err != ESP_OK) {
            ESP_LOGE("NVS", "Failed to erase NVS: %s", esp_err_to_name(err));
            return;
        }
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE("NVS", "Failed to initialize NVS: %s", esp_err_to_name(err));
        return;
    }
    
    // Initialize USBIP
    err = init_usbip();
    if (err != ESP_OK) {
        ESP_LOGE("MAIN", "Failed to initialize USBIP: %s", esp_err_to_name(err));
        return;
    }
    
    // Start TCP server
    ESP_LOGI("MAIN", "Starting TCP server");
    start_server();
}
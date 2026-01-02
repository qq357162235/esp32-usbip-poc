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

using namespace esp_usb;


extern "C" void start_server();
USBhost* host;
static USBipDevice* device;
static bool is_ready = false;
static USBIP usbip;

void client_event_callback(const usb_host_client_event_msg_t *event_msg, void *arg)
{
    ESP_LOGW("", "usb_host_client_event_msg_t event: %d", event_msg->event);
    if (event_msg->event == USB_HOST_CLIENT_EVENT_NEW_DEV)
    {
        host->open(event_msg);

        info = host->getDeviceInfo();
        ESP_LOGI("USB_HOST_CLIENT_EVENT_NEW_DEV", "device speed: %s, device address: %d, max ep_ctrl size: %d, config: %d", info.speed ? "USB_SPEED_FULL" : "USB_SPEED_LOW", info.dev_addr, info.bMaxPacketSize0, info.bConfigurationValue);
        dev_desc = host->getDeviceDescriptor();
        
        device = new USBipDevice();
        device->init(host);

        is_ready = true;
    }
    else
    {
        // Release all interfaces and clean up
        is_ready = false;
        device->deinit();
        delete(device);
    }
}

void init_usbip()
{
    host = new USBhost();
    host->registerClientCb(client_event_callback);
    host->init();
    
    // 安装CDC ACM驱动
    ESP_LOGI("VCP", "Installing CDC-ACM driver");
    ESP_ERROR_CHECK(cdc_acm_host_install(NULL));
    
    // 注册所有支持的VCP驱动
    ESP_LOGI("VCP", "Registering VCP drivers");
    VCP::register_driver<FT23x>();
    VCP::register_driver<CP210x>();
    VCP::register_driver<CH34x>();
}

extern "C" void app_main(void)
{
    // Set log level to INFO to see USB device enumeration and identification
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("USB_HOST", ESP_LOG_DEBUG);
    esp_log_level_set("USBIP", ESP_LOG_DEBUG);
    init_usbip();

    start_server();
}

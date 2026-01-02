#include "usbip_config.h"
#include "esp_log.h"
#include <string.h>

static const char* TAG = "USBIP_CONFIG";

// Default configuration
const usbip_config_t usbip_default_config = {
    // Network Configuration
    .tcp_port = TCP_SERVER_PORT,
    .max_clients = TCP_SERVER_MAX_CLIENTS,
    .ap_ssid = WIFI_AP_SSID,
    .ap_password = WIFI_AP_PASSWORD,
    
    // USB Configuration
    .max_transfers = USBIP_MAX_TRANSFERS,
    .transfer_timeout_ms = USBIP_TRANSFER_TIMEOUT_MS,
    .max_seqnum_cache = USBIP_MAX_SEQNUM_CACHE,
    
    // Buffer Configuration
    .recv_buffer_size = TCP_RECV_BUFFER_SIZE,
    .send_buffer_size = TCP_SEND_BUFFER_SIZE,
    .control_buffer_size = USB_CONTROL_BUFFER_SIZE,
    
    // Debug Configuration
    .debug_transfers = USBIP_DEBUG_TRANSFERS,
    .debug_protocol = USBIP_DEBUG_PROTOCOL,
    .debug_memory = USBIP_DEBUG_MEMORY,
    
    // Feature Flags
    .zero_copy = USBIP_ZERO_COPY,
    .async_transfer = USBIP_ASYNC_TRANSFER,
};

void usbip_config_init(usbip_config_t* config) {
    if (config == NULL) {
        ESP_LOGE(TAG, "Invalid parameter: config is NULL");
        return;
    }
    
    // Initialize with default values
    usbip_config_load_defaults(config);
}

void usbip_config_load_defaults(usbip_config_t* config) {
    if (config == NULL) {
        ESP_LOGE(TAG, "Invalid parameter: config is NULL");
        return;
    }
    
    // Copy default configuration
    memcpy(config, &usbip_default_config, sizeof(usbip_config_t));
    
    ESP_LOGI(TAG, "Configuration loaded with defaults");
    ESP_LOGI(TAG, "  TCP Port: %u", config->tcp_port);
    ESP_LOGI(TAG, "  Max Clients: %u", config->max_clients);
    ESP_LOGI(TAG, "  AP SSID: %s", config->ap_ssid);
    ESP_LOGI(TAG, "  Max Transfers: %u", config->max_transfers);
    ESP_LOGI(TAG, "  Transfer Timeout: %lu ms", config->transfer_timeout_ms);
}

int usbip_config_validate(const usbip_config_t* config) {
    if (config == NULL) {
        ESP_LOGE(TAG, "Invalid parameter: config is NULL");
        return USBIP_ERR_INVALID_PARAM;
    }
    
    // Validate network configuration
    if (config->tcp_port == 0 || config->tcp_port > 65535) {
        ESP_LOGE(TAG, "Invalid TCP port: %u", config->tcp_port);
        return USBIP_ERR_INVALID_PARAM;
    }
    
    if (config->max_clients == 0 || config->max_clients > 10) {
        ESP_LOGE(TAG, "Invalid max clients: %u", config->max_clients);
        return USBIP_ERR_INVALID_PARAM;
    }
    
    if (strlen(config->ap_ssid) == 0 || strlen(config->ap_ssid) >= 32) {
        ESP_LOGE(TAG, "Invalid AP SSID length");
        return USBIP_ERR_INVALID_PARAM;
    }
    
    if (strlen(config->ap_password) < 8 || strlen(config->ap_password) >= 64) {
        ESP_LOGE(TAG, "Invalid AP password length (must be 8-63 characters)");
        return USBIP_ERR_INVALID_PARAM;
    }
    
    // Validate USB configuration
    if (config->max_transfers == 0 || config->max_transfers > 64) {
        ESP_LOGE(TAG, "Invalid max transfers: %u", config->max_transfers);
        return USBIP_ERR_INVALID_PARAM;
    }
    
    if (config->transfer_timeout_ms == 0 || config->transfer_timeout_ms > 60000) {
        ESP_LOGE(TAG, "Invalid transfer timeout: %lu ms", config->transfer_timeout_ms);
        return USBIP_ERR_INVALID_PARAM;
    }
    
    if (config->max_seqnum_cache == 0 || config->max_seqnum_cache > 10000) {
        ESP_LOGE(TAG, "Invalid max seqnum cache: %u", config->max_seqnum_cache);
        return USBIP_ERR_INVALID_PARAM;
    }
    
    // Validate buffer configuration
    if (config->recv_buffer_size < 512 || config->recv_buffer_size > 16384) {
        ESP_LOGE(TAG, "Invalid recv buffer size: %u", config->recv_buffer_size);
        return USBIP_ERR_INVALID_PARAM;
    }
    
    if (config->send_buffer_size < 512 || config->send_buffer_size > 16384) {
        ESP_LOGE(TAG, "Invalid send buffer size: %u", config->send_buffer_size);
        return USBIP_ERR_INVALID_PARAM;
    }
    
    if (config->control_buffer_size < 256 || config->control_buffer_size > 8192) {
        ESP_LOGE(TAG, "Invalid control buffer size: %u", config->control_buffer_size);
        return USBIP_ERR_INVALID_PARAM;
    }
    
    ESP_LOGI(TAG, "Configuration validation passed");
    return 0;  // Success
}

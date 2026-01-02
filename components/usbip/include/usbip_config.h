#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// USBIP Configuration Constants
// ============================================================================

// USBIP Protocol Version
#define USBIP_VERSION_MAJOR        1
#define USBIP_VERSION_MINOR        11
#define USBIP_VERSION_STRING       "1.11"

// USBIP Header Size
#define USBIP_HEADER_SIZE          0x30  // 48 bytes
#define USBIP_CMD_HEADER_SIZE      0x30  // 48 bytes
#define USBIP_RESP_HEADER_SIZE     0x30  // 48 bytes

// USBIP Command Codes
#define USBIP_CMD_SUBMIT           0x01
#define USBIP_CMD_UNLINK           0x02
#define USBIP_RET_SUBMIT           0x03
#define USBIP_RET_UNLINK           0x04

// USBIP Operation Codes
#define USBIP_OP_REQ_DEVLIST       0x8005
#define USBIP_OP_REP_DEVLIST       0x0005
#define USBIP_OP_REQ_IMPORT        0x8003
#define USBIP_OP_REP_IMPORT        0x0003

// USBIP Event IDs
#define USBIP_EVENT_CTRL_RESP      0x1001
#define USBIP_EVENT_EPX_RESP       0x1002

// ============================================================================
// USB Configuration Constants
// ============================================================================

// USB Speed Constants
#define USB_SPEED_LOW              1
#define USB_SPEED_FULL             2
#define USB_SPEED_HIGH             3

// USB Endpoint Constants
#define USB_EP0_ADDRESS            0x00
#define USB_MAX_ENDPOINTS          16
#define USB_DEFAULT_MPS            64

// USB Transfer Constants
#define USB_SETUP_PACKET_SIZE      8
#define USB_MAX_CONTROL_BUFFER     2048
#define USB_MAX_BULK_BUFFER        4096
#define USB_MAX_INTERRUPT_BUFFER   1024

// USB Device Types
#define USB_DEVICE_TYPE_UNKNOWN    0x00
#define USB_DEVICE_TYPE_VCP         0x01
#define USB_DEVICE_TYPE_MSC        0x02
#define USB_DEVICE_TYPE_HID        0x03

// USB Interface Classes
#define USB_CLASS_PER_INTERFACE    0x00
#define USB_CLASS_AUDIO            0x01
#define USB_CLASS_COMM             0x02
#define USB_CLASS_HID              0x03
#define USB_CLASS_PHYSICAL         0x05
#define USB_CLASS_IMAGE            0x06
#define USB_CLASS_PRINTER          0x07
#define USB_CLASS_MASS_STORAGE     0x08
#define USB_CLASS_HUB              0x09
#define USB_CLASS_CDC_DATA         0x0A
#define USB_CLASS_SMART_CARD       0x0B
#define USB_CLASS_CONTENT_SEC      0x0D
#define USB_CLASS_VIDEO            0x0E
#define USB_CLASS_PERSONAL_HEALTH  0x0F
#define USB_CLASS_AUDIO_VIDEO      0x10
#define USB_CLASS_BILLBOARD        0x11
#define USB_CLASS_CDC_CONTROL      0x02
#define USB_CLASS_VENDOR_SPECIFIC  0xFF

// ============================================================================
// Network Configuration Constants
// ============================================================================

// TCP Server Configuration
#define TCP_SERVER_PORT            3240
#define TCP_SERVER_MAX_CLIENTS     5
#define TCP_SERVER_RECV_TIMEOUT    30
#define TCP_SERVER_SEND_TIMEOUT    30
#define TCP_SERVER_RECV_BUF_SIZE   4096

// WiFi AP Configuration
#define WIFI_AP_SSID               "ESP32-USBIP"
#define WIFI_AP_PASSWORD           "usbip1234"
#define WIFI_AP_CHANNEL            1
#define WIFI_AP_MAX_CONN           4
#define WIFI_AP_AUTH_MODE          WIFI_AUTH_WPA2_PSK

// WiFi IP Configuration
#define WIFI_AP_IP_ADDR            "192.168.4.1"
#define WIFI_AP_NETMASK            "255.255.255.0"
#define WIFI_AP_GATEWAY            "192.168.4.1"

// ============================================================================
// Memory and Buffer Configuration
// ============================================================================

// Sequence Number Cache Configuration
#define USBIP_MAX_SEQNUM_CACHE     1000
#define USBIP_SEQNUM_CACHE_WARN    900

// Event Loop Configuration
#define USBIP_EVENT_QUEUE_SIZE     100
#define USBIP_EVENT_TASK_PRIORITY  21
#define USBIP_EVENT_TASK_STACK     (4 * 1024)
#define USBIP_EVENT_TASK_CORE      0

// Transfer Pool Configuration
#define USBIP_MAX_TRANSFERS        32
#define USBIP_TRANSFER_TIMEOUT_MS  5000

// ============================================================================
// Logging Configuration
// ============================================================================

// Log Levels
#define USBIP_LOG_LEVEL_ERROR      ESP_LOG_ERROR
#define USBIP_LOG_LEVEL_WARN       ESP_LOG_WARN
#define USBIP_LOG_LEVEL_INFO       ESP_LOG_INFO
#define USBIP_LOG_LEVEL_DEBUG      ESP_LOG_DEBUG
#define USBIP_LOG_LEVEL_VERBOSE    ESP_LOG_VERBOSE

// Log Tags
#define USBIP_LOG_TAG              "USBIP"
#define USB_HOST_LOG_TAG          "USB_HOST"
#define TCP_SERVER_LOG_TAG        "TCP_SERVER"
#define WIFI_LOG_TAG              "WIFI"

// ============================================================================
// CDC ACM Configuration
// ============================================================================

// CDC ACM Request Codes
#define CDC_ACM_SET_LINE_CODING    0x20
#define CDC_ACM_GET_LINE_CODING    0x21
#define CDC_ACM_SET_CONTROL_LINE_STATE 0x22
#define CDC_ACM_SEND_BREAK         0x23
#define CDC_ACM_SET_LINE_CODING_LENGTH 7

// Default Line Coding
#define CDC_ACM_DEFAULT_BAUD_RATE  115200
#define CDC_ACM_DEFAULT_STOP_BITS  0    // 1 stop bit
#define CDC_ACM_DEFAULT_PARITY     0    // No parity
#define CDC_ACM_DEFAULT_DATA_BITS  8

// ============================================================================
// MSC Configuration
// ============================================================================

// MSC Command Codes
#define MSC_CBW_SIGNATURE          0x43425355  // "USBC"
#define MSC_CSW_SIGNATURE          0x53425355  // "USBS"
#define MSC_MAX_LUN                1
#define MSC_MAX_BLOCK_SIZE         512

// ============================================================================
// Error Codes
// ============================================================================

// USBIP Error Codes
#define USBIP_ERR_INVALID_PARAM    -1
#define USBIP_ERR_NO_MEMORY        -2
#define USBIP_ERR_NOT_READY        -3
#define USBIP_ERR_TIMEOUT          -4
#define USBIP_ERR_IO_ERROR         -5
#define USBIP_ERR_PROTOCOL         -6
#define USBIP_ERR_DEVICE_NOT_FOUND -7

// USB Transfer Status
#define USB_TRANSFER_STATUS_COMPLETED  0
#define USB_TRANSFER_STATUS_ERROR      1
#define USB_TRANSFER_STATUS_TIMEOUT   2
#define USB_TRANSFER_STATUS_CANCELLED  3

// ============================================================================
// Timeout Configuration
// ============================================================================

// USB Host Timeouts (in milliseconds)
#define USB_HOST_INIT_TIMEOUT       5000
#define USB_DEVICE_ENUM_TIMEOUT     10000
#define USB_TRANSFER_TIMEOUT        5000
#define USB_CONTROL_TIMEOUT         5000

// Network Timeouts (in milliseconds)
#define TCP_CONNECT_TIMEOUT         5000
#define TCP_RECV_TIMEOUT            30000
#define TCP_SEND_TIMEOUT            30000

// WiFi Timeouts (in milliseconds)
#define WIFI_CONNECT_TIMEOUT        10000
#define WIFI_START_TIMEOUT          5000

// ============================================================================
// Buffer Sizes
// ============================================================================

// Network Buffers
#define TCP_RECV_BUFFER_SIZE       4096
#define TCP_SEND_BUFFER_SIZE       4096
#define USBIP_REQUEST_BUFFER_SIZE  2048
#define USBIP_RESPONSE_BUFFER_SIZE 2048

// USB Buffers
#define USB_CONTROL_BUFFER_SIZE    2048
#define USB_BULK_BUFFER_SIZE       4096
#define USB_INTERRUPT_BUFFER_SIZE  1024

// ============================================================================
// Device Path Configuration
// ============================================================================

// USBIP Device Path
#define USBIP_DEVICE_PATH          "/espressif/usbip/usb1"
#define USBIP_BUS_ID               "1-1"
#define USBIP_BUS_NUM              1
#define USBIP_DEV_NUM              1

// ============================================================================
// Feature Flags
// ============================================================================

// Debug Features
#define USBIP_DEBUG_TRANSFERS      0
#define USBIP_DEBUG_PROTOCOL       0
#define USBIP_DEBUG_MEMORY         0

// Performance Features
#define USBIP_ZERO_COPY            0
#define USBIP_ASYNC_TRANSFER       1

// ============================================================================
// Helper Macros
// ============================================================================

// Byte swap macros
#define BSWAP_16(x)                __bswap_16(x)
#define BSWAP_32(x)                __bswap_32(x)

// Array size macro
#define ARRAY_SIZE(arr)            (sizeof(arr) / sizeof((arr)[0]))

// Min/Max macros
#define MIN(a, b)                  ((a) < (b) ? (a) : (b))
#define MAX(a, b)                  ((a) > (b) ? (a) : (b))

// Clamp macro
#define CLAMP(val, min, max)       (MIN(MAX(val, min), max))

// ============================================================================
// Configuration Structure
// ============================================================================

typedef struct {
    // Network Configuration
    uint16_t tcp_port;
    uint8_t max_clients;
    char ap_ssid[32];
    char ap_password[64];
    
    // USB Configuration
    uint8_t max_transfers;
    uint32_t transfer_timeout_ms;
    uint16_t max_seqnum_cache;
    
    // Buffer Configuration
    uint16_t recv_buffer_size;
    uint16_t send_buffer_size;
    uint16_t control_buffer_size;
    
    // Debug Configuration
    bool debug_transfers;
    bool debug_protocol;
    bool debug_memory;
    
    // Feature Flags
    bool zero_copy;
    bool async_transfer;
} usbip_config_t;

// Default configuration
extern const usbip_config_t usbip_default_config;

// Configuration management functions
void usbip_config_init(usbip_config_t* config);
void usbip_config_load_defaults(usbip_config_t* config);
int usbip_config_validate(const usbip_config_t* config);

#ifdef __cplusplus
}
#endif

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include <new>
#include "esp_log.h"
#include "esp_event.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

#include "usbip.hpp"

// Logging configuration
#define TAG "usbip"
#define USBIP_LOG_LEVEL ESP_LOG_INFO

// Conditional logging macros
#define USBIP_LOGE(fmt, ...) ESP_LOG_LEVEL_LOCAL(USBIP_LOG_LEVEL, ESP_LOG_ERROR, TAG, fmt, ##__VA_ARGS__)
#define USBIP_LOGW(fmt, ...) ESP_LOG_LEVEL_LOCAL(USBIP_LOG_LEVEL, ESP_LOG_WARN, TAG, fmt, ##__VA_ARGS__)
#define USBIP_LOGI(fmt, ...) ESP_LOG_LEVEL_LOCAL(USBIP_LOG_LEVEL, ESP_LOG_INFO, TAG, fmt, ##__VA_ARGS__)
#define USBIP_LOGD(fmt, ...) ESP_LOG_LEVEL_LOCAL(USBIP_LOG_LEVEL, ESP_LOG_DEBUG, TAG, fmt, ##__VA_ARGS__)
#define USBIP_LOGV(fmt, ...) ESP_LOG_LEVEL_LOCAL(USBIP_LOG_LEVEL, ESP_LOG_VERBOSE, TAG, fmt, ##__VA_ARGS__)

// Debug logging for USB transfers
#ifdef CONFIG_USBIP_DEBUG_TRANSFERS
#define USBIP_LOG_TRANSFER(tag, data, len) ESP_LOG_BUFFER_HEX_LEVEL(tag, data, len, ESP_LOG_DEBUG)
#else
#define USBIP_LOG_TRANSFER(tag, data, len) ((void)0)
#endif

// commands
#define OP_REQ_DEVLIST bswap_constant_16(0x8005)
#define OP_REP_DEVLIST bswap_constant_16(0x0005)
#define OP_REQ_IMPORT bswap_constant_16(0x8003)
#define OP_REP_IMPORT bswap_constant_16(0x0003)

// usbip_header_basic commands
#define USBIP_CMD_SUBMIT bswap_constant_16(0x01)
#define USBIP_RET_SUBMIT bswap_constant_32(0x03)
#define USBIP_CMD_UNLINK bswap_constant_16(0x02)
#define USBIP_RET_UNLINK bswap_constant_32(0x04)

#define USBIP_VERSION   bswap_constant_16(0x0111)   // v1.11
#define USB_LOW_SPEED   bswap_constant_32(1)
#define USB_FULL_SPEED  bswap_constant_32(2)

// Constants
#define USBIP_HEADER_SIZE 0x30
#define MAX_SEQNUM_CACHE 1000
#define USB_SETUP_PACKET_SIZE 8
#define MAX_CONTROL_BUFFER_SIZE 2048

static usbip_import_t import_data = {};
static usbip_devlist_t devlist_data = {};
static uint32_t last_seqnum = 0;
static uint32_t last_unlink = 0;

usb_device_info_t info;
const usb_device_desc_t *dev_desc;
static esp_event_loop_handle_t loop_handle;
static SemaphoreHandle_t usb_sem;
static SemaphoreHandle_t usb_sem1;
static int _sock = -1;

static bool is_ready = false;
static bool finished = false;
static usb_transfer_t *_transfer;
static USBipDevice* g_usbip_device = nullptr; // Global pointer to USBipDevice instance

// Forward declarations
static esp_err_t send_usbip_response(const void* data, size_t len, const char* log_tag);
static void handle_usbip_response(usb_transfer_t *transfer, bool is_ctrl_transfer);

// Helper function to send USBIP responses with error handling
static esp_err_t send_usbip_response(const void* data, size_t len, const char* log_tag)
{
    if (data == nullptr || len == 0) {
        USBIP_LOGE("Invalid parameters for send_usbip_response: data=%p, len=%d", data, len);
        return ESP_ERR_INVALID_ARG;
    }
    
    if (_sock < 0) {
        USBIP_LOGE("Invalid socket for send_usbip_response: sock=%d", _sock);
        return ESP_ERR_INVALID_STATE;
    }
    
    ssize_t sent = send(_sock, data, len, MSG_DONTWAIT);
    if (sent < 0) {
        USBIP_LOGE("Failed to send %s: errno=%d (%s)", log_tag, errno, strerror(errno));
        return ESP_FAIL;
    } else if (sent != (ssize_t)len) {
        USBIP_LOGW("Partial %s sent: %d/%d bytes", log_tag, (int)sent, (int)len);
        return ESP_OK; // Partial send is still OK
    }
    
    USBIP_LOGD("Successfully sent %s: %d bytes", log_tag, (int)len);
    return ESP_OK;
}

#define USB_CTRL_RESP   0x1001
#define USB_EPx_RESP    0x1002

ESP_EVENT_DECLARE_BASE( USBIP_EVENT_BASE );
ESP_EVENT_DEFINE_BASE(USBIP_EVENT_BASE);

#include <algorithm>
#include <vector>
static std::vector<uint32_t> vec;

// Helper function to check if seqnum is cached
static bool is_seqnum_cached(uint32_t seqnum)
{
    return std::find(vec.begin(), vec.end(), seqnum) != vec.end();
}

// Helper function to add seqnum to cache with size limit
static void add_seqnum_to_cache(uint32_t seqnum)
{
    vec.insert(vec.begin(), seqnum);
    if(vec.size() >= 999) vec.pop_back();
}

// Helper function to prepare USBIP response header
static void prepare_usbip_response_header(usbip_submit_t* req, int data_len, bool transfer_failed)
{
    req->header.command = USBIP_RET_SUBMIT;
    req->header.devid = 0;
    req->header.direction = 0;
    req->header.ep = 0;
    req->status = 0;
    req->length = __bswap_32(data_len);
    
    if (transfer_failed) {
        req->length = 0;
        req->status = -ETIME;
        req->error_count = 1;
    }
}

// Helper function to handle USBIP response (common logic for CTRL and EPx)
static void handle_usbip_response(USBipDevice* dev, usb_transfer_t* transfer, bool is_ctrl_transfer)
{
    usbip_submit_t* req = (usbip_submit_t*)transfer->context;
    uint32_t seqnum = __bswap_32(req->header.seqnum);
    
    // Check if seqnum is already cached
    if (is_seqnum_cached(seqnum)) {
        delete req;
        dev->deallocate(transfer);
        return;
    }
    
    // Add seqnum to cache
    add_seqnum_to_cache(seqnum);
    
    // Calculate data length
    int _len = is_ctrl_transfer ? (transfer->actual_num_bytes - 8) : transfer->actual_num_bytes;
    if(_len < 0) {
        dev->deallocate(transfer);
        delete req;
        return;
    }
    
    // Adjust length based on direction
    if (req->header.direction == 0) {
        _len = 0;
    }
    
    // Prepare response header
    bool transfer_failed = (transfer->status != USB_TRANSFER_STATUS_COMPLETED);
    prepare_usbip_response_header(req, _len, transfer_failed);
    
    // Copy data if available
    if (_len > 0 && !transfer_failed) {
        if (is_ctrl_transfer) {
            memcpy(&req->transfer_buffer[0], transfer->data_buffer + 8, _len);
        } else {
            req->start_frame = 0;
            req->padding = 0;
            memcpy(&req->transfer_buffer[0], transfer->data_buffer, transfer->actual_num_bytes);
        }
    }
    
    // Send response
    int to_write = 0x30 + _len;
    const char* log_tag = is_ctrl_transfer ? "USB_CTRL_RESP" : "USB_EPx_RESP";
    USBIP_LOG_TRANSFER(log_tag, (void*)req, to_write);
    send_usbip_response((void*)req, to_write, log_tag);
    
    // Clean up
    delete req;
    dev->deallocate(transfer);
}

static void usb_ctrl_cb(usb_transfer_t *transfer)
{
    esp_err_t err = esp_event_post_to(loop_handle, USBIP_EVENT_BASE, USB_CTRL_RESP, (void*)&transfer, sizeof(usb_transfer_t*), 10);
    if (err != ESP_OK) {
        USBIP_LOGE("Failed to post USB_CTRL_RESP event: %s", esp_err_to_name(err));
        if (g_usbip_device != nullptr) {
            g_usbip_device->deallocate(transfer); // Clean up if event post fails
        }
    }
}

static void usb_read_cb(usb_transfer_t *transfer)
{
    esp_err_t err = esp_event_post_to(loop_handle, USBIP_EVENT_BASE, USB_EPx_RESP, (void*)&transfer, sizeof(usb_transfer_t*), 10);
    if (err != ESP_OK) {
        USBIP_LOGE("Failed to post USB_EPx_RESP event: %s", esp_err_to_name(err));
        if (g_usbip_device != nullptr) {
            g_usbip_device->deallocate(transfer); // Clean up if event post fails
        }
    }
}

static void _event_handler(void* event_handler_arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    switch (event_id)
    {
    case USB_CTRL_RESP:{
        USBipDevice* dev = (USBipDevice*)event_handler_arg;
        usb_transfer_t *transfer = *(usb_transfer_t **)event_data;
        handle_usbip_response(dev, transfer, true); // true = is_ctrl_transfer
        break;
    }

    case USB_EPx_RESP:{
        USBipDevice* dev = (USBipDevice*)event_handler_arg;
        usb_transfer_t *transfer = *(usb_transfer_t **)event_data;
        handle_usbip_response(dev, transfer, false); // false = is_ctrl_transfer
        break;
    }

    default:
        break;
    }
}

static void _event_handler1(void* event_handler_arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    switch (event_id)
    {
    case USBIP_CMD_SUBMIT:{
        USBipDevice* dev = (USBipDevice*)event_handler_arg;
        urb_data_t* data = (urb_data_t*)event_data;
        int socket = data->socket;
        uint8_t* rx_buffer = (uint8_t*) data->rx_buffer;
        int len = data->len;
        int start = 0;
        int _len = len;
        usbip_submit_t* __req = (usbip_submit_t*)(rx_buffer);
        uint32_t seqnum = __bswap_32(__req->header.seqnum);

        do
        {
            usbip_submit_t* _req = (usbip_submit_t*)(rx_buffer + start);
            usbip_submit_t* req = new (std::nothrow) usbip_submit_t();
            if (req == nullptr) {
                USBIP_LOGE("Failed to allocate usbip_submit_t");
                break;
            }
            int tl = 0;
            if(_req->header.direction == 0) tl = __bswap_32(_req->length);

            memcpy(req, _req, 0x30 + tl);
            
            int tlen = 0;

            if(req->header.ep == 0) // EP0
            {
                tlen = dev->req_ctrl_xfer(req);
            } else { // EPx
                tlen = dev->req_ep_xfer(req);
            }
            start += 0x30 + tlen;
            _len -= 0x30 + tlen;
        } 
        while (_len >= 0x30);
        break;
    }

    case USBIP_CMD_UNLINK:{
        usbip_unlink_t* req = *(usbip_unlink_t**)event_data;
        req->header.command = USBIP_RET_UNLINK;
        req->header.devid = 0;
        req->header.direction = 0;
        req->header.ep = 0;
        req->status = 0;
        int to_write = 48;
        USBIP_LOG_TRANSFER("USBIP_RET_UNLINK", (void*)req, to_write);
        send_usbip_response((void*)req, to_write, "USBIP_RET_UNLINK");
        delete req;
        break;
    }

    default:
        break;
    }
}

static void _event_handler2(void* event_handler_arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    switch(event_id)
    {
        case OP_REQ_DEVLIST:{
            int to_write = 0;
            // 0xC + i*0x138 + m_(i-1)*4
            if(devlist_data.request.version == 0) /*!< assume there is no device connected yet */
            {
                to_write = 12;
                devlist_data.request.version = USBIP_VERSION;
                devlist_data.request.command = OP_REP_DEVLIST;
                devlist_data.request.status = 0;
                devlist_data.count = 0;
            } else {
                to_write = 0x0c + __bswap_32(devlist_data.count) * 0x138 + devlist_data.bNumInterfaces * 4;
            }
            send_usbip_response((void*)&devlist_data, to_write, "OP_REP_DEVLIST");
            break;
        }

        case OP_REQ_IMPORT:{
            int to_write = sizeof(usbip_import_t);
            send_usbip_response((void*)&import_data, to_write, "OP_REP_IMPORT");
            break;
        }
    }
}


USBipDevice::USBipDevice()
{
    usb_sem = xSemaphoreCreateBinary();
    usb_sem1 = xSemaphoreCreateBinary();
    xSemaphoreGive(usb_sem);
    xSemaphoreGive(usb_sem1);

    // Initialize global pointer
    g_usbip_device = this;
    
    // Initialize device type
    device_type = USB_DEVICE_TYPE_UNKNOWN;
    cdc_intf_num = 0;
    cdc_data_intf_num = 0;
    msc_intf_num = 0;

    esp_event_handler_register_with(loop_handle, USBIP_EVENT_BASE, USB_CTRL_RESP, _event_handler, this);
    esp_event_handler_register_with(loop_handle, USBIP_EVENT_BASE, USB_EPx_RESP, _event_handler, this);
    esp_event_handler_register_with(loop_handle, USBIP_EVENT_BASE, USBIP_CMD_SUBMIT, _event_handler1, this);
    esp_event_handler_register_with(loop_handle, USBIP_EVENT_BASE, USBIP_CMD_UNLINK, _event_handler1, this);
}

USBipDevice::~USBipDevice()
{
    esp_event_handler_unregister_with(loop_handle, USBIP_EVENT_BASE, ESP_EVENT_ANY_ID, _event_handler);
    esp_event_handler_unregister_with(loop_handle, USBIP_EVENT_BASE, ESP_EVENT_ANY_ID, _event_handler1);
    memset(&import_data, 0, sizeof(usbip_import_t));
    memset(&devlist_data, 0, sizeof(usbip_devlist_t));
    
    // Clear global pointer
    g_usbip_device = nullptr;
}

bool USBipDevice::init(USBhost* host)
{
    _host = host;

    // Use the global info variable already set in main.cpp
    esp_err_t err = USBhostDevice::init(1032);
    if (err != ESP_OK) {
        USBIP_LOGE("Failed to initialize USBhostDevice: %s", esp_err_to_name(err));
        return false;
    }
    xfer_ctrl->callback = usb_ctrl_cb;

    config_desc = host->getConfigurationDescriptor();
    
    // Detect device type based on USB interface classes
    detect_device_type();
    
    int offset = 0;
    for (size_t n = 0; n < config_desc->bNumInterfaces; n++)
    {
        const usb_intf_desc_t *intf = usb_parse_interface_descriptor(config_desc, n, 0, &offset);
        const usb_ep_desc_t *ep = nullptr;

        for (size_t i = 0; i < intf->bNumEndpoints; i++)
        {
            int _offset = 0;
            ep = usb_parse_endpoint_descriptor_by_index(intf, i, config_desc->wTotalLength, &_offset);
            uint8_t adr = ep->bEndpointAddress;
            if (adr & 0x80)
            {
                endpoints[adr & 0xf][1] = ep;
            } else {
                endpoints[adr & 0xf][0] = ep;
            }
        }
        err = usb_host_interface_claim(_host->clientHandle(), _host->deviceHandle(), n, 0);
        if (err != ESP_OK) {
            USBIP_LOGE("Failed to claim interface %d: %s", n, esp_err_to_name(err));
        }
    }

    fill_list_data();
    fill_import_data();
    return true;
}

void USBipDevice::detect_device_type()
{
    device_type = USB_DEVICE_TYPE_UNKNOWN;
    
    int offset = 0;
    for (size_t n = 0; n < config_desc->bNumInterfaces; n++)
    {
        const usb_intf_desc_t *intf = usb_parse_interface_descriptor(config_desc, n, 0, &offset);
        if (!intf) continue;
        
        // Check for CDC ACM (VCP) device
        if (intf->bInterfaceClass == 0x02 && intf->bInterfaceSubClass == 0x02)
        {
            // 通信类接口
            cdc_intf_num = n;
            device_type = USB_DEVICE_TYPE_VCP;
        }
        else if (intf->bInterfaceClass == 0x0A && intf->bInterfaceSubClass == 0x00)
        {
            // 数据类接口
            cdc_data_intf_num = n;
            device_type = USB_DEVICE_TYPE_VCP;
        }
        // Check for MSC device
        else if (intf->bInterfaceClass == 0x08 && intf->bInterfaceSubClass == 0x06 && intf->bInterfaceProtocol == 0x50)
        {
            // SCSI透明命令集 (Bulk-Only Transport)
            msc_intf_num = n;
            device_type = USB_DEVICE_TYPE_MSC;
        }
        // Check for HID device
        else if (intf->bInterfaceClass == 0x03)
        {
            device_type = USB_DEVICE_TYPE_HID;
        }
    }
}

void USBipDevice::fill_import_data()
{
    memset(&import_data, 0, sizeof(usbip_import_t));
    import_data.request.version = USBIP_VERSION;
    import_data.request.command = OP_REP_IMPORT;
    import_data.request.status = 0;
    strcpy(import_data.path, "/espressif/usbip/usb1");
    strcpy(import_data.busid, "1-1");
    import_data.busnum = __bswap_32(1);
    import_data.devnum = __bswap_32(1);

    import_data.speed = info.speed ? __bswap_32(2) : __bswap_32(1);
    import_data.idVendor = __bswap_16(dev_desc->idVendor);
    import_data.idProduct = __bswap_16(dev_desc->idProduct);
    import_data.bcdDevice = __bswap_16(dev_desc->bcdDevice);
    import_data.bDeviceClass = dev_desc->bDeviceClass;
    import_data.bDeviceSubClass = dev_desc->bDeviceSubClass;
    import_data.bDeviceProtocol = dev_desc->bDeviceProtocol;
    import_data.bConfigurationValue = config_desc->bConfigurationValue;
    import_data.bNumConfigurations = dev_desc->bNumConfigurations;
    import_data.bNumInterfaces = config_desc->bNumInterfaces;
}

void USBipDevice::fill_list_data()
{
    int offset = 0;
    for (size_t n = 0; n < config_desc->bNumInterfaces; n++)
    {
        const usb_intf_desc_t *intf = usb_parse_interface_descriptor(config_desc, n, 0, &offset);
        devlist_data.intfs[n].bInterfaceClass = intf->bInterfaceClass,
        devlist_data.intfs[n].bInterfaceSubClass = intf->bInterfaceSubClass,
        devlist_data.intfs[n].bInterfaceProtocol = intf->bInterfaceProtocol,
        devlist_data.intfs[n].padding  = 0;
    }

    devlist_data.request.version = USBIP_VERSION;
    devlist_data.request.command = OP_REP_DEVLIST;
    devlist_data.request.status = 0;
    devlist_data.busnum = __bswap_32(1);
    devlist_data.devnum = __bswap_32(1);
    devlist_data.count = __bswap_32(1);
    strcpy(devlist_data.path, "/espressif/usbip/usb1");
    strcpy(devlist_data.busid, "1-1");

    devlist_data.speed = info.speed ? USB_FULL_SPEED : USB_LOW_SPEED;
    devlist_data.idVendor = __bswap_16(dev_desc->idVendor);
    devlist_data.idProduct = __bswap_16(dev_desc->idProduct);
    devlist_data.bcdDevice = __bswap_16(dev_desc->bcdDevice);
    devlist_data.bDeviceClass = dev_desc->bDeviceClass;
    devlist_data.bDeviceSubClass = dev_desc->bDeviceSubClass;
    devlist_data.bDeviceProtocol = dev_desc->bDeviceProtocol;
    devlist_data.bConfigurationValue = config_desc->bConfigurationValue;
    devlist_data.bNumConfigurations = dev_desc->bNumConfigurations;
    devlist_data.bNumInterfaces = config_desc->bNumInterfaces;
}

// CDC ACM specific definitions
#define CDC_ACM_SET_LINE_CODING        0x20
#define CDC_ACM_GET_LINE_CODING        0x21
#define CDC_ACM_SET_CONTROL_LINE_STATE 0x22

// Line coding structure for CDC ACM
typedef struct {
    uint32_t dwDTERate;      // 波特率
    uint8_t bCharFormat;      // 停止位: 0=1, 1=1.5, 2=2
    uint8_t bParityType;      // 校验位: 0=无, 1=奇, 2=偶, 3=标记, 4=空格
    uint8_t bDataBits;        // 数据位: 5, 6, 7, 8, 16
} cdc_line_coding_t;

int USBipDevice::req_ctrl_xfer(usbip_submit_t* req)
{
    // Allocate larger buffer for control transfers (especially for configuration descriptors)
    usb_transfer_t* _xfer_ctrl = allocate(MAX_CONTROL_BUFFER_SIZE);
    if (_xfer_ctrl == NULL) {
        USBIP_LOGE("Failed to allocate control transfer buffer");
        return -1;
    }
    _xfer_ctrl->callback = usb_ctrl_cb;
    _xfer_ctrl->context = req;
    _xfer_ctrl->bEndpointAddress = __bswap_32(req->header.ep) | (__bswap_32(req->header.direction) << 7);

    usb_setup_packet_t * temp = (usb_setup_packet_t *)_xfer_ctrl->data_buffer;
    size_t n = 0;
    int _n = __bswap_32(req->length);
    
    // First copy the setup packet
    memcpy(temp->val, (uint8_t*)&req->setup, 8);
    
    // Check if this is a CDC ACM specific request
    if (device_type == USB_DEVICE_TYPE_VCP) {
        // Process CDC ACM control requests
        if (temp->bmRequestType == 0x21 && temp->bRequest == CDC_ACM_SET_LINE_CODING) {
            // This request sets the line coding parameters
            cdc_line_coding_t* line_coding = (cdc_line_coding_t*)&req->transfer_buffer;
        } else if (temp->bmRequestType == 0xA1 && temp->bRequest == CDC_ACM_GET_LINE_CODING) {
            // This request gets the current line coding parameters
            // We can return default values since we're just passing through
        } else if (temp->bmRequestType == 0x21 && temp->bRequest == CDC_ACM_SET_CONTROL_LINE_STATE) {
            // This request sets control line states (DTR/RTS)
            uint16_t control_signal = ((uint16_t*)&req->transfer_buffer)[0];
        }
    }
    
    // Then copy data if this is an OUT transfer
    if (req->header.direction == 0) // 0: USBIP_DIR_OUT
    {
        n = _n;
        memcpy(_xfer_ctrl->data_buffer + 8, (void*)&req->transfer_buffer, n);
    }
    // Transfer buffer length is set based on direction: OUT = transfer_buffer_length, IN = 0
    // ISO transfers: no padding between packets
    _xfer_ctrl->num_bytes = sizeof(usb_setup_packet_t) + __bswap_32(req->length);
    _xfer_ctrl->bEndpointAddress = __bswap_32(req->header.ep) | (__bswap_32(req->header.direction) << 7);
    _xfer_ctrl->context = req;
    
    esp_err_t err = usb_host_transfer_submit_control(_host->clientHandle(), _xfer_ctrl);
    if (err != ESP_OK) {
        USBIP_LOGE("Failed to submit control transfer: %s", esp_err_to_name(err));
        deallocate(_xfer_ctrl);
        return -1;
    }

    return  n;
}

int USBipDevice::req_ep_xfer(usbip_submit_t* req)
{
    size_t _len = __bswap_32(req->length);
    
    uint16_t mps = 64;
    uint8_t ep_addr = __bswap_32(req->header.ep);
    int direction = __bswap_32(req->header.direction);

    // Get maximum packet size from endpoint descriptor
    if (direction != 0)
    {
        const usb_ep_desc_t *ep = endpoints[ep_addr][1];
        if (ep)
        {
            mps = ep->wMaxPacketSize;
        } else {
            USBIP_LOGE("missing EP%d", ep_addr);
            return 0;
        }

        _len = usb_round_up_to_mps(_len, mps);
    }

    // For VCP devices, ensure we handle bulk transfers correctly
    if (device_type == USB_DEVICE_TYPE_VCP) {
        // VCP typically uses bulk endpoints for data transfer
        // No special handling needed here, just ensure proper buffer allocation
    }
    
    // Allocate transfer buffer
    usb_transfer_t *xfer_read = allocate(_len);
    if(xfer_read == NULL) {
        USBIP_LOGE("Failed to allocate transfer buffer: len=%d", _len);
        return 0;
    }
    
    xfer_read->callback = &usb_read_cb;
    xfer_read->context = req;
    xfer_read->bEndpointAddress = ep_addr | (direction << 7);
    
    int n = 0;
    if (direction == 0)
    {
        // OUT transfer: copy data from request to buffer
        memcpy(xfer_read->data_buffer, (void*)&req->transfer_buffer, _len);
        n = _len;
    }
    
    // Set transfer parameters
    xfer_read->num_bytes = _len;
    xfer_read->context = req;

    // Submit transfer
    esp_err_t err = usb_host_transfer_submit(xfer_read);
    if (err != ESP_OK) {
        USBIP_LOGE("Failed to submit endpoint transfer: %s", esp_err_to_name(err));
        deallocate(xfer_read);
        return -1;
    }

    return n;
}

// Process incoming USBIP requests and dispatch to event handlers
extern "C" void parse_request(const int sock, uint8_t* rx_buffer, size_t len)
{
    uint32_t cmd = ((usbip_request_t*)rx_buffer)->command;
    _sock = sock;

    switch (cmd)
    {
    case OP_REQ_DEVLIST:{
        esp_err_t err = esp_event_post_to(loop_handle, USBIP_EVENT_BASE, OP_REQ_DEVLIST, NULL, 0, 10);
    if (err != ESP_OK) {
        USBIP_LOGE("Failed to post OP_REQ_DEVLIST event: %s", esp_err_to_name(err));
    }
        break;
    }
    case OP_REQ_IMPORT:{
        esp_err_t err = esp_event_post_to(loop_handle, USBIP_EVENT_BASE, OP_REQ_IMPORT, NULL, 0, 10);
    if (err != ESP_OK) {
        USBIP_LOGE("Failed to post OP_REQ_IMPORT event: %s", esp_err_to_name(err));
    }
        break;
    }
    case USBIP_CMD_SUBMIT:{
        urb_data_t data = {
             .socket = sock,
             .len = (int)len,
             .rx_buffer = rx_buffer
        };
        usbip_submit_t* _req = (usbip_submit_t*)(rx_buffer);
        esp_err_t err = esp_event_post_to(loop_handle, USBIP_EVENT_BASE, USBIP_CMD_SUBMIT, &data, len + sizeof(int), 10);
        if (err != ESP_OK) {
            USBIP_LOGE("Failed to post USBIP_CMD_SUBMIT event: %s", esp_err_to_name(err));
        }
        break;
    }
    case USBIP_CMD_UNLINK:{
        usbip_submit_t* _req = (usbip_submit_t*)(rx_buffer);
        usbip_submit_t* req = new (std::nothrow) usbip_submit_t(); // Use nothrow version to avoid exceptions
        if (req == nullptr) {
            USBIP_LOGE("Failed to allocate usbip_submit_t");
            break;
        }
        last_unlink = __bswap_32(_req->flags);
        vec.insert(vec.begin(), last_unlink);
        memcpy(req, _req, 0x30);
        esp_err_t err = esp_event_post_to(loop_handle, USBIP_EVENT_BASE, USBIP_CMD_UNLINK, &req, sizeof(usbip_submit_t*), 10);
        if (err != ESP_OK) {
            USBIP_LOGE("Failed to post USBIP_CMD_UNLINK event: %s", esp_err_to_name(err));
            delete req; // Clean up if event post fails
        }
        break;
    }
    default:
        USBIP_LOGE("unknown command: %" PRIu32, cmd); // PRIu32
        break;
    }
}


// USBIP类实现
USBIP::USBIP()
    : last_seqnum(0)
    , last_unlink(0)
    , device_desc(nullptr)
    , event_loop_handle(nullptr)
    , usb_sem(nullptr)
    , usb_sem1(nullptr)
    , socket_fd(-1)
    , is_ready(false)
    , finished(false)
    , current_transfer(nullptr)
    , usbip_device(nullptr)
{
    seqnum_cache.reserve(MAX_SEQNUM_CACHE);
}

USBIP::~USBIP()
{
    deinit();
}

esp_err_t USBIP::init()
{
    // 创建事件循环
    esp_event_loop_args_t loop_args = {
        .queue_size = 100,
        .task_name = "usbip_events",
        .task_priority = 21,
        .task_stack_size = 4*1024,
        .task_core_id = 0
    };

    esp_err_t err = esp_event_loop_create(&loop_args, &event_loop_handle);
    if (err != ESP_OK) {
        USBIP_LOGE("Failed to create event loop: %s", esp_err_to_name(err));
        return err;
    }

    // 创建信号量
    usb_sem = xSemaphoreCreateMutex();
    if (usb_sem == nullptr) {
        USBIP_LOGE("Failed to create usb_sem");
        return ESP_ERR_NO_MEM;
    }

    usb_sem1 = xSemaphoreCreateMutex();
    if (usb_sem1 == nullptr) {
        USBIP_LOGE("Failed to create usb_sem1");
        return ESP_ERR_NO_MEM;
    }

    // 注册事件处理器
    register_event_handlers();

    return ESP_OK;
}

esp_err_t USBIP::deinit()
{
    // 注销事件处理器
    unregister_event_handlers();

    // 清理USBIP设备
    if (usbip_device != nullptr) {
        usbip_device->deinit();
        delete usbip_device;
        usbip_device = nullptr;
    }

    // 删除信号量
    if (usb_sem != nullptr) {
        vSemaphoreDelete(usb_sem);
        usb_sem = nullptr;
    }

    if (usb_sem1 != nullptr) {
        vSemaphoreDelete(usb_sem1);
        usb_sem1 = nullptr;
    }

    // 删除事件循环
    if (event_loop_handle != nullptr) {
        esp_event_loop_delete(event_loop_handle);
        event_loop_handle = nullptr;
    }

    // 关闭socket
    if (socket_fd >= 0) {
        close(socket_fd);
        socket_fd = -1;
    }

    return ESP_OK;
}

esp_err_t USBIP::set_usbip_device(USBipDevice* device)
{
    if (usbip_device != nullptr) {
        usbip_device->deinit();
        delete usbip_device;
    }
    usbip_device = device;
    return ESP_OK;
}

bool USBIP::is_seqnum_cached(uint32_t seqnum)
{
    return std::find(seqnum_cache.begin(), seqnum_cache.end(), seqnum) != seqnum_cache.end();
}

void USBIP::add_seqnum_to_cache(uint32_t seqnum)
{
    seqnum_cache.insert(seqnum_cache.begin(), seqnum);
    if (seqnum_cache.size() >= MAX_SEQNUM_CACHE) {
        seqnum_cache.pop_back();
    }
}

esp_err_t USBIP::send_response(const void* data, size_t len, const char* log_tag)
{
    if (data == nullptr || len == 0) {
        USBIP_LOGE("Invalid parameters for send_response: data=%p, len=%d", data, len);
        return ESP_ERR_INVALID_ARG;
    }
    
    if (socket_fd < 0) {
        USBIP_LOGE("Invalid socket for send_response: sock=%d", socket_fd);
        return ESP_ERR_INVALID_STATE;
    }
    
    ssize_t sent = send(socket_fd, data, len, MSG_DONTWAIT);
    if (sent < 0) {
        USBIP_LOGE("Failed to send %s: errno=%d (%s)", log_tag, errno, strerror(errno));
        return ESP_FAIL;
    } else if (sent != (ssize_t)len) {
        USBIP_LOGW("Partial %s sent: %d/%d bytes", log_tag, (int)sent, (int)len);
        return ESP_OK;
    }
    
    USBIP_LOGD("Successfully sent %s: %d bytes", log_tag, (int)len);
    return ESP_OK;
}

void USBIP::register_event_handlers()
{
    if (event_loop_handle == nullptr) {
        return;
    }

    esp_event_handler_register_with(event_loop_handle, USBIP_EVENT_BASE, 
                                     OP_REQ_DEVLIST, _event_handler2, this);
    esp_event_handler_register_with(event_loop_handle, USBIP_EVENT_BASE, 
                                     OP_REQ_IMPORT, _event_handler2, this);
    esp_event_handler_register_with(event_loop_handle, USBIP_EVENT_BASE, 
                                     USBIP_CMD_SUBMIT, submit_event_handler, this);
    esp_event_handler_register_with(event_loop_handle, USBIP_EVENT_BASE, 
                                     USBIP_CMD_UNLINK, _event_handler1, this);
}

void USBIP::unregister_event_handlers()
{
    if (event_loop_handle == nullptr) {
        return;
    }

    esp_event_handler_unregister_with(event_loop_handle, USBIP_EVENT_BASE, 
                                       OP_REQ_DEVLIST, _event_handler2);
    esp_event_handler_unregister_with(event_loop_handle, USBIP_EVENT_BASE, 
                                       OP_REQ_IMPORT, _event_handler2);
    esp_event_handler_unregister_with(event_loop_handle, USBIP_EVENT_BASE, 
                                       USBIP_CMD_SUBMIT, submit_event_handler);
    esp_event_handler_unregister_with(event_loop_handle, USBIP_EVENT_BASE, 
                                       USBIP_CMD_UNLINK, _event_handler1);
}

// 静态回调函数
void USBIP::usb_ctrl_callback(usb_transfer_t *transfer)
{
    USBIP* usbip = static_cast<USBIP*>(g_usbip_device);
    if (usbip != nullptr) {
        esp_err_t err = esp_event_post_to(usbip->get_event_loop_handle(), USBIP_EVENT_BASE, 
                                           USB_CTRL_RESP_EVENT, &transfer, sizeof(usb_transfer_t*), 10);
        if (err != ESP_OK) {
            USBIP_LOGE("Failed to post USB_CTRL_RESP event: %s", esp_err_to_name(err));
            if (usbip->get_usbip_device() != nullptr) {
                usbip->get_usbip_device()->deallocate(transfer);
            }
        }
    }
}

void USBIP::usb_read_callback(usb_transfer_t *transfer)
{
    USBIP* usbip = static_cast<USBIP*>(g_usbip_device);
    if (usbip != nullptr) {
        esp_err_t err = esp_event_post_to(usbip->get_event_loop_handle(), USBIP_EVENT_BASE, 
                                           USB_EPx_RESP_EVENT, &transfer, sizeof(usb_transfer_t*), 10);
        if (err != ESP_OK) {
            USBIP_LOGE("Failed to post USB_EPx_RESP event: %s", esp_err_to_name(err));
            if (usbip->get_usbip_device() != nullptr) {
                usbip->get_usbip_device()->deallocate(transfer);
            }
        }
    }
}

void USBIP::event_handler(void* event_handler_arg, esp_event_base_t event_base, 
                          int32_t event_id, void* event_data)
{
    USBIP* usbip = static_cast<USBIP*>(event_handler_arg);
    if (usbip == nullptr) {
        return;
    }

    switch (event_id) {
        case USB_CTRL_RESP_EVENT:
            usbip->handle_ctrl_response(*(usb_transfer_t**)event_data);
            break;
        case USB_EPx_RESP_EVENT:
            usbip->handle_ep_response(*(usb_transfer_t**)event_data);
            break;
        default:
            break;
    }
}

void USBIP::submit_event_handler(void* event_handler_arg, esp_event_base_t event_base, 
                                 int32_t event_id, void* event_data)
{
    USBIP* usbip = static_cast<USBIP*>(event_handler_arg);
    if (usbip == nullptr) {
        return;
    }

    if (event_id == USBIP_CMD_SUBMIT) {
        usbip->handle_submit_request(static_cast<urb_data_t*>(event_data));
    }
}

// 辅助函数实现
void USBIP::handle_ctrl_response(usb_transfer_t *transfer)
{
    if (usbip_device == nullptr) {
        return;
    }

    usbip_submit_t* req = (usbip_submit_t*)transfer->context;
    uint32_t seqnum = __bswap_32(req->header.seqnum);
    
    if (is_seqnum_cached(seqnum)) {
        delete req;
        usbip_device->deallocate(transfer);
        return;
    }
    
    add_seqnum_to_cache(seqnum);

    int _len = transfer->actual_num_bytes - 8;
    if(_len < 0) {
        usbip_device->deallocate(transfer);
        return;
    }
    
    if (req->header.direction == 0) {
        _len = 0;
    }

    req->header.command = USBIP_RET_SUBMIT;
    req->header.devid = 0;
    req->header.direction = 0;
    req->header.ep = 0;
    req->status = 0;
    req->length = __bswap_32(_len);
    req->padding = 0;
    
    if(_len) {
        memcpy(&req->transfer_buffer[0], transfer->data_buffer + 8, _len);
    }
    
    if(transfer->status != USB_TRANSFER_STATUS_COMPLETED) {
        _len = 0;
        req->length = 0;
        req->status = -ETIME;
        req->error_count = 1;
    }
    
    int to_write = 0x30 + _len;
    USBIP_LOG_TRANSFER("USB_CTRL_RESP", (void*)req, to_write);
    send_response((void*)req, to_write, "USB_CTRL_RESP");
    delete req;
    usbip_device->deallocate(transfer);
}

void USBIP::handle_ep_response(usb_transfer_t *transfer)
{
    if (usbip_device == nullptr) {
        return;
    }

    usbip_submit_t* req = (usbip_submit_t*)transfer->context;
    uint32_t seqnum = __bswap_32(req->header.seqnum);
    
    if (is_seqnum_cached(seqnum)) {
        delete req;
        usbip_device->deallocate(transfer);
        return;
    }
    
    add_seqnum_to_cache(seqnum);

    int _len = transfer->actual_num_bytes;
    if(_len <= 0) {
        usbip_device->deallocate(transfer);
        return;
    }
    
    if (req->header.direction == 0) {
        _len = 0;
    }

    req->header.command = USBIP_RET_SUBMIT;
    req->header.devid = 0;
    req->header.direction = 0;
    req->header.ep = 0;
    req->status = 0;
    req->length = __bswap_32(_len);
    req->start_frame = 0;
    req->padding = 0;
    memcpy(&req->transfer_buffer[0], transfer->data_buffer, transfer->actual_num_bytes);
    
    if(transfer->status != USB_TRANSFER_STATUS_COMPLETED) {
        _len = 0;
        req->length = 0;
        req->status = -ETIME;
        req->error_count = 1;
    }
    
    int to_write = 0x30 + _len;
    USBIP_LOG_TRANSFER("USB_EPx_RESP", (void*)req, to_write);
    send_response((void*)req, to_write, "USB_EPx_RESP");
    delete req;
    usbip_device->deallocate(transfer);
}

void USBIP::handle_submit_request(urb_data_t* data)
{
    if (usbip_device == nullptr || data == nullptr) {
        return;
    }

    int socket = data->socket;
    uint8_t* rx_buffer = (uint8_t*) data->rx_buffer;
    int len = data->len;
    int start = 0;
    int _len = len;
    usbip_submit_t* __req = (usbip_submit_t*)(rx_buffer);
    uint32_t seqnum = __bswap_32(__req->header.seqnum);

    do {
        usbip_submit_t* _req = (usbip_submit_t*)(rx_buffer + start);
        usbip_submit_t* req = new (std::nothrow) usbip_submit_t();
        if (req == nullptr) {
            USBIP_LOGE("Failed to allocate usbip_submit_t");
            break;
        }
        
        int tl = 0;
        if(_req->header.direction == 0) {
            tl = __bswap_32(_req->length);
        }

        memcpy(req, _req, 0x30 + tl);
        
        int tlen = 0;
        if(req->header.ep == 0) {
            tlen = usbip_device->req_ctrl_xfer(req);
        } else {
            tlen = usbip_device->req_ep_xfer(req);
        }

        if(tlen < 0) {
            delete req;
            break;
        }

        start += 0x30 + tl;
        _len -= 0x30 + tl;
    } while (_len > 0);
}




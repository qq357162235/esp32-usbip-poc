idf_component_register(SRCS "src/usbip.cpp" "usbip_config.c"
                     INCLUDE_DIRS "include"
                     PRIV_REQUIRES usb freertos esp_event espressif__usb_host_cdc_acm usb-host)
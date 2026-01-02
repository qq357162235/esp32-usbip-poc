idf_component_register(SRCS "src/usbip.cpp"
                     INCLUDE_DIRS "include"
                     PRIV_REQUIRES usb freertos esp_event esp_vfs_fat)

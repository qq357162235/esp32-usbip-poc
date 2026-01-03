idf_component_register(SRCS "main.cpp"
                    INCLUDE_DIRS "."
                    REQUIRES usb esp_event freertos log esp_system esp_wifi esp_netif nvs_flash fatfs lwip usbip tcpserver usb-host)


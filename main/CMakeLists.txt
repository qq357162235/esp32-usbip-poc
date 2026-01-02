idf_component_register(SRCS "main.cpp" "tcp_server.c" "usbip.cpp"
                    INCLUDE_DIRS "."
                    PRIV_REQUIRES usb esp_event freertos log esp_system esp_wifi esp_netif nvs_flash fatfs  lwip)

target_compile_options(${COMPONENT_LIB} PRIVATE -fexceptions)

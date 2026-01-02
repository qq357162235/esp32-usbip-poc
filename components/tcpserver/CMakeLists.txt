idf_component_register(SRCS "src/tcp_server.c"
                     INCLUDE_DIRS "include"
                     PRIV_REQUIRES freertos esp_wifi esp_netif lwip esp_event esp_log nvs_flash)

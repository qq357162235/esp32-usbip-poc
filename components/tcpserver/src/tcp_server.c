/* BSD Socket API Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include <errno.h>
#include <sys/time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>
#include "lwip/ip_addr.h"

void parse_request(const int sock, uint8_t* rx_buffer, size_t len);

// Constants
#define PORT                        CONFIG_EXAMPLE_PORT
#define KEEPALIVE_IDLE              CONFIG_EXAMPLE_KEEPALIVE_IDLE
#define KEEPALIVE_INTERVAL          CONFIG_EXAMPLE_KEEPALIVE_INTERVAL
#define KEEPALIVE_COUNT             CONFIG_EXAMPLE_KEEPALIVE_COUNT
#define EXAMPLE_WIFI_SSID           CONFIG_EXAMPLE_WIFI_SSID
#define EXAMPLE_WIFI_PASSWORD       CONFIG_EXAMPLE_WIFI_PASSWORD
#define RX_BUFFER_SIZE              (4 * 1024)
#define MAX_CLIENTS                 5
#define TCP_SERVER_TASK_STACK_SIZE  (4 * 1024)
#define TCP_SERVER_TASK_PRIORITY    21
#define TCP_CLIENT_TASK_STACK_SIZE  (4 * 1024)
#define TCP_CLIENT_TASK_PRIORITY    21
#define WIFI_AP_IP_ADDR             "192.168.4.1"
#define WIFI_AP_NETMASK             "255.255.255.0"
#define WIFI_AP_MAX_CONNECTION      5
#define WIFI_AP_HOSTNAME            "espressif-usbipd"

static const char *TAG = "tcp_server";
static EventGroupHandle_t wifi_event_grp;
static int active_clients = 0;
static SemaphoreHandle_t client_mutex = NULL;

uint8_t rx_buffer[RX_BUFFER_SIZE];

// Helper function to safely close a socket
static void close_socket_safely(int sock)
{
    if (sock >= 0) {
        shutdown(sock, SHUT_RDWR);
        lwip_close(sock);
    }
}

// Helper function to increment active client count
static void increment_client_count(void)
{
    if (client_mutex != NULL) {
        xSemaphoreTake(client_mutex, portMAX_DELAY);
        active_clients++;
        xSemaphoreGive(client_mutex);
    }
}

// Helper function to decrement active client count
static void decrement_client_count(void)
{
    if (client_mutex != NULL) {
        xSemaphoreTake(client_mutex, portMAX_DELAY);
        if (active_clients > 0) {
            active_clients--;
        }
        xSemaphoreGive(client_mutex);
    }
}

// Helper function to check if max clients reached
static bool is_max_clients_reached(void)
{
    bool reached = false;
    if (client_mutex != NULL) {
        xSemaphoreTake(client_mutex, portMAX_DELAY);
        reached = (active_clients >= MAX_CLIENTS);
        xSemaphoreGive(client_mutex);
    }
    return reached;
}

static void do_retransmit(void* p)
{
    const int sock = (int)p;
    int len;
    int total_bytes = 0;
    
    increment_client_count();
    ESP_LOGI(TAG, "Client connected. Active clients: %d/%d", active_clients, MAX_CLIENTS);
    
    do {
        len = recv(sock, rx_buffer, RX_BUFFER_SIZE, MSG_DONTWAIT);
        if (len < 0 && errno == EWOULDBLOCK) {
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        } else if (len < 0) {
            ESP_LOGE(TAG, "Error occurred during receiving: errno %d (%s)", errno, strerror(errno));
            break;
        } else if (len == 0) {
            ESP_LOGW(TAG, "Connection closed by client");
            break;
        } else {
            total_bytes += len;
            parse_request(sock, rx_buffer, len);
        }
    } while (1);
    
    decrement_client_count();
    close_socket_safely(sock);
    ESP_LOGI(TAG, "Client disconnected. Total bytes received: %d. Active clients: %d/%d", 
             total_bytes, active_clients, MAX_CLIENTS);
    
    vTaskDelete(NULL);
}

static void tcp_server_task(void *pvParameters)
{
    char addr_str[128];
    int addr_family = (int)pvParameters;
    int ip_protocol = 0;
    int keepAlive = 1;
    int keepIdle = KEEPALIVE_IDLE;
    int keepInterval = KEEPALIVE_INTERVAL;
    int keepCount = KEEPALIVE_COUNT;
    struct sockaddr_storage dest_addr;
    int listen_sock = -1;
    int err;

    if (addr_family == AF_INET) {
        struct sockaddr_in *dest_addr_ip4 = (struct sockaddr_in *)&dest_addr;
        dest_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY);
        dest_addr_ip4->sin_family = AF_INET;
        dest_addr_ip4->sin_port = htons(PORT);
        ip_protocol = IPPROTO_IP;
    }
#ifdef CONFIG_EXAMPLE_IPV6
    else if (addr_family == AF_INET6) {
        struct sockaddr_in6 *dest_addr_ip6 = (struct sockaddr_in6 *)&dest_addr;
        bzero(&dest_addr_ip6->sin6_addr.un, sizeof(dest_addr_ip6->sin6_addr.un));
        dest_addr_ip6->sin6_family = AF_INET6;
        dest_addr_ip6->sin6_port = htons(PORT);
        ip_protocol = IPPROTO_IPV6;
    }
#endif

    listen_sock = socket(addr_family, SOCK_STREAM, ip_protocol);
    if (listen_sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d (%s)", errno, strerror(errno));
        vTaskDelete(NULL);
        return;
    }

    // Set socket options
    int opt = 1;
    if (setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        ESP_LOGW(TAG, "Failed to set SO_REUSEADDR: errno %d", errno);
    }
    
    // Set receive timeout
    struct timeval timeout;
    timeout.tv_sec = 30;
    timeout.tv_usec = 0;
    if (setsockopt(listen_sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        ESP_LOGW(TAG, "Failed to set SO_RCVTIMEO: errno %d", errno);
    }

#if defined(CONFIG_EXAMPLE_IPV4) && defined(CONFIG_EXAMPLE_IPV6)
    // Note that by default IPV6 binds to both protocols, it is must be disabled
    // if both protocols used at the same time (used in CI)
    setsockopt(listen_sock, IPPROTO_IPV6, IPV6_V6ONLY, &opt, sizeof(opt));
#endif

    ESP_LOGI(TAG, "Socket created");

    err = bind(listen_sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err != 0) {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d (%s)", errno, strerror(errno));
        ESP_LOGE(TAG, "IPPROTO: %d", addr_family);
        goto CLEAN_UP;
    }
    ESP_LOGI(TAG, "Socket bound, port %d", PORT);

    err = listen(listen_sock, MAX_CLIENTS);
    if (err != 0) {
        ESP_LOGE(TAG, "Error occurred during listen: errno %d (%s)", errno, strerror(errno));
        goto CLEAN_UP;
    }

    ESP_LOGI(TAG, "TCP server listening on port %d, max clients: %d", PORT, MAX_CLIENTS);

    while (1) {
        struct sockaddr_storage source_addr;
        socklen_t addr_len = sizeof(source_addr);
        int sock = accept(listen_sock, (struct sockaddr *)&source_addr, &addr_len);
        
        if (sock < 0) {
            ESP_LOGE(TAG, "Unable to accept connection: errno %d (%s)", errno, strerror(errno));
            break;
        }

        // Check if max clients reached
        if (is_max_clients_reached()) {
            ESP_LOGW(TAG, "Max clients (%d) reached, rejecting connection from %s", 
                     MAX_CLIENTS, addr_str);
            close_socket_safely(sock);
            continue;
        }

        // Set TCP keepalive options
        if (setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &keepAlive, sizeof(int)) < 0) {
            ESP_LOGW(TAG, "Failed to set SO_KEEPALIVE: errno %d", errno);
        }
        if (setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &keepIdle, sizeof(int)) < 0) {
            ESP_LOGW(TAG, "Failed to set TCP_KEEPIDLE: errno %d", errno);
        }
        if (setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &keepInterval, sizeof(int)) < 0) {
            ESP_LOGW(TAG, "Failed to set TCP_KEEPINTVL: errno %d", errno);
        }
        if (setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, &keepCount, sizeof(int)) < 0) {
            ESP_LOGW(TAG, "Failed to set TCP_KEEPCNT: errno %d", errno);
        }

        // Convert IP address to string
        if (source_addr.ss_family == PF_INET) {
            inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr, addr_str, sizeof(addr_str) - 1);
        }
#ifdef CONFIG_EXAMPLE_IPV6
        else if (source_addr.ss_family == PF_INET6) {
            inet6_ntoa_r(((struct sockaddr_in6 *)&source_addr)->sin6_addr, addr_str, sizeof(addr_str) - 1);
        }
#endif
        ESP_LOGI(TAG, "Socket accepted from %s", addr_str);

        // Create client task
        BaseType_t ret = xTaskCreatePinnedToCore(
            do_retransmit, 
            "tcp_client", 
            TCP_CLIENT_TASK_STACK_SIZE, 
            (void*)sock, 
            TCP_CLIENT_TASK_PRIORITY, 
            NULL, 
            1
        );
        
        if (ret != pdPASS) {
            ESP_LOGE(TAG, "Failed to create client task");
            close_socket_safely(sock);
        }
    }

CLEAN_UP:
    if (listen_sock >= 0) {
        close_socket_safely(listen_sock);
    }
    ESP_LOGI(TAG, "TCP server task exiting");
    vTaskDelete(NULL);
}
#define WIFI_CONNECTED_BIT BIT0

void esp_event_handler(void* event_handler_arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT)
    {
        switch(event_id) 
        {
            case WIFI_EVENT_AP_START:
                ESP_LOGI(TAG, "AP started");
                // 获取AP的IP地址
                esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
                if (ap_netif) {
                    esp_netif_ip_info_t ip_info;
                    esp_netif_get_ip_info(ap_netif, &ip_info);
                    char myIP[20];
                    sprintf(myIP, IPSTR, IP2STR(&ip_info.ip));
                    ESP_LOGI(TAG, "AP IP address: %s", myIP);
                }
                xEventGroupSetBits(wifi_event_grp, WIFI_CONNECTED_BIT);
                break;
            
            case WIFI_EVENT_AP_STOP:
                ESP_LOGI(TAG, "AP stopped");
                xEventGroupClearBits(wifi_event_grp, WIFI_CONNECTED_BIT);
                break;
            
            case WIFI_EVENT_AP_STACONNECTED:{
                wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
                ESP_LOGI(TAG, "station %02x:%02x:%02x:%02x:%02x:%02x join, AID=%d", 
                         event->mac[0], event->mac[1], event->mac[2], 
                         event->mac[3], event->mac[4], event->mac[5], 
                         event->aid);
                break;
            }
            
            case WIFI_EVENT_AP_STADISCONNECTED:{
                wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
                ESP_LOGI(TAG, "station %02x:%02x:%02x:%02x:%02x:%02x leave, AID=%d", 
                         event->mac[0], event->mac[1], event->mac[2], 
                         event->mac[3], event->mac[4], event->mac[5], 
                         event->aid);
                break;
            }
            
            default:
                break;
        }
    }
}

void wifi_init_ap(char* ssid, char* pass)
{
    wifi_config_t wifi_config = {0};
    
    if (strlen(ssid) > sizeof(wifi_config.ap.ssid) - 1) {
        ESP_LOGE(TAG, "SSID too long");
        return;
    }
    
    strcpy((char*)wifi_config.ap.ssid, ssid);
    
    if (pass && strlen(pass) > 0) {
        if (strlen(pass) > sizeof(wifi_config.ap.password) - 1) {
            ESP_LOGE(TAG, "Password too long");
            return;
        }
        strcpy((char*)wifi_config.ap.password, pass);
        wifi_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
        wifi_config.ap.max_connection = WIFI_AP_MAX_CONNECTION;
    } else {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
        wifi_config.ap.max_connection = WIFI_AP_MAX_CONNECTION;
    }
    wifi_config.ap.ssid_hidden = 0;
    wifi_config.ap.beacon_interval = 100;

    esp_err_t err = esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set WiFi config: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "WiFi AP configured. SSID: %s, Auth: %s, Max connections: %d",
             ssid, pass ? "WPA2" : "OPEN", wifi_config.ap.max_connection);
}

// WiFi初始化辅助函数
static esp_err_t wifi_init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition was truncated, erasing...");
        err = nvs_flash_erase();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to erase NVS: %s", esp_err_to_name(err));
            return err;
        }
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize NVS: %s", esp_err_to_name(err));
        return err;
    }
    return ESP_OK;
}

static esp_err_t wifi_init_netif(void)
{
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize netif: %s", esp_err_to_name(err));
        return err;
    }
    return ESP_OK;
}

static esp_err_t wifi_init_event_loop(void)
{
    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create event loop: %s", esp_err_to_name(err));
        return err;
    }
    return ESP_OK;
}

static esp_err_t wifi_register_event_handler(void)
{
    esp_err_t err = esp_event_handler_instance_register(ESP_EVENT_ANY_BASE, ESP_EVENT_ANY_ID, 
                                                         esp_event_handler, NULL, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register event handler: %s", esp_err_to_name(err));
        return err;
    }
    return ESP_OK;
}

static esp_err_t wifi_configure_ap_netif(void)
{
    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
    if (ap_netif == NULL) {
        ESP_LOGE(TAG, "Failed to create AP netif");
        return ESP_FAIL;
    }

    esp_err_t err = esp_netif_dhcps_stop(ap_netif);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to stop DHCP server: %s", esp_err_to_name(err));
    }
    
    esp_netif_ip_info_t ip_info;
    memset(&ip_info, 0, sizeof(ip_info));
    ip_info.ip.addr = ipaddr_addr(WIFI_AP_IP_ADDR);
    ip_info.gw.addr = ipaddr_addr(WIFI_AP_IP_ADDR);
    ip_info.netmask.addr = ipaddr_addr(WIFI_AP_NETMASK);
    
    err = esp_netif_set_ip_info(ap_netif, &ip_info);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set IP info: %s", esp_err_to_name(err));
        return err;
    }
    
    err = esp_netif_dhcps_start(ap_netif);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to start DHCP server: %s", esp_err_to_name(err));
    }
    
    err = esp_netif_set_hostname(ap_netif, WIFI_AP_HOSTNAME);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set hostname: %s", esp_err_to_name(err));
    }
    
    return ESP_OK;
}

static esp_err_t wifi_init_wifi_stack(void)
{
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_wifi_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize WiFi: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set WiFi storage: %s", esp_err_to_name(err));
        return err;
    }
    
    err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set WiFi mode: %s", esp_err_to_name(err));
        return err;
    }
    
    return ESP_OK;
}

esp_err_t wifi_init(void)
{
    esp_err_t err;
    
    // Create event group
    wifi_event_grp = xEventGroupCreate();
    if (wifi_event_grp == NULL) {
        ESP_LOGE(TAG, "Failed to create WiFi event group");
        return ESP_FAIL;
    }

    // Initialize NVS
    err = wifi_init_nvs();
    if (err != ESP_OK) {
        return err;
    }

    // Initialize TCP/IP stack
    err = wifi_init_netif();
    if (err != ESP_OK) {
        return err;
    }

    // Create default event loop
    err = wifi_init_event_loop();
    if (err != ESP_OK) {
        return err;
    }

    // Register event handler
    err = wifi_register_event_handler();
    if (err != ESP_OK) {
        return err;
    }
    
    // Configure AP network interface
    err = wifi_configure_ap_netif();
    if (err != ESP_OK) {
        return err;
    }

    // Initialize WiFi stack
    err = wifi_init_wifi_stack();
    if (err != ESP_OK) {
        return err;
    }
    
    // Start WiFi
    err = esp_wifi_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WiFi: %s", esp_err_to_name(err));
        return err;
    }
    
    // Configure AP
    wifi_init_ap(EXAMPLE_WIFI_SSID, EXAMPLE_WIFI_PASSWORD);

    ESP_LOGI(TAG, "WiFi initialized successfully");
    return ESP_OK;
}

void start_server(void)
{
    // Create mutex for client count protection
    client_mutex = xSemaphoreCreateMutex();
    if (client_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create client mutex");
        return;
    }

    // Initialize WiFi
    esp_err_t err = wifi_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize WiFi");
        return;
    }

    // Create TCP server task
    BaseType_t ret = xTaskCreatePinnedToCore(
        tcp_server_task, 
        "tcp_server", 
        TCP_SERVER_TASK_STACK_SIZE, 
        (void*)AF_INET, 
        TCP_SERVER_TASK_PRIORITY, 
        NULL, 
        1
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create TCP server task");
        return;
    }
    
    ESP_LOGI(TAG, "Server started successfully");
}
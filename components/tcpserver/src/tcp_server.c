/* BSD Socket API Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include <sys/param.h>

#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"
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

#define PORT                        CONFIG_EXAMPLE_PORT
#define KEEPALIVE_IDLE              CONFIG_EXAMPLE_KEEPALIVE_IDLE
#define KEEPALIVE_INTERVAL          CONFIG_EXAMPLE_KEEPALIVE_INTERVAL
#define KEEPALIVE_COUNT             CONFIG_EXAMPLE_KEEPALIVE_COUNT
#define EXAMPLE_WIFI_SSID           CONFIG_EXAMPLE_WIFI_SSID
#define EXAMPLE_WIFI_PASSWORD       CONFIG_EXAMPLE_WIFI_PASSWORD

static const char *TAG = "example";
static EventGroupHandle_t wifi_event_grp;

uint8_t rx_buffer[4*1024];
void close_socket(int sock)
{
        shutdown(sock, 0);
        close(sock);
}

static void do_retransmit(void* p)
{
    const int sock = (int)p;
    int len;
    do {
        len = recv(sock, rx_buffer, sizeof(rx_buffer), MSG_DONTWAIT);
        if (len < 0 && errno == EWOULDBLOCK) {
            vTaskDelay(1);
            continue;
        } else if (len < 0) {
            ESP_LOGE(TAG, "Error occurred during receiving: errno %d", errno);
            break;
        } else if (len == 0) {
            ESP_LOGW(TAG, "Connection closed");
            break;
        } else {
            parse_request(sock, rx_buffer, len);
        }
    } while (1);

    close_socket(sock);
    vTaskDelete(NULL);
}

static void tcp_server_task(void *pvParameters)
{
    char addr_str[128];
    int addr_family = (int)pvParameters;
    int ip_protocol = 0;
    int keepAlive = 0;
    int keepIdle = KEEPALIVE_IDLE;
    int keepInterval = KEEPALIVE_INTERVAL;
    int keepCount = KEEPALIVE_COUNT;
    struct sockaddr_storage dest_addr;

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

    int listen_sock = socket(addr_family, SOCK_STREAM, ip_protocol);
    if (listen_sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }
    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#if defined(CONFIG_EXAMPLE_IPV4) && defined(CONFIG_EXAMPLE_IPV6)
    // Note that by default IPV6 binds to both protocols, it is must be disabled
    // if both protocols used at the same time (used in CI)
    setsockopt(listen_sock, IPPROTO_IPV6, IPV6_V6ONLY, &opt, sizeof(opt));
#endif

    ESP_LOGI(TAG, "Socket created");

    int err = bind(listen_sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err != 0) {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        ESP_LOGE(TAG, "IPPROTO: %d", addr_family);
        goto CLEAN_UP;
    }
    ESP_LOGI(TAG, "Socket bound, port %d", PORT);

    err = listen(listen_sock, 1);
    if (err != 0) {
        ESP_LOGE(TAG, "Error occurred during listen: errno %d", errno);
        goto CLEAN_UP;
    }

    while (1) {

        ESP_LOGI(TAG, "Socket listening");

        struct sockaddr_storage source_addr; // Large enough for both IPv4 or IPv6
        socklen_t addr_len = sizeof(source_addr);
        int sock = accept(listen_sock, (struct sockaddr *)&source_addr, &addr_len);
        if (sock < 0) {
            ESP_LOGE(TAG, "Unable to accept connection: errno %d", errno);
            break;
        }

        // Set tcp keepalive option
        setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &keepAlive, sizeof(int));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &keepIdle, sizeof(int));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &keepInterval, sizeof(int));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, &keepCount, sizeof(int));
        // Convert ip address to string
        if (source_addr.ss_family == PF_INET) {
            inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr, addr_str, sizeof(addr_str) - 1);
        }
#ifdef CONFIG_EXAMPLE_IPV6
        else if (source_addr.ss_family == PF_INET6) {
            inet6_ntoa_r(((struct sockaddr_in6 *)&source_addr)->sin6_addr, addr_str, sizeof(addr_str) - 1);
        }
#endif
        ESP_LOGI(TAG, "Socket accepted ip address: %s", addr_str);

        xTaskCreatePinnedToCore(do_retransmit, "tcp_tx", 1 * 4096, (void*)sock, 21, NULL, 1);
    }

CLEAN_UP:
    close(listen_sock);
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
    strcpy((char*)wifi_config.ap.ssid, ssid);
    if(pass) {
        strcpy((char*)wifi_config.ap.password, pass);
        wifi_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
        wifi_config.ap.max_connection = 5;
    } else {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }
    wifi_config.ap.ssid_hidden = 0;

    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config));

    ESP_LOGI(TAG, "wifi_init_ap finished. SSID:%s password:%s",
             ssid, pass ? pass : "none");
 
}

esp_err_t wifi_init()
{
    wifi_event_grp = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_event_handler_instance_register(ESP_EVENT_ANY_BASE, ESP_EVENT_ANY_ID, esp_event_handler, NULL, NULL);
    
    // 创建默认的AP网络接口
    esp_netif_create_default_wifi_ap();

    // 设置AP的IP地址和网络配置（可选，默认会使用192.168.4.1）
    esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (ap_netif) {
        esp_netif_dhcps_stop(ap_netif);
        esp_netif_ip_info_t ip_info;
        memset(&ip_info, 0, sizeof(ip_info));
        ip_info.ip.addr = ipaddr_addr("192.168.4.1");
        ip_info.gw.addr = ipaddr_addr("192.168.4.1");
        ip_info.netmask.addr = ipaddr_addr("255.255.255.0");
        esp_netif_set_ip_info(ap_netif, &ip_info);
        esp_netif_dhcps_start(ap_netif);
        
        // 设置主机名
        esp_netif_set_hostname(ap_netif, "espressif-usbipd");
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM) );
    // 设置为仅AP模式
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_start() );
    // 初始化AP，设置SSID和密码
    wifi_init_ap(EXAMPLE_WIFI_SSID, EXAMPLE_WIFI_PASSWORD);

    ESP_LOGI(TAG, "wifi_init finished.");

    return ESP_OK;
}

void start_server()
{
    ESP_ERROR_CHECK(nvs_flash_init());
    wifi_init();

    xTaskCreatePinnedToCore(tcp_server_task, "tcp_server", 1 * 4096, (void*)AF_INET, 21, NULL, 1);
}

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_http_server.h"
#include "driver/gpio.h"
#include "lwip/ip_addr.h"

// Configuration - adjust these to your setup
#define RELAY_COUNT 8
#define BUTTON_COUNT 8

static const char *TAG = "HomeAutomation";

// GPIO Configuration
static const gpio_num_t relay_pins[RELAY_COUNT] = {GPIO_NUM_12, GPIO_NUM_13, GPIO_NUM_14, GPIO_NUM_15, 
                                                  GPIO_NUM_16, GPIO_NUM_17, GPIO_NUM_18, GPIO_NUM_19};
static const gpio_num_t button_pins[BUTTON_COUNT] = {GPIO_NUM_21, GPIO_NUM_22, GPIO_NUM_23, GPIO_NUM_25,
                                                   GPIO_NUM_26, GPIO_NUM_27, GPIO_NUM_32, GPIO_NUM_33};

// Network Configuration
static const char *WIFI_SSID = "ACTFIBERNET";
static const char *WIFI_PASS = "act12345";
static const char *STATIC_IP = "192.168.0.201";
static const char *GATEWAY = "192.168.0.1";
static const char *NETMASK = "255.255.255.0";

// Global state
static uint8_t relay_states = 0; // Bitmask for relay states
static SemaphoreHandle_t relay_mutex;
static QueueHandle_t button_event_queue;
static httpd_handle_t server = NULL;  // Added missing server declaration

// Button event structure
typedef struct {
    gpio_num_t pin;
    uint32_t event_time;
} button_event_t;

// Initialize GPIO
static void init_gpio() {
    // Configure relay pins as outputs
    for (int i = 0; i < RELAY_COUNT; i++) {
        gpio_reset_pin(relay_pins[i]);
        gpio_set_direction(relay_pins[i], GPIO_MODE_OUTPUT);
        gpio_set_level(relay_pins[i], 1); // Start with relays off
    }

    // Configure button pins as inputs with pull-up
    for (int i = 0; i < BUTTON_COUNT; i++) {
        gpio_reset_pin(button_pins[i]);
        gpio_set_direction(button_pins[i], GPIO_MODE_INPUT);
        gpio_set_pull_mode(button_pins[i], GPIO_PULLUP_ONLY);
    }
}

// Set relay state
static void set_relay(int relay_num, bool state) {
    if (relay_num < 0 || relay_num >= RELAY_COUNT) return;

    xSemaphoreTake(relay_mutex, portMAX_DELAY);
    if (state) {
        relay_states |= (1 << relay_num);
    } else {
        relay_states &= ~(1 << relay_num);
    }
    gpio_set_level(relay_pins[relay_num], state);
    xSemaphoreGive(relay_mutex);

    ESP_LOGI(TAG, "Relay %d set to %s", relay_num, state ? "ON" : "OFF");
}

// Toggle relay state
static void toggle_relay(int relay_num) {
    xSemaphoreTake(relay_mutex, portMAX_DELAY);
    bool current_state = (relay_states >> relay_num) & 1;
    xSemaphoreGive(relay_mutex);
    
    set_relay(relay_num, !current_state);
}

// Button interrupt handler
static void IRAM_ATTR button_isr_handler(void *arg) {
    gpio_num_t pin = (gpio_num_t)(int)arg;
    button_event_t event = {
        .pin = pin,
        .event_time = xTaskGetTickCountFromISR()
    };
    xQueueSendFromISR(button_event_queue, &event, NULL);
}

// Initialize button interrupts
static void init_button_interrupts() {
    button_event_queue = xQueueCreate(10, sizeof(button_event_t));

    for (int i = 0; i < BUTTON_COUNT; i++) {
        gpio_int_type_t intr_type = GPIO_INTR_NEGEDGE; // Trigger on button press (assuming pull-up)
        gpio_set_intr_type(button_pins[i], intr_type);
        
        // Install ISR service if not already installed
        static bool isr_service_installed = false;
        if (!isr_service_installed) {
            gpio_install_isr_service(0);
            isr_service_installed = true;
        }
        
        // Add ISR handler
        gpio_isr_handler_add(button_pins[i], button_isr_handler, (void *)button_pins[i]);
    }
}

// Button task - processes button events with debouncing
static void button_task(void *pvParameter) {
    button_event_t event;
    const TickType_t debounce_delay = pdMS_TO_TICKS(50); // 50ms debounce delay
    TickType_t last_event_time[BUTTON_COUNT] = {0};

    while (1) {
        if (xQueueReceive(button_event_queue, &event, portMAX_DELAY)) {
            // Find which button this is
            int button_num = -1;
            for (int i = 0; i < BUTTON_COUNT; i++) {
                if (button_pins[i] == event.pin) {
                    button_num = i;
                    break;
                }
            }

            if (button_num == -1) continue;

            // Debounce check
            if ((event.event_time - last_event_time[button_num]) > debounce_delay) {
                last_event_time[button_num] = event.event_time;
                
                // Toggle corresponding relay
                if (button_num < RELAY_COUNT) {
                    toggle_relay(button_num);
                }
            }
        }
    }
}
/*
static esp_err_t relay_control_handler(httpd_req_t *req) {
    char buf[100];
    int ret, relay_num;
    int state_int;  // Temporary int for sscanf

    ret = httpd_req_recv(req, buf, req->content_len);
    if (ret <= 0) {
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    if (sscanf(buf, "/relay/%d/%d", &relay_num, &state_int) != 2) {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    if (relay_num < 0 || relay_num >= RELAY_COUNT) {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    set_relay(relay_num, (bool)state_int);
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

static esp_err_t status_handler(httpd_req_t *req) {
    char resp[100];
    xSemaphoreTake(relay_mutex, portMAX_DELAY);
    snprintf(resp, sizeof(resp), "Relay states: %02x", relay_states);
    xSemaphoreGive(relay_mutex);
    
    httpd_resp_sendstr(req, resp);
    return ESP_OK;
}



static esp_err_t relay_control_handler(httpd_req_t *req) {
    char* buf;
    size_t buf_len;
    
    // Get header value length
    buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1) {
        buf = malloc(buf_len);
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            ESP_LOGI(TAG, "Found URL query => %s", buf);
            
            int relay_num, state;
            if (httpd_query_key_value(buf, "relay", buf, buf_len) == ESP_OK &&
                sscanf(buf, "%d/%d", &relay_num, &state) == 2) {
                
                ESP_LOGI(TAG, "Relay: %d, State: %d", relay_num, state);
                set_relay(relay_num, state);
                free(buf);
                httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
                return ESP_OK;
            }
            free(buf);
        }
    }
    
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Invalid request");
    return ESP_FAIL;
}
*/


static esp_err_t relay_control_handler(httpd_req_t *req) {
    // Handle both formats:
    // 1. /relay/0/1 (path parameters)
    // 2. /relay?num=0&state=1 (query parameters)
    
    int relay_num = -1, state = -1;
    
    // 1. Try path parameters first
    char *uri = strdup(req->uri);
    if (sscanf(uri, "/relay/%d/%d", &relay_num, &state) == 2) {
        free(uri);
        goto check_and_set;
    }
    free(uri);
    
    // 2. Try query parameters
    char query[50];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char num_str[10], state_str[10];
        if (httpd_query_key_value(query, "num", num_str, sizeof(num_str)) == ESP_OK &&
            httpd_query_key_value(query, "state", state_str, sizeof(state_str)) == ESP_OK) {
            relay_num = atoi(num_str);
            state = atoi(state_str);
        }
    }
    
check_and_set:
    if (relay_num >= 0 && relay_num < RELAY_COUNT && (state == 0 || state == 1)) {
        set_relay(relay_num, (bool)state);
        httpd_resp_sendstr(req, "OK");
        return ESP_OK;
    }
    
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, 
        "Use either:\n/relay/<num>/<state>\nOR\n/relay?num=X&state=Y");
    return ESP_FAIL;
}

static esp_err_t status_handler(httpd_req_t *req) {
    if (req->method != HTTP_GET) {
        httpd_resp_send_err(req, HTTPD_405_METHOD_NOT_ALLOWED, "Method not allowed");
        return ESP_FAIL;
    }

    char resp[50];
    xSemaphoreTake(relay_mutex, portMAX_DELAY);
    snprintf(resp, sizeof(resp), "Relay states: %02x", relay_states);
    xSemaphoreGive(relay_mutex);

    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}


static void start_http_server() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 8001; // Use alternative port
    config.ctrl_port = 8081;
    config.lru_purge_enable = true;
    
    // Register URI handlers
    httpd_uri_t relay_control = {
        .uri = "/relay",
        .method = HTTP_POST,
        .handler = relay_control_handler,
        .user_ctx = NULL
    };
    
    httpd_uri_t status = {
        .uri = "/status",
        .method = HTTP_GET,
        .handler = status_handler,
        .user_ctx = NULL
    };

    ESP_LOGI(TAG, "Starting HTTP server on port %d...", config.server_port);
    esp_err_t ret = httpd_start(&server, &config);
    
    if (ret == ESP_OK) {
        httpd_register_uri_handler(server, &relay_control);
        httpd_register_uri_handler(server, &status);
        ESP_LOGI(TAG, "HTTP server started");
    } else {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(ret));
        // Cleanup and retry
        if (server) {
            httpd_stop(server);
            server = NULL;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
        ESP_LOGW(TAG, "Retrying server start...");
        httpd_start(&server, &config);
    }
}



static void wifi_event_handler(void* arg, esp_event_base_t event_base, 
                            int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT) {
        if (event_id == WIFI_EVENT_STA_START) {
            ESP_LOGI(TAG, "WiFi station start");
            esp_wifi_connect();
        } 
        else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
            wifi_event_sta_disconnected_t* event = (wifi_event_sta_disconnected_t*) event_data;
            ESP_LOGE(TAG, "Disconnect reason: %d", event->reason);
            
            switch(event->reason) {
                case WIFI_REASON_AUTH_EXPIRE:
                    ESP_LOGE(TAG, "Auth expired - check password");
                    break;
                case WIFI_REASON_NO_AP_FOUND:
                    ESP_LOGE(TAG, "AP not found - check SSID");
                    break;
                case WIFI_REASON_ASSOC_TOOMANY:
                    ESP_LOGE(TAG, "Too many devices connected to AP");
                    break;
                default:
                    ESP_LOGE(TAG, "Unknown disconnect reason");
            }
            vTaskDelay(pdMS_TO_TICKS(5000)); // Longer delay between retries
            esp_wifi_connect();
        }
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

static void init_wifi() {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_t *netif = esp_netif_create_default_wifi_sta();
    assert(netif);
    
    ESP_ERROR_CHECK(esp_netif_dhcpc_stop(netif)); // DHCP_STOP

    // Set static IP if configured
    if (STATIC_IP[0] != '\0') {
        esp_netif_ip_info_t ip_info;
        memset(&ip_info, 0, sizeof(ip_info));
        
        ip_info.ip.addr = ipaddr_addr(STATIC_IP);
        ip_info.netmask.addr = ipaddr_addr(NETMASK);
        ip_info.gw.addr = ipaddr_addr(GATEWAY);
        
        esp_netif_dhcpc_stop(netif);
        esp_netif_set_ip_info(netif, &ip_info);
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                      ESP_EVENT_ANY_ID,
                                                      &wifi_event_handler,
                                                      NULL,
                                                      NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = "",
            .password = "",
        },
    };
    strncpy((char*)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid));
    strncpy((char*)wifi_config.sta.password, WIFI_PASS, sizeof(wifi_config.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));

// Add this BEFORE esp_wifi_start():
ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, 
                                                 IP_EVENT_STA_GOT_IP,
                                                 &wifi_event_handler,
                                                 NULL,
                                                 NULL));

// Reduce WiFi power save mode (helps stability):
esp_wifi_set_ps(WIFI_PS_NONE);

    ESP_ERROR_CHECK(esp_wifi_start());

    // Connection verification
    ESP_LOGI(TAG, "Waiting for WiFi connection...");
    
    int retry_count = 0;
    while(retry_count < 20) {
        vTaskDelay(pdMS_TO_TICKS(500));
        
        wifi_ap_record_t ap_info;
        if(esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            ESP_LOGI(TAG, "Connected to AP: %s (RSSI: %d)", 
                    WIFI_SSID, ap_info.rssi);
            break;
        }
        retry_count++;
    }

    // Verify IP assignment
    esp_netif_ip_info_t ip_info;
    if(esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
        ESP_LOGI(TAG, "IP Address: " IPSTR, IP2STR(&ip_info.ip));
        ESP_LOGI(TAG, "Gateway: " IPSTR, IP2STR(&ip_info.gw));
        ESP_LOGI(TAG, "Netmask: " IPSTR, IP2STR(&ip_info.netmask));
    } else {
        ESP_LOGE(TAG, "Failed to get IP address!");
    }
}

/*
static void http_server_task(void *pvParameter) {
    while (1) {
        start_http_server();
        //vTaskDelay(pdMS_TO_TICKS(10000)); // Check every 10 seconds
            vTaskDelete(NULL);

    }
}
*/
void app_main() {
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize mutex
    relay_mutex = xSemaphoreCreateMutex();
    if (relay_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return;
    }

    // Initialize hardware
    init_gpio();
    init_button_interrupts();
    init_wifi();

    // Create tasks
    xTaskCreate(&button_task, "button_task", 4096, NULL, 5, NULL);
    //xTaskCreate(&http_server_task, "http_server_task", 8192, NULL, 4, NULL);
    start_http_server();

    ESP_LOGI(TAG, "Home automation system initialized");
}

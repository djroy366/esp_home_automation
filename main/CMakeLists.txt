idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES 
        esp_http_server
        esp_wifi
        driver
        freertos
        nvs_flash
        esp_netif
)

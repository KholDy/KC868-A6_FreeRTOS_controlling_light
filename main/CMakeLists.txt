idf_component_register(SRCS "main.c"
                       PRIV_REQUIRES spi_flash driver esp_system esp_wifi esp_event esp_http_server json esp_netif nvs_flash esp_http_server
                       INCLUDE_DIRS "")

idf_component_register(SRCS "hx711.c" "db.c"
                    INCLUDE_DIRS "."
                    REQUIRES json esp_driver_uart esp_wifi esp_netif esp_timer esp_event esp_http_server nvs_flash driver spiffs sqlite3)

idf_component_register(
    SRCS "main.c" "pir.c"
    INCLUDE_DIRS "."
    REQUIRES event_manager mqtt
    PRIV_REQUIRES esp_driver_gpio esp_event esp_netif esp_timer esp_wifi log nvs_flash
)
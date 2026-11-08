idf_component_register(
    SRCS
        "app_main.cpp"
        "twai_adapter.cpp"
        "uart_transport.cpp"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        isuzu_shared
        esp_driver_twai
        esp_driver_uart
        esp_driver_gpio
        esp_timer
        esp_app_format)

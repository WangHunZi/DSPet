idf_component_register(SRCS "app_main.c" "display.c" INCLUDE_DIRS "."
    REQUIRES esp_lcd esp_driver_gpio esp_driver_spi)

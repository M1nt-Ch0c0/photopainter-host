idf_component_register(SRCS "src/board.c" "src/epd_e6.c" "src/axp2101.c"
    INCLUDE_DIRS "include" PRIV_INCLUDE_DIRS "private_include"
    PRIV_REQUIRES esp_driver_gpio esp_driver_i2c esp_driver_spi freertos heap)

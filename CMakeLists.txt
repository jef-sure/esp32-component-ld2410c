idf_component_register(
    SRCS "src/ld2410c.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_uart esp_driver_gpio
)

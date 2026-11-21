# port_esp_idf - the ONLY component that includes ESP-IDF driver headers
# (GPIO, the SPI LCD bus, the I2S speaker) and the NimBLE host headers. Implements the
# app_hal interfaces.
idf_component_register(
    SRCS
        "port_audio.c"
        "port_ble_central.cpp"
        "port_clock.cpp"
        "port_gpio.cpp"
        "port_lcd.cpp"
        "port_result.cpp"
    REQUIRES app_hal
    PRIV_REQUIRES esp_driver_gpio esp_driver_spi esp_driver_i2s esp_rom esp_system bt nvs_flash log freertos
)

# Strict, but no -Wpedantic: ESP-IDF headers use GCC extensions.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall
    -Wextra
    -Werror
    -Werror=extra            # IDF globally sets -Wno-error=extra
    -Wunused-parameter       # IDF globally sets -Wno-unused-parameter
    -Wsign-compare           # IDF globally sets -Wno-sign-compare
)

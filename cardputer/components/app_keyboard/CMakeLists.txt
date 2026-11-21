# app_keyboard - Cardputer key matrix scan and debounce. Hardware only
# through app_hal (hal_gpio, hal_clock).
idf_component_register(
    SRCS "keyboard.cpp"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES app_hal
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall
    -Wextra
    -Werror
    -Werror=extra
    -Wunused-parameter
    -Wsign-compare
)

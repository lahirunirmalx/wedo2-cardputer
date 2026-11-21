# app_display - the Cardputer LCD as a themed text surface.
# st7789.c and font5x7.c are portable C11 from the Braid demo; canvas.cpp
# is the off-screen frame and its drawing primitives (portable); display.cpp
# glues the panel to hal_lcd; theme.cpp is the only place colours are defined.
# Hardware is reached only through app_hal.
idf_component_register(
    SRCS
        "src/canvas.cpp"
        "src/display.cpp"
        "src/font5x7.c"
        "src/st7789.c"
        "src/theme.cpp"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES app_hal freertos log
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall
    -Wextra
    -Werror
    -Werror=extra
    -Wunused-parameter
    -Wsign-compare
)

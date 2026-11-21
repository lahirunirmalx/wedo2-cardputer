# app_ui - the controller application: key handling and hub session owner
# (app_controller.cpp) and the card-style screen (screen.cpp, portable,
# previewed on a PC by test/host/render_preview.sh). Hardware only through
# app_hal and the app_* components.
idf_component_register(
    SRCS "app_controller.cpp" "screen.cpp"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES app_hal app_display app_keyboard app_wedo freertos log
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall
    -Wextra
    -Werror
    -Werror=extra
    -Wunused-parameter
    -Wsign-compare
)

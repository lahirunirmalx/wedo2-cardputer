# main - supervisor only: boot, init, start the tasks.
idf_component_register(
    SRCS "app_main.cpp"
    REQUIRES app_hal port_esp_idf app_ui log freertos esp_system
)

# Set per SOURCE, not per target: PlatformIO places main's target options
# before IDF's global ones, which would cancel them.
set_source_files_properties(app_main.cpp PROPERTIES COMPILE_OPTIONS
    "-Wall;-Wextra;-Werror;-Werror=extra;-Wunused-parameter;-Wsign-compare")

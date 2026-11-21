# app_wedo - LEGO WeDo 2.0 hub: wire format (wedo_proto.c, portable C11,
# host-tested by test/host/test_wedo_proto.c) and one hub session
# (wedo_hub.cpp) on top of hal_ble_central. No BLE stack headers here.
idf_component_register(
    SRCS
        "wedo_hub.cpp"
        "wedo_proto.c"
    INCLUDE_DIRS "include"
    REQUIRES app_hal
    PRIV_REQUIRES log
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall
    -Wextra
    -Werror
    -Werror=extra
    -Wunused-parameter
    -Wsign-compare
)

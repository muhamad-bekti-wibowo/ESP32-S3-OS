idf_component_register(
    SRCS "sys_reset.c" "main.c"
    INCLUDE_DIRS "."
    REQUIRES wifi_mgr web_ui logic_engine fbd_core modbus_slave_mgr esp_timer app_update esp_driver_gpio
)

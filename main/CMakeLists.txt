idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES wifi_mgr web_ui logic_engine fbd_core modbus_slave_mgr esp_timer app_update
)

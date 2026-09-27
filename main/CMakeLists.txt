idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES wifi_mgr web_ui logic_engine
)

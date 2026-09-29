idf_component_register(
    SRCS "fbd_json.c" "fbd_value.c" "fbd_nodes.c" "fbd_graph.c" "fbd_hw_sim.c" "fbd_hw_real.c"
         "i2c_bridge_real.c" "fbd_sys_vars.c"
    INCLUDE_DIRS "include"
    REQUIRES json driver esp_adc esp_driver_gpio esp_driver_ledc esp_driver_i2c esp_driver_rmt esp_timer
)

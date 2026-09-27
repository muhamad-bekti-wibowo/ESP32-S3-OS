idf_component_register(
    SRCS "fbd_json.c" "fbd_value.c" "fbd_nodes.c" "fbd_graph.c" "fbd_hw_sim.c" "fbd_hw_real.c"
    INCLUDE_DIRS "include"
    REQUIRES json driver esp_adc esp_driver_gpio esp_driver_ledc
)

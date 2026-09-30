idf_component_register(
    SRCS "modbus_slave_task.c" "modbus_tcp_task.c" "fbd_json.c" "fbd_value.c" "fbd_nodes.c" "fbd_graph.c" "fbd_hw_sim.c" "fbd_hw_real.c"
         "i2c_bridge_real.c" "fbd_sys_vars.c" "http_endpoint_bridge.c"
         "modbus_tcp_bridge.c" "modbus_slave_bridge.c"
    INCLUDE_DIRS "include"
    REQUIRES json driver esp_adc esp_driver_gpio esp_driver_ledc esp_driver_i2c esp_driver_rmt
             esp_driver_uart esp_timer freertos lwip esp_netif
)

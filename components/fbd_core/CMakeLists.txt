idf_component_register(
    SRCS "fbd_json.c" "fbd_value.c" "fbd_nodes.c" "fbd_graph.c"
    INCLUDE_DIRS "include"
    REQUIRES json
)

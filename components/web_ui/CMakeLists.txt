idf_component_register(
    SRCS "web_ui.c"
    INCLUDE_DIRS "include"
    REQUIRES logic_engine fbd_core wifi_mgr esp_http_server json spiffs freertos esp_timer app_update
)

# webroot/ (halaman editor) di-flash sebagai image SPIFFS terpisah, di-mount
# oleh web_ui.c ke /spiffs. Partisi "storage" harus ada di partitions.csv.
spiffs_create_partition_image(storage ${CMAKE_CURRENT_SOURCE_DIR}/webroot FLASH_IN_PROJECT)

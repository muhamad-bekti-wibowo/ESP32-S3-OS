#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "wifi_mgr.h"
#include "web_ui.h"
#include "logic_engine.h"

static const char *TAG = "app_main";

static logic_program_t s_program;

#define SCAN_PERIOD_MS 20  /* ~50 Hz, mirip laju scan PLC kecil */

void app_main(void)
{
    ESP_LOGI(TAG, "ESP32 Web Logic starting...");

    logic_engine_init(&s_program);

    wifi_mgr_start_ap();
    web_ui_start(&s_program);

    ESP_LOGI(TAG, "siap. Konek ke AP lalu buka http://192.168.4.1");

    while (1) {
        logic_engine_scan(&s_program);
        vTaskDelay(pdMS_TO_TICKS(SCAN_PERIOD_MS));
    }
}

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "wifi_mgr.h"
#include "web_ui.h"
#include "logic_engine.h"
#include "fbd_graph.h"

static const char *TAG = "app_main";

/* logic_engine v1: tetap dipakai web_ui untuk endpoint /api/program dan
 * /api/status apa adanya (format JSON belum diganti - itu pekerjaan
 * spec 03). BELUM terhubung ke scan cycle fbd_graph di bawah. */
static logic_program_t s_legacy_program;

/* fbd_graph: runtime scan cycle baru (graph ber-id + topological sort),
 * jalan di FreeRTOS task terpisah pinned ke Core 1. Diisi program default
 * untuk sekarang - endpoint web belum menulis ke sini (spec 03). */
static fbd_graph_t s_active_graph;

#define SCAN_PERIOD_MS 20  /* ~50 Hz, mirip laju scan PLC kecil */

/* Rangkaian default untuk verifikasi dual-task: digital_input -> TON ->
 * AND -> digital_output, sesuai contoh schema di plan.md §7.1. */
static void build_default_graph(fbd_graph_t *g)
{
    fbd_graph_init(g);

    fbd_graph_add_node(g, "n1", FBD_NODE_DIGITAL_IN);
    fbd_node_t *ton = fbd_graph_add_node(g, "n2", FBD_NODE_TON);
    ton->params.delay_ms = 2000;
    fbd_graph_add_node(g, "n3", FBD_NODE_AND);
    fbd_graph_add_node(g, "n4", FBD_NODE_DIGITAL_OUT);

    fbd_graph_add_link(g, "n1", 0, "n3", 0);
    fbd_graph_add_link(g, "n2", 0, "n3", 1);
    fbd_graph_add_link(g, "n3", 0, "n4", 0);

    if (!fbd_graph_compile(g)) {
        ESP_LOGE(TAG, "fbd_graph_compile() gagal: graph default cyclic/invalid!");
    }
}

static void fbd_scan_task(void *pvParameters)
{
    fbd_graph_t *graph = (fbd_graph_t *)pvParameters;
    const TickType_t scan_interval = pdMS_TO_TICKS(SCAN_PERIOD_MS);
    TickType_t last_wake = xTaskGetTickCount();

    ESP_LOGI(TAG, "fbd_scan_task jalan di core %d", xPortGetCoreID());

    for (;;) {
        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);

        /* Simulasi input sederhana: digital_input & trigger TON selalu
         * true, supaya digital_output ikut menyala setelah delay_ms TON
         * lewat. Diganti dengan pembacaan GPIO fisik di spec 05 (Level 1). */
        size_t n1_idx = fbd_graph_find_node(graph, "n1");
        size_t n2_idx = fbd_graph_find_node(graph, "n2");
        graph->nodes[n1_idx].inputs[0] = fbd_make_bool(true);
        graph->nodes[n2_idx].inputs[0] = fbd_make_bool(true);

        fbd_graph_execute_cycle(graph, now_ms);

        static uint32_t last_log_ms = 0;
        if (now_ms - last_log_ms >= 1000) {
            size_t n4_idx = fbd_graph_find_node(graph, "n4");
            ESP_LOGI(TAG, "scan cycle t=%lums, digital_output=%d",
                     (unsigned long)now_ms, fbd_to_bool(graph->nodes[n4_idx].outputs[0]));
            last_log_ms = now_ms;
        }

        vTaskDelayUntil(&last_wake, scan_interval);
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "ESP32 Web Logic starting...");
    ESP_LOGI(TAG, "app_main jalan di core %d", xPortGetCoreID());

    logic_engine_init(&s_legacy_program);
    build_default_graph(&s_active_graph);

    wifi_mgr_start_ap();
    web_ui_start(&s_legacy_program);

    xTaskCreatePinnedToCore(fbd_scan_task, "fbd_scan", 4096, &s_active_graph, 5, NULL, 1);

    ESP_LOGI(TAG, "siap. Konek ke AP lalu buka http://192.168.4.1");
}

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_ota_ops.h"

#include <string.h>

#include "wifi_mgr.h"
#include "web_ui.h"
#include "logic_engine.h"
#include "fbd_graph.h"
#include "fbd_hw_backend.h"
#include "fbd_sys_vars.h"

static const char *TAG = "app_main";

/* Provider SYS.* (spec 06): wifi_mgr punya getter WiFi, fbd_core tidak
 * boleh depend langsung ke wifi_mgr (fbd_core generik, wifi_mgr spesifik
 * proyek ini) - jadi didaftarkan di sini, layer yang tahu keduanya. */
static fbd_value_t sys_var_provider(const char *name)
{
    if (strcmp(name, "SYS.WIFI_CONNECTED") == 0) {
        return fbd_make_bool(wifi_mgr_is_sta_connected());
    }
    if (strcmp(name, "SYS.WIFI_RSSI") == 0) {
        return fbd_make_int(wifi_mgr_get_sta_rssi());
    }
    return fbd_make_empty();
}

/* logic_engine v1: tetap dipakai web_ui untuk endpoint /api/status apa
 * adanya (format lama, dipertahankan untuk debug selama migrasi bertahap
 * ke fbd_graph). BELUM terhubung ke scan cycle fbd_graph di bawah. */
static logic_program_t s_legacy_program;

/* Dual-buffer graph (spec 07, plan.md §11.1): dua instance fbd_graph_t
 * tetap (bukan alokasi dinamis - prinsip #9 plan.md), scan task baca dari
 * *g_active, web handler tulis ke *g_standby lalu set g_reload_requested.
 * Swap terjadi di AWAL scan cycle berikutnya (bukan mutex/lock kompleks) -
 * scan task TIDAK PERNAH menunggu web handler, dan web handler TIDAK
 * PERNAH menunggu scan task. Pertukaran pointer itu sendiri atomic di
 * arsitektur single-core-per-pointer ini (pointer write 32-bit selalu
 * atomic di Xtensa/RISC-V), jadi tidak butuh critical section. */
static fbd_graph_t s_graph_a;
static fbd_graph_t s_graph_b;
static fbd_graph_t *g_active_graph = &s_graph_a;
static fbd_graph_t *g_standby_graph = &s_graph_b;
static volatile bool g_reload_requested = false;

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

/* Simulasi input digital_input: set true untuk node FBD_NODE_DIGITAL_IN yang
 * MASIH mode simulated (params.hw_mode != FBD_HW_REAL) di graph yang SEDANG
 * aktif (bukan berasumsi id tertentu ada), supaya scan task tidak crash
 * setelah graph diganti lewat POST /api/program dengan node id berbeda.
 * Node dengan hw_mode "real" dibaca dari GPIO fisik sungguhan di
 * fbd_graph_execute_cycle() (lihat fbd_graph.c evaluate_node), inputs[0]
 * di sini diabaikan untuk node itu - aman untuk tetap di-set. */
static void simulate_digital_inputs(fbd_graph_t *graph)
{
    for (size_t i = 0; i < graph->node_count; ++i) {
        if (graph->nodes[i].type == FBD_NODE_DIGITAL_IN &&
            graph->nodes[i].params.hw_mode != FBD_HW_REAL) {
            graph->nodes[i].inputs[0] = fbd_make_bool(true);
        }
    }
}

static void fbd_scan_task(void *pvParameters)
{
    (void)pvParameters;
    const TickType_t scan_interval = pdMS_TO_TICKS(SCAN_PERIOD_MS);
    TickType_t last_wake = xTaskGetTickCount();

    ESP_LOGI(TAG, "fbd_scan_task jalan di core %d", xPortGetCoreID());

    for (;;) {
        /* Swap di AWAL cycle, sebelum baca graph apa pun - lihat komentar
         * dual-buffer di atas. Setelah baris ini, g_active_graph pasti
         * menunjuk ke graph yang sudah tervalidasi & compile() sukses
         * (web_ui.c hanya set flag ini setelah validasi lolos). */
        if (g_reload_requested) {
            fbd_graph_t *tmp = g_active_graph;
            g_active_graph = g_standby_graph;
            g_standby_graph = tmp;
            g_reload_requested = false;
            ESP_LOGI(TAG, "graph aktif diganti (live update tanpa reboot)");
        }

        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);

        simulate_digital_inputs(g_active_graph);
        fbd_graph_execute_cycle(g_active_graph, now_ms);

        static uint32_t last_log_ms = 0;
        if (now_ms - last_log_ms >= 1000) {
            ESP_LOGI(TAG, "scan cycle t=%lums, node_count=%u",
                     (unsigned long)now_ms, (unsigned)g_active_graph->node_count);
            last_log_ms = now_ms;
        }

        vTaskDelayUntil(&last_wake, scan_interval);
    }
}

/* Konfirmasi ke bootloader bahwa firmware yang baru saja diflash lewat
 * OTA ini sehat (berhasil boot sampai sini tanpa crash) - kalau TIDAK
 * dipanggil dan device reboot lagi tanpa konfirmasi ini (mis. crash
 * loop), bootloader otomatis rollback ke firmware sebelumnya
 * (CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE, lihat sdkconfig.defaults).
 * Aman dipanggil juga untuk boot normal (bukan OTA) - jadi no-op kalau
 * partisi sudah berstatus valid sebelumnya. */
static void confirm_ota_boot_healthy(void)
{
    esp_ota_img_states_t state;
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (esp_ota_get_state_partition(running, &state) == ESP_OK &&
        state == ESP_OTA_IMG_PENDING_VERIFY) {
        esp_ota_mark_app_valid_cancel_rollback();
        ESP_LOGI(TAG, "OTA: firmware baru dikonfirmasi sehat, rollback dibatalkan");
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "ESP32 Web Logic starting...");
    ESP_LOGI(TAG, "app_main jalan di core %d", xPortGetCoreID());

    confirm_ota_boot_healthy();

    logic_engine_init(&s_legacy_program);
    build_default_graph(g_active_graph);
    fbd_graph_init(g_standby_graph); /* standby kosong sampai POST /api/program pertama */

    /* Backend real selalu tersedia di firmware device fisik - node individual
     * memilih simulated/real lewat params.hw_mode masing-masing (dual backend,
     * plan.md §3), bukan lewat backend global. Default node baru tetap
     * "simulated" (lihat fbd_json.c parse_hw_mode) sampai user eksplisit
     * ganti ke "real" di editor atau JSON. */
    fbd_hw_set_backend(fbd_hw_real_backend());
    fbd_sys_vars_set_provider(sys_var_provider);

    wifi_mgr_start_apsta();
    web_ui_start(&s_legacy_program, &g_active_graph, &g_standby_graph, &g_reload_requested);

    xTaskCreatePinnedToCore(fbd_scan_task, "fbd_scan", 4096, NULL, 5, NULL, 1);

    ESP_LOGI(TAG, "siap. Konek ke AP lalu buka http://192.168.4.1");
}

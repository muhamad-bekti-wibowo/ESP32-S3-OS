#include "http_endpoint_bridge.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

typedef struct {
    char path[HTTP_ENDPOINT_BRIDGE_PATH_LEN];
    bool in_use;
    float query_a;
    float query_b;
    bool query_ever_set;
    float response_value;
    bool response_has_value;
    uint32_t response_revision;
} http_endpoint_slot_t;

static http_endpoint_slot_t s_slots[HTTP_ENDPOINT_BRIDGE_MAX_SLOTS];
static portMUX_TYPE s_spinlock = portMUX_INITIALIZER_UNLOCKED;

void http_endpoint_bridge_init(void)
{
    portENTER_CRITICAL(&s_spinlock);
    memset(s_slots, 0, sizeof(s_slots));
    portEXIT_CRITICAL(&s_spinlock);
}

/* Cari slot by path, buat baru kalau belum ada (sampai kapasitas
 * tercapai) - dipanggil dari DALAM critical section oleh caller, jadi
 * fungsi ini TIDAK boleh panggil portENTER_CRITICAL lagi (non-reentrant
 * di ESP-IDF FreeRTOS spinlock). */
static http_endpoint_slot_t *find_or_create_slot_locked(const char *path)
{
    for (int i = 0; i < HTTP_ENDPOINT_BRIDGE_MAX_SLOTS; ++i) {
        if (s_slots[i].in_use && strncmp(s_slots[i].path, path, HTTP_ENDPOINT_BRIDGE_PATH_LEN) == 0) {
            return &s_slots[i];
        }
    }
    for (int i = 0; i < HTTP_ENDPOINT_BRIDGE_MAX_SLOTS; ++i) {
        if (!s_slots[i].in_use) {
            s_slots[i].in_use = true;
            strncpy(s_slots[i].path, path, HTTP_ENDPOINT_BRIDGE_PATH_LEN - 1);
            s_slots[i].path[HTTP_ENDPOINT_BRIDGE_PATH_LEN - 1] = '\0';
            return &s_slots[i];
        }
    }
    return NULL; /* kapasitas penuh - caller abaikan (tidak fatal) */
}

void http_endpoint_bridge_set_query(const char *path, float query_a, float query_b)
{
    portENTER_CRITICAL(&s_spinlock);
    http_endpoint_slot_t *slot = find_or_create_slot_locked(path);
    if (slot) {
        slot->query_a = query_a;
        slot->query_b = query_b;
        slot->query_ever_set = true;
    }
    portEXIT_CRITICAL(&s_spinlock);
}

bool http_endpoint_bridge_get_query(const char *path, float *out_query_a, float *out_query_b)
{
    bool ever_set = false;
    portENTER_CRITICAL(&s_spinlock);
    http_endpoint_slot_t *slot = find_or_create_slot_locked(path);
    if (slot) {
        *out_query_a = slot->query_a;
        *out_query_b = slot->query_b;
        ever_set = slot->query_ever_set;
    } else {
        *out_query_a = 0.0f;
        *out_query_b = 0.0f;
    }
    portEXIT_CRITICAL(&s_spinlock);
    return ever_set;
}

void http_endpoint_bridge_set_response(const char *path, float response_value, bool has_response)
{
    portENTER_CRITICAL(&s_spinlock);
    http_endpoint_slot_t *slot = find_or_create_slot_locked(path);
    if (slot) {
        slot->response_value = response_value;
        slot->response_has_value = has_response;
        slot->response_revision++;
    }
    portEXIT_CRITICAL(&s_spinlock);
}

http_endpoint_bridge_response_t http_endpoint_bridge_get_response(const char *path)
{
    http_endpoint_bridge_response_t result = {0};
    portENTER_CRITICAL(&s_spinlock);
    http_endpoint_slot_t *slot = find_or_create_slot_locked(path);
    if (slot) {
        result.value = slot->response_value;
        result.has_response = slot->response_has_value;
        result.revision = slot->response_revision;
    }
    portEXIT_CRITICAL(&s_spinlock);
    return result;
}

http_endpoint_bridge_response_t http_endpoint_bridge_wait_response(const char *path,
                                                                     uint32_t before_revision,
                                                                     uint32_t timeout_ms)
{
    /* Polling dengan jeda pendek (2ms, jauh di bawah scan cycle 20ms)
     * sampai revision berubah DUA KALI atau timeout - task httpd yang
     * panggil ini boleh blocking singkat (bukan critical section, cuma
     * vTaskDelay biasa), TIDAK menahan scan task sama sekali karena
     * scan task tidak pernah menunggu balik ke sini.
     *
     * set_response() dipanggil TIAP scan cycle (bukan hanya saat query
     * baru), jadi revision naik terus menerus di ~50Hz walau query tidak
     * berubah. Kenaikan PERTAMA setelah set_query() hanya membuktikan
     * cycle itu SUDAH LEWAT node http_endpoint sendiri - untuk pola
     * feedback "endpoint -> node lain -> balik ke input endpoint" (mis.
     * kalkulator), nilai input0 yang mencerminkan query BARU baru
     * terdorong (push-propagate) SETELAH node hilir (mis. Math) selesai
     * dieksekusi di cycle YANG SAMA, sehingga baru terbaca oleh
     * http_endpoint di evaluate_node() pada cycle BERIKUTNYA (kenaikan
     * revision KEDUA). Endpoint statis (input0 tidak tersambung feedback,
     * atau nilainya tidak bergantung pada query cycle ini) tetap dapat
     * jawaban benar dari kenaikan pertama karena has_response konsisten
     * di kedua cycle - jadi menunggu 1 revision ekstra aman untuk semua
     * kasus, hanya menambah maks ~20ms. */
    TickType_t start = xTaskGetTickCount();
    TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);
    uint32_t target_revision = before_revision + 2;
    http_endpoint_bridge_response_t result;
    for (;;) {
        result = http_endpoint_bridge_get_response(path);
        if ((int32_t)(result.revision - target_revision) >= 0) {
            return result;
        }
        if ((xTaskGetTickCount() - start) >= timeout_ticks) {
            return result; /* timeout - caller tetap dapat nilai terakhir yang ada */
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

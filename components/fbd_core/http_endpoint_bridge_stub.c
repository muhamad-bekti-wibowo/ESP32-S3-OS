/* Stub http_endpoint_bridge untuk test host (PC) - TIDAK ada FreeRTOS di
 * host, jadi tidak perlu critical section sungguhan (test host
 * single-threaded). Perilaku fungsional (baca/tulis slot by path) tetap
 * sama persis dengan versi firmware (http_endpoint_bridge.c), supaya
 * test fbd_graph_test.c/fbd_json_test.c bisa memverifikasi logic node
 * FBD_NODE_HTTP_ENDPOINT tanpa perlu hardware ESP32. */
#include "http_endpoint_bridge.h"
#include <string.h>

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

void http_endpoint_bridge_init(void)
{
    memset(s_slots, 0, sizeof(s_slots));
}

static http_endpoint_slot_t *find_or_create_slot(const char *path)
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
    return NULL;
}

void http_endpoint_bridge_set_query(const char *path, float query_a, float query_b)
{
    http_endpoint_slot_t *slot = find_or_create_slot(path);
    if (slot) {
        slot->query_a = query_a;
        slot->query_b = query_b;
        slot->query_ever_set = true;
    }
}

bool http_endpoint_bridge_get_query(const char *path, float *out_query_a, float *out_query_b)
{
    http_endpoint_slot_t *slot = find_or_create_slot(path);
    if (slot) {
        *out_query_a = slot->query_a;
        *out_query_b = slot->query_b;
        return slot->query_ever_set;
    }
    *out_query_a = 0.0f;
    *out_query_b = 0.0f;
    return false;
}

void http_endpoint_bridge_set_response(const char *path, float response_value, bool has_response)
{
    http_endpoint_slot_t *slot = find_or_create_slot(path);
    if (slot) {
        slot->response_value = response_value;
        slot->response_has_value = has_response;
        slot->response_revision++;
    }
}

http_endpoint_bridge_response_t http_endpoint_bridge_get_response(const char *path)
{
    http_endpoint_bridge_response_t result = {0};
    http_endpoint_slot_t *slot = find_or_create_slot(path);
    if (slot) {
        result.value = slot->response_value;
        result.has_response = slot->response_has_value;
        result.revision = slot->response_revision;
    }
    return result;
}

http_endpoint_bridge_response_t http_endpoint_bridge_wait_response(const char *path,
                                                                     uint32_t before_revision,
                                                                     uint32_t timeout_ms)
{
    /* Test host tidak punya scan task terpisah yang jalan async - tidak
     * ada gunanya polling beneran, langsung return state saat ini. */
    (void)before_revision; (void)timeout_ms;
    return http_endpoint_bridge_get_response(path);
}

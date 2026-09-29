#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Jembatan data antara task httpd (server kedua, port terpisah - lihat
 * web_ui.c) dan fbd_scan_task (main.c) untuk node FBD_NODE_HTTP_ENDPOINT
 * dinamis - TANPA menyentuh fbd_graph_t/fbd_node_state_t sama sekali.
 *
 * KENAPA TIDAK taruh state ini di dalam node->state seperti node lain:
 * proyek ini pakai dual-buffer graph swap (pointer active/standby
 * ditukar tiap scan cycle, BUKAN mutex - lihat main.c/web_ui.h). Setiap
 * kali user Save program baru, SELURUH fbd_graph_t di-copy penuh
 * (*(*standby_graph_ptr) = s_scratch_graph) - pointer ke node/state
 * LAMA jadi tidak valid lagi. Task httpd server kedua TIDAK BOLEH pegang
 * pointer ke node di dalam graph untuk waktu lama (bisa jadi stale
 * kapan saja Save terjadi). Solusi: array slot STATIS terpisah,
 * diindeks dengan STRING PATH (stabil, tidak berubah walau graph
 * di-swap/di-Save ulang), bukan pointer ke node.
 *
 * Thread-safety: akses lewat portENTER_CRITICAL/portEXIT_CRITICAL
 * (disable interrupt SESAAT, bukan block/tunggu lama) - cukup untuk
 * baca/tulis beberapa float per slot, TIDAK perlu mutex/semaphore/task
 * terpisah (data sesederhana ini tidak butuh sinkronisasi kompleks). */

#define HTTP_ENDPOINT_BRIDGE_MAX_SLOTS 16 /* cukup untuk FBD_MAX_NODES/4 endpoint realistis */
#define HTTP_ENDPOINT_BRIDGE_PATH_LEN 32

void http_endpoint_bridge_init(void);

/* Dipanggil task httpd (endpoint_get_handler) SAAT request masuk - tulis
 * nilai query string ke slot path ini, tandai "ada request baru" (dibaca
 * fbd_graph.c evaluate_node() di scan cycle berikutnya). Slot dibuat
 * otomatis kalau path belum pernah dipakai (sampai
 * HTTP_ENDPOINT_BRIDGE_MAX_SLOTS tercapai). */
void http_endpoint_bridge_set_query(const char *path, float query_a, float query_b);

/* Dipanggil fbd_graph.c evaluate_node() tiap scan cycle - baca nilai
 * query TERBARU untuk path ini (dari http_endpoint_bridge_set_query()
 * kapan pun terakhir dipanggil). Return false kalau path belum pernah
 * ada request sama sekali (query_a/b diisi 0). */
bool http_endpoint_bridge_get_query(const char *path, float *out_query_a, float *out_query_b);

/* Dipanggil fbd_graph.c evaluate_node() tiap scan cycle - tulis nilai
 * response TERBARU (dari inputs[0] node, kalau tersambung) untuk path
 * ini, dan tandai "response siap" (revision counter naik). */
void http_endpoint_bridge_set_response(const char *path, float response_value, bool has_response);

/* Dipanggil task httpd (endpoint_get_handler) - baca response TERAKHIR
 * untuk path ini beserta revision counter saat ini. has_response=false
 * kalau node tidak punya input tersambung (endpoint_get_handler fallback
 * ke file statis di kasus ini). */
typedef struct {
    float value;
    bool has_response;
    uint32_t revision;
} http_endpoint_bridge_response_t;

http_endpoint_bridge_response_t http_endpoint_bridge_get_response(const char *path);

/* Polling helper: dipanggil task httpd berulang (dengan jeda singkat)
 * sampai revision naik DUA KALI dari before_revision atau timeout_ms
 * terlampaui - dipakai menunggu scan cycle memproses query yang baru
 * saja ditulis, supaya response HTTP membawa hasil TERBARU (bukan hasil
 * dari request sebelumnya). Kenaikan PERTAMA hanya membuktikan node
 * http_endpoint sudah lewat 1 scan cycle; untuk pola feedback
 * (http_endpoint -> node lain -> balik ke input0 http_endpoint, mis.
 * kalkulator), nilai baru baru terdorong ke input0 SETELAH node hilir
 * selesai di cycle yang sama, jadi baru kebaca evaluate_node() di cycle
 * BERIKUTNYA - makanya tunggu kenaikan KEDUA. Return response TERBARU
 * (revision baru kalau berhasil dalam waktu, revision lama kalau
 * timeout - caller tetap dapat nilai valid di kedua kasus, cuma
 * mungkin agak basi kalau timeout). */
http_endpoint_bridge_response_t http_endpoint_bridge_wait_response(const char *path,
                                                                     uint32_t before_revision,
                                                                     uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

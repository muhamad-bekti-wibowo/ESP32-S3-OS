#pragma once

#include "logic_engine.h"
#include "fbd_graph.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Mulai HTTP server.
 *
 * legacy_prog: dipakai endpoint /api/status (v1, format lama, dipertahankan
 *   untuk debug/monitor selama migrasi bertahap ke fbd_graph).
 *
 * Dual-buffer graph (spec 07, plan.md §11.1):
 *   active_graph_ptr:  alamat variabel pointer *g_active_graph* main.c.
 *     GET /api/program membaca *(*active_graph_ptr) - HARUS dereference
 *     dua kali karena pointer ini bisa berubah kapan saja (scan task
 *     menukar g_active_graph<->g_standby_graph di awal tiap cycle).
 *   standby_graph_ptr: alamat variabel pointer *g_standby_graph* main.c.
 *     POST /api/program SELALU parse+compile ke *(*standby_graph_ptr),
 *     TIDAK PERNAH menyentuh graph aktif secara langsung.
 *   reload_requested_ptr: di-set true oleh POST /api/program setelah
 *     validasi (parse+compile) standby graph sukses. Scan task membaca
 *     flag ini di awal cycle dan melakukan swap - lihat main.c.
 *
 * web_ui.c TIDAK PERNAH menunggu scan task (tidak ada mutex/lock di jalur
 * ini) - satu-satunya sinkronisasi adalah flag boolean yang di-set setelah
 * standby graph selesai divalidasi penuh, dan swap pointer di sisi scan
 * task sendiri. */
void web_ui_start(logic_program_t *legacy_prog,
                   fbd_graph_t **active_graph_ptr,
                   fbd_graph_t **standby_graph_ptr,
                   volatile bool *reload_requested_ptr);

#ifdef __cplusplus
}
#endif

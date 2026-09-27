#pragma once

#include "logic_engine.h"
#include "fbd_graph.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Mulai HTTP server.
 * legacy_prog: dipakai endpoint /api/status (v1, format lama, dipertahankan
 *   untuk debug/monitor selama migrasi bertahap ke fbd_graph).
 * active_graph: graph aktif fbd_core. POST /api/program mengganti isi
 *   *active_graph langsung (belum dual-buffer - itu spec 07) setelah
 *   validasi fbd_json_parse()+fbd_graph_compile() sukses. GET /api/program
 *   dump *active_graph sesuai schema.md. */
void web_ui_start(logic_program_t *legacy_prog, fbd_graph_t *active_graph);

/* Mutex yang melindungi *active_graph dari race condition write (endpoint
 * web, Core 0) vs read (scan task, Core 1). fbd_scan_task WAJIB lock ini
 * sebelum fbd_graph_execute_cycle() - lihat main.c. Valid setelah
 * web_ui_start() dipanggil. */
SemaphoreHandle_t web_ui_get_graph_mutex(void);

#ifdef __cplusplus
}
#endif

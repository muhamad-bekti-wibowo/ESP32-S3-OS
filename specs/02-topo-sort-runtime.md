# Spec 2 — Runtime Graph Berbasis ID + Topological Sort + Dual-Task

Ref: [plan.md §7.1](../plan.md), [§8.1–8.3](../plan.md), Fase 3.
Sebelumnya: [01-fbdvalue-core.md](01-fbdvalue-core.md) (harus lolos dulu).
Berikutnya: [03-schema-freeze.md](03-schema-freeze.md).

## Scope
**Kerjakan:**
- Ganti model node v1 (index array, urutan eksekusi = urutan definisi) di
  [logic_engine.h](../components/logic_engine/include/logic_engine.h) dengan
  model baru: node ber-`id` string, link eksplisit `{from_id, from_port,
  to_id, to_port}`, dan **topological sort (Kahn's algorithm)** wajib
  dijalankan sebelum eksekusi pertama.
- Pindahkan node dari spec 01 (`fbd_core`) ke struct node baru ini
  (`inputs[MAX_NODE_INPUTS]`, `outputs[MAX_NODE_OUTPUTS]` bertipe
  `fbd_value_t`, bukan `double`).
- Pecah `app_main` di [main.c](../main/main.c): scan cycle jadi FreeRTOS
  task terpisah pinned ke Core 1, web server tetap Core 0 (AsyncWebServer
  equivalent di ESP-IDF: `esp_http_server` yang sudah dipakai `web_ui.c` —
  cek dulu itu jalan di task/core mana saat ini).
- Deteksi cyclic dependency: kalau `compile()` gagal (execution_order tidak
  mencakup semua node), log error dan JANGAN jalankan scan cycle dengan
  graph rusak.

**JANGAN kerjakan di spec ini:**
- Jangan bikin dual-buffer graph swap (itu spec 07) — untuk sekarang boleh
  reload dengan cara sederhana (device restart scan task atau load sekali
  di awal), asal tidak corrupt.
- Jangan tambah node Level 1/2 (ADC, PWM, I2C) — tetap node Level 0 saja
  dari spec 01.
- Jangan sentuh Drawflow/editor — `web_ui` tetap serve `index.html` apa
  adanya untuk sekarang, endpoint JSON boleh tetap format v1 sementara
  (schema baru di-freeze di spec 03).

## File yang disentuh
```
components/fbd_core/
  include/fbd_graph.h   <- FBDNode, FBDLink, FBDGraph, compile(), execute_cycle()
  fbd_graph.c
components/logic_engine/   <- REVISI: pakai fbd_graph baru, atau deprecated & dihapus
                                bertahap (putuskan saat implementasi, catat di commit
                                message kalau logic_engine dihapus)
main/main.c                <- REVISI: xTaskCreatePinnedToCore untuk scan task
test_host/
  fbd_graph_test.c         <- test host: node ditambah TERBALIK urutannya,
                                 compile(), verifikasi execution_order benar
```

## Desain (C, terjemahan dari plan.md §8.1-8.2)
```c
#define MAX_NODES 64
#define MAX_LINKS 128
#define MAX_NODE_INPUTS 4
#define MAX_NODE_OUTPUTS 2
#define MAX_ID_LEN 16

typedef struct {
    char id[MAX_ID_LEN];
    node_type_t type;
    node_params_t params;   /* union/struct sesuai type, dari spec 01 */
    node_state_t state;     /* ton_state_t dkk dari spec 01 */
    fbd_value_t inputs[MAX_NODE_INPUTS];
    fbd_value_t outputs[MAX_NODE_OUTPUTS];
} fbd_node_t;

typedef struct {
    size_t from_idx; uint8_t from_port;
    size_t to_idx;   uint8_t to_port;
} fbd_link_t;

typedef struct {
    fbd_node_t nodes[MAX_NODES];
    size_t node_count;
    fbd_link_t links[MAX_LINKS];
    size_t link_count;
    size_t execution_order[MAX_NODES];
    size_t order_count;
} fbd_graph_t;

bool fbd_graph_add_node(fbd_graph_t *g, const fbd_node_t *node);
bool fbd_graph_add_link(fbd_graph_t *g, const char *from_id, uint8_t from_port,
                         const char *to_id, uint8_t to_port);
bool fbd_graph_compile(fbd_graph_t *g);   /* Kahn's algorithm, return false jika cyclic */
void fbd_graph_execute_cycle(fbd_graph_t *g, uint32_t now_ms);
```
Catatan: tidak ada `std::vector`/`std::unordered_map` di C — pakai array
fixed-size (`MAX_NODES`, `MAX_LINKS`) sesuai gaya v1 (`LOGIC_MAX_BLOCKS`).
Pencarian id→index pakai linear scan (jumlah node kecil, tidak perlu hash
map di tahap ini).

## Langkah kerja
1. Tulis `fbd_graph.h`/`.c` dengan Kahn's algorithm (in-degree array + queue
   array, bukan `std::queue`).
2. Tulis test host `fbd_graph_test.c`: tambah node dengan urutan `scale_1`,
   lalu `add_1`, lalu `const_2`, lalu `const_1` (sengaja terbalik dari
   dependency), tambah links yang benar, `compile()`, print
   `execution_order`, pastikan urutan hasil sesuai dependency (const dulu,
   scale terakhir) — BUKAN urutan penambahan ke array.
3. Compile & jalankan test host dulu (tanpa ESP32), baru integrasikan ke
   firmware.
4. Di `main.c`: buat `fbd_graph_t g_active_graph` global, isi dari JSON
   default/testing, `fbd_graph_compile()`, lalu
   `xTaskCreatePinnedToCore(fbd_scan_task, "fbd_scan", 4096, &g_active_graph, 2, NULL, 1)`.
   Pastikan `web_ui_start()` tetap dipanggil di context yang jalan di Core 0
   (default `app_main` biasanya core 0 di ESP32-S3 — verifikasi, jangan
   asumsi).
5. Uji dengan LED fisik + tombol (kalau tersedia) ATAU dengan Serial log
   timestamp: kirim HTTP request yang sengaja lambat (`vTaskDelay` 2 detik
   di handler test) sambil pantau log scan cycle tetap jalan tanpa telat.

## Kriteria selesai
- [ ] Test host `fbd_graph_test.c` lolos: urutan eksekusi sesuai dependency,
      bukan urutan penambahan node di memori.
- [ ] Firmware compile & flash sukses, scan task jalan di Core 1 (dibuktikan
      lewat `vTaskGetTraceInfo`/log atau minimal `xPortGetCoreID()` di dalam
      task).
- [ ] HTTP request lambat (delay 2 detik sengaja) TIDAK membuat scan cycle
      telat — dibuktikan lewat timestamp log tiap cycle tetap ~20ms.
- [ ] Graph dengan cyclic dependency (sengaja dibuat di test) ditolak oleh
      `fbd_graph_compile()` (return false), tidak crash.

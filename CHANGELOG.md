# Changelog

Semua perubahan penting proyek ini dicatat di file ini.

## [Unreleased]

### Added (schema JSON + endpoint save/load)
- `schema.md`: schema JSON final untuk Level 0-1 — semua tipe node dengan
  contoh `params` konkret, aturan `links` (`{node, port}` object, bukan
  notasi string), aturan `params` vs `state`, versioning.
- `components/fbd_core/fbd_json.*`: `fbd_json_parse()` (JSON → `fbd_graph_t`,
  menolak seluruh document kalau ada satu error — bukan partial-load) dan
  `fbd_json_serialize()` (`fbd_graph_t` → JSON). Round-trip dites di host.
- `web_ui.c`: `POST /api/program` sekarang parse+compile ke scratch graph
  dulu, baru diterapkan ke graph aktif kalau kedua langkah sukses (kalau
  gagal, graph aktif tidak disentuh). `GET /api/program` dump graph aktif
  sesuai schema.md. Endpoint dilindungi mutex (`web_ui_get_graph_mutex()`)
  supaya tidak race dengan `fbd_scan_task` di Core 1 - bukan dual-buffer
  proper (itu tahap berikutnya), cukup mencegah corruption.
- `wifi_mgr_start_apsta()`: mode WiFi APSTA (AP `ESP32-WebLogic` + STA ke
  jaringan rumah), dipakai `main.c` sebagai pengganti `wifi_mgr_start_ap()`
  supaya PC dev bisa akses device tanpa pindah koneksi WiFi manual.

### Fixed (ditemukan lewat verifikasi hardware nyata, bukan test host)
- **Stack overflow di `fbd_json_parse()`**: fungsi ini menaruh
  `fbd_graph_t tmp;` sebagai local variable (`sizeof` ~19KB) di dalam
  stack-nya sendiri, dipanggil dari task `httpd` yang stack-nya jauh
  lebih kecil (4-8KB). Overflow ini merusak heap TLSF secara diam-diam;
  crash (`assert failed: block_next ... !block_is_last(block)`) baru
  muncul di `cJSON_Delete()` berikutnya, sehingga awalnya terlihat seperti
  masalah jaringan (request timeout/connection reset), bukan bug
  firmware. Diperbaiki dengan menulis langsung ke `*out_graph` yang
  disediakan caller (scratch buffer statis di `web_ui.c`), bukan local
  variable — lihat kontrak baru di `fbd_json.h`.
- `program_post_handler`: `fbd_graph_t new_graph;` juga sempat dideklarasi
  sebagai local variable di handler (bug serupa, ikut diperbaiki jadi
  `static fbd_graph_t s_scratch_graph` sebelum root cause di atas
  ditemukan).

### Added
- `components/fbd_core/`: implementasi `FBDValue` (tagged union C, 16
  byte, tanpa alokasi heap) dan node Level 0 murni software (Logic
  AND/OR/NOT/XOR/NAND/NOR, Compare generik, Constant, Variable get/set,
  Math ADD/SUB/MUL/DIV/MIN/MAX/ABS/SCALE/CLAMP, Timing TON/TOF/TP/Counter
  CTU) — fondasi migrasi ke runtime Function Block Diagram (FBD).
- `components/fbd_core/fbd_graph.*`: runtime graph ber-id (`fbd_graph_t`)
  dengan topological sort (Kahn's algorithm, deteksi cyclic dependency)
  dan `fbd_graph_execute_cycle()`. Node type Level 0 dari `fbd_nodes.h`
  dipetakan ke satu `fbd_node_type_t` per tipe.
- `main.c`: scan cycle `fbd_graph` dipindah ke FreeRTOS task terpisah
  (`fbd_scan_task`) pinned ke Core 1 lewat `xTaskCreatePinnedToCore`;
  web server (`web_ui_start`) tetap di context `app_main` (Core 0).
  Endpoint HTTP (`logic_engine`/`web_ui`) belum terhubung ke `fbd_graph`
  — masih berjalan paralel, disambungkan di tahap berikutnya.
- `test_host/fbd_graph_test.c`: test host membuktikan topological sort
  bekerja walau node ditambah dengan urutan sengaja terbalik dari
  dependency-nya, dan graph cyclic ditolak `fbd_graph_compile()`.
- Rename `compare_op_t`/`math_op_t` (dan enumerator-nya) di `fbd_core`
  menjadi `fbd_compare_op_t`/`fbd_math_op_t` dengan prefix `FBD_` pada
  enumerator, karena nama tersebut bertabrakan dengan tipe yang sama di
  `logic_engine.h` (v1) saat kedua header di-include bersamaan di
  `main.c` — ditemukan lewat build firmware yang gagal compile.

### Fixed
- `main/CMakeLists.txt`: tambah `esp_timer` ke `REQUIRES` (dipakai
  `esp_timer_get_time()` di scan task), sebelumnya gagal compile dengan
  `fatal error: esp_timer.h: No such file or directory`.
- `main.c`: node `n2` (TON) di graph default tidak pernah diberi input
  (`inputs[0]` tetap `FBD_EMPTY` sejak `fbd_graph_init` yang memset nol),
  sehingga `digital_output` selalu false — timer tidak pernah start.
  Ditemukan lewat verifikasi hardware nyata (log serial menunjukkan
  `digital_output=0` terus-menerus melewati `delay_ms`), tidak ketahuan
  dari test host karena test host memberi input secara eksplisit.

### Verified (hardware ESP32-S3 nyata, via `idf.py -p COM4 flash monitor`)
- `app_main` jalan di Core 0, `fbd_scan_task` jalan di Core 1 (dibuktikan
  lewat `xPortGetCoreID()` di log boot).
- TON `delay_ms=2000`: `digital_output` tetap 0 di t=1018ms/2018ms, lalu
  jadi 1 di t=3018ms dan seterusnya — transisi timing benar.
- Endpoint HTTP disengaja `vTaskDelay(2000)` (`/api/debug_slow`, dihapus
  lagi setelah tes) memakan 2.06 detik nyata, sementara log scan cycle
  tetap konsisten setiap ~1000ms tanpa gap selama periode itu — Core 0
  (web server) tidak menahan Core 1 (scan cycle) sama sekali.

### Added (v1, sebelum migrasi ke runtime FBD)
- Struktur awal proyek ESP-IDF untuk ESP32-S3 (`esp32-web-logic`).
- `logic_engine`: interpreter runtime dengan block `const`, `input`,
  `output`, `compare`, `if_else`, `counter`, `math`. Program dieksekusi
  tiap scan cycle (~50 Hz).
- `wifi_mgr`: WiFi Access Point sederhana (SSID `ESP32-WebLogic`).
- `web_ui`: HTTP server dengan endpoint `POST /api/program` (muat program
  JSON baru tanpa reflash) dan `GET /api/status` (baca nilai output tiap
  block untuk debug).
- Halaman editor visual drag-drop (`webroot/index.html`) untuk menyusun
  block dan wire, lalu mengirim definisi program ke ESP32.
- README.md dengan dokumentasi arsitektur, format JSON program, cara
  build/flash, dan roadmap.

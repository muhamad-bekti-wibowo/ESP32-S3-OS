# Changelog

Semua perubahan penting proyek ini dicatat di file ini.

## [Unreleased]

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

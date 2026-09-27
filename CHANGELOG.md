# Changelog

Semua perubahan penting proyek ini dicatat di file ini.

## [Unreleased]

### Added (dual-buffer graph swap + live update tanpa reboot)
- `main.c`: dua instance `fbd_graph_t` tetap (`s_graph_a`/`s_graph_b`),
  pointer `g_active_graph`/`g_standby_graph`, `volatile bool
  g_reload_requested`. Scan task menukar pointer di AWAL tiap cycle
  kalau flag di-set - tidak ada mutex/lock antara web handler dan scan
  task (mutex sebelumnya, dari spec 03/06, dihapus sepenuhnya).
- `web_ui.h`/`web_ui_start()`: signature berubah dari `fbd_graph_t
  *active_graph` jadi `fbd_graph_t **active_graph_ptr` + `**standby_
  graph_ptr` + `volatile bool *reload_requested_ptr` - `GET /api/program`
  harus dereference dua kali karena pointer aktif bisa berubah kapan
  saja (swap oleh scan task).
- Auto-backup (`backup_program_file`/`prune_old_backups`): sebelum
  overwrite `program.json` di SPIFFS, salinan lama disimpan sebagai
  `program_backup_<uptime_ms>.json`. Retensi 5 backup terbaru - backup
  tertua otomatis dihapus kalau melebihi batas.
- Persistence (`load_program_file_at_startup`): `program.json` dimuat ke
  standby graph + swap saat boot, jadi program yang di-Save bertahan
  setelah reboot (sebelumnya hilang, kembali ke `build_default_graph()`).

### Verified (hardware ESP32-S3 nyata)
- Swap program (`c1=111` → `c2=222`) via `POST /api/program` selesai
  <200ms, scan cycle log tetap konsisten tanpa gap - tidak ada crash/
  corruption saat update terjadi ketika scan task sedang jalan.
- Cyclic dependency di-`POST` saat device running → ditolak (400 Bad
  Request), graph aktif TIDAK berubah, TIDAK ADA downtime.
- Retensi backup diuji dengan 8 `POST` berturut-turut: backup ke-6 dan
  seterusnya memicu penghapusan backup tertua (log `backup lama dihapus`),
  tidak menumpuk tanpa batas.
- Program bertahan setelah reboot fisik (log `program.json dimuat dari
  SPIFFS`, `GET /api/program` mengembalikan graph yang sama).

### Added (Level 2: I2C primitive + WiFi system variable)
- `i2c_bridge.h`: interface I2C register-level generik (`i2c_bridge_read_reg`/
  `write_reg`), TIDAK ADA mode simulated (I2C selalu bus fisik nyata, beda
  dari ADC/PWM/servo). `i2c_bridge_real.c` (ESP-IDF `i2c_master`, timeout
  8ms, cache device handle per alamat) untuk firmware; `i2c_bridge_stub.c`
  (selalu error) untuk test host.
- Node baru: `i2c_read_reg` (`outputs[0]`=raw_bytes, `outputs[1]`=error),
  `i2c_write_reg`, `sys_var_get`. Validasi params di `fbd_json.c`.
- `fbd_sys_vars.h`: provider pattern (function pointer terdaftar dari
  `main.c`) supaya `fbd_core` tidak depend langsung ke `wifi_mgr`.
  `SYS.WIFI_CONNECTED`/`SYS.WIFI_RSSI` didukung; `SYS.IP_ADDRESS`/
  `SYS.HOSTNAME` belum (string tidak muat di `fbd_value_t` 8-byte).
- `wifi_mgr`: getter read-only (`wifi_mgr_is_sta_connected`,
  `wifi_mgr_get_sta_rssi`, `wifi_mgr_get_sta_ip`) + config STA dari NVS
  (`load_sta_config_from_nvs`, `wifi_mgr_save_sta_config`) dengan fallback
  ke SSID hardcode kalau NVS kosong.
- `web_ui.c`: `GET /api/program` sekarang menyertakan `outputs` tiap node
  (live monitor polling minimal, pengganti WebSocket — lebih sederhana,
  tidak menambah risiko regresi di `httpd` yang sudah stabil). Endpoint
  baru `GET`/`POST /api/network` untuk tab System > Network
  (`network.html`, terpisah dari canvas Drawflow) — password tidak pernah
  diekspos balik lewat `GET`, config baru berlaku setelah reboot (sengaja).

### Verified (hardware ESP32-S3 + LCD 16x2 I2C nyata)
- `sys_var_get(SYS.WIFI_RSSI)` membaca RSSI asli (-48, -56 dBm sesuai
  kondisi nyata) dipakai `Compare` untuk warning LED — logic benar di
  kedua kondisi (RSSI kuat & lemah).
- `i2c_read_reg(address=0x27)` (LCD 16x2 PCF8574 nyata): sukses,
  `error=false`. `i2c_read_reg(address=100)` (device tidak ada): NACK
  terdeteksi, `error=true`, fallback `[0,0]`. Scan cycle log konsisten
  tanpa gap di kedua kasus — I2C gagal tidak pernah menahan scan cycle.
- `POST /api/network` (SSID=MIFON) tersimpan ke NVS, dibaca ulang saat
  reboot (log `Config WiFi STA dimuat dari NVS: SSID=MIFON`), device
  reconnect ke IP yang sama.

### Added (Level 1 I/O: dual backend simulated/real)
- `components/fbd_core/fbd_hw_backend.h`: interface dual backend (function
  pointer per operasi) untuk `digital_input`/`digital_output`/
  `analog_input`/`pwm_output`/`servo` — `params.hw_mode` per-node
  menentukan `"simulated"` atau `"real"`, dipilih tanpa branch hardcoded
  di `fbd_graph.c`.
- `fbd_hw_sim.c`: backend simulated, tidak menyentuh register apa pun.
  Dites di host (`test_host/fbd_hw_test.c`) — skenario kriteria selesai
  spec 05a: `analog_input(simulated) → SCALE → Compare > 50 →
  digital_output` jalan benar tanpa hardware ADC nyata.
- `fbd_hw_real.c`: backend real, driver ESP-IDF nyata (`gpio`,
  `adc_oneshot`, `ledc`). Init hardware per node dilakukan sekali
  (`state.hw_initialized`), bukan tiap scan cycle.
- Node type baru: `analog_input`, `pwm_output`, `servo` (params sesuai
  [schema.md](../schema.md)) + field `hw_mode` ditambahkan ke
  `digital_input`/`digital_output` yang sudah ada.
- Validasi pin GPIO ESP32-S3 di `fbd_json.c`: `POST /api/program` menolak
  `hw_mode: "real"` pada strapping pin (0/3/45/46), USB-JTAG (19/20), atau
  SPI flash/PSRAM (26-37, device ini pakai PSRAM Octal 8MB). Mode
  `simulated` tidak divalidasi.
- `main.c`: `fbd_hw_set_backend(fbd_hw_real_backend())` dipanggil sekali
  di startup — backend real selalu tersedia, node memilih lewat
  `params.hw_mode` masing-masing. `simulate_digital_inputs()` disesuaikan
  supaya tidak menimpa node yang sudah `hw_mode: real`.
- Editor: slider (`type: 'range'`) untuk `analog_input.sim_value` di
  panel Properties, sesuai kriteria spec 05 ("slider untuk analog_input").

### Verified (hardware ESP32-S3 nyata)
- `digital_input(pin=4, real, pullup)` → `digital_output(pin=5, real)`
  di-`POST` ke device, scan cycle tetap stabil (`node_count=2`, tidak ada
  gap) tanpa crash setelah GPIO real diaktifkan. Pengukuran multimeter
  langsung (tombol fisik → LED) belum dilakukan manusia.

### Added (editor Drawflow)
- Vendor Drawflow (drawflow.min.js/css, ~48KB total) ke `webroot/`, tanpa
  CDN — device offline-first, AP mode tanpa internet.
- `node-types.js`: metadata terpusat semua node Level 0-1 (label, jumlah
  input/output, daftar field params dengan tipe & default) — satu sumber
  kebenaran untuk palette, registrasi Drawflow node, dan converter.
- `app.js`: `drawflowToSchema()`/`schemaToDrawflow()` (converter dua arah
  eksplisit, terpisah dari kode UI umum), `saveProgram()`/`loadProgram()`
  (fetch ke `/api/program`). Dites headless via Node.js (tanpa browser)
  untuk skenario 2× `digital_input` → `AND` → `digital_output`, dan dites
  end-to-end via HTTP nyata ke device.
- `index.html` diganti total: canvas Drawflow + toolbar Save/Load,
  menggantikan editor v1 (canvas manual custom, format JSON lama).

### Fixed (editor Drawflow, ditemukan lewat verifikasi end-to-end)
- `web_ui.c`: handler `GET /` lama hardcode hanya serve
  `/spiffs/index.html` — file lain (`app.js`, `node-types.js`, dst) 404.
  Diperbaiki jadi handler wildcard (`/*`) generik yang serve file apa pun
  dari SPIFFS sesuai URI, dengan `Content-Type` sesuai ekstensi.
- `fbd_graph.h`/`fbd_json.c`: field `params.mode` untuk `digital_input`
  (ada di schema.md sejak spec 03) tidak pernah tersimpan — `fbd_node_
  params_t` tidak punya field untuk itu, jadi parse mengabaikannya diam-
  diam dan serialize tidak pernah menuliskannya balik. Ketahuan lewat uji
  end-to-end (POST lalu GET, `mode` hilang). Ditambahkan `fbd_pin_mode_t`
  + field `pin_mode`, divalidasi wajib saat parse `digital_input`.

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

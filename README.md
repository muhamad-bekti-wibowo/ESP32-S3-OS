# ESP32 Web Logic (FBD/Virtual PLC Runtime)

Proyek ESP-IDF (target ESP32-S3) untuk memprogram logika sederhana
(if-else, counter, operasi matematika, compare, I/O) langsung dari web
browser, tanpa perlu flash ulang firmware setiap kali logika berubah.

Arah pengembangan: runtime Function Block Diagram (FBD) / virtual PLC
open-source, dieksekusi dari JSON tanpa kompilasi ulang firmware, dengan
editor visual berbasis Drawflow yang di-hosting langsung dari ESP32-S3.

## Status implementasi

- **Selesai:** `FBDValue` tagged union (16 byte, tanpa alokasi heap) dan
  node Level 0 murni software — Logic (AND/OR/NOT/XOR/NAND/NOR/Compare),
  Data (Constant, Variable), Math (ADD/SUB/MUL/DIV/MIN/MAX/ABS/SCALE/
  CLAMP), Timing (TON/TOF/TP/Counter). Lihat `components/fbd_core/fbd_value.*`
  dan `components/fbd_core/fbd_nodes.*`.
- **Selesai:** runtime graph ber-id (`fbd_graph_t`) dengan topological sort
  (Kahn's algorithm) dan scan cycle FreeRTOS terpisah pinned ke Core 1
  (web server tetap di Core 0). Lihat `components/fbd_core/fbd_graph.*`
  dan `main/main.c`.
- **Selesai:** schema JSON di-freeze (lihat [schema.md](schema.md)) dan
  endpoint `GET`/`POST /api/program` terhubung langsung ke `fbd_graph`
  (parser/serializer di `components/fbd_core/fbd_json.*`). `POST` menolak
  JSON invalid atau graph cyclic tanpa mengubah graph aktif; `GET`
  mengembalikan graph aktif sesuai schema.md. Endpoint `/api/status`
  (v1/`logic_engine`) dipertahankan untuk debug selama migrasi.
- Semua parser/graph di atas dites di `test_host/` (PC, tanpa ESP32/idf.py)
  sebelum diintegrasikan ke firmware, DAN diverifikasi di hardware fisik
  (ESP32-S3 nyata via `idf.py flash monitor`): `app_main` di Core 0,
  `fbd_scan_task` di Core 1, TON timing benar, scan cycle tidak telat
  saat HTTP delay disengaja, `POST`/`GET /api/program` round-trip sukses,
  cyclic link ditolak dengan status 400 tanpa mengubah graph aktif.
- Firmware WiFi mode APSTA (`wifi_mgr_start_apsta`): AP `ESP32-WebLogic`
  tetap aktif, ditambah koneksi STA ke jaringan rumah — memudahkan akses
  device dari PC dev tanpa pindah koneksi WiFi manual berulang kali.
- **Selesai:** editor visual berbasis Drawflow di-vendor ke `webroot/`
  (offline, tanpa CDN). Tiap node type Level 0-1 di-registrasi 1:1 lewat
  metadata terpusat (`node-types.js`). Converter dua arah eksplisit
  (`drawflowToSchema`/`schemaToDrawflow` di `app.js`) — dites headless
  (Node.js, tanpa browser/DOM) untuk skenario dari kriteria selesai
  (2× `digital_input` → `AND` → `digital_output`), dan dites end-to-end
  lewat HTTP nyata ke device (`POST` lalu `GET /api/program` identik).
  **Belum dikonfirmasi:** klik manual drag-drop di canvas browser
  sungguhan (buka node dari palette, sambung wire dengan mouse, klik
  tombol Save/Load) — logic-nya sudah terbukti benar lewat test headless
  + endpoint HTTP, tapi interaksi UI itu sendiri (event listener Drawflow,
  render form params di node) belum diklik langsung oleh manusia.
- **Selesai:** dual backend (plan.md §3) untuk I/O fisik Level 1 —
  `digital_input`/`digital_output`/`analog_input`/`pwm_output`/`servo`
  masing-masing punya `params.hw_mode` (`"simulated"` atau `"real"`),
  dipilih per-node lewat function pointer di
  `components/fbd_core/fbd_hw_backend.h`:
  - `fbd_hw_sim.c` — tidak menyentuh register apa pun, dites di host
    (`test_host/fbd_hw_test.c`).
  - `fbd_hw_real.c` — driver ESP-IDF nyata (`gpio`, `adc_oneshot`, `ledc`).
    Init hardware (`gpio_config`/`ledc_channel_config`/dst) dipanggil
    sekali per node (`state.hw_initialized`), bukan tiap scan cycle.
  - Validasi pin: `POST /api/program` menolak `hw_mode: "real"` pada
    strapping pin (GPIO0/3/45/46), pin USB-JTAG (GPIO19/20), atau pin
    SPI flash/PSRAM (GPIO26-37) — lihat [schema.md](schema.md). Mode
    `simulated` tidak divalidasi (tidak pernah menyentuh GPIO fisik).
  - **Diverifikasi di hardware nyata:** `digital_input(pin=4, real,
    pullup)` → `digital_output(pin=5, real)` di-POST ke device, scan
    cycle tetap stabil tanpa crash. **Belum dikonfirmasi manusia:**
    pengukuran multimeter langsung (tombol fisik di GPIO4 → LED/multimeter
    di GPIO5).
  - Editor: slider (bukan number input polos) untuk `analog_input.sim_value`
    di panel Properties, sesuai kriteria spec 05.
- **Selesai:** Level 2 — I2C primitive register-level (`i2c_read_reg`/
  `i2c_write_reg`, HANYA primitive generik, bukan driver sensor spesifik)
  dan WiFi sebagai system variable read-only (`sys_var_get`):
  - `components/fbd_core/i2c_bridge.h` + `i2c_bridge_real.c` (ESP-IDF
    `i2c_master`, timeout 8ms — NACK/timeout tidak pernah menahan scan
    cycle) / `i2c_bridge_stub.c` (test host, selalu error karena tidak
    ada bus I2C fisik di PC).
  - `fbd_sys_vars.h`: provider pattern supaya `fbd_core` tidak depend
    langsung ke `wifi_mgr` — `main.c` mendaftarkan getter
    `SYS.WIFI_CONNECTED`/`SYS.WIFI_RSSI`.
  - Live monitor: `outputs[]` tiap node (termasuk `raw_bytes`/`error`
    dari `i2c_read_reg`) ditambahkan ke response `GET /api/program` —
    polling minimal, bukan WebSocket (lebih sederhana, tanpa menambah
    risiko regresi di `httpd` yang sudah stabil).
  - Tab **System > Network** (`network.html`) terpisah dari canvas
    Drawflow: `GET`/`POST /api/network` untuk baca/simpan SSID/password/
    hostname WiFi STA ke NVS (`wifi_mgr_save_sta_config`). Password
    tidak pernah diekspos balik lewat `GET`. Config baru diterapkan
    setelah reboot (sengaja, supaya tidak memutus response HTTP yang
    sedang mengirim "sukses" itu sendiri).
  - **Diverifikasi penuh di hardware nyata:** `sys_var_get(SYS.WIFI_RSSI)`
    membaca RSSI asli device (`-48`, `-56` dBm sesuai kondisi nyata,
    bukan mock) dan dipakai `Compare` untuk warning LED. `i2c_read_reg`
    dites dengan LCD 16x2 (PCF8574, alamat `0x27`) nyata — sukses
    (`error=false`) dan gagal (alamat tidak ada, `error=true`) keduanya
    terverifikasi, scan cycle tidak pernah macet di kedua kasus. Config
    WiFi STA disimpan ke NVS lalu **dibaca ulang saat reboot** (bukan
    hardcode lagi) — dibuktikan lewat log boot `Config WiFi STA dimuat
    dari NVS`.
- **Selesai:** live update program tanpa reboot (dual-buffer graph swap,
  tahap terakhir roadmap aktif — Level 3/SPI/I2S tetap ditunda total):
  - `main.c`: dua instance `fbd_graph_t` tetap (`s_graph_a`/`s_graph_b`,
    bukan alokasi dinamis), pointer `g_active_graph`/`g_standby_graph`,
    dan `volatile bool g_reload_requested`. Scan task menukar pointer di
    **awal tiap cycle** kalau flag di-set — tidak ada mutex/lock,
    web handler tidak pernah menunggu scan task dan sebaliknya.
  - `web_ui.c`: `POST /api/program` SELALU parse+compile ke standby graph
    terlebih dulu; graph aktif baru tersentuh (via swap pointer di scan
    task) setelah validasi lolos sepenuhnya.
  - Auto-backup: sebelum overwrite `program.json` di SPIFFS, salinan lama
    disimpan sebagai `program_backup_<uptime_ms>.json`. Retensi dibatasi
    5 backup terbaru (`prune_old_backups`) — tidak menumpuk tanpa batas.
  - Persistence: `program.json` dimuat otomatis saat boot (`load_program_
    file_at_startup`), jadi program yang di-Save bertahan setelah reboot
    (sebelumnya hilang, kembali ke default).
  - Live monitor: field `outputs` di `GET /api/program` (dari spec 06)
    memenuhi kebutuhan "indikator status node" — polling, bukan WebSocket.
  - **Diverifikasi penuh di hardware nyata:** swap program (`c1=111` →
    `c2=222`) selesai <200ms setelah `POST`, scan cycle log tetap
    konsisten tanpa gap. Cyclic dependency di-`POST` saat device running →
    ditolak (400), graph aktif tidak berubah, tidak ada downtime. Backup
    file muncul tiap overwrite (dibuktikan lewat log), retensi 5 backup
    teruji dengan 8 `POST` berturut-turut (backup ke-6 dst memicu hapus
    backup tertua). Program bertahan setelah reboot fisik (log `program.
    json dimuat dari SPIFFS`, `GET /api/program` mengembalikan graph yang
    sama, bukan default).

- **Selesai:** OTA (Over-The-Air firmware update) lewat web UI, di luar
  roadmap aktif spec 01-07 tapi ditambahkan atas permintaan setelah
  ditemukan flash fisik device sebenarnya **16MB** (bukan 8MB seperti
  konfigurasi awal - separuh flash tidak terpakai, dikonfirmasi lewat
  `esptool flash_id` di hardware nyata):
  - `sdkconfig.defaults`: `CONFIG_ESPTOOLPY_FLASHSIZE_16MB`,
    `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` (rollback otomatis).
  - `partitions.csv`: dual app partition `ota_0`/`ota_1` (3MB masing-
    masing) + `otadata` (8KB), SPIFFS diperbesar ke 4MB. Firmware
    sekarang ~900KB - jauh di bawah 3MB, banyak ruang tumbuh.
  - `main.c`: `confirm_ota_boot_healthy()` dipanggil di awal `app_main()`
    - membatalkan rollback (`esp_ota_mark_app_valid_cancel_rollback()`)
    kalau firmware berhasil boot sampai situ. Kalau TIDAK dipanggil
    (mis. firmware baru crash sebelum sampai app_main) dan device
    reboot lagi, bootloader otomatis rollback ke firmware lama sendiri
    - device tidak bisa ter-brick permanen hanya karena upload firmware
    yang salah/corrupt.
  - `web_ui.c`: `POST /api/ota` terima body binary firmware `.bin`
    langsung (bukan JSON), streaming ke `esp_ota_write()` (bukan
    dibuffer penuh di RAM), tulis ke partisi OTA yang SEDANG TIDAK
    AKTIF (`esp_ota_get_next_update_partition`), lalu `esp_ota_set_boot_
    partition` dan `esp_restart()`. Partisi lama TIDAK disentuh sampai
    firmware baru terbukti valid (`esp_ota_end` memvalidasi image).
    `GET /api/ota/status` untuk cek partisi/versi/state firmware yang
    sedang jalan.
  - `ota.html` (tab System > Firmware): upload file `.bin` via
    `<input type=file>` + `XMLHttpRequest` (progress bar upload).
  - **Diverifikasi penuh di hardware nyata:** flash penuh dengan
    partition table baru sukses (`SPI Flash Size: 16MB` terkonfirmasi
    di boot log), config WiFi STA di NVS tetap tersambung otomatis
    setelah flash (offset NVS tidak berubah). `GET /api/ota/status`
    awal: partisi `ota_0`, state `valid`. Upload firmware yang sama
    (915KB) lewat `POST /api/ota` sukses dalam ~17 detik, device reboot
    otomatis, `GET /api/ota/status` setelah reboot: partisi berpindah
    ke **`ota_1`**, state `valid` (rollback berhasil dibatalkan otomatis
    karena boot sukses sampai `app_main`).

### Bug signifikan yang ditemukan & diperbaiki selama verifikasi hardware
- **Flash dikonfigurasi 8MB padahal fisiknya 16MB**: sejak awal proyek
  `sdkconfig.defaults`/`partitions.csv` mengasumsikan flash 8MB tanpa
  pernah dicek ke hardware asli. Ketahuan saat merencanakan fitur OTA
  (butuh partisi ekstra) dan mengecek `esptool flash_id` di device -
  separuh kapasitas flash (8MB) tidak pernah terpakai selama ini.
  Diperbaiki bersamaan dengan menambah dual app partition OTA.
- **Stack overflow di `fbd_json_parse()`**: `sizeof(fbd_graph_t)` ~19KB,
  jauh lebih besar dari stack task `httpd` (default 4-8KB). Versi awal
  menaruh `fbd_graph_t tmp;` sebagai local variable di dalam
  `fbd_json_parse()` — overflow ini merusak heap TLSF secara diam-diam
  dan crash baru terlihat jauh setelahnya (`cJSON_Delete()` berikutnya),
  membuat request POST/GET terlihat seperti masalah jaringan padahal
  bug ada di firmware. Diperbaiki dengan menulis langsung ke `*out_graph`
  yang disediakan caller (scratch buffer statis), bukan local variable.
  Lihat komentar di `fbd_json.h`/`fbd_json.c` untuk detail kontraknya.
- **`web_ui.c` hanya serve `index.html`**: editor Drawflow butuh beberapa
  file terpisah (`app.js`, `node-types.js`, `style.css`, `drawflow.min.*`),
  tapi handler `GET /` lama hardcode hanya kirim `/spiffs/index.html` —
  file lain 404. Diperbaiki jadi handler wildcard generik yang serve file
  apa pun dari SPIFFS berdasarkan URI, dengan MIME type sesuai ekstensi.
- **`params.mode` untuk `digital_input` tidak pernah disimpan**: field ini
  ada di contoh schema.md sejak awal, tapi `fbd_node_params_t` tidak
  punya field untuk itu — `fbd_json_parse()` diam-diam mengabaikannya dan
  `fbd_json_serialize()` tidak pernah menuliskannya balik. Ketahuan lewat
  uji end-to-end (POST lalu GET, `mode` hilang dari hasil). Diperbaiki
  dengan menambah `fbd_pin_mode_t` + field `pin_mode` di params, divalidasi
  wajib saat parse.

## Arsitektur

- **`components/fbd_core/`** — inti runtime FBD: `FBDValue` tagged union
  (`fbd_value.*`), node Level 0 (`fbd_nodes.*`), graph ber-id + topological
  sort (`fbd_graph.*`), parser/serializer JSON sesuai [schema.md](schema.md)
  (`fbd_json.*`).
- **`components/web_ui/`** — HTTP server (esp_http_server) yang:
  - menyajikan halaman editor visual drag-drop dari SPIFFS (`webroot/index.html`)
  - `POST /api/program` — terima JSON sesuai schema.md, validasi
    (parse + topological sort) ke **standby graph** (dual-buffer, tidak
    langsung ke graph aktif), backup + persist `program.json` ke SPIFFS
  - `GET /api/program` — dump graph aktif sesuai schema.md, termasuk
    `outputs` tiap node (live monitor polling)
  - `GET /api/status` — nilai output block v1/`logic_engine` (debug, akan
    dipensiunkan setelah migrasi penuh)
  - `GET`/`POST /api/network` — konfigurasi WiFi STA (tab System > Network)
- **`components/wifi_mgr/`** — WiFi APSTA: Access Point
  (SSID `ESP32-WebLogic`, password `logic1234`, IP `192.168.4.1`) DAN
  koneksi STA (SSID/password dari NVS, fallback ke `MIFON` kalau belum
  pernah dikonfigurasi via tab System > Network). Getter read-only untuk
  `SYS.*` system variable (`wifi_mgr_is_sta_connected`,
  `wifi_mgr_get_sta_rssi`, dst).
- **`components/logic_engine/`** — interpreter v1 (block+index array),
  dipertahankan sementara hanya untuk `/api/status`, akan dihapus setelah
  migrasi ke `fbd_graph` selesai penuh.

## Tutorial pemakaian

Panduan langkah demi langkah cara pakai web editor (flash, konek WiFi,
buat rangkaian, GPIO/analog/PWM/servo real, I2C, live update, dst) ada
di [docs/tutorials/00-daftar-isi.md](docs/tutorials/00-daftar-isi.md).

## Format program JSON

Lihat [schema.md](schema.md) untuk spesifikasi lengkap (semua tipe node
Level 0 dengan contoh `params`, aturan `links`, versioning). Contoh
singkat:

```json
{
  "version": 1,
  "nodes": [
    { "id": "n1", "type": "digital_input", "params": { "pin": 4, "mode": "pullup", "invert": false } },
    { "id": "n2", "type": "ton", "params": { "delay_ms": 2000 } },
    { "id": "n3", "type": "and", "params": {} },
    { "id": "n4", "type": "digital_output", "params": { "pin": 2, "invert": false } }
  ],
  "links": [
    { "from": { "node": "n1", "port": 0 }, "to": { "node": "n3", "port": 0 } },
    { "from": { "node": "n2", "port": 0 }, "to": { "node": "n3", "port": 1 } },
    { "from": { "node": "n3", "port": 0 }, "to": { "node": "n4", "port": 0 } }
  ]
}
```

## Build & Flash

```
idf.py set-target esp32s3
idf.py build
idf.py -p <PORT> flash monitor
```

Setelah boot, buka `http://192.168.4.1` (via AP `ESP32-WebLogic`) atau IP
STA device (dicetak di log serial saat boot, lewat jaringan rumah). Susun
node lewat palette di editor Drawflow, sambungkan port, klik **Save**
untuk kirim ke device, **Load** untuk ambil graph aktif dari device.

## Menjalankan test host (tanpa hardware)

`components/fbd_core/` (FBDValue, node Level 0, dan graph/topological
sort) bisa dites di PC langsung, tanpa ESP32 dan tanpa `idf.py`:

```powershell
test_host\build_and_run.ps1
```

Script ini pakai MSVC `cl.exe` (Developer Command Prompt Visual Studio),
karena `esp-clang` di toolchain ESP-IDF cross-compile ke target
Xtensa/RISC-V ESP dan tidak bisa menghasilkan binary native PC.

## Roadmap

- [x] Level 0 — Logic/Math/Timer/Data murni software (`fbd_core`)
- [x] Runtime graph dengan topological sort + dual-task FreeRTOS
- [x] Schema JSON di-freeze + endpoint save/load
- [x] Editor visual Drawflow
- [x] Level 1 — I/O fisik (digital/ADC/PWM/servo), dual backend simulated/real
- [x] Level 2 — I2C primitive register-level + WiFi sebagai system variable
- [x] Live update program tanpa reboot, dual-buffer graph swap
- [ ] Level 3 (SPI/I2S/CAN/dst) — **ditunda total**, bukan roadmap aktif

**Roadmap aktif (spec 01-07) selesai.** Semua item checklist anti-gagal
(plan.md §13) tercentang, termasuk verifikasi hardware nyata untuk tiap
tahap. Level 3 tetap ditunda sampai ada kebutuhan konkret yang mendorongnya.

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
- **Berikutnya:** I/O fisik Level 1 (ADC/PWM/servo, simulated dulu baru
  real) dan Level 2 (I2C primitive, WiFi sebagai system variable).

### Bug signifikan yang ditemukan & diperbaiki selama verifikasi hardware
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
    (parse + topological sort), terapkan ke graph aktif kalau sukses
  - `GET /api/program` — dump graph aktif sesuai schema.md
  - `GET /api/status` — nilai output block v1/`logic_engine` (debug, akan
    dipensiunkan setelah migrasi penuh)
- **`components/wifi_mgr/`** — WiFi APSTA: Access Point
  (SSID `ESP32-WebLogic`, password `logic1234`, IP `192.168.4.1`) DAN
  koneksi STA ke jaringan rumah, untuk kemudahan dev.
- **`components/logic_engine/`** — interpreter v1 (block+index array),
  dipertahankan sementara hanya untuk `/api/status`, akan dihapus setelah
  migrasi ke `fbd_graph` selesai penuh.

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
- [ ] Level 1 — I/O fisik (ADC/PWM/servo), simulated dulu baru real
- [ ] Level 2 — I2C primitive register-level + WiFi sebagai system variable
- [ ] Live update program tanpa reboot, dual-buffer graph swap
- [ ] Level 3 (SPI/I2S/CAN/dst) — **ditunda total**, bukan roadmap aktif

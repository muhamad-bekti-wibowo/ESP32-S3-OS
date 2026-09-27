# Changelog

Semua perubahan penting proyek ini dicatat di file ini.

## [Unreleased]

### Added
- `plan.md`: rencana lengkap v2 runtime FBD/virtual PLC — arsitektur,
  skema data `FBDValue`, roadmap peripheral 4 level, checklist anti-gagal.
- `specs/`: plan.md dipecah jadi 7 spec tahap eksekusi (00-overview s.d.
  07-live-update), tiap tahap punya scope, file yang disentuh, dan
  kriteria selesai eksplisit.
- `components/fbd_core/`: implementasi `FBDValue` (tagged union C, 16
  byte, tanpa alokasi heap) dan node Level 0 (Logic AND/OR/NOT/XOR/NAND/
  NOR, Compare generik, Constant, Variable get/set, Math ADD/SUB/MUL/DIV/
  MIN/MAX/ABS/SCALE/CLAMP, Timing TON/TOF/TP/Counter CTU). Sesuai spec 01.
- `test_host/`: test host C murni (27 assertion, semua lolos) yang
  memverifikasi `fbd_core` tanpa ESP32/idf.py, dijalankan via
  `build_and_run.ps1` (MSVC `cl.exe`).

### Added (v1, sebelum plan.md v2)
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

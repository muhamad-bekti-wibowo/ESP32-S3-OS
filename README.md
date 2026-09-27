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
  CLAMP), Timing (TON/TOF/TP/Counter). Lihat `components/fbd_core/` dan
  `test_host/` (27 test lolos, dijalankan tanpa ESP32/idf.py).
- **Berikutnya:** migrasi runtime ke graph ber-id dengan topological sort
  dan dual-task FreeRTOS (Core 0 web server, Core 1 scan cycle), lalu
  editor visual berbasis Drawflow, lalu I/O fisik Level 1 (ADC/PWM/servo)
  dan Level 2 (I2C primitive, WiFi sebagai system variable).

## Arsitektur (v1 — sedang bermigrasi ke runtime FBD)

- **`components/logic_engine/`** — interpreter runtime. Program disusun
  sebagai daftar *block* (const, input, output, compare, if_else, counter,
  math) yang saling terhubung lewat referensi index ("wire"). Dieksekusi
  tiap scan cycle (~50 Hz), mirip siklus scan PLC.
- **`components/web_ui/`** — HTTP server (esp_http_server) yang:
  - menyajikan halaman editor visual drag-drop dari SPIFFS (`webroot/index.html`)
  - `POST /api/program` — menerima definisi program dalam JSON, memuatnya ke logic_engine
  - `GET /api/status` — mengembalikan nilai output tiap block untuk debug/monitor
- **`components/wifi_mgr/`** — WiFi Access Point sederhana
  (SSID `ESP32-WebLogic`, password `logic1234`, IP `192.168.4.1`).

## Format program JSON

```json
{
  "blocks": [
    { "name": "b0", "type": "input",  "in": [], "cfg": { "gpio": 0, "analog": false } },
    { "name": "b1", "type": "const",  "in": [], "cfg": { "value": 1 } },
    { "name": "b2", "type": "compare","in": [0, 1], "cfg": { "op": "==" } },
    { "name": "b3", "type": "output", "in": [2], "cfg": { "gpio": 2 } }
  ]
}
```

`in` berisi index block lain (bukan nama) sebagai sumber tiap input port.
Urutan array `blocks` menentukan urutan evaluasi — block yang dipakai
sebagai input harus didefinisikan lebih dulu.

## Build & Flash

```
idf.py set-target esp32s3
idf.py build
idf.py -p <PORT> flash monitor
```

Setelah boot, konek ke WiFi AP `ESP32-WebLogic`, buka `http://192.168.4.1`,
susun block di editor, klik **Kirim ke ESP32**.

## Menjalankan test host (tanpa hardware)

`components/fbd_core/` bisa dites di PC langsung, tanpa ESP32 dan tanpa
`idf.py` — lihat [specs/01-fbdvalue-core.md](specs/01-fbdvalue-core.md).

```powershell
test_host\build_and_run.ps1
```

Script ini pakai MSVC `cl.exe` (Developer Command Prompt Visual Studio),
karena `esp-clang` di toolchain ESP-IDF cross-compile ke target
Xtensa/RISC-V ESP dan tidak bisa menghasilkan binary native PC.

## Roadmap

- [x] Level 0 — Logic/Math/Timer/Data murni software (`fbd_core`)
- [ ] Runtime graph dengan topological sort + dual-task FreeRTOS
- [ ] Schema JSON di-freeze + endpoint save/load
- [ ] Editor visual Drawflow
- [ ] Level 1 — I/O fisik (ADC/PWM/servo), simulated dulu baru real
- [ ] Level 2 — I2C primitive register-level + WiFi sebagai system variable
- [ ] Live update program tanpa reboot, dual-buffer graph swap
- [ ] Level 3 (SPI/I2S/CAN/dst) — **ditunda total**, bukan roadmap aktif

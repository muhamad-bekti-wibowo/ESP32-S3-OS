# ESP32 Web Logic (FBD/Virtual PLC Runtime)

Proyek ESP-IDF (target ESP32-S3) untuk memprogram logika sederhana
(if-else, counter, operasi matematika, compare, I/O) langsung dari web
browser, tanpa perlu flash ulang firmware setiap kali logika berubah.

Arah pengembangan: runtime Function Block Diagram (FBD) / virtual PLC
open-source, dieksekusi dari JSON tanpa kompilasi ulang firmware, dengan
editor visual berbasis Drawflow yang di-hosting langsung dari ESP32-S3.

**Dokumen rencana lengkap:** [plan.md](plan.md) — arsitektur, skema data,
roadmap peripheral 4 level, dan alasan tiap keputusan desain.
**Spec eksekusi per tahap:** [specs/](specs/00-overview.md) — plan.md
dipecah jadi tahap-tahap kecil yang bisa dikerjakan satu per satu, tiap
tahap punya kriteria selesai eksplisit sebelum lanjut ke tahap berikutnya.

## Status implementasi

| Spec | Fokus | Status |
|---|---|---|
| [01](specs/01-fbdvalue-core.md) | `FBDValue` tagged union + node Level 0, dites di host | Selesai — lihat `components/fbd_core/`, `test_host/` |
| [02](specs/02-topo-sort-runtime.md) | Graph ber-id + topological sort + dual-task Core0/Core1 | Belum dikerjakan |
| [03](specs/03-schema-freeze.md) | Freeze schema JSON + endpoint save/load | Belum dikerjakan |
| [04](specs/04-drawflow-editor.md) | Editor visual Drawflow | Belum dikerjakan |
| [05](specs/05-level1-io.md) | Level 1 I/O (analog/PWM/servo, simulated→real) | Belum dikerjakan |
| [06](specs/06-level2-i2c-wifi.md) | Level 2 (I2C primitive + WiFi system variable) | Belum dikerjakan |
| [07](specs/07-live-update.md) | Dual-buffer graph swap, live update tanpa reboot | Belum dikerjakan |

## Arsitektur (v1 — akan bermigrasi bertahap sesuai specs/)

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

Roadmap detail ada di [plan.md §5](plan.md) (4 level peripheral) dan
[plan.md §12](plan.md) (prioritas eksekusi). Ringkasan:

- [x] Level 0 — Logic/Math/Timer/Data murni software (`fbd_core`, spec 01)
- [ ] Runtime graph dengan topological sort + dual-task FreeRTOS (spec 02)
- [ ] Schema JSON di-freeze + endpoint save/load (spec 03)
- [ ] Editor visual Drawflow (spec 04)
- [ ] Level 1 — I/O fisik (ADC/PWM/servo), simulated dulu baru real (spec 05)
- [ ] Level 2 — I2C primitive register-level + WiFi sebagai system variable (spec 06)
- [ ] Live update program tanpa reboot, dual-buffer graph swap (spec 07)
- [ ] Level 3 (SPI/I2S/CAN/dst) — **ditunda total**, bukan roadmap aktif

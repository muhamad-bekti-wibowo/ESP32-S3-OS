# ESP32 Web Logic (FBD / Virtual PLC Runtime)

Runtime **Function Block Diagram (FBD) / virtual PLC** untuk ESP32-S3,
diprogram lewat editor visual drag-drop di browser — tanpa perlu
menulis/flash ulang firmware setiap kali logika berubah. Device sendiri
yang menghosting editornya (server web berjalan di ESP32), dan program
yang disusun langsung dieksekusi di firmware dalam siklus scan tetap
(~50Hz), mirip cara kerja PLC industri kecil.

Program disimpan sebagai JSON sederhana (lihat [schema.md](schema.md)),
bisa di-Save/Load lewat editor atau lewat API HTTP langsung — cocok untuk
otomatisasi kecil, kontrol I/O, monitoring sensor, sampai jadi jembatan
komunikasi antar perangkat (HTTP, I2C, Modbus) tanpa menyentuh kode C
sama sekali.

## Fitur utama

### Editor visual berbasis Drawflow

Disajikan langsung dari ESP32 (SPIFFS, offline — tidak butuh internet
atau CDN apa pun). Susun node lewat drag-drop dari palette, sambungkan
port dengan wire, klik **Save** untuk kirim program ke device atau
**Load** untuk ambil program yang sedang aktif. Live monitor
menampilkan nilai output tiap node secara real-time (polling, bukan
WebSocket) tanpa perlu masuk mode debug khusus.

### Logic, Data, Math, Timer

Blok dasar function block diagram: `AND`/`OR`/`NOT`/`XOR`/`NAND`/`NOR`,
`Compare`, `Constant`, `Variable Get/Set`, operasi matematika
(`ADD`/`SUB`/`MUL`/`DIV`/`MIN`/`MAX`/`ABS`/`SCALE`/`CLAMP`), timer
(`TON`/`TOF`/`TP`), `Counter` (CTUD dengan mode auto-reset opsional),
dan `Oscillator`. Semua murni software, tidak menyentuh hardware sama
sekali — cocok untuk logika kontrol apa pun.

### I/O fisik (GPIO, analog, PWM, servo, LED, sensor jarak)

`Digital Input/Output`, `Analog Input` (ADC), `PWM Output`, `Servo`,
`WS2812` (LED RGB via RMT), dan `Ultrasonic` (sensor jarak HC-SR04
dkk). Setiap node punya **dual backend**: mode `simulated` (nilai dari
UI, tidak menyentuh pin fisik — aman untuk uji logika tanpa hardware)
atau `real` (driver ESP-IDF asli). Pin GPIO yang berisiko (strapping
pin, USB-JTAG, SPI flash/PSRAM) divalidasi otomatis dan ditolak kalau
dipakai di mode `real`.

### I2C primitive (register-level)

`I2C Read Reg`/`I2C Write Reg`/`I2C Write Burst` — primitive generik
baca/tulis register I2C mentah (bukan driver sensor spesifik), jadi
bisa dipakai untuk device I2C apa pun (LCD backpack, sensor, expander,
dst) selama tahu alamat & registernya. Timeout selalu pendek, gagal
komunikasi tidak pernah menghentikan scan cycle node lain.

### HTTP Endpoint (bikin API sendiri dari graph)

Node `HTTP Endpoint` membuat route HTTP baru (GET) di server kedua
(port terpisah dari editor). Bisa dipakai untuk endpoint status statis
(serve file HTML/teks), atau — lewat 2 output (nilai query string) dan
1 input (nilai response) — dijadikan endpoint **dinamis** yang
menjalankan graph tiap request masuk (misalnya endpoint kalkulator:
`/add?a=3&b=4` membalas hasil dari node Math di graph).

### Modbus TCP client & Modbus RTU slave

Dua arah komunikasi Modbus:
- **`Modbus TCP Read`/`Modbus TCP Write`** — ESP32 jadi *client/master*,
  membaca/menulis register atau coil dari device Modbus TCP lain di
  jaringan (WiFi/LAN, alamat `IP:Port`). Non-blocking — request
  dijalankan di task terpisah supaya device remote yang lambat/putus
  tidak pernah menunda node lain di graph.
- **`Modbus Slave Reg`** — ESP32 jadi *server (slave)* Modbus RTU lewat
  UART2/RS485, diakses master eksternal (SCADA/PLC). Register bisa
  baca-tulis dari dua arah sekaligus: nilai dari graph dibaca master,
  dan nilai yang ditulis master masuk kembali ke graph.

### WiFi System Variable & konfigurasi via web

Node `Sys Var Get` membaca status WiFi (`SYS.WIFI_CONNECTED`,
`SYS.WIFI_RSSI`) sebagai variabel read-only di graph — berguna untuk
logika seperti indikator sinyal lemah. Semua konfigurasi non-logika
(WiFi STA, port HTTP Endpoint, Slave ID/Baud Modbus, upload firmware)
punya tab tersendiri (menu ☰ di toolbar editor), terpisah dari canvas.

### Live update tanpa reboot & persistence

Program yang di-Save langsung aktif tanpa reboot (arsitektur
dual-buffer graph swap — web handler dan scan task tidak pernah saling
menunggu). Program tersimpan otomatis ke SPIFFS dan dimuat ulang saat
boot, jadi tidak hilang setelah device restart/mati listrik. Setiap
Save juga membuat backup otomatis (retensi 5 backup terbaru).

### OTA (update firmware lewat web, tanpa kabel USB)

Tab System > Firmware menerima upload file `.bin` langsung dari
browser. Firmware baru ditulis ke partisi OTA yang sedang tidak aktif;
kalau firmware baru gagal boot, bootloader otomatis rollback ke
firmware sebelumnya — device tidak bisa "mati total" hanya karena
upload firmware yang salah.

## Arsitektur singkat

- **`components/fbd_core/`** — inti runtime: tipe nilai (`FBDValue`,
  tagged union tanpa alokasi heap), definisi semua node, graph
  ber-id + topological sort (Kahn's algorithm), parser/serializer JSON.
  Berjalan di task FreeRTOS terpisah (`fbd_scan_task`, pinned ke Core 1)
  dengan siklus scan tetap (~50Hz / 20ms).
- **`components/web_ui/`** — server HTTP (`esp_http_server`) yang
  menyajikan editor dari SPIFFS dan endpoint API (`/api/program`,
  `/api/network`, `/api/ota`, dst). Berjalan di Core 0, tidak pernah
  saling blocking dengan scan task (dual-buffer graph swap).
- **`components/wifi_mgr/`** — WiFi mode APSTA: Access Point
  (`ESP32-WebLogic-XXXX`) selalu aktif untuk akses langsung, sekaligus
  koneksi STA ke jaringan rumah/kantor (config tersimpan di NVS).
- **`components/endpoint_mgr/`**, **`components/modbus_slave_mgr/`** —
  konfigurasi NVS untuk fitur HTTP Endpoint dan Modbus RTU Slave.

Detail arsitektur, alasan desain tiap keputusan teknis (dual-buffer
swap, kenapa I2C tidak punya mode simulated, dsb), dan riwayat
perbaikan bug ada di commit history — README ini fokus ke fitur dan
cara pakai, bukan catatan pengembangan.

## Format program JSON

Lihat [schema.md](schema.md) untuk spesifikasi lengkap tiap tipe node
(params, port, contoh). Contoh singkat:

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

Setelah boot, sambungkan ke AP perangkat lalu buka `http://192.168.4.1`.
SSID AP **unik per perangkat**: `ESP32-WebLogic-XXXX` (XXXX = 2 byte
terakhir MAC), jadi beberapa perangkat di ruangan yang sama mudah
dibedakan. Password AP: `logic1234`. Alternatifnya, pakai IP STA device
(dicetak di log serial saat boot) kalau sudah dikonfigurasi ke WiFi
rumah/kantor lewat tab System > Network. Susun node lewat palette di
editor, sambungkan port, klik **Save** untuk kirim ke device.

## Tutorial pemakaian

Panduan langkah demi langkah (flash, konek WiFi, buat rangkaian,
GPIO/analog/PWM/servo real, I2C, live update, dst) ada di
[docs/tutorials/00-daftar-isi.md](docs/tutorials/00-daftar-isi.md).

## Menjalankan test host (tanpa hardware)

Sebagian besar logika inti (`fbd_core/`: tipe nilai, node, graph +
topological sort, parser JSON, bridge komunikasi) punya test yang bisa
dijalankan langsung di PC, tanpa ESP32 dan tanpa `idf.py`:

```powershell
test_host\build_and_run.ps1
```

Script ini pakai MSVC `cl.exe` (Developer Command Prompt Visual
Studio), karena `esp-clang` di toolchain ESP-IDF cross-compile ke
target Xtensa/RISC-V ESP dan tidak bisa menghasilkan binary native PC.

## Roadmap

- [x] Logic/Math/Timer/Data murni software
- [x] Runtime graph dengan topological sort + dual-task FreeRTOS
- [x] Editor visual Drawflow
- [x] I/O fisik (digital/ADC/PWM/servo/LED/ultrasonic), dual backend simulated/real
- [x] I2C primitive register-level
- [x] WiFi sebagai system variable + konfigurasi via web
- [x] Live update program tanpa reboot (dual-buffer graph swap) + persistence
- [x] OTA firmware update lewat web
- [x] HTTP Endpoint kustom (statis & dinamis, bidirectional)
- [x] Modbus TCP client + Modbus RTU slave
- [ ] Level 3 (SPI/I2S/CAN/dst) — belum ada rencana konkret

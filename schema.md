# Schema JSON — FBD Runtime (Level 0-1)

Status: **FINAL/LOCKED** untuk Level 0-1. Sesuai [plan.md §7.1](plan.md)
dan [specs/03-schema-freeze.md](specs/03-schema-freeze.md). Perubahan yang
menambah field baru untuk Level 1 (ADC/PWM/servo) atau Level 2 (I2C/WiFi)
boleh dilakukan di spec berikutnya — field itu ditandai "reserved" di sini.
Perubahan yang mengubah struktur `nodes[]`/`links[]` yang sudah ada TIDAK
boleh dilakukan tanpa menaikkan `version`.

## Struktur root

```json
{
  "version": 1,
  "nodes": [ ... ],
  "links": [ ... ]
}
```

- `version` — integer, wajib. `1` untuk Level 0-1. Parser HARUS menolak
  document dengan `version` yang tidak dikenal (jangan asumsikan
  forward-compatible).
- `nodes` — array objek node (lihat di bawah).
- `links` — array objek link (lihat di bawah).

## Node

```json
{ "id": "n1", "type": "const", "params": { ... }, "state": { ... } }
```

- `id` — string, wajib, unik dalam satu document, maksimal 15 karakter
  (`FBD_MAX_ID_LEN - 1`, lihat `fbd_graph.h`).
- `type` — string, wajib, salah satu dari daftar di bawah.
- `params` — object, konfigurasi **statis** yang diisi user (tidak
  berubah tiap scan cycle kecuali user mengedit ulang). Field yang
  relevan tergantung `type`.
- `state` — object, opsional, data runtime yang berubah tiap scan cycle
  (misal `timer_start_ms` pada TON). **Boleh diabaikan saat parse** —
  runtime yang membuat state awal sendiri (nol/default). Kalau device
  sedang reload program yang sama untuk sinkronisasi UI, `state` boleh
  dikirim balik dari `GET /api/program` agar user bisa lihat, tapi
  endpoint `POST /api/program` TIDAK wajib memakainya untuk restore
  timer/counter di tengah jalan — itu di luar scope Level 0-1.

**Aturan wajib (dari plan.md, jangan dilanggar):** `params` (konfigurasi
statis) dan `state` (runtime) HARUS selalu terpisah sebagai dua object,
tidak pernah dicampur jadi satu object flat.

## Link

```json
{ "from": { "node": "n1", "port": 0 }, "to": { "node": "n3", "port": 0 } }
```

- `from.node` / `to.node` — string, wajib, harus merujuk `id` node yang
  ada di `nodes[]` pada document yang sama.
- `from.port` — integer, wajib, index output port node sumber
  (`0` sampai `FBD_MAX_NODE_OUTPUTS - 1`, saat ini maksimal 1).
- `to.port` — integer, wajib, index input port node tujuan
  (`0` sampai `FBD_MAX_NODE_INPUTS - 1`, saat ini maksimal 3).

**Keputusan desain:** dipilih object eksplisit `{"node": ..., "port": ...}`
bukan notasi string `"n1.out"`, karena lebih murah di-parse dengan cJSON
di C (tidak perlu string split manual) dan tidak ambigu kalau nama node
mengandung titik.

Parser HARUS menolak seluruh document (bukan partial-load) kalau ada
link yang merujuk `node` yang tidak ada di `nodes[]`, atau `port` di luar
rentang valid.

## Daftar `type` node Level 0 (lengkap)

Semua field `params` di bawah wajib diisi kecuali disebutkan default.

### Logic

| `type` | `params` | Keterangan |
|---|---|---|
| `and` | `{}` | 2 input (`in0`, `in1`), 1 output bool |
| `or` | `{}` | sama seperti `and` |
| `not` | `{}` | 1 input, 1 output bool |
| `xor` | `{}` | 2 input |
| `nand` | `{}` | 2 input |
| `nor` | `{}` | 2 input |
| `compare` | `{ "op": "gt" }` | `op` salah satu: `gt`, `lt`, `eq`, `neq`, `gte`, `lte` |

Contoh:
```json
{ "id": "cmp1", "type": "compare", "params": { "op": "gt" } }
```

### Data

| `type` | `params` | Keterangan |
|---|---|---|
| `const` | `{ "datatype": "float", "value": 25.5 }` | `datatype`: `bool`\|`int32`\|`float` |
| `var_get` | `{ "name": "counter_a" }` | baca dari storage key-value global |
| `var_set` | `{ "name": "counter_a" }` | tulis input ke storage, pass-through output |

Contoh:
```json
{ "id": "c1", "type": "const", "params": { "datatype": "float", "value": 25.5 } }
{ "id": "v1", "type": "var_get", "params": { "name": "counter_a" } }
```

### Math

| `type` | `params` | Keterangan |
|---|---|---|
| `math` | `{ "op": "add" }` | `op`: `add`\|`sub`\|`mul`\|`div` |
| `min` | `{}` | 2 input |
| `max` | `{}` | 2 input |
| `abs` | `{}` | 1 input |
| `scale` | `{ "in_min": 0, "in_max": 4095, "out_min": 0, "out_max": 100 }` | linear map |
| `clamp` | `{ "min": 0, "max": 100 }` | batas rentang |

Contoh:
```json
{ "id": "sc1", "type": "scale", "params": { "in_min": 0, "in_max": 100, "out_min": 0, "out_max": 1 } }
```

### Timing

| `type` | `params` | `state` (opsional, lihat aturan di atas) |
|---|---|---|
| `ton` | `{ "delay_ms": 2000 }` | `{ "start_ms": 0, "running": false, "prev_input": false }` |
| `tof` | `{ "delay_ms": 2000 }` | sama seperti `ton` |
| `tp` | `{ "pulse_ms": 500 }` | sama seperti `ton` |
| `osc` | `{ "on_ms": 500, "off_ms": 500 }` | sama seperti `ton` (`running` dipakai sebagai fase ON/OFF sekarang, bukan "sedang menghitung") |
| `ctu` | `{ "preset": 3, "auto_reset": false }` | `{ "count": 0, "prev_up": false, "prev_down": false }` |

Contoh:
```json
{ "id": "t1", "type": "ton", "params": { "delay_ms": 2000 } }
```

`ctu` (Counter Up/Down) punya **4 input port** dan **2 output port** —
beda dari node timer lain di atas. Perilaku port `reset` tergantung
`params.auto_reset`:

| Port input | Arti |
|---|---|
| `in0` (up) | Hitungan +1 tiap transisi false→true (edge naik) |
| `in1` (down) | Hitungan -1 tiap transisi false→true (edge naik), independen dari `up` |
| `in2` (reset) | **Diabaikan sepenuhnya kalau `auto_reset: true`.** Kalau `auto_reset: false` — selama true, hitungan dipaksa ke nilai `in3` |
| `in3` (reset_value) | Nilai tujuan reset — **bukan selalu 0**, dipakai di KEDUA mode, dibaca live tiap scan cycle |

| Port output | Arti |
|---|---|
| `out0` | `bool`, `true` kalau hitungan `>= params.preset` |
| `out1` | `int32`, hitungan itu sendiri (nilai mentah, bukan hasil threshold) |

`up` dan `down` independen — kalau kebetulan tepi naik keduanya terjadi di
scan cycle yang sama, keduanya tetap diproses (+1 dan -1), saling
meniadakan, bukan salah satu diabaikan.

**`auto_reset: false` (default, mode manual)** — port `reset` adalah
saklar yang harus di-trigger eksplisit (mis. dari tombol/timer). Kalau
port `reset` dibiarkan kosong (selalu false), hitungan naik/turun tak
terbatas dari `up`/`down`, `reset_value` tidak pernah dipakai.

Contoh — counter naik/turun tombol fisik, reset manual ke 10:
```json
{ "id": "btn_up", "type": "digital_input", "params": { "pin": 4, "mode": "pullup", "invert": true, "hw_mode": "real" } }
{ "id": "btn_down", "type": "digital_input", "params": { "pin": 5, "mode": "pullup", "invert": true, "hw_mode": "real" } }
{ "id": "btn_reset", "type": "digital_input", "params": { "pin": 6, "mode": "pullup", "invert": true, "hw_mode": "real" } }
{ "id": "reset_to", "type": "const", "params": { "datatype": "int32", "value": 10 } }
{ "id": "counter1", "type": "ctu", "params": { "preset": 20, "auto_reset": false } }
```
(link: `btn_up→counter1.in0`, `btn_down→counter1.in1`, `btn_reset→counter1.in2`, `reset_to→counter1.in3`)

**`auto_reset: true` (mode loop/self-resetting)** — port `reset`
diabaikan sepenuhnya (boleh dibiarkan tanpa sambungan). Begitu hitungan
`>= params.preset`, **otomatis** dikembalikan ke nilai `in3`
(reset_value) di scan cycle yang sama — tidak pernah "terlihat"
melebihi preset walau cuma sesaat. Cocok untuk pola berulang (mis.
animasi LED bertahap) tanpa perlu node tombol/timer tambahan untuk
reset.

Contoh — siklus 0→4 berulang terus (mis. untuk indeks animasi):
```json
{ "id": "clk1", "type": "osc", "params": { "on_ms": 200, "off_ms": 200 } }
{ "id": "zero1", "type": "const", "params": { "datatype": "int32", "value": 0 } }
{ "id": "idx1", "type": "ctu", "params": { "preset": 5, "auto_reset": true } }
```
(link: `clk1→idx1.in0`, `zero1→idx1.in3`) — `idx1.out1` menghasilkan
0,1,2,3,4,0,1,2,3,4,... terus-menerus tiap `clk1` berkedip, port `in2`
(reset) tidak perlu disambung sama sekali karena diabaikan di mode ini.

`osc` (osilator/clock generator) **tidak punya input** — output bergantian
`true`/`false` terus-menerus tanpa dipicu apa pun, dimulai dari fase
`false` (OFF) selama `off_ms`, lalu `true` (ON) selama `on_ms`, berulang.
`on_ms`/`off_ms` independen: isi keduanya sama untuk blink simetris
(paling umum, mis. LED berkedip rata), atau beda untuk pola asimetris
(mis. `on_ms: 200, off_ms: 800` untuk kedip cepat).

Contoh — LED berkedip tiap 1 detik (simetris):
```json
{ "id": "blink1", "type": "osc", "params": { "on_ms": 500, "off_ms": 500 } }
{ "id": "led1", "type": "digital_output", "params": { "pin": 5, "invert": false, "hw_mode": "real" } }
```
(link: `blink1 → led1.in0`)

### I/O digital (Level 1)

Field `params.mode` (`"simulated"` atau `"real"`) menentukan dual backend
(plan.md §3): `"simulated"` (default) tidak menyentuh GPIO fisik sama
sekali — `digital_input` pass-through `inputs[0]`/`digital_output`
pass-through nilai tanpa efek fisik. `"real"` memanggil driver GPIO
ESP-IDF (`gpio_config`/`gpio_get_level`/`gpio_set_level`) sungguhan.

| `type` | `params` | Keterangan |
|---|---|---|
| `digital_input` | `{ "pin": 4, "mode": "pullup", "invert": false, "hw_mode": "simulated" }` | `mode`: `pullup`\|`pulldown`\|`floating` (konfigurasi pull resistor, hanya dipakai saat `hw_mode: "real"`) |
| `digital_output` | `{ "pin": 5, "invert": false, "hw_mode": "simulated" }` | |

Contoh:
```json
{ "id": "n1", "type": "digital_input", "params": { "pin": 4, "mode": "pullup", "invert": false, "hw_mode": "simulated" } }
```

#### Validasi pin GPIO (WAJIB, hanya berlaku saat `hw_mode: "real"`)

Parser (`fbd_json.c`) menolak `POST /api/program` yang memakai pin di
bawah ini dengan `hw_mode: "real"` untuk `digital_input`/`digital_output`/
`analog_input`/`pwm_output`/`servo`. Saat `hw_mode: "simulated"`, angka
pin murni informasional — tidak divalidasi karena tidak pernah menyentuh
GPIO fisik.

| Pin | Alasan dilarang |
|---|---|
| GPIO0, 3, 45, 46 | Strapping pin (menentukan boot mode) — mengubahnya bisa membuat device gagal boot atau masuk mode download tak sengaja |
| GPIO19, 20 | Default dipakai USB-JTAG bawaan — dipakai GPIO lain akan mematikan debugging via USB |
| GPIO26–37 | Dipakai SPI flash/PSRAM (device proyek ini pakai PSRAM Octal 8MB) — dipakai GPIO lain berisiko corrupt flash/PSRAM |

Pin yang aman dipakai (contoh, tidak lengkap): **GPIO4, 5, 6, 7, 8, 9, 10,
11, 12, 13, 14, 15, 16, 17, 18, 21, 38–48**. Selalu cek breakout board
fisik — tidak semua nomor GPIO di atas ke-broke-out di setiap board.

### I/O analog & PWM (Level 1)

Field `params.hw_mode` sama seperti I/O digital — `"simulated"` (default)
atau `"real"`.

| `type` | `params` | Keterangan |
|---|---|---|
| `analog_input` | `{ "pin": 4, "resolution": 12, "attenuation": 11, "hw_mode": "simulated", "sim_value": 2048 }` | `resolution`: bit ADC (9-12). `attenuation`: dB (0/2/6/11, menentukan rentang tegangan terukur — 11dB = ~0-3.3V). `sim_value`: nilai dipakai saat `hw_mode: "simulated"`, di-set lewat slider UI atau langsung di JSON |
| `pwm_output` | `{ "pin": 5, "frequency": 1000, "resolution": 12, "hw_mode": "simulated" }` | Input node (`inputs[0]`) adalah duty cycle dalam persen (0-100), bukan raw register. `resolution`: bit duty cycle LEDC |
| `servo` | `{ "pin": 18, "min_us": 500, "max_us": 2500, "hw_mode": "simulated" }` | Input node adalah sudut 0-180 derajat (di-clamp otomatis kalau di luar rentang). `min_us`/`max_us`: pulse width di sudut 0°/180° |

Contoh:
```json
{ "id": "ai1", "type": "analog_input", "params": { "pin": 4, "resolution": 12, "attenuation": 11, "hw_mode": "simulated", "sim_value": 2048 } }
{ "id": "pwm1", "type": "pwm_output", "params": { "pin": 5, "frequency": 1000, "resolution": 12, "hw_mode": "simulated" } }
{ "id": "servo1", "type": "servo", "params": { "pin": 18, "min_us": 500, "max_us": 2500, "hw_mode": "simulated" } }
```

**Kriteria konsistensi mode:** rangkaian yang jalan benar di `hw_mode:
"simulated"` (tanpa hardware sama sekali) harus tetap jalan dengan arah/
skala logic yang sama setelah `hw_mode` diganti `"real"` — nilai presisi
boleh beda (ADC nyata punya noise, potensiometer fisik bukan angka bulat),
tapi arah naik/turun dan threshold logic harus konsisten.

### LED RGB addressable (Level 1, WS2812/NeoPixel)

Protokol 1-wire (bukan I2C, bukan PWM biasa) — dikendalikan lewat ESP-IDF
`driver/rmt_tx` dengan bytes encoder timing WS2812 standar (800kHz: bit1 =
0.8µs HIGH + 0.45µs LOW, bit0 = 0.4µs HIGH + 0.85µs LOW). Urutan byte fisik
yang dikirim ke LED adalah GRB (bukan RGB) sesuai kebanyakan chip WS2812 —
konversi ini dilakukan di firmware, params/input tetap R/G/B intuitif.

| `type` | `params` | Keterangan |
|---|---|---|
| `ws2812` | `{ "pin": 8, "count": 30, "hw_mode": "simulated" }` | 3 input: R, G, B (0-255, di-clamp otomatis). `count`: jumlah LED di strip (1-256) — **semua LED diset warna yang sama** (bukan per-LED individual, di luar scope Level 1). |

Contoh — strip 30 LED warna ungu tetap:
```json
{ "id": "r1", "type": "const", "params": { "datatype": "int32", "value": 128 } }
{ "id": "g1", "type": "const", "params": { "datatype": "int32", "value": 0 } }
{ "id": "b1", "type": "const", "params": { "datatype": "int32", "value": 255 } }
{ "id": "led1", "type": "ws2812", "params": { "pin": 8, "count": 30, "hw_mode": "real" } }
```
(link: `r1→led1.in0`, `g1→led1.in1`, `b1→led1.in2`)

Sama seperti `analog_input`/`pwm_output`/`servo`, kriteria konsistensi
mode berlaku: desain dulu di `hw_mode: "simulated"`, baru pindah ke
`"real"` setelah yakin logikanya benar.

### Sensor jarak ultrasonik (Level 1, HC-SR04 dkk, trig+echo)

Pengukuran **blocking** (bukan async seperti WS2812/RMT) — satu-satunya
cara mengukur jarak dari sensor ini adalah mengukur durasi pulsa echo
secara langsung. Timeout dibatasi **10ms** (jangkauan efektif ~1.7m,
BUKAN jangkauan penuh spec sensor ~4m/23ms) — trade-off disengaja supaya
node ini sendirian tidak pernah menahan satu scan cycle (20ms) melebihi
periodenya sendiri, konsisten dengan aturan wajib `I2C_BRIDGE_TIMEOUT_MS`
untuk I2C.

| `type` | `params` | Keterangan |
|---|---|---|
| `ultrasonic` | `{ "pin": 4, "echo_pin": 5, "hw_mode": "simulated", "sim_distance_cm": 50 }` | `pin`: pin Trig. `echo_pin`: pin Echo (WAJIB berbeda dari `pin`, ditolak parser kalau sama). Tidak punya input. Output: `outputs[0]`=jarak (`float`, cm), `outputs[1]`=error (`bool`, `true` kalau timeout/sensor tidak terpasang — dipakai membedakan "0cm valid" dari "gagal ukur", karena 0cm secara fisik memang mungkin terjadi kalau objek nempel di sensor). `sim_distance_cm`: nilai dipakai saat `hw_mode: "simulated"`, di-set lewat slider UI |

Contoh — LED nyala kalau ada objek dalam jarak 20cm:
```json
{ "id": "us1", "type": "ultrasonic", "params": { "pin": 4, "echo_pin": 5, "hw_mode": "real" } }
{ "id": "th1", "type": "const", "params": { "datatype": "float", "value": 20 } }
{ "id": "cmp1", "type": "compare", "params": { "op": "lt" } }
{ "id": "led1", "type": "digital_output", "params": { "pin": 6, "invert": false, "hw_mode": "real" } }
```
(link: `us1→cmp1.in0`, `th1→cmp1.in1`, `cmp1→led1.in0`)

Sama seperti node fisik lain, kriteria konsistensi mode berlaku: desain
dulu di `hw_mode: "simulated"` (slider Sim Distance), baru pindah ke
`"real"` setelah yakin logikanya benar.

### I2C primitive register-level (Level 2)

**HANYA primitive generik, BUKAN driver sensor spesifik** (plan.md prinsip
#2). Sensor riil (LCD1602 backpack, INA219, dst) dikomposisikan dari
`i2c_read_reg`/`i2c_write_reg` di level graph JSON — TIDAK ADA kode C baru
per sensor.

Tidak ada `params.hw_mode` untuk I2C (beda dari analog/PWM/servo) — I2C
selalu berarti bus fisik sungguhan, tidak ada "simulasi" yang masuk akal
untuk device I2C dengan alamat nyata. Timeout wajib pendek (8ms, lihat
`I2C_BRIDGE_TIMEOUT_MS`) — NACK/timeout tidak pernah menahan scan cycle
lebih lama dari itu.

| `type` | `params` | Keterangan |
|---|---|---|
| `i2c_read_reg` | `{ "bus": 0, "address": 39, "register": 1, "length": 2 }` | `address`: 7-bit (contoh: `39` = `0x27`, alamat khas LCD1602 backpack PCF8574). `length`: 1-8. Output: `outputs[0]`=raw bytes (`FBD_BYTES`), `outputs[1]`=error (`bool`, `true` kalau NACK/timeout) |
| `i2c_write_reg` | `{ "bus": 0, "address": 39, "register": 0, "data": [16, 32] }` | `data`: array 1-7 byte. Output: `outputs[0]`=sukses (`bool`) |

Contoh:
```json
{ "id": "i2c1", "type": "i2c_write_reg", "params": { "bus": 0, "address": 39, "register": 0, "data": [16, 32] } }
{ "id": "i2c2", "type": "i2c_read_reg", "params": { "bus": 0, "address": 39, "register": 1, "length": 2 } }
```

### I2C burst — banyak command dalam satu scan cycle

`i2c_write_burst` mengirim **beberapa** command register write berurutan
dalam SATU scan cycle — dipakai device yang butuh command sequence,
misalnya LCD1602 lewat backpack PCF8574 (mode 4-bit: kirim nibble
tinggi, nibble rendah, toggle bit E, delay tertentu, berulang per
karakter). Tetap primitive generik (BUKAN driver LCD) — siapa pun bisa
susun sequence apa pun lewat `commands[]`, cocok untuk periferal I2C
lain juga.

| `type` | `params` | Keterangan |
|---|---|---|
| `i2c_write_burst` | `{ "bus": 0, "address": 39, "delay_us": 50, "commands": [{ "register": 0, "data": [56] }, { "register": 0, "data": [12] }] }` | `commands`: array 1-8 objek `{register, data}`. Tiap `data`: array 1-4 byte. `delay_us`: jeda antar command (0 = tanpa delay). Output: `outputs[0]`=sukses (`bool`) — `false` kalau command manapun gagal (berhenti di command pertama yang NACK/timeout, sisa command TIDAK dicoba) |

Batas: maksimal **8 command** per node, tiap command maksimal **4 byte**
data. Kalau butuh command lebih banyak, sambung beberapa node
`i2c_write_burst` berurutan di canvas (link output node pertama ke
input tidak diperlukan — cukup tempatkan berurutan, keduanya tetap
dieksekusi tiap scan cycle sesuai topological order).

Contoh — init dasar LCD1602 PCF8574 (alamat `0x27` = `39`), sequence
disederhanakan (nilai command sebenarnya tergantung wiring backpack,
selalu cek datasheet/pinout PCF8574↔LCD board kamu):
```json
{
  "id": "lcd_init", "type": "i2c_write_burst",
  "params": {
    "bus": 0, "address": 39, "delay_us": 50,
    "commands": [
      { "register": 0, "data": [56] },
      { "register": 0, "data": [12] },
      { "register": 0, "data": [1] },
      { "register": 0, "data": [6] }
    ]
  }
}
```

**Live monitor:** `outputs[0]`/`outputs[1]` tiap node (termasuk `raw_bytes`/
`error` dari `i2c_read_reg`) muncul di field `"outputs"` pada response
`GET /api/program` — polling minimal, bukan field yang dikirim balik lewat
`POST` (field `outputs` diabaikan saat parse, murni untuk observasi).

### System variable read-only (Level 2)

**BUKAN node yang dikonfigurasi di canvas dengan SSID/password** — WiFi
tetap dikonfigurasi lewat tab "System > Network" terpisah di web UI
(disimpan ke NVS). FBD hanya membaca statusnya sebagai variable read-only,
mirip `%SM` di PLC Siemens/Omron.

| `type` | `params` | Keterangan |
|---|---|---|
| `sys_var_get` | `{ "name": "SYS.WIFI_RSSI" }` | `name` salah satu: `SYS.WIFI_CONNECTED` (bool), `SYS.WIFI_RSSI` (int32, dBm) |

Contoh — warning LED saat sinyal WiFi lemah:
```json
{ "id": "rssi1", "type": "sys_var_get", "params": { "name": "SYS.WIFI_RSSI" } }
{ "id": "th1", "type": "const", "params": { "datatype": "float", "value": -80 } }
{ "id": "cmp1", "type": "compare", "params": { "op": "lt" } }
{ "id": "led1", "type": "digital_output", "params": { "pin": 5, "invert": false, "hw_mode": "real" } }
```
(link: `rssi1→cmp1.in0`, `th1→cmp1.in1`, `cmp1→led1.in0`)

`SYS.IP_ADDRESS`/`SYS.HOSTNAME` **belum didukung** lewat `sys_var_get` —
representasi string tidak muat di `fbd_value_t` (maks 8 byte). Kalau
dibutuhkan, akan diekspos lewat endpoint HTTP terpisah, bukan dipaksa
lewat FBDValue.

### HTTP Endpoint kustom (Level 2)

Bikin route HTTP baru (GET) yang membalas file HTML/teks statis dari
SPIFFS, ATAU nilai dinamis dari graph lain — berjalan di **server httpd
KEDUA**, port terpisah dari server editor (port 80). Port diatur lewat
tab "System > HTTP Endpoints" (disimpan ke NVS). Route (path) sendiri
**BUKAN dieksekusi tiap scan cycle** — murni definisi statis, dibaca
sekali saat boot untuk mendaftarkan route. Tapi node ini **PUNYA 1 input
dan 2 output** yang tetap dievaluasi tiap scan cycle seperti node biasa,
lewat sebuah "bridge" state terpisah (`http_endpoint_bridge`, key by
path string, bukan pointer node — supaya tetap valid walau graph
di-swap/di-save ulang).

| `type` | `params` | Keterangan |
|---|---|---|
| `http_endpoint` | `{ "path": "/add", "query_a_name": "a", "query_b_name": "b", "file": "status.html", "content_type": "text/html" }` | `path`: route, wajib mulai `/`. `query_a_name`/`query_b_name`: nama query string HTTP yang dipetakan ke output 1/2 (kosong = output selalu 0). `file`: nama file fallback (bukan path lengkap) di `/spiffs/endpoints/`, dipakai HANYA kalau input node ini TIDAK tersambung. `content_type`: `text/html` atau `text/plain` — berlaku untuk file statis MAUPUN response dinamis dari input. |

**Port bidirectional (1 input, 2 output):**
- **Output 1/2** = nilai query string dari request HTTP terakhir yang
  masuk ke route ini (mis. akses `/add?a=3&b=4` dengan
  `query_a_name="a"`, `query_b_name="b"` → output1=3, output2=4).
  Bisa disambung ke node lain seperti Math, Scale, dst.
- **Input** = kalau disambung ke node lain (mis. hasil Math ADD), nilai
  itu jadi **response HTTP dinamis** endpoint ini (dikirim sebagai teks,
  bukan lagi file statis). Kalau input TIDAK disambung, endpoint balik
  ke perilaku lama: serve file statis dari `file`.
- Request HTTP menunggu maks ~80ms (4× scan cycle) untuk memastikan
  response yang dikirim adalah hasil PALING BARU: butuh 2 scan cycle
  penuh supaya nilai dari node hilir (mis. Math) sempat terdorong balik
  ke input endpoint sebelum dibaca (1 cycle endpoint proses query, 1
  cycle lagi node hilir proses & push balik).
- Kombinasi umum — "endpoint jadi kalkulator": sambungkan output 1/2 ke
  Math, lalu sambungkan balik output Math ke input node
  `http_endpoint` yang sama (atau node `http_endpoint` kedua dengan path
  berbeda). Lihat contoh di bawah.

**Upload file** (dari web UI, tombol "Upload" di panel Properties node):
```
POST /api/endpoint_file?name=status.html
Content-Type: application/octet-stream
<isi file HTML/teks>
```
Nama file tidak boleh mengandung `/` atau `..` (dicegah path traversal),
maksimal 64KB per file.

**PENTING — wajib reboot:** `esp_http_server` ESP-IDF tidak mendukung
pendaftaran route secara dinamis. Menambah/mengubah node `http_endpoint`
lalu Save **tidak langsung aktif** seperti node lain — server kedua
membaca ulang daftar route HANYA saat boot, dari `program.json` yang
tersimpan. Device harus di-reboot manual setelah Save supaya perubahan
route berlaku.

Contoh — endpoint status statis:
```json
{ "id": "ep1", "type": "http_endpoint", "params": { "path": "/status", "file": "status.html", "content_type": "text/html" } }
```
Setelah reboot, `http://<ip-device>:<port>/status` akan membalas isi
file `status.html` yang sudah di-upload.

Contoh — endpoint kalkulator dinamis (`/add?a=3&b=4` → balas `"7"`):
```json
{
  "nodes": [
    { "id": "ep1", "type": "http_endpoint",
      "params": { "path": "/add", "query_a_name": "a", "query_b_name": "b" } },
    { "id": "sum1", "type": "math", "params": { "op": "add" } }
  ],
  "links": [
    { "from": { "node": "ep1", "port": 0 }, "to": { "node": "sum1", "port": 0 } },
    { "from": { "node": "ep1", "port": 1 }, "to": { "node": "sum1", "port": 1 } },
    { "from": { "node": "sum1", "port": 0 }, "to": { "node": "ep1", "port": 0 } }
  ]
}
```
Setelah reboot, akses `http://<ip-device>:<port>/add?a=3&b=4` akan
membalas `7` (dihitung live tiap request, lewat scan cycle terkini).

### Modbus TCP client (Level 2, ESP32 jadi master via WiFi/LAN)

Baca/tulis register/coil dari device Modbus TCP **lain** di jaringan
(alamat `IP:Port`, biasanya port 502) — ESP32 di sini jadi **client/
master**. Hand-rolled langsung di atas BSD socket (lwip), TIDAK memakai
vendor library `esp-modbus` — konsisten dengan filosofi primitive kecil
project ini (I2C/ultrasonic juga hand-rolled).

**Async, TIDAK blocking scan cycle** — koneksi/response time device
remote lewat WiFi/LAN tidak bisa dijamin cepat seperti I2C lokal
(selalu <8ms). Request dikirim ke task background terpisah
(`modbus_tcp_task`, jalan selamanya sejak boot, idle kalau tidak ada
request), `evaluate_node()` tiap scan cycle cuma **membaca hasil
TERAKHIR** yang sudah ada — nilainya "hasil polling terakhir", bisa
beberapa scan cycle basi tergantung kecepatan respons device, BUKAN
realtime per-cycle. Trade-off ini disengaja demi keamanan scan cycle:
satu device Modbus lambat/network putus tidak akan menunda node lain
di graph.

| `type` | `params` | Keterangan |
|---|---|---|
| `modbus_tcp_read` | `{ "ip": "192.168.1.50", "port": 502, "unit_id": 1, "reg_type": "holding", "address": 0, "count": 1 }` | `reg_type`: `holding`/`input`/`coil`/`discrete`. `count`: 1-4 (batas `fbd_value_t` 8 byte = 4x uint16 — butuh lebih, sambung node lain dengan address awal beda). Output 1 = nilai (count=1, integer) atau raw bytes count x uint16 little-endian (count>1). Output 2 = error (true kalau request terakhir gagal/timeout ATAU belum pernah ada response). Tidak punya input. |
| `modbus_tcp_write` | `{ "ip": "192.168.1.50", "port": 502, "unit_id": 1, "reg_type": "holding", "address": 0 }` | `reg_type`: `holding`/`coil` saja (input register/discrete input read-only di device). Input = nilai yang ditulis (coil: boolean, holding: integer 16-bit). Output = sukses/gagal REQUEST TERAKHIR (async, bukan konfirmasi instan tulisan kali ini). |

Contoh — baca holding register 100 dari PLC lain, nyalakan LED kalau > 50:
```json
{
  "nodes": [
    { "id": "mb1", "type": "modbus_tcp_read",
      "params": { "ip": "192.168.1.50", "port": 502, "unit_id": 1, "reg_type": "holding", "address": 100, "count": 1 } },
    { "id": "th1", "type": "const", "params": { "datatype": "int32", "value": 50 } },
    { "id": "cmp1", "type": "compare", "params": { "op": "gt" } },
    { "id": "out1", "type": "digital_output", "params": { "pin": 2, "hw_mode": "real" } }
  ],
  "links": [
    { "from": { "node": "mb1", "port": 0 }, "to": { "node": "cmp1", "port": 0 } },
    { "from": { "node": "th1", "port": 0 }, "to": { "node": "cmp1", "port": 1 } },
    { "from": { "node": "cmp1", "port": 0 }, "to": { "node": "out1", "port": 0 } }
  ]
}
```

### Modbus RTU slave (Level 2, ESP32 jadi server via UART2/RS485)

ESP32 di sini jadi **slave (server)** — master Modbus RTU eksternal
(SCADA/PLC dkk) yang inisiasi request lewat RS485, firmware ini cuma
merespons. Pin UART2 fixed: TX=GPIO17, RX=GPIO16 (RS485 TTL module,
mis. MAX485). Slave ID dan baud rate diatur global di tab
**System > Modbus Slave** (satu bus RS485 dipakai bersama SEMUA node
`modbus_slave_reg`), bukan per-node.

Node `modbus_slave_reg` expose **SATU** alamat register/coil. Dua arah
TERPISAH (mirip pola `http_endpoint`, tapi dua nilai berbeda bukan satu):
- **Input** (kalau tersambung ke node lain) → nilai yang **DIBACA**
  master lewat FC03 (holding)/FC01 (coil).
- **Output** → nilai TERAKHIR yang **DITULIS** master lewat FC06
  (holding)/FC05 (coil). 0/false kalau master belum pernah menulis.

Register bisa berfungsi baca-tulis dari KEDUA sisi (graph maupun
master luar) karena dua arah ini independen — bukan read-only atau
write-only kaku. Dua node dengan `address`+`reg_type` sama akan
bentrok secara logis (mengekspos slot bridge yang sama) — hindari.

| `type` | `params` | Keterangan |
|---|---|---|
| `modbus_slave_reg` | `{ "address": 100, "reg_type": "holding" }` | `reg_type`: `holding`/`coil` saja. |

**PENTING — wajib reboot:** sama seperti `http_endpoint`, UART driver
diinstall sekali saat boot (juga cuma di-start SAMA SEKALI kalau ada
minimal 1 node `modbus_slave_reg` di graph — hindari alokasi GPIO17/16
sia-sia kalau tidak dipakai). Menambah/mengubah node atau config Slave
ID/Baud lalu Save **tidak langsung aktif** — device harus di-reboot
manual.

Contoh — expose hasil Math (jumlah dua sensor) ke register holding 200:
```json
{
  "nodes": [
    { "id": "s1", "type": "analog_input", "params": { "pin": 1, "hw_mode": "real" } },
    { "id": "s2", "type": "analog_input", "params": { "pin": 2, "hw_mode": "real" } },
    { "id": "sum1", "type": "math", "params": { "op": "add" } },
    { "id": "slv1", "type": "modbus_slave_reg", "params": { "address": 200, "reg_type": "holding" } }
  ],
  "links": [
    { "from": { "node": "s1", "port": 0 }, "to": { "node": "sum1", "port": 0 } },
    { "from": { "node": "s2", "port": 0 }, "to": { "node": "sum1", "port": 1 } },
    { "from": { "node": "sum1", "port": 0 }, "to": { "node": "slv1", "port": 0 } }
  ]
}
```
Setelah reboot, master SCADA/PLC yang baca holding register 200 lewat
RS485 (FC03) akan mendapat jumlah kedua sensor analog, diperbarui tiap
scan cycle.

## Contoh document lengkap (dari plan.md §7.1)

```json
{
  "version": 1,
  "nodes": [
    { "id": "n1", "type": "digital_input",  "params": { "pin": 4, "mode": "pullup", "invert": false } },
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

## Versioning

- `version: 1` — schema ini, Level 0-1 (Logic/Math/Timing/Data + I/O
  digital dasar).
- `version: 2` (masa depan, belum diimplementasikan) — akan menambah
  field Level 1 penuh (analog/PWM/servo mode) dan Level 2 (I2C/system
  variable) ke `type` yang sudah ada. Kemungkinan besar TIDAK memerlukan
  migrator otomatis kalau perubahan hanya menambah `type`/field baru
  (backward-compatible ke parser v1 selama parser v2 tetap terima
  document `version: 1` tanpa field baru). Migrator eksplisit baru
  diperlukan kalau struktur `nodes[]`/`links[]` di atas berubah bentuk.

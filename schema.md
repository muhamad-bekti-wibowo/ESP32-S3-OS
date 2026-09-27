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
| `ctu` | `{ "preset": 3 }` | `{ "count": 0, "prev_clk": false }` |

Contoh:
```json
{ "id": "t1", "type": "ton", "params": { "delay_ms": 2000 } }
```

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

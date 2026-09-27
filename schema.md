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

### I/O digital (Level 1, bagian dasar sudah ada sejak v1)

| `type` | `params` | Keterangan |
|---|---|---|
| `digital_input` | `{ "pin": 4, "mode": "pullup", "invert": false }` | Level 1 fisik — implementasi hardware di spec 05, untuk sekarang pass-through |
| `digital_output` | `{ "pin": 2, "invert": false }` | sama |

Contoh:
```json
{ "id": "n1", "type": "digital_input", "params": { "pin": 4, "mode": "pullup", "invert": false } }
```

### Reserved untuk Level 1 (belum diimplementasikan, JANGAN dipakai sebelum spec 05)

`analog_input`, `pwm_output`, `servo` — field `params.mode` (`"simulated"`
atau `"real"`) akan ditambahkan di spec 05. Jangan kirim `type` ini di
`POST /api/program` sebelum spec 05 selesai — parser akan menolaknya
sebagai `type` tidak dikenal.

### Reserved untuk Level 2 (belum diimplementasikan, JANGAN dipakai sebelum spec 06)

`i2c_read_reg`, `i2c_write_reg`, `sys_var_get` — lihat
[specs/06-level2-i2c-wifi.md](specs/06-level2-i2c-wifi.md).

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

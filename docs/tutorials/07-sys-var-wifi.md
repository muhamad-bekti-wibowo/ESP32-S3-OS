# 7. Variabel Sistem & Konfigurasi WiFi

## Tab System > Network

WiFi **tidak dikonfigurasi lewat node di canvas** — konfigurasi SSID/
password dilakukan lewat halaman terpisah, `System > Network` (link di
toolbar editor), supaya kredensial WiFi tidak tercampur dengan logika
FBD dan tidak pernah ter-expose lewat `GET /api/program`.

Langkah:

1. Klik **System > Network** di toolbar.
2. Isi SSID, password, dan hostname (opsional) untuk koneksi STA.
3. Klik **Save**.
4. Config disimpan ke NVS (bertahan setelah reboot), tapi **baru
   diterapkan setelah device reboot** — sengaja tidak langsung
   diterapkan supaya response HTTP "sukses" tidak terputus akibat
   device pindah jaringan di tengah proses.
5. Password **tidak pernah dikirim balik** lewat `GET /api/network` —
   kalau kamu buka halaman ini lagi, field password akan kosong
   (bukan berarti tersimpan kosong, cuma tidak ditampilkan ulang demi
   keamanan).

AP bawaan (`ESP32-WebLogic`) tetap aktif terus, terlepas dari config STA
ini (mode APSTA) — jadi kamu tidak akan pernah "terkunci" tanpa akses
ke device kalau config STA salah.

## sys_var_get — baca status sistem dari FBD

Node `sys_var_get` membaca variabel sistem read-only, mirip `%SM` di
PLC Siemens/Omron. Saat ini hanya dua variabel yang didukung:

| `name` | Tipe | Keterangan |
|---|---|---|
| `SYS.WIFI_CONNECTED` | bool | `true` kalau STA sedang terkoneksi ke WiFi |
| `SYS.WIFI_RSSI` | int32 (dBm) | Kekuatan sinyal WiFi STA, misal `-48` |

`SYS.IP_ADDRESS`/`SYS.HOSTNAME` **belum didukung** lewat `sys_var_get`
(representasi string tidak muat di tipe value internal yang dibatasi
8 byte) — kalau dibutuhkan nanti akan lewat endpoint HTTP terpisah.

## Contoh: LED peringatan sinyal WiFi lemah

```
sys_var_get(name=SYS.WIFI_RSSI) --> compare(op=lt).in0
const(datatype=float, value=-80) --------------------> compare.in1
compare --> digital_output(pin=5, invert=false, hw_mode=real)
```

Logika: LED menyala kalau RSSI lebih kecil dari -80 dBm (sinyal lemah).

Setup node:
1. `sys_var_get`, params `{ "name": "SYS.WIFI_RSSI" }`.
2. `const`, params `{ "datatype": "float", "value": -80 }`.
3. `compare`, params `{ "op": "lt" }`.
4. `digital_output`, params `{ "pin": 5, "invert": false, "hw_mode": "real" }`.
5. Sambungkan: `sys_var_get → compare.in0`, `const → compare.in1`,
   `compare → digital_output.in0`.
6. Save, lalu jauhkan device dari router WiFi untuk melihat LED menyala
   saat sinyal melemah.

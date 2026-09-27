# 1. Mulai Cepat

Tutorial ini untuk yang baru pertama kali pakai firmware ini. Asumsi:
kamu sudah punya board ESP32-S3 dan sudah bisa `idf.py build flash monitor`
(kalau belum, lihat README.md bagian build).

## Langkah 1 — Flash firmware

```
idf.py -p COMx build flash monitor
```

Tunggu sampai log serial menampilkan sesuatu seperti:

```
I (xxx) wifi_mgr: AP ESP32-WebLogic aktif, IP 192.168.4.1
I (xxx) wifi_mgr: Config WiFi STA dimuat dari NVS
I (xxx) web_ui: program.json dimuat dari SPIFFS
```

Kalau baris "Config WiFi STA" tidak muncul, berarti belum pernah diisi —
itu normal untuk flash pertama kali, lanjut ke Langkah 2.

## Langkah 2 — Sambungkan ke AP bawaan

Device selalu menyalakan WiFi Access Point sendiri, nama **`ESP32-WebLogic`**,
tanpa password (atau sesuai default di `wifi_mgr.h` kalau sudah diubah).
Sambungkan laptop/HP ke AP ini dulu untuk konfigurasi awal.

Buka browser ke:

```
http://192.168.4.1
```

## Langkah 3 (opsional tapi disarankan) — Sambungkan device ke WiFi rumah

Device mendukung mode APSTA — AP bawaan tetap aktif SEKALIGUS device bisa
konek ke WiFi rumah/kantor. Ini memudahkan supaya kamu tidak perlu
pindah-pindah koneksi WiFi setiap kali mau akses device dari laptop biasa.

1. Di web UI, klik menu **System > Network**.
2. Isi SSID dan password WiFi rumah kamu.
3. Klik **Save**.
4. Device perlu **reboot** supaya koneksi STA baru diterapkan (config
   disimpan ke NVS, tidak langsung connect saat itu — supaya response
   HTTP "sukses" tidak terputus oleh device yang tiba-tiba pindah
   jaringan). Reboot manual (tombol reset) atau lewat power cycle.
5. Setelah reboot, cek log serial untuk baris `Config WiFi STA dimuat
   dari NVS` dan IP yang didapat di jaringan rumah. Mulai saat ini kamu
   bisa akses device dari IP itu, tanpa perlu konek ke AP `ESP32-WebLogic`
   lagi (kecuali AP itu memang mau dipakai).

## Langkah 4 — Buka editor

Buka `http://<ip-device>/` di browser. Ini akan menampilkan canvas
editor FBD kosong (atau program terakhir yang tersimpan, kalau bukan
flash pertama kali — program otomatis di-load dari SPIFFS saat boot).

Lanjut ke [02-mengenal-editor.md](02-mengenal-editor.md) untuk mengenal
bagian-bagian editornya.

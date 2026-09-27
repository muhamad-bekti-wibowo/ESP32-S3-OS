# 9. Troubleshooting

## Tidak bisa akses web editor sama sekali

- Pastikan sudah konek ke AP `ESP32-WebLogic` ATAU ke jaringan STA yang
  sama dengan device (lihat [01-mulai-cepat.md](01-mulai-cepat.md)).
- Cek IP device lewat serial monitor (`idf.py monitor`) — cari baris log
  WiFi yang menampilkan IP AP (`192.168.4.1`) atau IP STA.
- PC dual-homed (Ethernet + WiFi aktif bersamaan di subnet yang sama)
  bisa menyebabkan routing ambigu dan request timeout yang terlihat
  seperti masalah firmware padahal environment jaringan PC. Coba
  matikan salah satu interface kalau mengalami timeout tidak konsisten.

## Halaman editor terbuka tapi app.js/style.css/dst 404

Firmware versi lama pernah punya bug ini (hardcode hanya serve
`index.html`) — sudah diperbaiki jadi handler wildcard. Kalau masih
terjadi, kemungkinan SPIFFS belum di-flash ulang dengan versi terbaru;
jalankan `idf.py flash` (bukan cuma `idf.py app-flash`) supaya partisi
SPIFFS ikut ter-update.

## Klik Save, muncul error, program di device tidak berubah

Ini **perilaku normal, bukan bug** — device selalu validasi dulu sebelum
menerapkan (lihat [08-live-update.md](08-live-update.md)). Penyebab
error paling umum:

- **Rangkaian melingkar (cyclic)**: ada node yang secara tidak langsung
  jadi input untuk dirinya sendiri. Cek ulang sambungan wire.
- **Pin `real` terlarang**: memakai `hw_mode: real` pada strapping pin/
  USB-JTAG pin/pin PSRAM (lihat tabel di
  [04-io-digital.md](04-io-digital.md)). Ganti ke pin yang aman.
- **Field wajib kosong**: misalnya `const` tanpa `datatype`/`value`.
  Cek semua node di panel Properties, pastikan tidak ada field kosong.

## digital_output real tidak menyalakan LED

- Cek `hw_mode` benar-benar `real` di kedua node (input DAN output) —
  kalau salah satu masih `simulated`, sinyal tidak akan sinkron dengan
  hardware fisik.
- Cek wiring LED (anoda/katoda, resistor, GND bersama dengan ESP32).
- Cek `invert` — kalau logikanya kebalik, coba toggle `invert` di node
  `digital_output`.
- Ukur langsung dengan multimeter di pin (lihat catatan di akhir
  [04-io-digital.md](04-io-digital.md)) untuk memastikan level tegangan
  benar-benar berubah, memisahkan masalah firmware dari masalah LED/
  wiring.

## i2c_read_reg selalu error=true

- Cek wiring SDA (GPIO8) dan SCL (GPIO9) — termasuk apakah sudah ada
  pull-up resistor (banyak breakout board sudah punya bawaan, tapi
  tidak semua).
- Cek alamat 7-bit yang dipakai — banyak modul I2C punya alamat yang
  bisa berubah lewat jumper/solder pad (misal PCF8574 bisa 0x20-0x27
  tergantung jumper A0/A1/A2).
- Coba `i2c_read_reg` dengan `length=1` ke `register=0` dulu sebagai
  tes paling sederhana ada/tidaknya device (lihat
  [06-i2c.md](06-i2c.md)).

## Program hilang setelah reboot

Kalau ini masih terjadi di firmware terbaru, berarti ada regresi —
seharusnya `program.json` otomatis dimuat ulang saat boot (lihat log
`program.json dimuat dari SPIFFS`). Kalau baris log itu tidak muncul,
kemungkinan SPIFFS penuh atau partisi belum benar — cek `partitions.csv`
dan sisa ruang SPIFFS.

## WiFi STA tidak konek setelah isi Network tab

- Config baru **butuh reboot** untuk diterapkan (bukan langsung saat
  Save) — reboot manual device dulu.
- Cek SSID/password tidak typo (password tidak ditampilkan ulang demi
  keamanan, jadi kalau ragu, isi ulang dari awal lalu Save lagi).
- AP `ESP32-WebLogic` tetap aktif meski STA gagal konek — kamu tidak
  akan pernah kehilangan akses ke device sama sekali.

# 4. I/O Digital Nyata (GPIO Fisik)

## simulated vs real

Setiap node `digital_input`/`digital_output` punya field `hw_mode`:

- **`simulated`** (default) — tidak menyentuh GPIO fisik sama sekali.
  `digital_input` nilainya kamu atur manual lewat panel Properties;
  `digital_output` cuma pass-through nilai tanpa efek fisik apa pun.
  Cocok untuk desain/tes logika dulu sebelum ada hardware terpasang.
- **`real`** — device benar-benar memanggil driver GPIO ESP-IDF
  (`gpio_config`, `gpio_get_level`, `gpio_set_level`). `digital_input`
  membaca level pin sungguhan; `digital_output` menyalakan/mematikan
  pin sungguhan.

**Aturan konsistensi**: rangkaian yang sudah benar logikanya di
`simulated` harus tetap benar arah logikanya setelah dipindah ke `real`
— jadi selalu didesain di `simulated` dulu (seperti tutorial 03),
lalu ganti `hw_mode` ke `real` setelah yakin logikanya benar.

## Pin yang AMAN dan yang DILARANG

Device ini menolak `POST /api/program` yang memakai `hw_mode: real`
pada pin berbahaya berikut (validasi wajib, tidak bisa dimatikan):

| Pin | Kenapa dilarang |
|---|---|
| GPIO0, 3, 45, 46 | Strapping pin — menentukan mode boot, dipakai I/O bisa membuat device gagal boot |
| GPIO19, 20 | Dipakai USB-JTAG bawaan — dipakai I/O mematikan debugging via USB |
| GPIO26–37 | Dipakai SPI flash/PSRAM (board ini pakai PSRAM Octal 8MB) — dipakai I/O berisiko corrupt memori |

Pin yang aman dipakai (contoh, cek breakout board fisik kamu — tidak
semua nomor di-broke-out di semua board): **GPIO4, 5, 6, 7, 8, 9, 10,
11, 12, 13, 14, 15, 16, 17, 18, 21, 38–48**.

Kalau kamu mencoba Save dengan pin terlarang + `hw_mode: real`, device
akan menolak request (HTTP 400) dan **program yang sedang jalan di
device tidak berubah** — jadi aman dicoba-coba, tidak akan membuat
device dalam keadaan setengah jadi/rusak.

Catatan: validasi ini HANYA berlaku untuk `hw_mode: real`. Kalau
`hw_mode: simulated`, nomor pin di JSON murni informasional (boleh
angka apa saja, tidak divalidasi, karena tidak pernah menyentuh GPIO).

## Contoh: tombol fisik (GPIO4) → LED fisik (GPIO5)

Rangkaian fisik:
- Tombol: satu kaki ke GPIO4, kaki lain ke GND. `mode: pullup` supaya
  default HIGH, LOW saat ditekan (makanya biasanya dipasangkan
  `invert: true` agar logika program tetap "true = ditekan").
- LED: anoda ke GPIO5 (lewat resistor secukupnya), katoda ke GND.

Langkah:

1. Buat ulang rangkaian dari [03-program-pertama.md](03-program-pertama.md)
   (atau **Load** kalau sudah tersimpan).
2. Klik node `digital_input`, ubah:
   - `pin = 4`
   - `mode = pullup`
   - `invert = true`
   - `hw_mode = real`
3. Klik node `digital_output`, ubah:
   - `pin = 5`
   - `invert = false`
   - `hw_mode = real`
4. Klik **Save**.
5. Tekan tombol fisik di GPIO4, tunggu 2 detik (sesuai `ton.delay_ms`),
   LED di GPIO5 harus menyala.

Kalau tidak menyala, lihat [09-troubleshooting.md](09-troubleshooting.md).

## Mengukur dengan multimeter (opsional)

Kalau ingin memverifikasi lebih presisi (tanpa LED), pasang probe
multimeter (mode voltage DC) langsung ke pin `digital_output` (GPIO5)
dan GND. Level HIGH pada ESP32-S3 idealnya terbaca ~3.3V, LOW ~0V.

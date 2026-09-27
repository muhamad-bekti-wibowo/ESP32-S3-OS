# 3. Membuat Program Pertama

Kita akan buat rangkaian sederhana yang jadi contoh baku di
`schema.md`/`plan.md`: **tombol + timer ON-delay → AND → output**.

Logika: output menyala hanya kalau tombol ditekan DAN sudah ditahan
minimal 2 detik (timer TON).

## Langkah 1 — Tambah node

Drag 4 node dari palette ke canvas:

1. `digital_input` (kategori I/O Digital)
2. `ton` (kategori Timing)
3. `and` (kategori Logic)
4. `digital_output` (kategori I/O Digital)

## Langkah 2 — Atur Properties tiap node

Klik tiap node satu-satu, isi panel Properties di kanan:

- **digital_input**: `pin = 4`, `mode = pullup`, `invert = false`,
  `hw_mode = simulated` (biarkan simulated dulu — kita akan coba hardware
  nyata di [04-io-digital.md](04-io-digital.md)).
- **ton**: `delay_ms = 2000` (2 detik).
- **and**: tidak ada params (biarkan kosong).
- **digital_output**: `pin = 5`, `invert = false`, `hw_mode = simulated`.

## Langkah 3 — Sambungkan

Tarik wire sesuai urutan ini:

```
digital_input --(port 0)--> and (in0)
ton           --(port 0)--> and (in1)
and           --(port 0)--> digital_output (in0)
```

Catatan: node `ton` di rangkaian nyata biasanya juga menerima trigger
dari sinyal yang sama (`digital_input`) supaya delay dihitung sejak
tombol ditekan. Untuk versi paling sederhana ini kita biarkan `ton`
berdiri sendiri dulu (delay dihitung terus dari boot) — sambungkan
`digital_input → ton.in0` juga kalau mau delay dihitung sejak tombol
ditekan, bukan sejak boot.

## Langkah 4 — Save

Klik **Save** di toolbar. Kalau berhasil, status di toolbar menunjukkan
sukses dan device langsung menjalankan program baru — **tanpa reboot**
(lihat [08-live-update.md](08-live-update.md) untuk detail mekanismenya).

## Langkah 5 — Verifikasi

Karena semua node masih `hw_mode: simulated`, kamu belum bisa lihat efek
di GPIO fisik. Untuk sekarang, cek lewat:

```
GET http://<ip-device>/api/program
```

Response-nya berisi field `outputs` per node — nilai `digital_output`
akan berubah dari `false` ke `true` setelah `digital_input` "aktif"
(karena simulated, kamu perlu ubah `params` node itu manual dan Save
ulang untuk mengubah kondisi input — mode simulated tidak punya switch
fisik).

Untuk pengujian dengan tombol/LED fisik sungguhan, lanjut ke
[04-io-digital.md](04-io-digital.md).

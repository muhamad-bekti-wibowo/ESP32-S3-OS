# 2. Mengenal Editor

Layout web editor terbagi jadi 4 bagian:

```
+----------+----------------------------------+--------------+
| Palette  |                                  |  Properties  |
| (kiri)   |      Canvas Drawflow (tengah)     |   (kanan)    |
|          |                                  |              |
+----------+----------------------------------+--------------+
| Toolbar (bawah): Save | Load | System > Network | status   |
+---------------------------------------------------------------+
```

## Palette (kiri)

Daftar semua node type yang tersedia, dikelompokkan sesuai kategori di
`schema.md`: Logic, Data, Math, Timing, I/O Digital, I/O Analog/PWM/Servo,
I2C, System. Klik/drag salah satu untuk menambahkan node itu ke canvas.

## Canvas (tengah)

Area kerja utama. Beberapa hal penting:

- **Menambah node**: drag dari palette ke canvas.
- **Menyambung node**: klik-tahan dari port output (kanan node) lalu
  tarik ke port input node lain (kiri node). Satu port output boleh
  disambung ke lebih dari satu input; satu port input hanya boleh
  menerima satu sambungan.
- **Memilih node**: klik pada node. Node terpilih ditandai **segitiga
  kecil di pojok** — bukan border tebal atau warna solid, supaya tetap
  gampang dibaca kalau ada banyak node menumpuk.
- **Menghapus**: pilih node/link, tekan Delete (atau klik kanan sesuai
  perilaku bawaan Drawflow).
- **Menggeser canvas**: klik-tahan area kosong dan geser. Scroll untuk
  zoom in/out.

## Panel Properties (kanan)

Panel ini **selalu terbuka**, isinya berubah sesuai node yang sedang
dipilih di canvas (bukan popup/modal yang menutupi canvas). Kalau tidak
ada node terpilih, panel kosong/menunjukkan placeholder.

Field yang muncul di panel ini persis mengikuti `params` node tersebut
sesuai `schema.md` — misalnya node `const` akan menampilkan field
`datatype` dan `value`, node `digital_input` akan menampilkan `pin`,
`mode`, `invert`, `hw_mode`, dan seterusnya. Beberapa field punya kontrol
khusus:

- `analog_input.sim_value` → slider (bukan angka polos), supaya gampang
  disimulasikan tanpa hardware ADC nyata.
- `i2c_write_reg.data` → input CSV byte (contoh: `16, 32`), otomatis
  dikonversi ke array angka saat disimpan.

Perubahan di panel Properties langsung mengubah node di canvas — tapi
**belum terkirim ke device** sampai kamu klik **Save** di toolbar.

## Toolbar (bawah)

- **Save** — mengonversi seluruh canvas jadi JSON sesuai `schema.md`,
  lalu `POST` ke `/api/program`. Kalau device menolak (JSON tidak valid,
  ada rangkaian melingkar/cyclic, dst), akan muncul pesan error dan
  program yang jalan di device **tidak berubah** (device selalu validasi
  dulu sebelum menerapkan — lihat [08-live-update.md](08-live-update.md)).
- **Load** — mengambil program yang sedang aktif di device
  (`GET /api/program`) dan menggambarnya ulang di canvas, menimpa apa
  pun yang sedang di-edit di canvas saat ini.
- **System > Network** — pindah ke halaman konfigurasi WiFi STA
  (lihat [07-sys-var-wifi.md](07-sys-var-wifi.md)).
- **status** — indikator singkat hasil aksi terakhir (misal "Saved",
  atau pesan error dari device).

Lanjut ke [03-program-pertama.md](03-program-pertama.md) untuk membuat
rangkaian pertama.

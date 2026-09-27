# Spec 4 — Editor Visual Drawflow (Browser)

Ref: [plan.md §9](../plan.md), Fase 4.
Sebelumnya: [03-schema-freeze.md](03-schema-freeze.md).
Berikutnya: [05-level1-io.md](05-level1-io.md).

## Scope
**Kerjakan:**
- Ganti [index.html](../components/web_ui/webroot/index.html) placeholder
  dengan editor berbasis library **Drawflow** (vendor file-nya ke
  `webroot/`, jangan CDN — device offline-first, AP mode tanpa internet).
- Tiap node type Level 0 (dari spec 01/03) di-mapping 1:1 ke Drawflow node
  — TIDAK ada abstraksi tambahan di sisi editor.
- Fungsi converter eksplisit dua arah: Drawflow export format (internal
  Drawflow, beda dari schema.md) ↔ schema `fbd_core` dari spec 03. Ini
  HARUS fungsi terpisah yang jelas, bukan tercampur di kode UI umum.
- Tombol Save → `POST /api/program` (pakai endpoint dari spec 03). Tombol
  Load → `GET /api/program` → converter balik → render ulang canvas.

**JANGAN kerjakan di spec ini:**
- Belum ada eksekusi logic live di UI (indikator nyala/mati node) — itu
  spec 07 (`live monitoring` via WebSocket).
- Belum ada slider simulasi Level 1 (`analog_input`, dst) — node itu belum
  ada di schema sampai spec 05. Kalau spec 05 sudah lebih dulu, boleh
  digabung, tapi urutan default: 04 dulu baru 05.

## File yang disentuh
```
components/web_ui/webroot/
  index.html          <- REVISI total: canvas Drawflow + toolbar Save/Load
  drawflow.min.js      <- BARU: vendor library (download sekali, commit ke repo)
  drawflow.min.css     <- BARU: vendor
  app.js               <- BARU: logic UI (converter, fetch save/load, node registration)
  style.css             <- BARU (opsional, styling minimal)
```

## Langkah kerja
1. Ambil Drawflow (https://github.com/jerosoler/Drawflow) versi minified,
   simpan ke `webroot/`. Cek ukuran total tidak membengkak
   littlefs/spiffs partition (lihat [partitions.csv](../partitions.csv)).
2. Registrasi tiap node type Level 0 sebagai custom Drawflow node (HTML
   template sederhana per tipe: label + input field untuk `params`).
3. Tulis `app.js`:
   - `drawflowToSchema(drawflowExport)` → object sesuai schema.md.
   - `schemaToDrawflow(schemaObj)` → object siap di-import Drawflow.
   - `saveProgram()`: ambil `editor.export()`, convert, `POST /api/program`.
   - `loadProgram()`: `GET /api/program`, convert balik, `editor.import()`.
4. Test manual di browser (bisa mock endpoint dulu di host tanpa ESP32
   kalau mau iterasi cepat, baru tes end-to-end ke device).

## Kriteria selesai
- [ ] Susun graph: 2× `digital_input` → `AND` → `digital_output` di canvas.
- [ ] Klik Save, refresh browser (full reload), klik Load — graph yang
      sama muncul kembali persis (posisi node boleh beda, tapi
      node+params+links harus identik).
- [ ] Konten JSON yang dikirim ke `/api/program` valid menurut schema.md
      (verifikasi manual lewat DevTools Network tab atau log server).
- [ ] Tidak ada logic dieksekusi dari sisi UI ini (scan cycle tetap di
      firmware, editor hanya CRUD graph).

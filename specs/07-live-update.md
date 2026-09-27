# Spec 7 — Dual-Buffer Graph Swap + Live Update Tanpa Reboot

Ref: [plan.md §11](../plan.md), Fase 6.
Sebelumnya: [06-level2-i2c-wifi.md](06-level2-i2c-wifi.md).
Berikutnya: tidak ada — ini tahap terakhir roadmap aktif. Level 3
(SPI/I2S/dst) tetap DITUNDA setelah ini.

## Scope
**Kerjakan:**
- Implementasi dual-buffer graph (`active_graph` + `standby_graph`) sesuai
  [plan.md §11.1](../plan.md): web handler (Core 0) parse+compile ke
  `standby_graph`, set `reload_requested = true` (volatile bool atau
  atomic), scan task (Core 1) cek flag ini di **awal tiap cycle** dan
  swap pointer — bukan mutex kompleks.
- Revisi endpoint `/api/program` (spec 03) supaya pakai mekanisme ini,
  gantikan modifikasi langsung ke graph aktif yang dipakai sementara di
  spec 03/06.
- Validasi sebelum swap: `standby_graph->compile()` harus sukses dulu
  (tidak cyclic, semua link valid) — kalau gagal, `reload_requested` TIDAK
  di-set, endpoint return error, `active_graph` tidak tersentuh.
- Auto-backup: sebelum overwrite file JSON aktif di LittleFS/SPIFFS, simpan
  salinan lama dengan timestamp (`program_<timestamp>.json.bak` atau
  sejenis).
- Live monitoring lewat WebSocket (kalau spec 06 belum bikin ini secara
  lengkap): push status tiap node (atau tiap N cycle) untuk indikator
  nyala/mati di editor Drawflow.

**JANGAN kerjakan di spec ini:**
- Jangan tambah node type baru — spec ini murni tentang lifecycle/safety
  update program, bukan fitur node.
- Jangan mulai Level 3 (SPI/I2S) — checklist plan.md §13 baris terakhir
  eksplisit melarang ini sampai semua sebelumnya tercentang.

## File yang disentuh
```
components/fbd_core/
  fbd_graph.h / .c       <- REVISI: tambah active/standby pointer, reload_requested
main/main.c               <- REVISI: scan task cek reload_requested di awal cycle
components/web_ui/
  web_ui.c                <- REVISI: handler /api/program pakai standby+flag,
                                bukan modify langsung
  (storage backup)         <- BARU: fungsi backup file sebelum overwrite
```

## Langkah kerja
1. Refactor `main.c`: definisikan dua instance `fbd_graph_t` global,
   `fbd_graph_t *g_active = &graph_a, *g_standby = &graph_b;`, dan
   `volatile bool g_reload_requested = false;`.
2. Di scan task, tambahkan di awal loop (sebelum `execute_cycle`):
   ```c
   if (g_reload_requested) {
       fbd_graph_t *tmp = g_active;
       g_active = g_standby;
       g_standby = tmp;
       g_reload_requested = false;
   }
   ```
3. Di handler `/api/program`: parse ke `g_standby` (bukan `g_active`),
   `fbd_graph_compile(g_standby)` — kalau gagal, return HTTP error dan
   JANGAN set flag. Kalau sukses, backup file lama, tulis file baru,
   `g_reload_requested = true`.
4. Tambah backup: sebelum `fopen(path, "w")` menimpa file program aktif,
   copy file lama ke `program_backup_<uptime_or_counter>.json` di
   LittleFS/SPIFFS (cek dulu berapa free space tersisa — jangan sampai
   backup memenuhi partition `littlefs` dari [partitions.csv](../partitions.csv)).
5. Uji: device running dengan logic A, ubah ke logic B di editor sambil
   device tetap running (LED fisik menyala sesuai logic A), klik Save,
   tanpa reboot, verifikasi logic B langsung aktif — LED berubah sesuai
   logic baru, TANPA crash/freeze.

## Kriteria selesai
- [ ] Update program saat scan task sedang jalan → tidak ada crash,
      corruption, atau nilai "nyasar" dari graph lama.
- [ ] JSON dengan cyclic dependency dikirim ke `/api/program` saat device
      running → ditolak, `active_graph` tetap jalan dengan logic lama,
      tidak ada downtime.
- [ ] File backup dengan timestamp/counter muncul di LittleFS setiap kali
      program di-overwrite, dan device tidak kehabisan space partition
      littlefs akibat backup menumpuk tanpa batas (tentukan retensi, misal
      simpan maksimal N backup terakhir).
- [ ] Indikator status node di editor (WebSocket) update live sesuai scan
      cycle sebenarnya, terlihat berubah real-time saat logic dieksekusi.
- [ ] Semua item checklist [plan.md §13](../plan.md#13-checklist-anti-gagal-final-cek-sebelum-lanjut-fase-berikutnya)
      tercentang setelah spec ini selesai.

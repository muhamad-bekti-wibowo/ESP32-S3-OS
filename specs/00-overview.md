# Spec Overview — FBD/Virtual PLC Runtime

Sumber: [plan.md](../plan.md). Dokumen ini memecah plan.md jadi spec per
tahap supaya tiap tahap bisa dikerjakan dalam 1 sesi/prompt dan punya
kriteria selesai yang jelas sebelum lanjut ke tahap berikutnya.

**Konteks penting yang TIDAK sesuai asumsi plan.md:**
- Project ini pakai **ESP-IDF plain C** (bukan Arduino/C++). Semua contoh
  kode `struct`/`class` C++ di plan.md harus diterjemahkan ke C idiomatis
  (`enum`, `struct` tanpa constructor, function pointer kalau perlu
  polymorphism).
- Sudah ada v1 berjalan: [logic_engine](../components/logic_engine),
  [web_ui](../components/web_ui), [wifi_mgr](../components/wifi_mgr) — lihat
  [main.c](../main/main.c). v1 ini **belum** punya topological sort (asumsi
  urutan definisi = urutan eksekusi), **belum** punya tagged union
  `FBDValue` (pakai `double` polos), **belum** punya dual-task FreeRTOS
  (scan cycle jalan di `app_main` langsung), **belum** ada web editor visual
  (Drawflow) — baru static `index.html`.
- Setiap spec tahap di bawah harus dibaca dengan asumsi: **migrasi dari v1**,
  bukan nulis dari nol.

## Daftar spec per tahap (urutan wajib)

| # | File | Fokus | Sesuai plan.md |
|---|---|---|---|
| 1 | [01-fbdvalue-core.md](01-fbdvalue-core.md) | Tagged union `FBDValue` + node Level 0 (logic/math/timer/data), dites di host (bukan ESP32) | §4, §5 Level 0, §8.4 |
| 2 | [02-topo-sort-runtime.md](02-topo-sort-runtime.md) | Ganti runtime v1: `id`-based node, topological sort, dual-task Core0/Core1 | §7.1, §8.1–8.3, Fase 3 |
| 3 | [03-schema-freeze.md](03-schema-freeze.md) | Freeze schema JSON Level 0-1 ke `schema.md`, endpoint save/load | §7.1, §12 tahap 2 |
| 4 | [04-drawflow-editor.md](04-drawflow-editor.md) | Editor visual Drawflow, converter JSON, mode simulasi Level 1 | §9, Fase 4 |
| 5 | [05-level1-io.md](05-level1-io.md) | `analog_input`, `pwm_output`, `servo` — simulated dulu, lalu real | §5 Level 1, Fase 4 lanjutan |
| 6 | [06-level2-i2c-wifi.md](06-level2-i2c-wifi.md) | I2C primitive register-level + WiFi/mDNS sebagai system variable | §5 Level 2, Fase 5 |
| 7 | [07-live-update.md](07-live-update.md) | Dual-buffer graph swap, live update tanpa reboot, validasi & backup | §11, Fase 6 |

**Level 3 (SPI/I2S/dst) sengaja tidak punya spec — DITUNDA total sampai 1-7 selesai.**

## Cara pakai
Kerjakan satu file spec per sesi. Tiap file punya:
- Scope (apa yang dikerjakan, apa yang TIDAK)
- File yang disentuh
- Langkah kerja
- Kriteria selesai (harus lolos sebelum lanjut ke spec berikutnya)

Checklist final gabungan tetap ada di [plan.md §13](../plan.md#13-checklist-anti-gagal-final-cek-sebelum-lanjut-fase-berikutnya).

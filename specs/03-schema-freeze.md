# Spec 3 — Freeze Schema JSON Level 0-1 + Endpoint Save/Load

Ref: [plan.md §7.1](../plan.md), [§12 tahap 2](../plan.md), checklist §13
baris 3.
Sebelumnya: [02-topo-sort-runtime.md](02-topo-sort-runtime.md).
Berikutnya: [04-drawflow-editor.md](04-drawflow-editor.md).

## Scope
**Kerjakan:**
- Tulis `schema.md` (dokumen terpisah, final untuk Level 0-1) yang
  mendefinisikan format JSON: `version`, `nodes[]` (`id`, `type`, `params`,
  `state`), `links[]` (`from`, `to` pakai notasi `"node_id.port_name"` atau
  index port — putuskan salah satu, konsisten dengan `fbd_link_t` dari
  spec 02).
- Implementasikan parser JSON→`fbd_graph_t` (pakai `cJSON`, sudah dipakai
  v1 di `logic_engine_load_json`) dan serializer `fbd_graph_t`→JSON, sesuai
  schema yang di-freeze.
- Endpoint `web_ui`: `POST /api/program` (load graph baru, `compile()`,
  kalau gagal return error JSON, JANGAN diterapkan ke `active_graph`),
  `GET /api/program` (dump graph aktif sebagai JSON).
- Validasi saat parse: semua `links[].from`/`to` merujuk id/port valid,
  kalau tidak, tolak seluruh load (bukan partial-load).

**JANGAN kerjakan di spec ini:**
- Jangan bikin dual-buffer swap yang aman dari race condition — untuk
  sekarang endpoint boleh langsung modify graph aktif (spec 07 yang benerin
  ini jadi thread-safe). Cukup pastikan tidak corrupt kalau dipanggil saat
  scan task tidak jalan/sedang restart sederhana.
- Jangan tambah field schema untuk node Level 1/2 (ADC, PWM, I2C) — itu
  ditambahkan di spec 05/06, tapi struktur `params`/`state` harus sudah
  cukup generik (schema.md boleh mencatat "reserved for Level 1" pada
  field yang akan datang).

## File yang disentuh
```
schema.md                         <- BARU, root project (sejajar plan.md)
components/fbd_core/
  fbd_json.h / fbd_json.c          <- BARU: parse_json_to_graph(), graph_to_json()
components/web_ui/
  web_ui.c                        <- REVISI: endpoint /api/program pakai fbd_json baru
```

## Isi minimal `schema.md`
Contoh dari [plan.md §7.1](../plan.md) sebagai basis, tapi WAJIB diputuskan
dan didokumentasikan secara eksplisit:
- Notasi port di `links`: apakah `"n1.out"` (string, perlu di-parse) atau
  `{"node":"n1","port":0}` (lebih mudah di-parse di C tanpa string split).
  **Rekomendasi:** pakai object eksplisit, lebih murah di-parse dengan
  cJSON dibanding split string di C.
- Daftar lengkap `type` yang valid di Level 0 (dari spec 01) dengan skema
  `params` masing-masing (contoh konkret per tipe, bukan cuma deskripsi).
- Aturan `params` vs `state` (statis vs runtime) — tegaskan ulang dari
  plan.md, ini sering dilanggar kalau tidak ditulis eksplisit.
- Versioning: `version: 1` sekarang, catat kapan/gimana migrasi ke `version: 2`
  nanti (tidak perlu implementasi migrator sekarang, cukup dicatat).

## Langkah kerja
1. Tulis `schema.md` — tunjukkan ke user/diri sendiri sebagai dokumen yang
   di-lock, bukan draft.
2. Implementasikan `fbd_json_parse()`/`fbd_json_serialize()` di
   `fbd_core`, dites di **host** dulu (tanpa ESP-IDF) mirip spec 01/02:
   round-trip JSON→graph→JSON harus identik (atau setara secara semantik).
3. Integrasikan ke `web_ui.c`: revisi handler `/api/program` existing di
   [web_ui.c](../components/web_ui/web_ui.c) (cek dulu implementasi
   sekarang sebelum menimpa).
4. Test manual: `curl -X POST .../api/program -d @contoh.json`, lalu
   `curl .../api/program`, bandingkan.

## Kriteria selesai
- [ ] `schema.md` ada di root, mencakup semua tipe node Level 0 dari spec 01
      dengan contoh JSON konkret per tipe.
- [ ] Round-trip parse→serialize dites di host, hasil setara dengan input.
- [ ] `POST /api/program` dengan JSON valid berhasil mengganti graph aktif
      dan `compile()` sukses.
- [ ] `POST /api/program` dengan JSON yang punya cyclic link ATAU link ke
      id yang tidak ada → ditolak dengan pesan error jelas, graph aktif
      TIDAK berubah.
- [ ] `GET /api/program` mengembalikan JSON yang bisa langsung dipakai lagi
      sebagai input `POST` (round-trip lewat HTTP, bukan cuma di host test).

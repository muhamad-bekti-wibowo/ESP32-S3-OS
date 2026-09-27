# Spec 5 — Level 1 I/O: analog_input, pwm_output, servo (Simulated → Real)

Ref: [plan.md §5 Level 1](../plan.md), Fase 4-5 lanjutan.
Sebelumnya: [04-drawflow-editor.md](04-drawflow-editor.md).
Berikutnya: [06-level2-i2c-wifi.md](06-level2-i2c-wifi.md).

## Scope
**Kerjakan, dalam DUA sub-tahap wajib berurutan:**

### 5a — Simulated mode (tanpa hardware)
- Tambah node type `analog_input`, `pwm_output`, `servo` ke `fbd_core`
  (params sesuai contoh di [plan.md §5 Level 1](../plan.md): `pin`,
  `resolution`/`attenuation` untuk ADC; `frequency`/`resolution` untuk PWM;
  `min_us`/`max_us` untuk servo). Tambahkan field `params.mode`:
  `"simulated"` atau `"real"`.
- Mode `simulated`: `analog_input` ambil nilai dari `params.value` yang
  di-set lewat UI (slider di Drawflow editor — field baru di node
  template), BUKAN dari ADC fisik. `pwm_output`/`servo` di mode apapun
  cukup tampilkan angka Freq/Duty atau sudut di UI (belum perlu real
  hardware sampai 5b).
- Update `schema.md` (dari spec 03) untuk menambahkan 3 node type ini —
  jangan bikin dokumen skema kedua yang terpisah.
- Update Drawflow editor (spec 04): tambah slider untuk `analog_input`,
  indikator angka untuk `pwm_output`, indikator sudut untuk `servo`.

### 5b — Real mode (setelah 5a lolos dan stabil)
- Implementasi real: `analog_input` pakai ADC driver ESP-IDF
  (`adc_oneshot` API, bukan Arduino `analogRead`), `pwm_output` pakai LEDC
  driver, `servo` pakai LEDC dengan konversi angle→pulse width.
- `params.mode: "real"` mengaktifkan path hardware; kode simulated tetap
  ada (dual backend, bukan dihapus) — lihat [plan.md §3](../plan.md)
  "Dual backend".

**JANGAN kerjakan di spec ini:**
- Jangan mulai 5b sebelum 5a lolos kriteria selesai — ini pelanggaran
  prinsip #1 plan.md ("Level 0 dulu, tuntas").
- Jangan sentuh I2C/WiFi system variable (spec 06).

## File yang disentuh
```
components/fbd_core/
  fbd_nodes.h / .c      <- REVISI: tambah eval untuk ANALOG_IN, PWM_OUT, SERVO
  fbd_hw_backend.h       <- BARU: interface real vs simulated (function pointer per operasi)
  fbd_hw_real.c           <- BARU (5b): implementasi ESP-IDF adc_oneshot + ledc
  fbd_hw_sim.c            <- BARU (5a): implementasi simulated (baca dari params.value)
schema.md                <- REVISI: tambah 3 node type
components/web_ui/webroot/app.js <- REVISI: UI slider/indikator utk 3 node type
```

## Langkah kerja
1. **5a dulu:** desain `fbd_hw_backend.h` sebagai interface — misal:
   ```c
   typedef struct {
       fbd_value_t (*read_analog)(int pin, int resolution, int attenuation, const fbd_value_t *sim_value);
       void (*write_pwm)(int pin, uint32_t freq, uint32_t duty, uint32_t resolution);
       void (*write_servo)(int pin, float angle, uint32_t min_us, uint32_t max_us);
   } fbd_hw_backend_t;
   ```
   Node evaluate function pakai backend ini (dipilih dari `params.mode`
   sebelum dipanggil, bukan branch hardcoded di setiap tempat).
2. Implementasi `fbd_hw_sim.c`: `read_analog` return `*sim_value` langsung;
   `write_pwm`/`write_servo` cukup simpan angka ke `outputs[]` node untuk
   ditampilkan UI (tidak menyentuh register apa pun).
3. Uji rangkaian: `analog_input(simulated, value=2048)` → `SCALE` →
   `Compare > 50` → `digital_output` — set `value` lewat slider UI atau
   lewat `POST /api/program` manual, verifikasi `digital_output` berubah
   sesuai threshold.
4. **Baru setelah 5a lolos**, implementasi 5b: `fbd_hw_real.c` pakai
   `adc_oneshot_read()`, `ledc_set_duty()`, dst. Ganti `params.mode` jadi
   `"real"` pada node yang sama, uji dengan potensiometer fisik — hasil
   harus konsisten arah/skalanya dengan versi simulasi (nilai boleh beda
   presisi, tapi arah/skala logic harus sama).

## Kriteria selesai
- [ ] **5a:** rangkaian `analog_input(simulated) → SCALE → Compare > 50 →
      digital_output` jalan benar TANPA hardware ADC nyata tersambung.
- [ ] **5a:** slider di UI mengubah `params.value`, tersimpan lewat Save,
      muncul kembali setelah Load.
- [ ] **5b:** ganti `mode: "real"`, uji dengan potensiometer fisik — hasil
      logic konsisten (arah naik/turun sama) dengan hasil simulasi.
- [ ] **5b:** kode simulated (`fbd_hw_sim.c`) TIDAK dihapus, tetap bisa
      dipilih lewat `params.mode` — pembuktian dual backend nyata berfungsi,
      bukan cuma diganti total.

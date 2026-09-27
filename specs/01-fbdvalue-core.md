# Spec 1 — `FBDValue` Tagged Union + Node Level 0 (Host-only)

Ref: [plan.md §4](../plan.md), [§5 Level 0](../plan.md), [§8.4](../plan.md).
Sebelumnya: tidak ada (ini titik awal migrasi). Berikutnya:
[02-topo-sort-runtime.md](02-topo-sort-runtime.md).

## Scope
**Kerjakan:**
- Definisikan `FBDValue` (tagged union C, bukan C++ class) di komponen baru
  `fbd_core` — menggantikan `double out_value` polos di
  [logic_block_t](../components/logic_engine/include/logic_engine.h).
- Implementasikan evaluasi node Level 0 murni sebagai fungsi C biasa,
  dites di **host** (PC), tanpa ESP-IDF sama sekali:
  - Logic: AND, OR, NOT, XOR, NAND, NOR, Compare (satu fungsi generik
    dengan param operator, bukan 6 fungsi).
  - Data: Constant, Variable get/set (storage key-value global terpisah
    dari node).
  - Math: ADD, SUB, MUL, DIV, MIN, MAX, ABS, SCALE, CLAMP.
  - Timing: TON (sudah ada versi sederhana di v1 sebagai konsep timer,
    tapi tulis ulang pakai `FBDValue`), TOF, TP (one-shot), Counter (CTU).

**JANGAN kerjakan di spec ini:**
- Jangan sentuh `logic_engine.c`/`web_ui`/`wifi_mgr` yang lama — biarkan v1
  tetap jalan apa adanya sampai spec 02 selesai.
- Jangan bikin topological sort atau graph — itu spec 02.
- Jangan bikin apa pun yang menyentuh GPIO/hardware.

## File yang disentuh (baru)
```
components/fbd_core/
  include/fbd_value.h      <- definisi FBDValue + konversi
  include/fbd_nodes.h       <- signature fungsi evaluate per NodeType Level 0
  fbd_value.c
  fbd_nodes.c
  CMakeLists.txt            <- REQUIRES kosong, harus bisa dicompile host-only
test_host/
  fbd_core_test.c           <- program C murni, main() sendiri, tanpa Arduino/ESP-IDF
  build_and_run.sh atau .ps1 <- compile pakai gcc/cl langsung, TANPA idf.py
```

## Desain `FBDValue` (C, bukan C++)
```c
typedef enum { FBD_EMPTY=0, FBD_BOOL, FBD_INT32, FBD_FLOAT, FBD_BYTES } fbd_type_t;

typedef struct {
    fbd_type_t type;
    union {
        bool b;
        int32_t i;
        float f;
        struct { uint8_t data[8]; uint8_t len; } bytes;
    };
} fbd_value_t;

fbd_value_t fbd_make_bool(bool v);
fbd_value_t fbd_make_int(int32_t v);
fbd_value_t fbd_make_float(float v);
fbd_value_t fbd_make_bytes(const uint8_t *src, uint8_t len);
float fbd_to_float(fbd_value_t v);
bool  fbd_to_bool(fbd_value_t v);
```
Catatan: pastikan `sizeof(fbd_value_t)` tidak membengkak karena padding —
cek dengan `printf("%zu\n", sizeof(fbd_value_t))` di test host, harus di
sekitar 12-16 byte tergantung alignment platform, bukan puluhan byte.

## Langkah kerja
1. Buat `fbd_value.h`/`fbd_value.c` sesuai desain di atas.
2. Buat `fbd_nodes.h` dengan satu fungsi per operasi Level 0, signature
   seragam, misal:
   ```c
   fbd_value_t fbd_eval_and(fbd_value_t a, fbd_value_t b);
   fbd_value_t fbd_eval_compare(fbd_value_t a, fbd_value_t b, compare_op_t op);
   fbd_value_t fbd_eval_scale(fbd_value_t in, float in_min, float in_max, float out_min, float out_max);
   ```
   Untuk timer/counter (butuh state), pakai struct state terpisah yang
   di-passing sebagai pointer, misal:
   ```c
   typedef struct { uint32_t start_ms; bool running; bool prev_input; } ton_state_t;
   fbd_value_t fbd_eval_ton(fbd_value_t input, uint32_t delay_ms, uint32_t now_ms, ton_state_t *state);
   ```
3. Tulis `test_host/fbd_core_test.c`: rangkaian Constant → Math(ADD/SCALE) →
   Compare → TON, print hasil setiap langkah, assert manual (pakai
   `assert()` dari `<assert.h>` atau print PASS/FAIL).
4. Compile & jalankan di host: `gcc test_host/fbd_core_test.c components/fbd_core/fbd_value.c components/fbd_core/fbd_nodes.c -Icomponents/fbd_core/include -o test_host/fbd_test && ./test_host/fbd_test` (sesuaikan untuk PowerShell/MSVC bila `gcc` tidak ada — cek dulu `gcc --version`).

## Kriteria selesai
- [ ] Compile & run sukses di host, TANPA `idf.py`, tanpa ESP32 tersambung.
- [ ] `sizeof(fbd_value_t)` diprint dan masuk akal (dicatat di komentar test).
- [ ] Rangkaian Constant→Math→Compare→Timer menghasilkan nilai benar
      (dibuktikan lewat print PASS di test, bukan asumsi).
- [ ] TON: input ON, sebelum `delay_ms` output masih false, setelah lewat
      `delay_ms` output true — dites dengan simulasi `now_ms` manual
      (increment di loop test, bukan `sleep()` asli).
- [ ] Tidak ada alokasi heap (`malloc`/`new`) di manapun dalam `fbd_nodes.c`.

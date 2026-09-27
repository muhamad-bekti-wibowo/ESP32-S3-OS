# Spec 6 — Level 2: I2C Primitive + WiFi/mDNS sebagai System Variable

Ref: [plan.md §5 Level 2](../plan.md), Fase 5.
Sebelumnya: [05-level1-io.md](05-level1-io.md) (harus stabil & dipakai nyata,
sesuai prinsip #1 plan.md).
Berikutnya: [07-live-update.md](07-live-update.md).

## Scope
**Kerjakan, dua bagian independen:**

### 6a — I2C primitive register-level
- Tambah node type `i2c_write_reg`, `i2c_read_reg` ke `fbd_core` — HANYA
  primitive generik, dengan params `bus`, `address`, `register`, `data`
  (write) / `length` (read), output `raw_bytes` + `error` (BOOL).
- **Timeout wajib pendek (5-10ms)** pakai `i2c_master` driver ESP-IDF
  (`i2c_master_transmit`/`i2c_master_receive` dengan parameter timeout).
  Kalau NACK/timeout: set `error=true` + `raw_bytes` fallback (nilai
  kosong/terakhir), JANGAN blocking lebih dari timeout, JANGAN menahan
  scan cycle.
- Live monitor: nilai `raw_bytes`/`error` dari `i2c_read_reg` harus bisa
  dilihat lewat WebSocket (kalau WebSocket belum ada di `web_ui`, buat versi
  minimal dulu — jangan tunggu spec 07 untuk sekadar polling/push status).

### 6b — WiFi/DNS/mDNS sebagai read-only system variable
- **BUKAN node yang ditarik di canvas.** Implementasi sebagai storage
  global read-only yang diisi dari `wifi_mgr` (sudah ada), diakses lewat
  tipe node baru `sys_var_get` dengan params `name` bernilai salah satu:
  `SYS.WIFI_CONNECTED` (BOOL), `SYS.WIFI_RSSI` (INT32), `SYS.IP_ADDRESS`
  (BYTES/string), `SYS.HOSTNAME` (mDNS — perlu tambah `mdns` component
  ESP-IDF kalau belum ada).
- Tab "System > Network" terpisah di web UI untuk konfigurasi SSID/password/
  DHCP/hostname (simpan ke NVS) — pisah dari canvas Drawflow.

**JANGAN kerjakan di spec ini:**
- Jangan bikin node C++ /C baru untuk sensor spesifik (INA219, LCD1602,
  dst) — kalau butuh contoh sensor riil, komposisikan dari
  `i2c_read_reg`/`i2c_write_reg` di level graph JSON, bukan kode baru.
  Ini pelanggaran prinsip #2 plan.md kalau dilanggar.
- Jangan implementasi SPI/I2S apa pun (Level 3, ditunda total).

## File yang disentuh
```
components/fbd_core/
  fbd_nodes.h / .c        <- REVISI: I2C_READ_REG, I2C_WRITE_REG, SYS_VAR_GET
components/i2c_bridge/     <- BARU (opsional, atau taruh di fbd_core):
  i2c_bridge.h / .c        <- wrapper i2c_master dgn timeout pendek
components/wifi_mgr/
  wifi_mgr.h / .c          <- REVISI: expose getter RSSI/IP/connected utk sys var
components/sys_vars/        <- BARU: storage read-only key-value utk SYS.*
components/web_ui/
  web_ui.c                 <- REVISI: WebSocket endpoint /ws (kalau belum ada),
                                 tab System>Network baru di webroot
schema.md                   <- REVISI: tambah i2c_read_reg/i2c_write_reg/sys_var_get
```

## Langkah kerja
1. Cek dulu apakah `web_ui.c` sudah punya WebSocket (`esp_http_server`
   mendukung ws handler) — kalau belum, tambah handler `/ws` minimal
   (push JSON status tiap N cycle).
2. Implementasi `i2c_bridge` dengan `i2c_master_bus_add_device` +
   `i2c_master_transmit_receive` dengan `xfer_timeout_ms` kecil (5-10ms).
3. Uji 6a: sengaja lepas/putus device I2C di tengah scan cycle, verifikasi
   scan cycle TETAP jalan (log timestamp tiap cycle, tidak ada gap besar)
   dan `error=true` muncul di live monitor.
4. Implementasi `sys_vars` storage + getter dari `wifi_mgr` (RSSI via
   `esp_wifi_sta_get_ap_info`, IP via `esp_netif_get_ip_info`).
5. Uji 6b: rangkaian `sys_var_get(SYS.WIFI_RSSI)` → `Compare < -80` →
   `digital_output` (warning LED), matikan/jauhkan AP, verifikasi LED nyala.

## Kriteria selesai
- [ ] Baca 2 byte dari device I2C nyata lewat `i2c_read_reg`, nilai tampil
      benar di live monitor WebSocket — TANPA kode driver sensor spesifik
      ditulis.
- [ ] Putus salah satu device I2C secara sengaja → scan cycle TIDAK
      berhenti/telat, `error=true` muncul, sistem tetap responsif.
- [ ] `SYS.WIFI_CONNECTED`/`SYS.WIFI_RSSI` terbaca dan bisa dipakai dalam
      node Compare di graph, hasil logic benar.
- [ ] WiFi config (SSID/password/hostname) ada di tab UI terpisah dari
      canvas, tersimpan ke NVS, bukan node yang di-drag di Drawflow.

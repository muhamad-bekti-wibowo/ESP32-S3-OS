#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Tombol reset fisik: tahan BOOT (GPIO0) 5 detik -> hapus config WiFi di
 * NVS lalu reboot. LED WS2812 di GPIO48 jadi umpan balik:
 *   - tahan 1-5 detik : kuning kedip lambat (lepas = batal)
 *   - tahan >= 5 detik: merah kedip cepat, config dihapus, lepas tombol
 *                       untuk reboot
 * Program FBD, config Modbus dan HTTP Endpoint TIDAK disentuh.
 *
 * Panggil SEKALI dari app_main(), SEBELUM fbd_scan_task dibuat (task ini
 * menginisialisasi channel RMT pin 48 lebih dulu supaya tidak balapan
 * dengan node ws2812 pengguna di pin yang sama). */
void sys_reset_start(void);

#ifdef __cplusplus
}
#endif

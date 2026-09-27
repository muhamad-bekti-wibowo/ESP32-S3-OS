#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Mulai WiFi dalam mode Access Point sederhana.
 * SSID/password didefinisikan di wifi_mgr.c untuk versi awal ini. */
void wifi_mgr_start_ap(void);

/* Mulai WiFi mode APSTA: AP (ESP32-WebLogic) tetap aktif seperti biasa,
 * DITAMBAH koneksi STA ke jaringan rumah (SSID/password didefinisikan di
 * wifi_mgr.c). Tujuannya supaya PC dev bisa akses device lewat jaringan
 * rumah yang sama tanpa harus pindah koneksi WiFi ke AP device - AP
 * softAP ESP32 kadang kurang stabil untuk dev loop cepat (banyak
 * request berulang). Device bisa diakses lewat IP yang di-print ke log
 * serial setelah STA connect, atau tetap lewat 192.168.4.1 (AP). */
void wifi_mgr_start_apsta(void);

#ifdef __cplusplus
}
#endif

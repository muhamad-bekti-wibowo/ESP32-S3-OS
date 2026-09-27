#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

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

/* Getter read-only untuk SYS.* system variable (spec 06 - WiFi diakses
 * FBD hanya sebagai variable read-only, BUKAN node yang dikonfigurasi di
 * canvas). Semua getter ini aman dipanggil dari task mana pun, termasuk
 * fbd_scan_task, dan TIDAK melakukan blocking I/O jaringan - hanya baca
 * state WiFi driver yang sudah ada di memori. */
bool wifi_mgr_is_sta_connected(void);
/* RSSI dalam dBm (negatif, makin dekat ke 0 makin kuat). Return -127 kalau
 * STA belum connect (dianggap "sangat lemah", aman untuk Compare < -80). */
int32_t wifi_mgr_get_sta_rssi(void);
/* Tulis IP STA sebagai string "a.b.c.d" ke buf (minimal 16 byte). Return
 * false + buf="0.0.0.0" kalau STA belum connect. */
bool wifi_mgr_get_sta_ip(char *buf, size_t buf_len);

/* Tab "System > Network" (spec 06): konfigurasi SSID/password/hostname
 * WiFi STA, terpisah dari canvas Drawflow, disimpan ke NVS.
 *
 * wifi_mgr_get_sta_config(): SSID & hostname yang SEDANG dipakai (setelah
 * fallback kalau NVS kosong) - password TIDAK PERNAH diekspos lewat sini.
 * wifi_mgr_save_sta_config(): simpan config baru ke NVS. TIDAK langsung
 * diterapkan - device perlu reboot (idf.py monitor/reset fisik) supaya
 * wifi_mgr_start_apsta() membaca ulang dari NVS saat boot berikutnya.
 * Ini SENGAJA, bukan keterbatasan: mengganti config WiFi STA di tengah
 * jalan (re-init interface) berisiko memutus koneksi HTTP yang sedang
 * mengirim response "sukses" itu sendiri. */
void wifi_mgr_get_sta_config(char *ssid_buf, size_t ssid_len, char *hostname_buf, size_t hostname_len);
bool wifi_mgr_save_sta_config(const char *ssid, const char *password, const char *hostname);

#ifdef __cplusplus
}
#endif

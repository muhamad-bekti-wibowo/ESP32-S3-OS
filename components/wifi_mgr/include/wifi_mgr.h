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

/* Mode WiFi yang bisa dipilih (disimpan di NVS, berlaku setelah reboot):
 * - AP:    hanya Access Point (STA mati).
 * - STA:   hanya koneksi ke jaringan (AP mati). Kalau kredensial salah,
 *          perangkat tidak terjangkau lewat jaringan - pemulihan: tahan
 *          tombol BOOT 5 detik (lihat main/sys_reset.c).
 * - APSTA: AP + STA bersamaan (default, perilaku lama).
 * - AUTO:  mulai STA saja; kalau STA tidak tersambung >= 15 detik
 *          berturut-turut, AP dinyalakan otomatis (AP tetap hidup sampai
 *          reboot). */
typedef enum {
    WIFI_MGR_MODE_AP = 0,
    WIFI_MGR_MODE_STA,
    WIFI_MGR_MODE_APSTA,
    WIFI_MGR_MODE_AUTO,
} wifi_mgr_mode_t;

/* Mulai WiFi sesuai mode tersimpan (default APSTA). Tunggu koneksi STA
 * maks 10 detik kecuali mode AP. Device bisa diakses lewat IP STA yang
 * dicetak di log serial, atau lewat 192.168.4.1 kalau AP aktif. */
void wifi_mgr_start(void);

wifi_mgr_mode_t wifi_mgr_get_mode(void);
bool wifi_mgr_save_mode(wifi_mgr_mode_t mode);
const char *wifi_mgr_mode_to_str(wifi_mgr_mode_t mode);
bool wifi_mgr_mode_from_str(const char *s, wifi_mgr_mode_t *out);

/* Hapus SELURUH config WiFi di NVS (SSID/password/hostname STA dan mode).
 * Dipakai reset lewat tombol BOOT. Tidak menyentuh data lain. */
bool wifi_mgr_erase_config(void);

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
 * wifi_mgr_start() membaca ulang dari NVS saat boot berikutnya.
 * Ini SENGAJA, bukan keterbatasan: mengganti config WiFi STA di tengah
 * jalan (re-init interface) berisiko memutus koneksi HTTP yang sedang
 * mengirim response "sukses" itu sendiri. */
void wifi_mgr_get_sta_config(char *ssid_buf, size_t ssid_len, char *hostname_buf, size_t hostname_len);
bool wifi_mgr_save_sta_config(const char *ssid, const char *password, const char *hostname);

#ifdef __cplusplus
}
#endif

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Config port server httpd KEDUA (khusus node http_endpoint - lihat
 * schema.md), terpisah dari server utama (port 80, editor + /api/...).
 * Disimpan ke NVS supaya persisten setelah reboot, sama pola dengan
 * wifi_mgr_save_sta_config(). Port BARU baru berlaku setelah device
 * reboot (server kedua didaftarkan sekali saat boot, sama seperti
 * alasan node http_endpoint sendiri butuh reboot - esp_http_server
 * ESP-IDF tidak didesain untuk route/listener dinamis). */

#define ENDPOINT_MGR_DEFAULT_PORT 8080

/* Baca port yang TERSIMPAN di NVS (bukan yang sedang aktif kalau device
 * belum reboot sejak diubah) - fallback ENDPOINT_MGR_DEFAULT_PORT kalau
 * NVS belum pernah diisi. */
uint16_t endpoint_mgr_get_port(void);

/* Simpan port baru ke NVS. Return false kalau gagal tulis NVS. */
bool endpoint_mgr_save_port(uint16_t port);

#ifdef __cplusplus
}
#endif

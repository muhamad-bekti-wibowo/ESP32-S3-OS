#pragma once

#include "fbd_value.h"

#ifdef __cplusplus
extern "C" {
#endif

/* SYS.* read-only system variable (spec 06 - WiFi diakses FBD hanya lewat
 * ini, BUKAN node yang dikonfigurasi di canvas). fbd_core tidak boleh
 * depend langsung ke wifi_mgr (beda layer - fbd_core generik, wifi_mgr
 * spesifik proyek ini), jadi getter di-daftarkan lewat function pointer
 * dari main.c saat startup (fbd_sys_vars_set_provider()).
 *
 * Nama variable yang didukung (lihat schema.md):
 *   "SYS.WIFI_CONNECTED" -> FBD_BOOL
 *   "SYS.WIFI_RSSI"      -> FBD_INT32
 * Nama tidak dikenal -> FBD_EMPTY.
 *
 * SYS.IP_ADDRESS/SYS.HOSTNAME BELUM didukung lewat mekanisme ini: string
 * IP "255.255.255.255\0" (16 byte) tidak muat di fbd_value_t.bytes (maks
 * 8 byte, kontrak sejak spec 01 - sizeof(fbd_value_t)=16 total). Kalau
 * dibutuhkan nanti, exposed lewat endpoint HTTP terpisah (mis. GET
 * /api/status), bukan dipaksa lewat FBDValue yang didesain untuk nilai
 * scan-cycle kecil (bool/int32/float/8-byte register I2C). */
typedef fbd_value_t (*fbd_sys_var_provider_fn)(const char *name);

void fbd_sys_vars_set_provider(fbd_sys_var_provider_fn provider);
fbd_value_t fbd_sys_vars_get(const char *name);

#ifdef __cplusplus
}
#endif

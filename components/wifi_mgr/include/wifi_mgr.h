#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Mulai WiFi dalam mode Access Point sederhana.
 * SSID/password didefinisikan di wifi_mgr.c untuk versi awal ini. */
void wifi_mgr_start_ap(void);

#ifdef __cplusplus
}
#endif

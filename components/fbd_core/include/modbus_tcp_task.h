#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Task background yang benar-benar membuka koneksi TCP ke device Modbus
 * lain, mengambil pekerjaan dari modbus_tcp_bridge (lihat komentar
 * lengkap alasan async di modbus_tcp_bridge.h). Dipanggil SEKALI dari
 * main.c app_main(), berjalan selamanya di task-nya sendiri - TIDAK
 * pernah menahan fbd_scan_task. Aman dipanggil walau tidak ada node
 * modbus_tcp_read/write di graph manapun (task cuma idle, vTaskDelay,
 * tidak buka socket kalau tidak ada request masuk ke bridge). */
void modbus_tcp_task_start(void);

#ifdef __cplusplus
}
#endif

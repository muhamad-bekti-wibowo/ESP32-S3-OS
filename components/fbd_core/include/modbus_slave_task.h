#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Task background yang menjalankan Modbus RTU SLAVE di UART2 (RS485) -
 * ESP32 di sini jadi server, master eksternal (SCADA/PLC dkk) yang
 * inisiasi request. Dipanggil SEKALI dari main.c app_main() KALAU ada
 * minimal 1 node modbus_slave_reg di graph aktif (kalau tidak ada,
 * jangan panggil - hindari alokasi UART/task sia-sia, sama filosofi
 * dengan "server httpd kedua jangan selalu start" di web_ui.c).
 *
 * Pin UART2 default: TX=GPIO17, RX=GPIO16 (aman, bukan strapping/
 * USB-JTAG/PSRAM pin) - device RS485 TTL module (mis. MAX485) biasanya
 * TIDAK butuh pin DE/RE terpisah kalau modul auto-direction, tapi kalau
 * modul butuh kontrol arah manual, sambungkan DE+RE ke pin fixed
 * MODBUS_SLAVE_DE_PIN (lihat modbus_slave_task.c) - toggle otomatis
 * sebelum/sesudah kirim response. */
void modbus_slave_task_start(uint8_t slave_id, uint32_t baud_rate);

#ifdef __cplusplus
}
#endif

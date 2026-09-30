#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Config Modbus RTU slave (UART2/RS485) - slave_id dan baud_rate, mirip
 * pola endpoint_mgr.h (port server httpd kedua). Disimpan ke NVS supaya
 * persisten setelah reboot. Baru berlaku setelah device reboot (UART
 * driver diinstall sekali saat boot - lihat modbus_slave_task.h). */

#define MODBUS_SLAVE_MGR_DEFAULT_SLAVE_ID 1
#define MODBUS_SLAVE_MGR_DEFAULT_BAUD_RATE 9600

uint8_t modbus_slave_mgr_get_slave_id(void);
uint32_t modbus_slave_mgr_get_baud_rate(void);

/* Simpan config baru ke NVS. Return false kalau gagal tulis NVS. */
bool modbus_slave_mgr_save_config(uint8_t slave_id, uint32_t baud_rate);

#ifdef __cplusplus
}
#endif

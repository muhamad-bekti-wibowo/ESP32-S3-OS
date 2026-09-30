#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "fbd_graph.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Jembatan data antara task Modbus RTU slave (UART2/RS485, dibuat di
 * main.c - lihat modbus_slave_uart_bridge.h untuk driver UART-nya) dan
 * fbd_scan_task untuk node FBD_NODE_MODBUS_SLAVE_REG - pola SAMA dengan
 * http_endpoint_bridge.h: state TERPISAH dari fbd_graph_t/node->state,
 * diindeks dengan KEY STABIL (bukan pointer node), supaya aman terhadap
 * dual-buffer graph swap saat Save (lihat komentar lengkap alasannya di
 * http_endpoint_bridge.h).
 *
 * ESP32 di sini jadi SLAVE (server) - master Modbus RTU eksternal
 * (SCADA/PLC dkk) yang inisiasi request lewat RS485, task slave di
 * firmware ini cuma MERESPONS. Beda arah dari modbus_tcp_bridge.h (ESP32
 * jadi client/master di sana).
 *
 * Key slot = "reg_type:address" (mis. "holding:100") - kombinasi ini
 * yang harus UNIK di seluruh graph (dicek saat compile/validasi, 2 node
 * modbus_slave_reg dengan address+type sama = konflik, DITOLAK). */

#define MODBUS_SLAVE_BRIDGE_MAX_SLOTS 32
#define MODBUS_SLAVE_BRIDGE_KEY_LEN 24

void modbus_slave_bridge_init(void);

void modbus_slave_bridge_make_key(char *out_key, fbd_modbus_reg_type_t reg_type, uint16_t address);

/* Dua nilai TERPISAH per slot - JANGAN digabung jadi satu field, supaya
 * arah baca dan arah tulis tidak saling menimpa:
 * - "to_master"   : ditulis evaluate_node() dari inputs[0] node (nilai
 *                   dari node LAIN di graph), dibaca task slave UART
 *                   saat master minta FC03/FC01/FC02/FC04.
 * - "from_master" : ditulis task slave UART saat master kirim FC06/
 *                   FC16/FC05/FC15, dibaca evaluate_node() jadi
 *                   outputs[0] node di scan cycle berikutnya.
 * Kalau node TIDAK punya inputs[0] tersambung, "to_master" tetap nilai
 * TERAKHIR dari "from_master" (lihat evaluate_node() FBD_NODE_MODBUS_
 * SLAVE_REG) - jadi register itu berfungsi seperti "holding register
 * biasa" yang bisa ditulis DUA arah (dari graph ATAU dari master),
 * bukan cuma read-only atau write-only. */

/* Dipanggil evaluate_node() (fbd_graph.c) tiap scan cycle untuk node
 * modbus_slave_reg - tulis nilai yang akan DIBACA master luar lewat
 * RS485 (dari inputs[0] node, kalau tersambung ke node lain di graph). */
void modbus_slave_bridge_set_to_master(const char *key, uint16_t value);

/* Dipanggil task slave UART (saat master kirim request baca FC03/FC01
 * dkk) - baca nilai TERBARU untuk key ini. Return false kalau key belum
 * pernah didaftarkan node modbus_slave_reg mana pun di graph aktif
 * (task slave balas exception "illegal data address" di kasus ini). */
bool modbus_slave_bridge_get_to_master(const char *key, uint16_t *out_value);

/* Dipanggil task slave UART (saat master kirim request TULIS FC06/FC16/
 * FC05/FC15) - tulis nilai dari master ke slot. Return false kalau key
 * belum didaftarkan (task slave balas exception, tidak ada penulisan). */
bool modbus_slave_bridge_set_from_master(const char *key, uint16_t value);

/* Dipanggil evaluate_node() tiap scan cycle - baca nilai TERAKHIR yang
 * ditulis master (dari modbus_slave_bridge_set_from_master()), jadi
 * outputs[0] node ini. 0 kalau master belum pernah menulis. */
uint16_t modbus_slave_bridge_get_from_master(const char *key);

/* Dipanggil evaluate_node() setiap scan cycle untuk "mendaftarkan" bahwa
 * key ini AKTIF di graph saat ini (dipanggil di awal evaluate_node()
 * SEBELUM baca/tulis value) - supaya modbus_slave_bridge_get_value()
 * tahu membedakan "key belum pernah ada node-nya" vs "key ada nodenya
 * tapi belum pernah dieksekusi". Slot dibuat otomatis kalau belum ada. */
void modbus_slave_bridge_mark_active(const char *key);

#ifdef __cplusplus
}
#endif

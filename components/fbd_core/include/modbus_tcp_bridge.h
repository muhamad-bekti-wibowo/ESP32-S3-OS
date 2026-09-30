#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "fbd_graph.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Jembatan async antara fbd_scan_task (evaluate_node(), main.c) dan task
 * background terpisah (modbus_tcp_task, dibuat di main.c) yang benar-benar
 * membuka koneksi TCP ke device Modbus lain - pola SAMA dengan
 * http_endpoint_bridge.h (lihat komentar lengkap di sana untuk alasan
 * dual-buffer graph swap).
 *
 * KENAPA TIDAK request TCP langsung di evaluate_node(): koneksi/response
 * time device Modbus TCP remote lewat WiFi/LAN TIDAK bisa dijamin cepat
 * seperti I2C lokal (yang selalu <8ms). Kalau evaluate_node() nunggu
 * socket connect/recv langsung, satu device lambat/network putus bisa
 * membuat scan cycle molor jauh dari target 20ms, menunda SEMUA node lain
 * di graph (bukan cuma node Modbus itu). Solusi: evaluate_node() cuma
 * TULIS "tolong minta data ini" ke slot bridge (non-blocking, langsung
 * return), lalu BACA hasil TERAKHIR yang sudah ada di slot (mungkin dari
 * request beberapa cycle sebelumnya) - modbus_tcp_task yang benar-benar
 * blocking nunggu response, jalan di task-nya sendiri, tidak pernah
 * menahan fbd_scan_task.
 *
 * Konsekuensi: outputs node modbus_tcp_read TIDAK realtime per-cycle
 * seperti node lokal (I2C/GPIO) - nilainya "hasil polling terakhir",
 * bisa beberapa scan cycle basi tergantung seberapa cepat device Modbus
 * merespons. Ini trade-off yang disengaja demi keamanan scan cycle.
 *
 * Slot diindeks dengan STRING KEY gabungan
 * "ip:port:unit_id:reg_type:address:count" (bukan pointer node, bukan id
 * node) - stabil walau graph di-Save/di-swap ulang, DAN otomatis
 * membedakan dua node yang minta request berbeda ke device yang sama. */

#define MODBUS_TCP_BRIDGE_MAX_SLOTS 16
#define MODBUS_TCP_BRIDGE_KEY_LEN 48

void modbus_tcp_bridge_init(void);

/* Bangun key slot dari parameter request - dipakai baik oleh
 * evaluate_node() (fbd_graph.c) maupun modbus_tcp_task (main.c) supaya
 * selalu konsisten. out_key harus buffer minimal MODBUS_TCP_BRIDGE_KEY_LEN. */
void modbus_tcp_bridge_make_key(char *out_key, const char *ip, uint16_t port,
                                 uint8_t unit_id, fbd_modbus_reg_type_t reg_type,
                                 uint16_t address, uint8_t count);

/* Dipanggil evaluate_node() (fbd_graph.c) tiap scan cycle untuk node
 * modbus_tcp_read - "ajukan" request ini (dibuat slot kalau belum ada,
 * ditandai need_request=true supaya modbus_tcp_task tahu ada permintaan
 * aktif). TIDAK blocking, TIDAK langsung mengirim apa pun ke network. */
void modbus_tcp_bridge_request_read(const char *key, const char *ip, uint16_t port,
                                     uint8_t unit_id, fbd_modbus_reg_type_t reg_type,
                                     uint16_t address, uint8_t count);

/* Dipanggil evaluate_node() untuk node modbus_tcp_write - sama pola
 * dengan request_read, tapi bawa juga nilai yang mau ditulis. write_values
 * dipakai untuk holding register (uint16 per elemen), write_coil untuk
 * coil tunggal (true/false), tergantung reg_type. */
void modbus_tcp_bridge_request_write(const char *key, const char *ip, uint16_t port,
                                      uint8_t unit_id, fbd_modbus_reg_type_t reg_type,
                                      uint16_t address, uint16_t write_value, bool write_coil);

typedef struct {
    uint16_t values[FBD_MODBUS_MAX_COUNT]; /* holding/input register, atau 0/1 per coil/discrete */
    uint8_t count;
    bool ok;         /* true kalau request TERAKHIR ke slot ini sukses */
    bool ever_ran;   /* false kalau belum pernah ada response sama sekali (slot baru) */
    uint32_t revision; /* naik tiap kali modbus_tcp_task menulis hasil baru */
} modbus_tcp_bridge_result_t;

/* Dipanggil evaluate_node() untuk baca hasil TERAKHIR (non-blocking,
 * langsung return apa pun yang ada). */
modbus_tcp_bridge_result_t modbus_tcp_bridge_get_result(const char *key);

/* Dipanggil modbus_tcp_task (main.c) - ambil SATU slot yang butuh request
 * (need_request=true), tandai sedang diproses (need_request=false) supaya
 * tidak diambil task lain/diulang cycle berikutnya sampai ada permintaan
 * baru dari evaluate_node(). Return false kalau tidak ada slot yang butuh
 * request saat ini (task boleh vTaskDelay lalu coba lagi). out_key harus
 * buffer minimal MODBUS_TCP_BRIDGE_KEY_LEN. */
typedef struct {
    char key[MODBUS_TCP_BRIDGE_KEY_LEN];
    char ip[16];
    uint16_t port;
    uint8_t unit_id;
    fbd_modbus_reg_type_t reg_type;
    uint16_t address;
    uint8_t count;
    bool is_write;
    uint16_t write_value;
    bool write_coil;
} modbus_tcp_bridge_pending_request_t;

bool modbus_tcp_bridge_take_pending(modbus_tcp_bridge_pending_request_t *out_req);

/* Dipanggil modbus_tcp_task setelah request selesai (sukses atau gagal) -
 * tulis hasil ke slot, naikkan revision. */
void modbus_tcp_bridge_set_result(const char *key, const uint16_t *values, uint8_t count, bool ok);

#ifdef __cplusplus
}
#endif

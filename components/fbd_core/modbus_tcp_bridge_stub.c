/* Stub modbus_tcp_bridge untuk test host (PC) - TIDAK ada task/FreeRTOS
 * sungguhan, jadi tidak ada socket yang benar-benar terbuka. Perilaku:
 * request SELALU "belum pernah direspons" (ever_ran=false, ok=false) -
 * cukup untuk fbd_graph_test.c memverifikasi bahwa evaluate_node()
 * membaca hasil dari bridge dengan benar (bukan menguji network
 * sungguhan, yang butuh hardware/koneksi asli). */
#include "modbus_tcp_bridge.h"
#include <string.h>
#include <stdio.h>

typedef struct {
    char key[MODBUS_TCP_BRIDGE_KEY_LEN];
    bool in_use;
    modbus_tcp_bridge_pending_request_t pending;
    bool need_request;
    modbus_tcp_bridge_result_t result;
} modbus_tcp_slot_t;

static modbus_tcp_slot_t s_slots[MODBUS_TCP_BRIDGE_MAX_SLOTS];

void modbus_tcp_bridge_init(void)
{
    memset(s_slots, 0, sizeof(s_slots));
}

void modbus_tcp_bridge_make_key(char *out_key, const char *ip, uint16_t port,
                                 uint8_t unit_id, fbd_modbus_reg_type_t reg_type,
                                 uint16_t address, uint8_t count)
{
    snprintf(out_key, MODBUS_TCP_BRIDGE_KEY_LEN, "%s:%u:%u:%d:%u:%u",
             ip, (unsigned)port, (unsigned)unit_id, (int)reg_type,
             (unsigned)address, (unsigned)count);
}

static modbus_tcp_slot_t *find_or_create_slot(const char *key)
{
    for (int i = 0; i < MODBUS_TCP_BRIDGE_MAX_SLOTS; ++i) {
        if (s_slots[i].in_use && strncmp(s_slots[i].key, key, MODBUS_TCP_BRIDGE_KEY_LEN) == 0) {
            return &s_slots[i];
        }
    }
    for (int i = 0; i < MODBUS_TCP_BRIDGE_MAX_SLOTS; ++i) {
        if (!s_slots[i].in_use) {
            s_slots[i].in_use = true;
            strncpy(s_slots[i].key, key, MODBUS_TCP_BRIDGE_KEY_LEN - 1);
            return &s_slots[i];
        }
    }
    return NULL;
}

void modbus_tcp_bridge_request_read(const char *key, const char *ip, uint16_t port,
                                     uint8_t unit_id, fbd_modbus_reg_type_t reg_type,
                                     uint16_t address, uint8_t count)
{
    modbus_tcp_slot_t *slot = find_or_create_slot(key);
    if (!slot) return;
    slot->need_request = true;
    slot->pending.is_write = false;
    strncpy(slot->pending.key, key, MODBUS_TCP_BRIDGE_KEY_LEN - 1);
    strncpy(slot->pending.ip, ip, sizeof(slot->pending.ip) - 1);
    slot->pending.port = port;
    slot->pending.unit_id = unit_id;
    slot->pending.reg_type = reg_type;
    slot->pending.address = address;
    slot->pending.count = count;
}

void modbus_tcp_bridge_request_write(const char *key, const char *ip, uint16_t port,
                                      uint8_t unit_id, fbd_modbus_reg_type_t reg_type,
                                      uint16_t address, uint16_t write_value, bool write_coil)
{
    modbus_tcp_slot_t *slot = find_or_create_slot(key);
    if (!slot) return;
    slot->need_request = true;
    slot->pending.is_write = true;
    strncpy(slot->pending.key, key, MODBUS_TCP_BRIDGE_KEY_LEN - 1);
    strncpy(slot->pending.ip, ip, sizeof(slot->pending.ip) - 1);
    slot->pending.port = port;
    slot->pending.unit_id = unit_id;
    slot->pending.reg_type = reg_type;
    slot->pending.address = address;
    slot->pending.write_value = write_value;
    slot->pending.write_coil = write_coil;
}

modbus_tcp_bridge_result_t modbus_tcp_bridge_get_result(const char *key)
{
    modbus_tcp_slot_t *slot = find_or_create_slot(key);
    if (!slot) {
        modbus_tcp_bridge_result_t empty = {0};
        return empty;
    }
    return slot->result;
}

bool modbus_tcp_bridge_take_pending(modbus_tcp_bridge_pending_request_t *out_req)
{
    for (int i = 0; i < MODBUS_TCP_BRIDGE_MAX_SLOTS; ++i) {
        if (s_slots[i].in_use && s_slots[i].need_request) {
            s_slots[i].need_request = false;
            *out_req = s_slots[i].pending;
            return true;
        }
    }
    return false;
}

void modbus_tcp_bridge_set_result(const char *key, const uint16_t *values, uint8_t count, bool ok)
{
    modbus_tcp_slot_t *slot = find_or_create_slot(key);
    if (!slot) return;
    uint8_t n = count;
    if (n > FBD_MODBUS_MAX_COUNT) n = FBD_MODBUS_MAX_COUNT;
    memcpy(slot->result.values, values, n * sizeof(uint16_t));
    slot->result.count = n;
    slot->result.ok = ok;
    slot->result.ever_ran = true;
    slot->result.revision++;
}

/* Stub modbus_slave_bridge untuk test host (PC) - TIDAK ada UART/RS485
 * sungguhan, jadi tidak ada master eksternal yang benar-benar mengakses
 * slot ini. Perilaku baca/tulis slot tetap identik dengan versi firmware
 * (single-threaded, tanpa critical section - cukup untuk
 * fbd_graph_test.c). */
#include "modbus_slave_bridge.h"
#include <string.h>
#include <stdio.h>

typedef struct {
    char key[MODBUS_SLAVE_BRIDGE_KEY_LEN];
    bool in_use;
    uint16_t to_master;
    uint16_t from_master;
} modbus_slave_slot_t;

static modbus_slave_slot_t s_slots[MODBUS_SLAVE_BRIDGE_MAX_SLOTS];

void modbus_slave_bridge_init(void)
{
    memset(s_slots, 0, sizeof(s_slots));
}

void modbus_slave_bridge_make_key(char *out_key, fbd_modbus_reg_type_t reg_type, uint16_t address)
{
    snprintf(out_key, MODBUS_SLAVE_BRIDGE_KEY_LEN, "%d:%u", (int)reg_type, (unsigned)address);
}

static modbus_slave_slot_t *find_or_create_slot(const char *key)
{
    for (int i = 0; i < MODBUS_SLAVE_BRIDGE_MAX_SLOTS; ++i) {
        if (s_slots[i].in_use && strncmp(s_slots[i].key, key, MODBUS_SLAVE_BRIDGE_KEY_LEN) == 0) {
            return &s_slots[i];
        }
    }
    for (int i = 0; i < MODBUS_SLAVE_BRIDGE_MAX_SLOTS; ++i) {
        if (!s_slots[i].in_use) {
            s_slots[i].in_use = true;
            strncpy(s_slots[i].key, key, MODBUS_SLAVE_BRIDGE_KEY_LEN - 1);
            return &s_slots[i];
        }
    }
    return NULL;
}

void modbus_slave_bridge_mark_active(const char *key)
{
    find_or_create_slot(key);
}

void modbus_slave_bridge_set_to_master(const char *key, uint16_t value)
{
    modbus_slave_slot_t *slot = find_or_create_slot(key);
    if (slot) slot->to_master = value;
}

bool modbus_slave_bridge_get_to_master(const char *key, uint16_t *out_value)
{
    for (int i = 0; i < MODBUS_SLAVE_BRIDGE_MAX_SLOTS; ++i) {
        if (s_slots[i].in_use && strncmp(s_slots[i].key, key, MODBUS_SLAVE_BRIDGE_KEY_LEN) == 0) {
            *out_value = s_slots[i].to_master;
            return true;
        }
    }
    *out_value = 0;
    return false;
}

bool modbus_slave_bridge_set_from_master(const char *key, uint16_t value)
{
    for (int i = 0; i < MODBUS_SLAVE_BRIDGE_MAX_SLOTS; ++i) {
        if (s_slots[i].in_use && strncmp(s_slots[i].key, key, MODBUS_SLAVE_BRIDGE_KEY_LEN) == 0) {
            s_slots[i].from_master = value;
            return true;
        }
    }
    return false;
}

uint16_t modbus_slave_bridge_get_from_master(const char *key)
{
    for (int i = 0; i < MODBUS_SLAVE_BRIDGE_MAX_SLOTS; ++i) {
        if (s_slots[i].in_use && strncmp(s_slots[i].key, key, MODBUS_SLAVE_BRIDGE_KEY_LEN) == 0) {
            return s_slots[i].from_master;
        }
    }
    return 0;
}

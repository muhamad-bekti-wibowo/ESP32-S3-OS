/* Slot table modbus_slave_bridge (firmware) - dua nilai per slot
 * (to_master/from_master), lihat penjelasan lengkap kenapa TERPISAH di
 * modbus_slave_bridge.h. Thread-safety: portENTER_CRITICAL/
 * portEXIT_CRITICAL, pola sama dengan http_endpoint_bridge.c/
 * modbus_tcp_bridge.c. */
#include "modbus_slave_bridge.h"
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"

typedef struct {
    char key[MODBUS_SLAVE_BRIDGE_KEY_LEN];
    bool in_use;
    uint16_t to_master;
    uint16_t from_master;
} modbus_slave_slot_t;

static modbus_slave_slot_t s_slots[MODBUS_SLAVE_BRIDGE_MAX_SLOTS];
static portMUX_TYPE s_spinlock = portMUX_INITIALIZER_UNLOCKED;

void modbus_slave_bridge_init(void)
{
    portENTER_CRITICAL(&s_spinlock);
    memset(s_slots, 0, sizeof(s_slots));
    portEXIT_CRITICAL(&s_spinlock);
}

void modbus_slave_bridge_make_key(char *out_key, fbd_modbus_reg_type_t reg_type, uint16_t address)
{
    snprintf(out_key, MODBUS_SLAVE_BRIDGE_KEY_LEN, "%d:%u", (int)reg_type, (unsigned)address);
}

static modbus_slave_slot_t *find_or_create_slot_locked(const char *key)
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
    portENTER_CRITICAL(&s_spinlock);
    find_or_create_slot_locked(key);
    portEXIT_CRITICAL(&s_spinlock);
}

void modbus_slave_bridge_set_to_master(const char *key, uint16_t value)
{
    portENTER_CRITICAL(&s_spinlock);
    modbus_slave_slot_t *slot = find_or_create_slot_locked(key);
    if (slot) slot->to_master = value;
    portEXIT_CRITICAL(&s_spinlock);
}

bool modbus_slave_bridge_get_to_master(const char *key, uint16_t *out_value)
{
    bool found = false;
    portENTER_CRITICAL(&s_spinlock);
    for (int i = 0; i < MODBUS_SLAVE_BRIDGE_MAX_SLOTS; ++i) {
        if (s_slots[i].in_use && strncmp(s_slots[i].key, key, MODBUS_SLAVE_BRIDGE_KEY_LEN) == 0) {
            *out_value = s_slots[i].to_master;
            found = true;
            break;
        }
    }
    portEXIT_CRITICAL(&s_spinlock);
    if (!found) *out_value = 0;
    return found;
}

bool modbus_slave_bridge_set_from_master(const char *key, uint16_t value)
{
    bool found = false;
    portENTER_CRITICAL(&s_spinlock);
    for (int i = 0; i < MODBUS_SLAVE_BRIDGE_MAX_SLOTS; ++i) {
        if (s_slots[i].in_use && strncmp(s_slots[i].key, key, MODBUS_SLAVE_BRIDGE_KEY_LEN) == 0) {
            s_slots[i].from_master = value;
            found = true;
            break;
        }
    }
    portEXIT_CRITICAL(&s_spinlock);
    return found;
}

uint16_t modbus_slave_bridge_get_from_master(const char *key)
{
    uint16_t value = 0;
    portENTER_CRITICAL(&s_spinlock);
    for (int i = 0; i < MODBUS_SLAVE_BRIDGE_MAX_SLOTS; ++i) {
        if (s_slots[i].in_use && strncmp(s_slots[i].key, key, MODBUS_SLAVE_BRIDGE_KEY_LEN) == 0) {
            value = s_slots[i].from_master;
            break;
        }
    }
    portEXIT_CRITICAL(&s_spinlock);
    return value;
}

/* Slot table modbus_tcp_bridge - dipakai baik firmware (dgn task TCP
 * sungguhan di main.c, lihat modbus_tcp_task) maupun bisa dikompilasi ke
 * test host TANPA FreeRTOS beneran (portENTER_CRITICAL/portEXIT_CRITICAL
 * dari freertos/FreeRTOS.h tetap dibutuhkan header-nya - makanya file ini
 * HANYA dipakai firmware, test host pakai modbus_tcp_bridge_stub.c yang
 * tidak butuh FreeRTOS sama sekali). Thread-safety sama seperti
 * http_endpoint_bridge.c: portENTER_CRITICAL/portEXIT_CRITICAL (disable
 * interrupt sesaat), bukan mutex/semaphore - cukup untuk baca/tulis
 * beberapa field kecil per slot. */
#include "modbus_tcp_bridge.h"
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"

typedef struct {
    char key[MODBUS_TCP_BRIDGE_KEY_LEN];
    bool in_use;
    modbus_tcp_bridge_pending_request_t pending;
    bool need_request;
    modbus_tcp_bridge_result_t result;
} modbus_tcp_slot_t;

static modbus_tcp_slot_t s_slots[MODBUS_TCP_BRIDGE_MAX_SLOTS];
static portMUX_TYPE s_spinlock = portMUX_INITIALIZER_UNLOCKED;

void modbus_tcp_bridge_init(void)
{
    portENTER_CRITICAL(&s_spinlock);
    memset(s_slots, 0, sizeof(s_slots));
    portEXIT_CRITICAL(&s_spinlock);
}

void modbus_tcp_bridge_make_key(char *out_key, const char *ip, uint16_t port,
                                 uint8_t unit_id, fbd_modbus_reg_type_t reg_type,
                                 uint16_t address, uint8_t count)
{
    snprintf(out_key, MODBUS_TCP_BRIDGE_KEY_LEN, "%s:%u:%u:%d:%u:%u",
             ip, (unsigned)port, (unsigned)unit_id, (int)reg_type,
             (unsigned)address, (unsigned)count);
}

/* Dipanggil dari DALAM critical section - tidak boleh masuk critical
 * section lagi (spinlock ESP-IDF tidak reentrant). */
static modbus_tcp_slot_t *find_or_create_slot_locked(const char *key)
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
    return NULL; /* kapasitas penuh - caller abaikan (tidak fatal) */
}

void modbus_tcp_bridge_request_read(const char *key, const char *ip, uint16_t port,
                                     uint8_t unit_id, fbd_modbus_reg_type_t reg_type,
                                     uint16_t address, uint8_t count)
{
    portENTER_CRITICAL(&s_spinlock);
    modbus_tcp_slot_t *slot = find_or_create_slot_locked(key);
    if (slot) {
        slot->need_request = true;
        slot->pending.is_write = false;
        strncpy(slot->pending.key, key, MODBUS_TCP_BRIDGE_KEY_LEN - 1);
        strncpy(slot->pending.ip, ip, sizeof(slot->pending.ip) - 1);
        slot->pending.ip[sizeof(slot->pending.ip) - 1] = '\0';
        slot->pending.port = port;
        slot->pending.unit_id = unit_id;
        slot->pending.reg_type = reg_type;
        slot->pending.address = address;
        slot->pending.count = count;
    }
    portEXIT_CRITICAL(&s_spinlock);
}

void modbus_tcp_bridge_request_write(const char *key, const char *ip, uint16_t port,
                                      uint8_t unit_id, fbd_modbus_reg_type_t reg_type,
                                      uint16_t address, uint16_t write_value, bool write_coil)
{
    portENTER_CRITICAL(&s_spinlock);
    modbus_tcp_slot_t *slot = find_or_create_slot_locked(key);
    if (slot) {
        slot->need_request = true;
        slot->pending.is_write = true;
        strncpy(slot->pending.key, key, MODBUS_TCP_BRIDGE_KEY_LEN - 1);
        strncpy(slot->pending.ip, ip, sizeof(slot->pending.ip) - 1);
        slot->pending.ip[sizeof(slot->pending.ip) - 1] = '\0';
        slot->pending.port = port;
        slot->pending.unit_id = unit_id;
        slot->pending.reg_type = reg_type;
        slot->pending.address = address;
        slot->pending.write_value = write_value;
        slot->pending.write_coil = write_coil;
    }
    portEXIT_CRITICAL(&s_spinlock);
}

modbus_tcp_bridge_result_t modbus_tcp_bridge_get_result(const char *key)
{
    modbus_tcp_bridge_result_t result = {0};
    portENTER_CRITICAL(&s_spinlock);
    modbus_tcp_slot_t *slot = find_or_create_slot_locked(key);
    if (slot) {
        result = slot->result;
    }
    portEXIT_CRITICAL(&s_spinlock);
    return result;
}

bool modbus_tcp_bridge_take_pending(modbus_tcp_bridge_pending_request_t *out_req)
{
    bool found = false;
    portENTER_CRITICAL(&s_spinlock);
    for (int i = 0; i < MODBUS_TCP_BRIDGE_MAX_SLOTS; ++i) {
        if (s_slots[i].in_use && s_slots[i].need_request) {
            s_slots[i].need_request = false;
            *out_req = s_slots[i].pending;
            found = true;
            break;
        }
    }
    portEXIT_CRITICAL(&s_spinlock);
    return found;
}

void modbus_tcp_bridge_set_result(const char *key, const uint16_t *values, uint8_t count, bool ok)
{
    portENTER_CRITICAL(&s_spinlock);
    modbus_tcp_slot_t *slot = find_or_create_slot_locked(key);
    if (slot) {
        uint8_t n = count;
        if (n > FBD_MODBUS_MAX_COUNT) n = FBD_MODBUS_MAX_COUNT;
        memcpy(slot->result.values, values, n * sizeof(uint16_t));
        slot->result.count = n;
        slot->result.ok = ok;
        slot->result.ever_ran = true;
        slot->result.revision++;
    }
    portEXIT_CRITICAL(&s_spinlock);
}

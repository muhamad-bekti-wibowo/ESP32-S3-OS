#include "modbus_slave_mgr.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

#define NVS_NAMESPACE "modbus_slv_cfg"
#define NVS_KEY_SLAVE_ID "slave_id"
#define NVS_KEY_BAUD "baud"

static const char *TAG = "modbus_slave_mgr";

uint8_t modbus_slave_mgr_get_slave_id(void)
{
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return MODBUS_SLAVE_MGR_DEFAULT_SLAVE_ID;
    }
    uint8_t slave_id = MODBUS_SLAVE_MGR_DEFAULT_SLAVE_ID;
    if (nvs_get_u8(handle, NVS_KEY_SLAVE_ID, &slave_id) != ESP_OK) {
        slave_id = MODBUS_SLAVE_MGR_DEFAULT_SLAVE_ID;
    }
    nvs_close(handle);
    return slave_id;
}

uint32_t modbus_slave_mgr_get_baud_rate(void)
{
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return MODBUS_SLAVE_MGR_DEFAULT_BAUD_RATE;
    }
    uint32_t baud = MODBUS_SLAVE_MGR_DEFAULT_BAUD_RATE;
    if (nvs_get_u32(handle, NVS_KEY_BAUD, &baud) != ESP_OK) {
        baud = MODBUS_SLAVE_MGR_DEFAULT_BAUD_RATE;
    }
    nvs_close(handle);
    return baud;
}

bool modbus_slave_mgr_save_config(uint8_t slave_id, uint32_t baud_rate)
{
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        ESP_LOGE(TAG, "gagal buka NVS untuk simpan config modbus slave");
        return false;
    }
    esp_err_t err = nvs_set_u8(handle, NVS_KEY_SLAVE_ID, slave_id);
    err |= nvs_set_u32(handle, NVS_KEY_BAUD, baud_rate);
    err |= nvs_commit(handle);
    nvs_close(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gagal simpan config modbus slave ke NVS");
        return false;
    }
    ESP_LOGI(TAG, "config modbus slave disimpan ke NVS (slave_id=%u, baud=%u). Reboot device untuk menerapkan.",
             slave_id, (unsigned)baud_rate);
    return true;
}

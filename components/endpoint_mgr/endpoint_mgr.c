#include "endpoint_mgr.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

#define NVS_NAMESPACE "endpoint_cfg"
#define NVS_KEY_PORT "port"

static const char *TAG = "endpoint_mgr";

uint16_t endpoint_mgr_get_port(void)
{
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return ENDPOINT_MGR_DEFAULT_PORT;
    }
    uint16_t port = ENDPOINT_MGR_DEFAULT_PORT;
    if (nvs_get_u16(handle, NVS_KEY_PORT, &port) != ESP_OK) {
        port = ENDPOINT_MGR_DEFAULT_PORT;
    }
    nvs_close(handle);
    return port;
}

bool endpoint_mgr_save_port(uint16_t port)
{
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        ESP_LOGE(TAG, "gagal buka NVS untuk simpan port endpoint");
        return false;
    }
    esp_err_t err = nvs_set_u16(handle, NVS_KEY_PORT, port);
    err |= nvs_commit(handle);
    nvs_close(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gagal simpan port endpoint ke NVS");
        return false;
    }
    ESP_LOGI(TAG, "port endpoint disimpan ke NVS (%u). Reboot device untuk menerapkan.", port);
    return true;
}

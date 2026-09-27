/* Implementasi I2C primitive nyata (ESP-IDF i2c_master driver). Hanya
 * dikompilasi sebagai bagian firmware ESP32-S3 - TIDAK dikompilasi untuk
 * test host (lihat i2c_bridge_stub.c & test_host/build_and_run.ps1). */
#include "i2c_bridge.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "i2c_bridge";

/* Cache dev_handle per (bus, address) - i2c_master_bus_add_device() mahal
 * kalau dipanggil tiap scan cycle (~20ms), jadi disimpan sekali per alamat
 * yang pernah dipakai. Hanya bus 0 didukung untuk sekarang (ESP32-S3 punya
 * 2 controller I2C fisik, tapi proyek ini baru pakai satu). */
#define I2C_BRIDGE_MAX_DEVICES 8

static i2c_master_bus_handle_t s_bus_handle = NULL;
static struct {
    uint8_t address;
    i2c_master_dev_handle_t handle;
    bool inited;
} s_devices[I2C_BRIDGE_MAX_DEVICES];
static int s_device_count = 0;

/* Pin default bus I2C - SDA=GPIO8, SCL=GPIO9 (aman, bukan strapping/USB-
 * JTAG/PSRAM pin, lihat schema.md aturan validasi pin GPIO real). */
#define I2C_BRIDGE_SDA_PIN 8
#define I2C_BRIDGE_SCL_PIN 9

static bool ensure_bus_init(void)
{
    if (s_bus_handle) {
        return true;
    }
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = 0,
        .sda_io_num = I2C_BRIDGE_SDA_PIN,
        .scl_io_num = I2C_BRIDGE_SCL_PIN,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t err = i2c_new_master_bus(&bus_cfg, &s_bus_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gagal init I2C bus: %d", err);
        return false;
    }
    return true;
}

static i2c_master_dev_handle_t get_or_add_device(uint8_t address)
{
    for (int i = 0; i < s_device_count; ++i) {
        if (s_devices[i].address == address) {
            return s_devices[i].handle;
        }
    }
    if (!ensure_bus_init() || s_device_count >= I2C_BRIDGE_MAX_DEVICES) {
        return NULL;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = 100000, /* 100kHz standard mode, aman utk kebanyakan device */
    };
    i2c_master_dev_handle_t handle;
    esp_err_t err = i2c_master_bus_add_device(s_bus_handle, &dev_cfg, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "gagal add device I2C 0x%02X: %d", address, err);
        return NULL;
    }

    s_devices[s_device_count].address = address;
    s_devices[s_device_count].handle = handle;
    s_devices[s_device_count].inited = true;
    s_device_count++;
    return handle;
}

bool i2c_bridge_read_reg(int bus, uint8_t address, uint8_t reg,
                          uint8_t *out_data, uint8_t length)
{
    (void)bus; /* hanya bus 0 didukung untuk sekarang */
    memset(out_data, 0, length);

    i2c_master_dev_handle_t handle = get_or_add_device(address);
    if (!handle) {
        return false;
    }

    esp_err_t err = i2c_master_transmit_receive(handle, &reg, 1, out_data, length, I2C_BRIDGE_TIMEOUT_MS);
    if (err != ESP_OK) {
        /* NACK/timeout - JANGAN retry, JANGAN blocking lebih lama.
         * out_data sudah di-nolkan di atas (fallback). */
        memset(out_data, 0, length);
        return false;
    }
    return true;
}

bool i2c_bridge_write_reg(int bus, uint8_t address, uint8_t reg,
                           const uint8_t *data, uint8_t length)
{
    (void)bus;
    if (length > I2C_BRIDGE_MAX_DATA_LEN - 1) {
        return false;
    }

    i2c_master_dev_handle_t handle = get_or_add_device(address);
    if (!handle) {
        return false;
    }

    uint8_t buf[I2C_BRIDGE_MAX_DATA_LEN];
    buf[0] = reg;
    memcpy(&buf[1], data, length);

    esp_err_t err = i2c_master_transmit(handle, buf, length + 1, I2C_BRIDGE_TIMEOUT_MS);
    return err == ESP_OK;
}

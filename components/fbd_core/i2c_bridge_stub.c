/* Stub I2C untuk test host (PC) - tidak ada bus I2C fisik di PC, jadi
 * SELALU return false (error) sesuai kontrak i2c_bridge.h. Dipakai
 * test_host/fbd_i2c_test.c untuk membuktikan node i2c_read_reg/write_reg
 * menghasilkan error=true dengan benar saat I2C gagal, tanpa perlu
 * hardware I2C nyata. Firmware ESP32-S3 pakai i2c_bridge_real.c. */
#include "i2c_bridge.h"
#include <string.h>

bool i2c_bridge_read_reg(int bus, uint8_t address, uint8_t reg,
                          uint8_t *out_data, uint8_t length)
{
    (void)bus; (void)address; (void)reg;
    memset(out_data, 0, length);
    return false;
}

bool i2c_bridge_write_reg(int bus, uint8_t address, uint8_t reg,
                           const uint8_t *data, uint8_t length)
{
    (void)bus; (void)address; (void)reg; (void)data; (void)length;
    return false;
}

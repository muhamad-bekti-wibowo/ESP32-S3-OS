#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* I2C primitive register-level (plan.md prinsip #2): HANYA baca/tulis
 * register generik, BUKAN driver sensor spesifik. Sensor riil (LCD1602,
 * INA219, dst) dikomposisikan dari primitive ini di level graph JSON,
 * bukan kode C baru per sensor.
 *
 * TIDAK ada mode simulated/real seperti fbd_hw_backend.h - I2C selalu
 * berarti bus fisik sungguhan dengan alamat device nyata (tidak ada
 * "I2C tersimulasi" yang masuk akal, beda dari ADC/PWM/servo yang bisa
 * disimulasikan dengan angka). Firmware real: i2c_bridge_real.c (driver
 * ESP-IDF i2c_master). Test host: i2c_bridge_stub.c (selalu return
 * error=true, TIDAK ADA bus I2C di PC).
 *
 * Timeout WAJIB pendek (5-10ms, lihat I2C_BRIDGE_TIMEOUT_MS) - kalau
 * NACK/timeout, return false + out_data di-nolkan, JANGAN blocking lebih
 * lama, JANGAN menahan scan cycle (plan.md §5.3 aturan wajib). */

#define I2C_BRIDGE_TIMEOUT_MS 8
#define I2C_BRIDGE_MAX_DATA_LEN 8

/* Baca `length` byte dari register `reg` di device `address` pada bus `bus`.
 * Return true kalau sukses (out_data terisi `length` byte), false kalau
 * NACK/timeout/error apa pun (out_data di-nolkan). TIDAK PERNAH blocking
 * lebih dari I2C_BRIDGE_TIMEOUT_MS. */
bool i2c_bridge_read_reg(int bus, uint8_t address, uint8_t reg,
                          uint8_t *out_data, uint8_t length);

/* Tulis `length` byte dari `data` ke register `reg` di device `address`
 * pada bus `bus`. Return true kalau sukses, false kalau NACK/timeout. */
bool i2c_bridge_write_reg(int bus, uint8_t address, uint8_t reg,
                           const uint8_t *data, uint8_t length);

/* Delay mikrosekon antar command dalam satu burst (FBD_NODE_I2C_WRITE_BURST)
 * - dipakai device yang butuh jeda antar command (mis. toggle bit E pada
 * LCD1602 PCF8574). Dibatasi maks beberapa ms total per node (lihat
 * fbd_graph.c) supaya tidak menahan scan cycle lama. Stub (test host):
 * no-op, tidak ada bus fisik untuk disimulasikan delaynya. */
void i2c_bridge_delay_us(uint32_t us);

#ifdef __cplusplus
}
#endif

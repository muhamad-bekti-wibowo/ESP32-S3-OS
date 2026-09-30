/* Modbus RTU SLAVE (server) minimal - hand-rolled di atas ESP-IDF UART
 * driver (esp_driver_uart), BUKAN vendor library (lihat modbus_tcp_task.c
 * untuk alasan yang sama). Dukung function code 0x01 (read coils), 0x02
 * (read discrete input - dipetakan sama seperti coil di slave_bridge,
 * beda cuma access mode di sisi master), 0x03 (read holding), 0x04
 * (read input register), 0x05 (write single coil), 0x06 (write single
 * holding register).
 *
 * Framing RTU: TIDAK ada delimiter eksplisit seperti TCP - satu frame
 * dianggap SELESAI kalau tidak ada byte baru selama >3.5 karakter time
 * (aturan standar Modbus RTU). Didekati dengan uart_read_bytes() ber-
 * timeout pendek dan menganggap "tidak ada data baru dalam
 * FRAME_IDLE_TIMEOUT_MS" sebagai akhir frame - cukup akurat untuk baud
 * rate umum (9600-115200) tanpa perlu timer hardware presisi karakter. */
#include "modbus_slave_task.h"
#include "modbus_slave_bridge.h"
#include "fbd_graph.h"
#include <string.h>
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "modbus_slave";

#define MODBUS_SLAVE_UART_PORT UART_NUM_2
#define MODBUS_SLAVE_TX_PIN 17
#define MODBUS_SLAVE_RX_PIN 16
#define MODBUS_SLAVE_UART_BUF_SIZE 256
#define MODBUS_SLAVE_FRAME_IDLE_TIMEOUT_MS 20 /* >3.5 char time bahkan di 9600 baud (~4ms/karakter) */
#define MODBUS_SLAVE_FRAME_MAX_LEN 256

static uint16_t crc16_modbus(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

static void send_exception(uint8_t slave_id, uint8_t function_code, uint8_t exception_code)
{
    uint8_t frame[5];
    frame[0] = slave_id;
    frame[1] = function_code | 0x80;
    frame[2] = exception_code;
    uint16_t crc = crc16_modbus(frame, 3);
    frame[3] = (uint8_t)(crc & 0xFF);
    frame[4] = (uint8_t)(crc >> 8);
    uart_write_bytes(MODBUS_SLAVE_UART_PORT, frame, sizeof(frame));
}

/* Function code 1/2: baca N coil/discrete berturutan mulai address.
 * modbus_slave_bridge diindeks per-register (bukan per-bit-range), jadi
 * baca satu-satu lewat modbus_slave_bridge_get_to_master() per address. */
static void handle_read_bits(uint8_t slave_id, uint8_t function_code,
                              uint16_t start_addr, uint16_t count)
{
    if (count == 0 || count > 2000) {
        send_exception(slave_id, function_code, 0x03); /* illegal data value */
        return;
    }
    fbd_modbus_reg_type_t reg_type = FBD_MODBUS_REG_COIL; /* discrete input dipetakan slot yang sama - lihat header */
    uint8_t byte_count = (uint8_t)((count + 7) / 8);
    uint8_t frame[3 + 250 + 2];
    frame[0] = slave_id;
    frame[1] = function_code;
    frame[2] = byte_count;
    memset(&frame[3], 0, byte_count);

    for (uint16_t i = 0; i < count; ++i) {
        char key[MODBUS_SLAVE_BRIDGE_KEY_LEN];
        modbus_slave_bridge_make_key(key, reg_type, (uint16_t)(start_addr + i));
        uint16_t value = 0;
        if (!modbus_slave_bridge_get_to_master(key, &value)) {
            send_exception(slave_id, function_code, 0x02); /* illegal data address */
            return;
        }
        if (value) {
            frame[3 + i / 8] |= (uint8_t)(1 << (i % 8));
        }
    }

    uint16_t crc = crc16_modbus(frame, 3 + byte_count);
    frame[3 + byte_count] = (uint8_t)(crc & 0xFF);
    frame[3 + byte_count + 1] = (uint8_t)(crc >> 8);
    uart_write_bytes(MODBUS_SLAVE_UART_PORT, frame, 3 + byte_count + 2);
}

/* Function code 3/4: baca N holding/input register berturutan. */
static void handle_read_registers(uint8_t slave_id, uint8_t function_code,
                                   uint16_t start_addr, uint16_t count)
{
    if (count == 0 || count > 125) {
        send_exception(slave_id, function_code, 0x03);
        return;
    }
    fbd_modbus_reg_type_t reg_type = FBD_MODBUS_REG_HOLDING; /* input register dipetakan slot yang sama - lihat header */
    uint8_t byte_count = (uint8_t)(count * 2);
    uint8_t frame[3 + 250 + 2];
    frame[0] = slave_id;
    frame[1] = function_code;
    frame[2] = byte_count;

    for (uint16_t i = 0; i < count; ++i) {
        char key[MODBUS_SLAVE_BRIDGE_KEY_LEN];
        modbus_slave_bridge_make_key(key, reg_type, (uint16_t)(start_addr + i));
        uint16_t value = 0;
        if (!modbus_slave_bridge_get_to_master(key, &value)) {
            send_exception(slave_id, function_code, 0x02);
            return;
        }
        frame[3 + i * 2] = (uint8_t)(value >> 8);
        frame[3 + i * 2 + 1] = (uint8_t)(value & 0xFF);
    }

    uint16_t crc = crc16_modbus(frame, 3 + byte_count);
    frame[3 + byte_count] = (uint8_t)(crc & 0xFF);
    frame[3 + byte_count + 1] = (uint8_t)(crc >> 8);
    uart_write_bytes(MODBUS_SLAVE_UART_PORT, frame, 3 + byte_count + 2);
}

static void handle_write_single_coil(uint8_t slave_id, uint16_t addr, uint16_t raw_value,
                                      const uint8_t *request_frame, size_t request_len)
{
    if (raw_value != 0x0000 && raw_value != 0xFF00) {
        send_exception(slave_id, 0x05, 0x03);
        return;
    }
    char key[MODBUS_SLAVE_BRIDGE_KEY_LEN];
    modbus_slave_bridge_make_key(key, FBD_MODBUS_REG_COIL, addr);
    if (!modbus_slave_bridge_set_from_master(key, raw_value ? 1 : 0)) {
        send_exception(slave_id, 0x05, 0x02);
        return;
    }
    /* FC05 sukses: echo request apa adanya (standar Modbus). */
    uart_write_bytes(MODBUS_SLAVE_UART_PORT, request_frame, request_len);
}

static void handle_write_single_register(uint8_t slave_id, uint16_t addr, uint16_t value,
                                          const uint8_t *request_frame, size_t request_len)
{
    char key[MODBUS_SLAVE_BRIDGE_KEY_LEN];
    modbus_slave_bridge_make_key(key, FBD_MODBUS_REG_HOLDING, addr);
    if (!modbus_slave_bridge_set_from_master(key, value)) {
        send_exception(slave_id, 0x06, 0x02);
        return;
    }
    /* FC06 sukses: echo request apa adanya (standar Modbus). */
    uart_write_bytes(MODBUS_SLAVE_UART_PORT, request_frame, request_len);
}

static void process_frame(uint8_t my_slave_id, const uint8_t *frame, size_t len)
{
    if (len < 4) return; /* minimal slave_id + function + crc(2) */

    uint16_t crc_calc = crc16_modbus(frame, len - 2);
    uint16_t crc_recv = (uint16_t)frame[len - 2] | ((uint16_t)frame[len - 1] << 8);
    if (crc_calc != crc_recv) {
        return; /* CRC salah - noise elektrik/collision di bus, abaikan diam-diam (standar RTU) */
    }

    uint8_t slave_id = frame[0];
    if (slave_id != my_slave_id && slave_id != 0) { /* 0 = broadcast, tidak kita respons (tidak ada state global utk broadcast) */
        return; /* bukan untuk kita */
    }
    if (slave_id == 0) return;

    uint8_t function_code = frame[1];
    if (len < 8) return; /* semua FC yang didukung butuh minimal 6 byte PDU + slave_id */

    uint16_t addr = ((uint16_t)frame[2] << 8) | frame[3];
    uint16_t value_or_count = ((uint16_t)frame[4] << 8) | frame[5];

    switch (function_code) {
        case 0x01:
        case 0x02:
            handle_read_bits(slave_id, function_code, addr, value_or_count);
            break;
        case 0x03:
        case 0x04:
            handle_read_registers(slave_id, function_code, addr, value_or_count);
            break;
        case 0x05:
            handle_write_single_coil(slave_id, addr, value_or_count, frame, len);
            break;
        case 0x06:
            handle_write_single_register(slave_id, addr, value_or_count, frame, len);
            break;
        default:
            send_exception(slave_id, function_code, 0x01); /* illegal function */
            break;
    }
}

static void modbus_slave_task(void *arg)
{
    uint8_t slave_id = (uint8_t)(uintptr_t)arg;
    ESP_LOGI(TAG, "modbus_slave_task jalan di core %d, slave_id=%u", xPortGetCoreID(), (unsigned)slave_id);

    uint8_t frame[MODBUS_SLAVE_FRAME_MAX_LEN];
    size_t frame_len = 0;

    for (;;) {
        uint8_t byte;
        int n = uart_read_bytes(MODBUS_SLAVE_UART_PORT, &byte, 1,
                                 pdMS_TO_TICKS(MODBUS_SLAVE_FRAME_IDLE_TIMEOUT_MS));
        if (n > 0) {
            if (frame_len < sizeof(frame)) {
                frame[frame_len++] = byte;
            }
            /* Terus baca byte berikutnya SELAMA masih datang tanpa idle -
             * loop luar cuma masuk sini kalau ADA byte, jadi drain semua
             * byte yang sudah nunggu di buffer UART dulu sebelum anggap
             * frame selesai. */
            for (;;) {
                uint8_t next;
                int m = uart_read_bytes(MODBUS_SLAVE_UART_PORT, &next, 1, 0);
                if (m <= 0) break;
                if (frame_len < sizeof(frame)) {
                    frame[frame_len++] = next;
                }
            }
            continue; /* cek lagi - kalau timeout berikutnya idle beneran, baru proses */
        }

        /* Timeout tanpa byte baru = frame (kalau ada) sudah lengkap. */
        if (frame_len > 0) {
            process_frame(slave_id, frame, frame_len);
            frame_len = 0;
        }
    }
}

void modbus_slave_task_start(uint8_t slave_id, uint32_t baud_rate)
{
    modbus_slave_bridge_init();

    uart_config_t uart_cfg = {
        .baud_rate = (int)baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    uart_driver_install(MODBUS_SLAVE_UART_PORT, MODBUS_SLAVE_UART_BUF_SIZE, MODBUS_SLAVE_UART_BUF_SIZE, 0, NULL, 0);
    uart_param_config(MODBUS_SLAVE_UART_PORT, &uart_cfg);
    uart_set_pin(MODBUS_SLAVE_UART_PORT, MODBUS_SLAVE_TX_PIN, MODBUS_SLAVE_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    xTaskCreate(modbus_slave_task, "modbus_slave", 4096, (void *)(uintptr_t)slave_id, 4, NULL);
}

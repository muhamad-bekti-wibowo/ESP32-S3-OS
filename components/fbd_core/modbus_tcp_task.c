/* Modbus TCP CLIENT (master) minimal - hand-rolled langsung di atas BSD
 * socket (lwip), BUKAN vendor library (esp-modbus tidak di-vendor di
 * project ini, konsisten filosofi "primitive kecil, tanpa driver
 * eksternal" yang sudah dipakai di I2C/ultrasonic/WS2812). Cukup untuk
 * function code paling umum: 0x01 (read coils), 0x02 (read discrete
 * input), 0x03 (read holding), 0x04 (read input register), 0x05 (write
 * single coil), 0x06 (write single holding register).
 *
 * Jalan di task terpisah dari fbd_scan_task (lihat modbus_tcp_bridge.h
 * untuk alasan lengkap) - ambil SATU pending request dari bridge tiap
 * iterasi, buka socket baru (connect+request+response+close) per
 * request (bukan connection pooling - lebih sederhana & robust terhadap
 * device yang reboot/ganti IP, harga yang dibayar cuma latensi TCP
 * handshake tambahan ~1-2 RTT per request, dianggap OK karena node
 * modbus_tcp_read/write sendiri sudah didesain "hasil polling terakhir,
 * bukan realtime per-cycle"). */
#include "modbus_tcp_task.h"
#include "modbus_tcp_bridge.h"
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "modbus_tcp";

#define MODBUS_TCP_SOCKET_TIMEOUT_MS 200 /* device remote via LAN - jauh lebih longgar dari I2C (8ms), tapi tetap dibatasi supaya 1 device macet tidak menggantung task selamanya */
#define MODBUS_TCP_IDLE_DELAY_MS 20      /* jeda saat tidak ada pending request, hindari busy-loop */

static uint8_t s_next_transaction_id_hi = 0, s_next_transaction_id_lo = 1;

static void next_transaction_id(uint8_t *hi, uint8_t *lo)
{
    *hi = s_next_transaction_id_hi;
    *lo = s_next_transaction_id_lo;
    if (++s_next_transaction_id_lo == 0) ++s_next_transaction_id_hi;
}

static uint8_t function_code_for_read(fbd_modbus_reg_type_t reg_type)
{
    switch (reg_type) {
        case FBD_MODBUS_REG_COIL: return 0x01;
        case FBD_MODBUS_REG_DISCRETE: return 0x02;
        case FBD_MODBUS_REG_INPUT: return 0x04;
        case FBD_MODBUS_REG_HOLDING:
        default: return 0x03;
    }
}

/* Buka koneksi, kirim SATU request, tunggu response, tutup socket.
 * Return false kalau connect/send/recv gagal ATAU response Modbus
 * berisi exception code - values/count TIDAK diubah kalau gagal. */
static bool do_read_request(const modbus_tcp_bridge_pending_request_t *req,
                             uint16_t *out_values, uint8_t *out_count)
{
    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) return false;

    struct timeval tv = { .tv_sec = 0, .tv_usec = MODBUS_TCP_SOCKET_TIMEOUT_MS * 1000 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(req->port);
    if (inet_pton(AF_INET, req->ip, &addr.sin_addr) != 1) {
        close(sock);
        return false;
    }

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(sock);
        return false;
    }

    uint8_t tx[12];
    uint8_t tid_hi, tid_lo;
    next_transaction_id(&tid_hi, &tid_lo);
    tx[0] = tid_hi; tx[1] = tid_lo;      /* transaction id */
    tx[2] = 0; tx[3] = 0;                /* protocol id (selalu 0 utk Modbus TCP) */
    tx[4] = 0; tx[5] = 6;                /* length: unit_id + PDU (6 byte berikut) */
    tx[6] = req->unit_id;
    tx[7] = function_code_for_read(req->reg_type);
    tx[8] = (uint8_t)(req->address >> 8);
    tx[9] = (uint8_t)(req->address & 0xFF);
    tx[10] = (uint8_t)(req->count >> 8);
    tx[11] = (uint8_t)(req->count & 0xFF);

    if (send(sock, tx, sizeof(tx), 0) != (int)sizeof(tx)) {
        close(sock);
        return false;
    }

    uint8_t rx[256];
    int n = recv(sock, rx, sizeof(rx), 0);
    close(sock);

    if (n < 9) return false; /* minimal: MBAP(7) + function code + byte count */
    if (rx[0] != tid_hi || rx[1] != tid_lo) return false; /* transaction id tidak cocok - abaikan */
    uint8_t function_code = rx[7];
    if (function_code & 0x80) return false; /* exception response dari slave */

    bool is_bit_type = (req->reg_type == FBD_MODBUS_REG_COIL || req->reg_type == FBD_MODBUS_REG_DISCRETE);
    uint8_t byte_count = rx[8];
    uint8_t count = req->count;
    if (count > FBD_MODBUS_MAX_COUNT) count = FBD_MODBUS_MAX_COUNT;

    if (is_bit_type) {
        if (9 + ((count + 7) / 8) > n) return false;
        for (uint8_t i = 0; i < count; ++i) {
            uint8_t byte = rx[9 + (i / 8)];
            out_values[i] = (byte >> (i % 8)) & 1;
        }
    } else {
        if (9 + count * 2 > n || byte_count < count * 2) return false;
        for (uint8_t i = 0; i < count; ++i) {
            out_values[i] = ((uint16_t)rx[9 + i * 2] << 8) | rx[9 + i * 2 + 1];
        }
    }
    *out_count = count;
    return true;
}

static bool do_write_request(const modbus_tcp_bridge_pending_request_t *req)
{
    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) return false;

    struct timeval tv = { .tv_sec = 0, .tv_usec = MODBUS_TCP_SOCKET_TIMEOUT_MS * 1000 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(req->port);
    if (inet_pton(AF_INET, req->ip, &addr.sin_addr) != 1) {
        close(sock);
        return false;
    }
    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(sock);
        return false;
    }

    uint8_t tx[12];
    uint8_t tid_hi, tid_lo;
    next_transaction_id(&tid_hi, &tid_lo);
    tx[0] = tid_hi; tx[1] = tid_lo;
    tx[2] = 0; tx[3] = 0;
    tx[4] = 0; tx[5] = 6;
    tx[6] = req->unit_id;
    tx[7] = req->write_coil ? 0x05 : 0x06;
    tx[8] = (uint8_t)(req->address >> 8);
    tx[9] = (uint8_t)(req->address & 0xFF);
    if (req->write_coil) {
        tx[10] = req->write_value ? 0xFF : 0x00; /* FC05: 0xFF00=ON, 0x0000=OFF */
        tx[11] = 0x00;
    } else {
        tx[10] = (uint8_t)(req->write_value >> 8);
        tx[11] = (uint8_t)(req->write_value & 0xFF);
    }

    if (send(sock, tx, sizeof(tx), 0) != (int)sizeof(tx)) {
        close(sock);
        return false;
    }

    uint8_t rx[16];
    int n = recv(sock, rx, sizeof(rx), 0);
    close(sock);

    if (n < 8) return false;
    if (rx[0] != tid_hi || rx[1] != tid_lo) return false;
    if (rx[7] & 0x80) return false; /* exception */
    return true;
}

static void modbus_tcp_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "modbus_tcp_task jalan di core %d", xPortGetCoreID());

    for (;;) {
        modbus_tcp_bridge_pending_request_t req;
        if (!modbus_tcp_bridge_take_pending(&req)) {
            vTaskDelay(pdMS_TO_TICKS(MODBUS_TCP_IDLE_DELAY_MS));
            continue;
        }

        if (req.is_write) {
            bool ok = do_write_request(&req);
            uint16_t dummy[1] = { req.write_value };
            modbus_tcp_bridge_set_result(req.key, dummy, 1, ok);
            if (!ok) {
                ESP_LOGW(TAG, "write ke %s:%u unit=%u addr=%u gagal", req.ip, (unsigned)req.port,
                         (unsigned)req.unit_id, (unsigned)req.address);
            }
        } else {
            uint16_t values[FBD_MODBUS_MAX_COUNT] = {0};
            uint8_t count = 0;
            bool ok = do_read_request(&req, values, &count);
            if (!ok) {
                ESP_LOGW(TAG, "read dari %s:%u unit=%u addr=%u gagal", req.ip, (unsigned)req.port,
                         (unsigned)req.unit_id, (unsigned)req.address);
                count = req.count;
                if (count > FBD_MODBUS_MAX_COUNT) count = FBD_MODBUS_MAX_COUNT;
                memset(values, 0, sizeof(values));
            }
            modbus_tcp_bridge_set_result(req.key, values, count, ok);
        }
    }
}

void modbus_tcp_task_start(void)
{
    modbus_tcp_bridge_init();
    xTaskCreate(modbus_tcp_task, "modbus_tcp", 4096, NULL, 4, NULL);
}

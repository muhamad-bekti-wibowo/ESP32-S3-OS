#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "fbd_value.h"
#include "fbd_nodes.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FBD_MAX_NODES 64
#define FBD_MAX_LINKS 128
#define FBD_MAX_NODE_INPUTS 4
#define FBD_MAX_NODE_OUTPUTS 2
#define FBD_MAX_ID_LEN 16

typedef enum {
    FBD_NODE_CONST = 0,
    FBD_NODE_VAR_GET,
    FBD_NODE_VAR_SET,
    FBD_NODE_AND,
    FBD_NODE_OR,
    FBD_NODE_NOT,
    FBD_NODE_XOR,
    FBD_NODE_NAND,
    FBD_NODE_NOR,
    FBD_NODE_COMPARE,
    FBD_NODE_MATH,
    FBD_NODE_MIN,
    FBD_NODE_MAX,
    FBD_NODE_ABS,
    FBD_NODE_SCALE,
    FBD_NODE_CLAMP,
    FBD_NODE_TON,
    FBD_NODE_TOF,
    FBD_NODE_TP,
    FBD_NODE_OSC,
    FBD_NODE_CTU,
    FBD_NODE_DIGITAL_IN,
    FBD_NODE_DIGITAL_OUT,
    FBD_NODE_ANALOG_IN,
    FBD_NODE_PWM_OUT,
    FBD_NODE_SERVO,
    FBD_NODE_WS2812,
    FBD_NODE_ULTRASONIC,
    FBD_NODE_I2C_READ_REG,
    FBD_NODE_I2C_WRITE_REG,
    FBD_NODE_I2C_WRITE_BURST,
    FBD_NODE_SYS_VAR_GET,
    FBD_NODE_HTTP_ENDPOINT,
    FBD_NODE_MODBUS_TCP_READ,
    FBD_NODE_MODBUS_TCP_WRITE,
    FBD_NODE_MODBUS_SLAVE_REG,
    FBD_NODE_TYPE_COUNT
} fbd_node_type_t;

/* FBD_NODE_MODBUS_TCP_READ/WRITE: register_type request - dipetakan ke
 * function code Modbus standar (FC03/04 baca, FC06/16 tulis holding;
 * FC01/05 baca-tulis coil; FC02 baca discrete input). */
typedef enum {
    FBD_MODBUS_REG_HOLDING = 0,
    FBD_MODBUS_REG_INPUT,
    FBD_MODBUS_REG_COIL,
    FBD_MODBUS_REG_DISCRETE
} fbd_modbus_reg_type_t;

typedef enum {
    FBD_PIN_MODE_PULLUP = 0,
    FBD_PIN_MODE_PULLDOWN,
    FBD_PIN_MODE_FLOATING
} fbd_pin_mode_t;

/* Dual backend (plan.md §3): setiap node peripheral fisik punya params.mode
 * "simulated" (nilai dari UI, tidak menyentuh hardware) atau "real"
 * (ESP-IDF driver nyata). Dipakai FBD_NODE_ANALOG_IN/PWM_OUT/SERVO. */
typedef enum {
    FBD_HW_SIMULATED = 0,
    FBD_HW_REAL
} fbd_hw_mode_t;

/* FBD_NODE_I2C_WRITE_BURST: kirim beberapa command register write
 * berurutan dalam SATU scan cycle - dipakai untuk device I2C yang butuh
 * command sequence (mis. LCD1602 lewat backpack PCF8574: kirim nibble
 * demi nibble dengan toggle bit E). TETAP primitive generik (bukan
 * driver LCD) - siapa pun bisa susun sequence apa pun lewat commands[]
 * ini, cocok untuk periferal I2C lain juga, tidak spesifik LCD.
 * Dibatasi statis (tanpa alokasi dinamis, konsisten dgn fbd_graph_t
 * lain): FBD_I2C_BURST_MAX_CMDS command, tiap command maks
 * FBD_I2C_BURST_MAX_DATA byte data. Kalau butuh command lebih banyak,
 * sambung beberapa node i2c_write_burst berurutan di canvas. */
#define FBD_I2C_BURST_MAX_CMDS 8
#define FBD_I2C_BURST_MAX_DATA 4

typedef struct {
    uint8_t reg;
    uint8_t data[FBD_I2C_BURST_MAX_DATA];
    uint8_t data_len;
} fbd_i2c_burst_cmd_t;

typedef struct {
    fbd_value_t const_value;         /* FBD_NODE_CONST */
    char var_name[FBD_VAR_NAME_LEN]; /* FBD_NODE_VAR_GET / FBD_NODE_VAR_SET */
    fbd_compare_op_t compare_op;         /* FBD_NODE_COMPARE */
    fbd_math_op_t math_op;               /* FBD_NODE_MATH */
    float in_min, in_max, out_min, out_max; /* FBD_NODE_SCALE */
    float clamp_min, clamp_max;       /* FBD_NODE_CLAMP */
    uint32_t delay_ms;                 /* FBD_NODE_TON/TOF/TP */
    uint32_t osc_on_ms, osc_off_ms;    /* FBD_NODE_OSC: durasi fase ON/OFF */
    int32_t preset;                    /* FBD_NODE_CTU: threshold (batas naik / auto-reset). reset_value port (inputs[3]) dipakai di kedua mode */
    bool ctu_auto_reset;                /* FBD_NODE_CTU: true = reset otomatis begitu count>=preset (port reset diabaikan); false = reset manual lewat port reset */
    int pin;                            /* FBD_NODE_DIGITAL_IN/OUT/ANALOG_IN/PWM_OUT/SERVO */
    bool invert;                        /* FBD_NODE_DIGITAL_IN/OUT */
    fbd_pin_mode_t pin_mode;            /* FBD_NODE_DIGITAL_IN saja */

    fbd_hw_mode_t hw_mode;              /* FBD_NODE_DIGITAL_IN/OUT/ANALOG_IN/PWM_OUT/SERVO */
    int resolution;                     /* FBD_NODE_ANALOG_IN (bit) / FBD_NODE_PWM_OUT (bit) */
    int attenuation;                    /* FBD_NODE_ANALOG_IN (dB, 0/2/6/11) */
    fbd_value_t sim_value;              /* FBD_NODE_ANALOG_IN mode simulated: nilai dari UI */
    uint32_t frequency;                 /* FBD_NODE_PWM_OUT (Hz) */
    uint32_t min_us, max_us;            /* FBD_NODE_SERVO: pulse width di sudut 0/180 derajat */
    int ws2812_count;                   /* FBD_NODE_WS2812: jumlah LED di strip, semua diset warna sama */
    int ultrasonic_echo_pin;            /* FBD_NODE_ULTRASONIC: pin echo (pin utama "pin" dipakai untuk trig) */
    float ultrasonic_sim_distance_cm;   /* FBD_NODE_ULTRASONIC mode simulated: jarak (cm) dari UI, tanpa sentuh GPIO */

    int i2c_bus;                        /* FBD_NODE_I2C_READ_REG/WRITE_REG: index bus (0) */
    uint8_t i2c_address;                /* alamat 7-bit device I2C */
    uint8_t i2c_register;               /* alamat register di dalam device */
    uint8_t i2c_data[8];                /* FBD_NODE_I2C_WRITE_REG: data yang ditulis */
    uint8_t i2c_data_len;                /* panjang i2c_data (write) / panjang dibaca (read) */

    fbd_i2c_burst_cmd_t i2c_burst_cmds[FBD_I2C_BURST_MAX_CMDS]; /* FBD_NODE_I2C_WRITE_BURST */
    uint8_t i2c_burst_cmd_count;         /* jumlah command terisi di i2c_burst_cmds, 1-FBD_I2C_BURST_MAX_CMDS */
    uint32_t i2c_burst_delay_us;         /* delay antar command (mis. untuk toggle bit E LCD), 0 = tanpa delay */

    char sys_var_name[24];              /* FBD_NODE_SYS_VAR_GET: "SYS.WIFI_CONNECTED" dkk */

    /* FBD_NODE_HTTP_ENDPOINT: route HTTP di server httpd KEDUA (port
     * terpisah dari editor, lihat endpoint_mgr.h). path/file/content_type
     * dibaca sekali saat boot untuk register handler (BUTUH REBOOT kalau
     * berubah - esp_http_server tidak dukung route dinamis). query_a_name/
     * query_b_name DAN input/output port DIEKSEKUSI tiap scan cycle
     * seperti node biasa (lihat http_endpoint_bridge.h untuk kenapa state
     * runtime-nya TERPISAH dari fbd_node_state_t). */
    char http_path[32];                 /* route, wajib mulai "/" (mis. "/status") */
    char http_file[32];                 /* nama file HTML fallback di /spiffs/endpoints/ - dipakai HANYA kalah inputs[0] tidak tersambung */
    bool http_content_type_html;        /* true="text/html", false="text/plain" - berlaku utk file statis MAUPUN response dinamis dari inputs[0] */
    char http_query_a_name[16];          /* nama query string utk outputs[0], mis. "a" (kosong = outputs[0] selalu 0) */
    char http_query_b_name[16];          /* nama query string utk outputs[1], mis. "b" */

    /* FBD_NODE_MODBUS_TCP_READ/WRITE: request Modbus TCP ke device LAIN
     * (ESP32 jadi client/master). TIDAK BOLEH blocking network I/O
     * langsung di evaluate_node() (koneksi/response time device remote
     * tidak bisa dijamin cepat seperti I2C lokal) - request dikirim ke
     * task terpisah (modbus_tcp_bridge.h), scan cycle cuma baca hasil
     * TERAKHIR yang sudah ada di bridge (lihat komentar lengkap di
     * modbus_tcp_bridge.h, pola sama seperti http_endpoint_bridge.h). */
    char modbus_ip[16];                  /* alamat IPv4 dotted-string device target, mis. "192.168.1.50" */
    uint16_t modbus_port;                /* default 502 (port standar Modbus TCP) */
    uint8_t modbus_unit_id;              /* unit/slave id di dalam device (banyak device abaikan, isi 1) */
    fbd_modbus_reg_type_t modbus_reg_type; /* holding/input/coil/discrete */
    uint16_t modbus_address;             /* alamat register/coil awal (0-based) */
    uint8_t modbus_count;                /* jumlah register/coil dibaca (READ saja), maks FBD_MODBUS_MAX_COUNT */
    uint32_t modbus_poll_interval_ms;    /* jarak minimal antar request baru ke device ini (hindari flood LAN), 0 = tiap scan cycle coba request baru kalau bridge idle */

    /* FBD_NODE_MODBUS_SLAVE_REG: ESP32 jadi SLAVE Modbus RTU (server) di
     * UART2/RS485 - node ini expose 1 alamat register ke master luar
     * (SCADA/PLC dkk). Konfigurasi port serial (baud/slave_id) global,
     * diatur di tab System > Modbus Slave (mirip System > HTTP
     * Endpoints), BUKAN per-node, karena satu device cuma punya 1 bus
     * RS485 - lihat modbus_slave_bridge.h. */
    uint16_t modbus_slave_address;       /* alamat register/coil yang diexpose node ini */
    fbd_modbus_reg_type_t modbus_slave_reg_type; /* holding atau coil saja (read-write dari sisi master) */
} fbd_node_params_t;

/* FBD_NODE_MODBUS_TCP_READ: dibatasi 4 register/coil per node (fbd_value_t
 * FBD_BYTES maksimal 8 byte = 4x uint16) - konsisten filosofi primitive
 * kecil project ini (I2C juga dibatasi FBD_BYTES 8 byte). Butuh lebih
 * banyak? Sambung beberapa node modbus_tcp_read dengan address awal
 * beda-beda. */
#define FBD_MODBUS_MAX_COUNT 4

typedef struct {
    fbd_timer_state_t timer;
    fbd_counter_state_t counter;
    bool hw_initialized; /* FBD_NODE_DIGITAL_IN/OUT/ANALOG_IN/PWM_OUT/SERVO:
                          * true setelah backend->*_init() dipanggil sekali.
                          * Init hardware TIDAK boleh dipanggil tiap scan
                          * cycle (gpio_config/ledc_channel_config mahal &
                          * bisa reset state fisik tiap 20ms). */
} fbd_node_state_t;

typedef struct {
    char id[FBD_MAX_ID_LEN];
    fbd_node_type_t type;
    fbd_node_params_t params;
    fbd_node_state_t state;
    fbd_value_t inputs[FBD_MAX_NODE_INPUTS];
    fbd_value_t outputs[FBD_MAX_NODE_OUTPUTS];
} fbd_node_t;

typedef struct {
    size_t from_idx; uint8_t from_port;
    size_t to_idx;   uint8_t to_port;
} fbd_link_t;

typedef struct {
    fbd_node_t nodes[FBD_MAX_NODES];
    size_t node_count;

    fbd_link_t links[FBD_MAX_LINKS];
    size_t link_count;

    size_t execution_order[FBD_MAX_NODES];
    size_t order_count;

    fbd_var_store_t vars;
} fbd_graph_t;

void fbd_graph_init(fbd_graph_t *g);

/* Return NULL kalau graph penuh (FBD_MAX_NODES tercapai). */
fbd_node_t *fbd_graph_add_node(fbd_graph_t *g, const char *id, fbd_node_type_t type);

/* Return false kalau id tidak ditemukan atau link penuh (FBD_MAX_LINKS). */
bool fbd_graph_add_link(fbd_graph_t *g, const char *from_id, uint8_t from_port,
                         const char *to_id, uint8_t to_port);

/* Cari index node by id. Return (size_t)-1 kalau tidak ditemukan. */
size_t fbd_graph_find_node(const fbd_graph_t *g, const char *id);

/* Topological sort (Kahn's algorithm). Return false kalau ada cyclic
 * dependency - execution_order tidak dipakai kalau gagal. */
bool fbd_graph_compile(fbd_graph_t *g);

/* Jalankan satu scan cycle sesuai execution_order. Panggil fbd_graph_compile()
 * dulu minimal sekali sebelum ini. */
void fbd_graph_execute_cycle(fbd_graph_t *g, uint32_t now_ms);

#ifdef __cplusplus
}
#endif

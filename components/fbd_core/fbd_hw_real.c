/* Backend real: driver ESP-IDF nyata (gpio, adc_oneshot, ledc). Hanya
 * dikompilasi sebagai bagian firmware ESP32-S3 (butuh header ESP-IDF) -
 * TIDAK dikompilasi untuk test host, lihat test_host/build_and_run.ps1. */
#include "fbd_hw_backend.h"
#include "fbd_graph.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "fbd_hw_real";

/* ---- Digital I/O ---- */

static void real_digital_init_input(int pin, int pin_mode)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = (pin_mode == FBD_PIN_MODE_PULLUP) ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = (pin_mode == FBD_PIN_MODE_PULLDOWN) ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
}

static void real_digital_init_output(int pin)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
}

static bool real_digital_read(int pin)
{
    return gpio_get_level((gpio_num_t)pin) != 0;
}

static void real_digital_write(int pin, bool level)
{
    gpio_set_level((gpio_num_t)pin, level ? 1 : 0);
}

/* ---- Analog input (ADC oneshot) ----
 * ESP32-S3 hanya ADC1 yang stabil dipakai bersama WiFi (ADC2 berbagi
 * resource dengan WiFi driver) - proyek ini WiFi selalu aktif (AP+STA),
 * jadi hanya pin ADC1 yang didukung. Cache satu unit handle + channel
 * config per pin supaya tidak re-init tiap scan cycle (mahal & bisa leak
 * handle kalau dipanggil ribuan kali/detik). */
#define FBD_HW_MAX_ADC_PINS 8

static adc_oneshot_unit_handle_t s_adc1_handle = NULL;
static struct {
    int pin;
    adc_channel_t channel;
    bool inited;
} s_adc_pins[FBD_HW_MAX_ADC_PINS];
static int s_adc_pin_count = 0;

static bool pin_to_adc1_channel(int pin, adc_channel_t *out_channel)
{
    /* Mapping GPIO -> ADC1 channel ESP32-S3 (GPIO1-10 = ADC1 CH0-9). */
    if (pin < 1 || pin > 10) {
        return false;
    }
    *out_channel = (adc_channel_t)(pin - 1);
    return true;
}

static void real_analog_init(int pin, int resolution, int attenuation)
{
    adc_channel_t channel;
    if (!pin_to_adc1_channel(pin, &channel)) {
        ESP_LOGE(TAG, "analog_init: GPIO%d bukan ADC1 channel valid (ESP32-S3: GPIO1-10)", pin);
        return;
    }

    if (!s_adc1_handle) {
        adc_oneshot_unit_init_cfg_t unit_cfg = { .unit_id = ADC_UNIT_1 };
        if (adc_oneshot_new_unit(&unit_cfg, &s_adc1_handle) != ESP_OK) {
            ESP_LOGE(TAG, "gagal init adc_oneshot unit");
            return;
        }
    }

    adc_atten_t atten = ADC_ATTEN_DB_12; /* default: rentang penuh ~0-3.3V */
    switch (attenuation) {
        case 0:  atten = ADC_ATTEN_DB_0;  break;
        case 2:  atten = ADC_ATTEN_DB_2_5; break;
        case 6:  atten = ADC_ATTEN_DB_6;  break;
        case 11: atten = ADC_ATTEN_DB_12; break;
        default: break;
    }
    adc_bitwidth_t bitwidth = (resolution == 9) ? ADC_BITWIDTH_9 :
                               (resolution == 10) ? ADC_BITWIDTH_10 :
                               (resolution == 11) ? ADC_BITWIDTH_11 : ADC_BITWIDTH_12;

    adc_oneshot_chan_cfg_t chan_cfg = { .bitwidth = bitwidth, .atten = atten };
    if (adc_oneshot_config_channel(s_adc1_handle, channel, &chan_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "gagal config channel ADC untuk GPIO%d", pin);
        return;
    }

    if (s_adc_pin_count < FBD_HW_MAX_ADC_PINS) {
        s_adc_pins[s_adc_pin_count].pin = pin;
        s_adc_pins[s_adc_pin_count].channel = channel;
        s_adc_pins[s_adc_pin_count].inited = true;
        s_adc_pin_count++;
    }
}

static fbd_value_t real_analog_read(int pin, int resolution, int attenuation, fbd_value_t sim_value)
{
    (void)sim_value;
    adc_channel_t channel;
    if (!pin_to_adc1_channel(pin, &channel)) {
        return fbd_make_int(0);
    }

    bool already_inited = false;
    for (int i = 0; i < s_adc_pin_count; ++i) {
        if (s_adc_pins[i].pin == pin) { already_inited = true; break; }
    }
    if (!already_inited) {
        real_analog_init(pin, resolution, attenuation);
    }

    if (!s_adc1_handle) {
        return fbd_make_int(0);
    }

    int raw = 0;
    esp_err_t err = adc_oneshot_read(s_adc1_handle, channel, &raw);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "adc_oneshot_read gagal untuk GPIO%d: %d", pin, err);
        return fbd_make_int(0);
    }
    return fbd_make_int(raw);
}

/* ---- PWM output (LEDC) ---- */

#define FBD_HW_MAX_LEDC_PINS 8
static struct {
    int pin;
    ledc_channel_t channel;
    bool inited;
} s_ledc_pins[FBD_HW_MAX_LEDC_PINS];
static int s_ledc_pin_count = 0;

static ledc_channel_t find_or_alloc_ledc_channel(int pin, bool *is_new)
{
    for (int i = 0; i < s_ledc_pin_count; ++i) {
        if (s_ledc_pins[i].pin == pin) {
            *is_new = false;
            return s_ledc_pins[i].channel;
        }
    }
    ledc_channel_t ch = (ledc_channel_t)(s_ledc_pin_count % LEDC_CHANNEL_MAX);
    if (s_ledc_pin_count < FBD_HW_MAX_LEDC_PINS) {
        s_ledc_pins[s_ledc_pin_count].pin = pin;
        s_ledc_pins[s_ledc_pin_count].channel = ch;
        s_ledc_pins[s_ledc_pin_count].inited = true;
        s_ledc_pin_count++;
    }
    *is_new = true;
    return ch;
}

static void real_pwm_init(int pin, uint32_t frequency, int resolution)
{
    bool is_new;
    ledc_channel_t channel = find_or_alloc_ledc_channel(pin, &is_new);
    if (!is_new) {
        return; /* sudah pernah di-init */
    }

    ledc_timer_config_t timer_cfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = (ledc_timer_bit_t)resolution,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = frequency,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timer_cfg);

    ledc_channel_config_t ch_cfg = {
        .gpio_num = pin,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = channel,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0,
    };
    ledc_channel_config(&ch_cfg);
}

static void real_pwm_write(int pin, uint32_t duty)
{
    bool is_new;
    ledc_channel_t channel = find_or_alloc_ledc_channel(pin, &is_new);
    if (is_new) {
        /* Belum pernah di-init (pwm_init tidak dipanggil) - pakai default
         * 1kHz/12-bit supaya tidak crash, meski idealnya params.frequency/
         * resolution node yang dipakai (dipanggil dari fbd_graph.c). */
        real_pwm_init(pin, 1000, 12);
    }
    ledc_set_duty(LEDC_LOW_SPEED_MODE, channel, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, channel);
}

/* ---- Servo (LEDC 50Hz + konversi angle->pulse width) ---- */

static void real_servo_init(int pin, uint32_t min_us, uint32_t max_us)
{
    (void)min_us; (void)max_us;
    /* Servo standar: 50Hz, resolusi 16-bit cukup presisi untuk pulse
     * width 500-2500us dalam periode 20000us. */
    real_pwm_init(pin, 50, 16);
}

static void real_servo_write(int pin, float angle_deg, uint32_t min_us, uint32_t max_us)
{
    uint32_t pulse_us = min_us + (uint32_t)((angle_deg / 180.0f) * (float)(max_us - min_us));
    uint32_t period_us = 1000000 / 50; /* 20000us pada 50Hz */
    uint32_t max_duty = (1u << 16) - 1u;
    uint32_t duty = (uint32_t)(((uint64_t)pulse_us * max_duty) / period_us);
    real_pwm_write(pin, duty);
}

/* ---- LED RGB addressable (WS2812/NeoPixel, protokol RMT 1-wire) ----
 * Timing WS2812 standar (800kHz): bit1 = 0.8us HIGH + 0.45us LOW,
 * bit0 = 0.4us HIGH + 0.85us LOW, reset/latch >= 50us LOW di akhir frame.
 * RMT resolution 10MHz (100ns/tick) supaya timing di atas bisa dibulatkan
 * ke tick bulat tanpa drift signifikan. Urutan byte per LED: G,R,B (bukan
 * R,G,B - WS2812 hampir semua varian mengirim GRB, dikonversi di
 * real_ws2812_write() supaya API backend tetap r,g,b intuitif). */
#define FBD_HW_MAX_WS2812_PINS 4
#define FBD_HW_WS2812_MAX_LEDS 256 /* batas statis - cukup untuk strip indikator, bukan instalasi besar */

static struct {
    int pin;
    rmt_channel_handle_t channel;
    rmt_encoder_handle_t encoder;
    bool inited;
} s_ws2812_pins[FBD_HW_MAX_WS2812_PINS];
static int s_ws2812_pin_count = 0;

static int find_ws2812_slot(int pin)
{
    for (int i = 0; i < s_ws2812_pin_count; ++i) {
        if (s_ws2812_pins[i].pin == pin) return i;
    }
    return -1;
}

static void real_ws2812_init(int pin, int count)
{
    (void)count;
    if (find_ws2812_slot(pin) >= 0 || s_ws2812_pin_count >= FBD_HW_MAX_WS2812_PINS) {
        return; /* sudah pernah di-init, atau slot penuh */
    }

    rmt_tx_channel_config_t tx_cfg = {
        .gpio_num = pin,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000, /* 10MHz -> 1 tick = 100ns */
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
    };
    rmt_channel_handle_t channel;
    if (rmt_new_tx_channel(&tx_cfg, &channel) != ESP_OK) {
        ESP_LOGE(TAG, "ws2812_init: gagal buat RMT TX channel di GPIO%d", pin);
        return;
    }
    if (rmt_enable(channel) != ESP_OK) {
        ESP_LOGE(TAG, "ws2812_init: gagal enable RMT channel GPIO%d", pin);
        return;
    }

    /* bit1: HIGH 0.8us (8 tick @10MHz) + LOW 0.45us (4-5 tick, dibulatkan 4).
     * bit0: HIGH 0.4us (4 tick) + LOW 0.85us (8-9 tick, dibulatkan 9). */
    rmt_bytes_encoder_config_t bytes_cfg = {
        .bit0 = { .level0 = 1, .duration0 = 4, .level1 = 0, .duration1 = 9 },
        .bit1 = { .level0 = 1, .duration0 = 8, .level1 = 0, .duration1 = 4 },
        .flags.msb_first = 1,
    };
    rmt_encoder_handle_t encoder;
    if (rmt_new_bytes_encoder(&bytes_cfg, &encoder) != ESP_OK) {
        ESP_LOGE(TAG, "ws2812_init: gagal buat bytes encoder GPIO%d", pin);
        return;
    }

    s_ws2812_pins[s_ws2812_pin_count].pin = pin;
    s_ws2812_pins[s_ws2812_pin_count].channel = channel;
    s_ws2812_pins[s_ws2812_pin_count].encoder = encoder;
    s_ws2812_pins[s_ws2812_pin_count].inited = true;
    s_ws2812_pin_count++;
}

/* Pin WS2812 yang sedang "dipinjam" firmware untuk umpan balik sistem
 * (mis. LED reset di GPIO48, lihat main/sys_reset.c): tulisan dari graph
 * (node ws2812 pengguna di pin yang sama) DIABAIKAN selama override aktif,
 * supaya kedipan umpan balik tidak tertimpa tiap scan cycle 20ms. -1 = mati. */
static volatile int s_ws2812_override_pin = -1;

static void ws2812_write_impl(int pin, int count, uint8_t r, uint8_t g, uint8_t b)
{
    int slot = find_ws2812_slot(pin);
    if (slot < 0) {
        real_ws2812_init(pin, count);
        slot = find_ws2812_slot(pin);
        if (slot < 0) return; /* init gagal (mis. slot penuh) */
    }
    if (count < 1) count = 1;
    if (count > FBD_HW_WS2812_MAX_LEDS) count = FBD_HW_WS2812_MAX_LEDS;

    /* Buffer GRB per LED - dialokasikan di stack, aman karena dibatasi
     * FBD_HW_WS2812_MAX_LEDS (256*3 = 768 byte, jauh di bawah stack
     * fbd_scan_task 4096 byte, TIDAK sebesar fbd_graph_t 19KB yang
     * memang wajib scratch buffer statis). Semua LED warna sama sesuai
     * desain node (bukan per-LED individual). */
    uint8_t buf[FBD_HW_WS2812_MAX_LEDS * 3];
    for (int i = 0; i < count; ++i) {
        buf[i * 3 + 0] = g;
        buf[i * 3 + 1] = r;
        buf[i * 3 + 2] = b;
    }

    rmt_transmit_config_t tx_cfg = { .loop_count = 0 };
    esp_err_t err = rmt_transmit(s_ws2812_pins[slot].channel, s_ws2812_pins[slot].encoder,
                                  buf, (size_t)count * 3, &tx_cfg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ws2812_write: rmt_transmit gagal GPIO%d: %d", pin, err);
        return;
    }
    /* Tunggu transmit selesai sebelum return - scan cycle 20ms jauh lebih
     * lama dari waktu kirim WS2812 (mis. 30 LED ~= 30*24 bit*1.25us =
     * ~900us), aman blocking singkat di sini tanpa mengganggu timing
     * scan cycle secara signifikan. */
    rmt_tx_wait_all_done(s_ws2812_pins[slot].channel, 100);
}

static void real_ws2812_write(int pin, int count, uint8_t r, uint8_t g, uint8_t b)
{
    if (pin == s_ws2812_override_pin) {
        return;
    }
    ws2812_write_impl(pin, count, r, g, b);
}

void fbd_hw_ws2812_set_override(int pin)
{
    s_ws2812_override_pin = pin;
}

void fbd_hw_ws2812_write_override(int pin, int count, uint8_t r, uint8_t g, uint8_t b)
{
    ws2812_write_impl(pin, count, r, g, b);
}

/* ---- Sensor jarak ultrasonik (HC-SR04 dkk, trig+echo) ----
 * BLOCKING measurement - satu-satunya cara mengukur jarak dari sensor
 * ini adalah menghitung durasi pulsa echo secara langsung, tidak ada
 * interrupt/DMA yang membebaskan CPU di sini (beda dari RMT WS2812 yang
 * async). Timeout DIBATASI KETAT (lihat ULTRASONIC_TIMEOUT_US) supaya
 * satu node ini tidak menahan scan cycle terlalu lama kalau sensor
 * tidak terpasang/echo tidak pernah naik/turun - konsisten dengan
 * prinsip proyek "jangan pernah menahan scan cycle" yang sudah dipegang
 * untuk I2C_BRIDGE_TIMEOUT_MS.
 *
 * ULTRASONIC_TIMEOUT_US = 10000 (10ms) -> jangkauan efektif ~1.7m
 * (10ms * 343m/s / 2), BUKAN jangkauan penuh spec HC-SR04 (~4m/23ms) -
 * trade-off disengaja: timeout lebih pendek dari SCAN_PERIOD_MS (20ms)
 * supaya node ini sendirian tidak pernah membuat satu scan cycle
 * melebihi periodenya sendiri. Echo lebih lama dari batas ini dianggap
 * timeout/out-of-range (return false), BUKAN jarak jauh yang valid. */
#define ULTRASONIC_TIMEOUT_US 10000
#define ULTRASONIC_SOUND_SPEED_CM_PER_US 0.0343f /* 343 m/s = 0.0343 cm/us */

static void real_ultrasonic_init(int trig_pin, int echo_pin)
{
    gpio_config_t trig_cfg = {
        .pin_bit_mask = 1ULL << trig_pin,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&trig_cfg);
    gpio_set_level((gpio_num_t)trig_pin, 0);

    gpio_config_t echo_cfg = {
        .pin_bit_mask = 1ULL << echo_pin,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&echo_cfg);
}

static bool real_ultrasonic_read(int trig_pin, int echo_pin, float *out_distance_cm)
{
    *out_distance_cm = 0.0f;

    /* Trig pulse 10us (spec HC-SR04: minimal 10us HIGH untuk memicu
     * pengukuran) - esp_rom_delay_us blocking singkat, sama pola dengan
     * i2c_bridge_delay_us untuk WS2812/I2C burst. */
    gpio_set_level((gpio_num_t)trig_pin, 1);
    esp_rom_delay_us(10);
    gpio_set_level((gpio_num_t)trig_pin, 0);

    int64_t start_wait = esp_timer_get_time();
    /* Tunggu echo naik ke HIGH (sensor mulai mengukur). Timeout di sini
     * pakai SISA budget ULTRASONIC_TIMEOUT_US, bukan alokasi tetap
     * terpisah - total waktu tunggu-naik + ukur-durasi tidak pernah
     * melebihi ULTRASONIC_TIMEOUT_US. */
    while (gpio_get_level((gpio_num_t)echo_pin) == 0) {
        if (esp_timer_get_time() - start_wait > ULTRASONIC_TIMEOUT_US) {
            return false; /* echo tidak pernah naik - sensor tidak terpasang/rusak */
        }
    }

    int64_t echo_start = esp_timer_get_time();
    while (gpio_get_level((gpio_num_t)echo_pin) == 1) {
        if (esp_timer_get_time() - start_wait > ULTRASONIC_TIMEOUT_US) {
            return false; /* echo tidak pernah turun - di luar jangkauan efektif atau macet */
        }
    }
    int64_t echo_duration_us = esp_timer_get_time() - echo_start;

    /* Jarak = (durasi pulsa * kecepatan suara) / 2 - dibagi 2 karena
     * pulsa echo mengukur waktu tempuh PULANG-PERGI (ke objek dan
     * kembali), bukan satu arah. */
    *out_distance_cm = ((float)echo_duration_us * ULTRASONIC_SOUND_SPEED_CM_PER_US) / 2.0f;
    return true;
}

static const fbd_hw_backend_t s_real_backend = {
    .digital_init_input = real_digital_init_input,
    .digital_init_output = real_digital_init_output,
    .digital_read = real_digital_read,
    .digital_write = real_digital_write,
    .analog_init = real_analog_init,
    .analog_read = real_analog_read,
    .pwm_init = real_pwm_init,
    .pwm_write = real_pwm_write,
    .servo_init = real_servo_init,
    .servo_write = real_servo_write,
    .ws2812_init = real_ws2812_init,
    .ws2812_write = real_ws2812_write,
    .ultrasonic_init = real_ultrasonic_init,
    .ultrasonic_read = real_ultrasonic_read,
};

const fbd_hw_backend_t *fbd_hw_real_backend(void)
{
    return &s_real_backend;
}

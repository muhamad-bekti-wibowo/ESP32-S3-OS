/* Backend real: driver ESP-IDF nyata (gpio, adc_oneshot, ledc). Hanya
 * dikompilasi sebagai bagian firmware ESP32-S3 (butuh header ESP-IDF) -
 * TIDAK dikompilasi untuk test host, lihat test_host/build_and_run.ps1. */
#include "fbd_hw_backend.h"
#include "fbd_graph.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_adc/adc_oneshot.h"
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
};

const fbd_hw_backend_t *fbd_hw_real_backend(void)
{
    return &s_real_backend;
}

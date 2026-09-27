#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "fbd_value.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Interface dual backend (plan.md §3): setiap operasi peripheral fisik
 * dipanggil lewat function pointer ini, bukan branch if/else hardcoded di
 * fbd_graph.c. Backend dipilih via fbd_hw_set_backend() saat startup
 * firmware (spec 05: fbd_hw_sim_backend() default, atau fbd_hw_real_backend()
 * kalau target ESP32-S3 sungguhan). Node individual tetap punya
 * params.hw_mode sendiri (FBD_HW_SIMULATED/FBD_HW_REAL) untuk analog_input/
 * pwm_output/servo - itu menentukan APAKAH backend real dipanggil untuk
 * node itu, bukan backend global mana yang aktif. digital_input/digital_output
 * SELALU pakai backend aktif (tidak ada mode simulated/real per-node,
 * karena GPIO digital sudah stabil sejak Level 0/1 dasar). */
typedef struct {
    /* --- Digital I/O --- */
    void (*digital_init_input)(int pin, int pin_mode);   /* pin_mode: fbd_pin_mode_t */
    void (*digital_init_output)(int pin);
    bool (*digital_read)(int pin);
    void (*digital_write)(int pin, bool level);

    /* --- Analog input (ADC) --- */
    void (*analog_init)(int pin, int resolution, int attenuation);
    /* sim_value dipakai backend simulated (abaikan pin/resolution/attenuation
     * fisik), backend real membaca ADC sungguhan dan mengabaikan sim_value. */
    fbd_value_t (*analog_read)(int pin, int resolution, int attenuation, fbd_value_t sim_value);

    /* --- PWM output (LEDC) --- */
    void (*pwm_init)(int pin, uint32_t frequency, int resolution);
    void (*pwm_write)(int pin, uint32_t duty);

    /* --- Servo (LEDC + konversi angle->pulse width) --- */
    void (*servo_init)(int pin, uint32_t min_us, uint32_t max_us);
    void (*servo_write)(int pin, float angle_deg, uint32_t min_us, uint32_t max_us);
} fbd_hw_backend_t;

/* Backend simulated: tidak menyentuh register apa pun, dipakai default
 * supaya runtime bisa jalan 100% tanpa hardware (plan.md prinsip #4). */
const fbd_hw_backend_t *fbd_hw_sim_backend(void);

/* Backend real: ESP-IDF driver nyata (gpio, adc_oneshot, ledc). Hanya
 * dikompilasi & dipakai kalau target ESP32-S3 (lihat fbd_hw_real.c). */
const fbd_hw_backend_t *fbd_hw_real_backend(void);

/* Set backend aktif untuk fbd_graph_execute_cycle(). Panggil sekali di
 * app startup sebelum scan cycle jalan. Default (sebelum pernah dipanggil):
 * fbd_hw_sim_backend(), supaya test host tetap jalan tanpa setup apa pun. */
void fbd_hw_set_backend(const fbd_hw_backend_t *backend);
const fbd_hw_backend_t *fbd_hw_get_backend(void);

#ifdef __cplusplus
}
#endif

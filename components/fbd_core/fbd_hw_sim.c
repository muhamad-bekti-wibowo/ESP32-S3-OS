/* Backend simulated: tidak menyentuh register/hardware apa pun. Dites di
 * host (PC) tanpa ESP32 - lihat test_host/fbd_hw_test.c. */
#include "fbd_hw_backend.h"
#include <stddef.h>

static void sim_digital_init_input(int pin, int pin_mode)
{
    (void)pin; (void)pin_mode;
}

static void sim_digital_init_output(int pin)
{
    (void)pin;
}

static bool sim_digital_read(int pin)
{
    /* Tidak ada hardware nyata untuk dibaca - default false (aman: relay/LED
     * tidak menyala tanpa sengaja saat simulated). Node digital_input pass-
     * through inputs[0] tetap dipakai lewat evaluate_node() di fbd_graph.c
     * untuk skenario test host yang set inputs[0] manual. */
    (void)pin;
    return false;
}

static void sim_digital_write(int pin, bool level)
{
    (void)pin; (void)level;
}

static void sim_analog_init(int pin, int resolution, int attenuation)
{
    (void)pin; (void)resolution; (void)attenuation;
}

static fbd_value_t sim_analog_read(int pin, int resolution, int attenuation, fbd_value_t sim_value)
{
    (void)pin; (void)resolution; (void)attenuation;
    return sim_value;
}

static void sim_pwm_init(int pin, uint32_t frequency, int resolution)
{
    (void)pin; (void)frequency; (void)resolution;
}

static void sim_pwm_write(int pin, uint32_t duty)
{
    (void)pin; (void)duty;
}

static void sim_servo_init(int pin, uint32_t min_us, uint32_t max_us)
{
    (void)pin; (void)min_us; (void)max_us;
}

static void sim_servo_write(int pin, float angle_deg, uint32_t min_us, uint32_t max_us)
{
    (void)pin; (void)angle_deg; (void)min_us; (void)max_us;
}

static void sim_ws2812_init(int pin, int count)
{
    (void)pin; (void)count;
}

static void sim_ws2812_write(int pin, int count, uint8_t r, uint8_t g, uint8_t b)
{
    (void)pin; (void)count; (void)r; (void)g; (void)b;
}

static const fbd_hw_backend_t s_sim_backend = {
    .digital_init_input = sim_digital_init_input,
    .digital_init_output = sim_digital_init_output,
    .digital_read = sim_digital_read,
    .digital_write = sim_digital_write,
    .analog_init = sim_analog_init,
    .analog_read = sim_analog_read,
    .pwm_init = sim_pwm_init,
    .pwm_write = sim_pwm_write,
    .servo_init = sim_servo_init,
    .servo_write = sim_servo_write,
    .ws2812_init = sim_ws2812_init,
    .ws2812_write = sim_ws2812_write,
};

const fbd_hw_backend_t *fbd_hw_sim_backend(void)
{
    return &s_sim_backend;
}

static const fbd_hw_backend_t *s_active_backend = &s_sim_backend;

void fbd_hw_set_backend(const fbd_hw_backend_t *backend)
{
    s_active_backend = backend ? backend : &s_sim_backend;
}

const fbd_hw_backend_t *fbd_hw_get_backend(void)
{
    return s_active_backend;
}

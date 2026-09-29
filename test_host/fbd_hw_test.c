/* Test host murni C untuk node Level 1 (analog_input/pwm_output/servo)
 * mode simulated - TANPA hardware ADC/PWM/servo nyata sama sekali.
 * Lihat specs/05-level1-io.md kriteria selesai 5a. */
#include <stdio.h>
#include "fbd_graph.h"
#include "fbd_hw_backend.h"

static int g_fail = 0;

#define CHECK(cond, msg) do { \
    if (cond) { printf("PASS: %s\n", msg); } \
    else { printf("FAIL: %s\n", msg); g_fail++; } \
} while (0)

/* Kriteria selesai 5a: analog_input(simulated, value=2048) -> SCALE(0-4095 -> 0-100)
 * -> Compare > 50 -> digital_output, TANPA hardware ADC nyata tersambung. */
static void test_analog_scale_compare_digital_out(void)
{
    fbd_graph_t g;
    fbd_graph_init(&g);

    fbd_node_t *ai = fbd_graph_add_node(&g, "ai1", FBD_NODE_ANALOG_IN);
    ai->params.hw_mode = FBD_HW_SIMULATED;
    ai->params.pin = 4;
    ai->params.resolution = 12;
    ai->params.attenuation = 11;
    ai->params.sim_value = fbd_make_int(1500); /* ~36.6% dari rentang 12-bit (0-4095), jelas di bawah 50 */

    fbd_node_t *sc = fbd_graph_add_node(&g, "sc1", FBD_NODE_SCALE);
    sc->params.in_min = 0; sc->params.in_max = 4095;
    sc->params.out_min = 0; sc->params.out_max = 100;

    fbd_node_t *cmp = fbd_graph_add_node(&g, "cmp1", FBD_NODE_COMPARE);
    cmp->params.compare_op = FBD_CMP_GT;

    fbd_node_t *cst = fbd_graph_add_node(&g, "cst1", FBD_NODE_CONST);
    cst->params.const_value = fbd_make_float(50.0f);

    fbd_node_t *dout = fbd_graph_add_node(&g, "dout1", FBD_NODE_DIGITAL_OUT);
    dout->params.hw_mode = FBD_HW_SIMULATED;
    dout->params.pin = 2;

    fbd_graph_add_link(&g, "ai1", 0, "sc1", 0);
    fbd_graph_add_link(&g, "sc1", 0, "cmp1", 0);
    fbd_graph_add_link(&g, "cst1", 0, "cmp1", 1);
    fbd_graph_add_link(&g, "cmp1", 0, "dout1", 0);

    CHECK(fbd_graph_compile(&g), "compile() sukses untuk rangkaian analog_input->scale->compare->digital_out");

    fbd_graph_execute_cycle(&g, 0);

    size_t sc_idx = fbd_graph_find_node(&g, "sc1");
    float scaled = fbd_to_float(g.nodes[sc_idx].outputs[0]);
    printf("scaled value (1500/4095 -> 0-100): %.2f\n", scaled);
    CHECK(scaled > 35.0f && scaled < 38.0f, "SCALE(1500, 0-4095 -> 0-100) mendekati 36.6");

    size_t dout_idx = fbd_graph_find_node(&g, "dout1");
    CHECK(fbd_to_bool(g.nodes[dout_idx].outputs[0]) == false,
          "value=1500 (~36.6%) TIDAK > threshold 50 -> digital_output false");

    /* Naikkan sim_value melewati threshold, verifikasi output berubah. */
    size_t ai_idx = fbd_graph_find_node(&g, "ai1");
    g.nodes[ai_idx].params.sim_value = fbd_make_int(3500); /* ~85% */
    fbd_graph_execute_cycle(&g, 20);
    CHECK(fbd_to_bool(g.nodes[dout_idx].outputs[0]) == true,
          "value=3500 (~85%) > threshold 50 -> digital_output true");

    /* Backend simulated TIDAK PERNAH menyentuh hardware - tidak ada cara
     * langsung membuktikan "no register access" dari test host (tidak ada
     * register di PC), tapi fbd_hw_sim_backend() dipakai default tanpa
     * pernah memanggil fbd_hw_set_backend(real) - itu jaminan struktural
     * bahwa test ini tidak pernah menyentuh apa pun selain fbd_hw_sim.c. */
    CHECK(fbd_hw_get_backend() == fbd_hw_sim_backend(),
          "backend aktif default adalah simulated (bukan real) tanpa perlu setup apa pun");
}

/* pwm_output dan servo mode simulated: cukup pass-through/hitung angka
 * untuk indikator UI, tidak menyentuh hardware. */
static void test_pwm_and_servo_simulated(void)
{
    fbd_graph_t g;
    fbd_graph_init(&g);

    fbd_node_t *pwm = fbd_graph_add_node(&g, "pwm1", FBD_NODE_PWM_OUT);
    pwm->params.hw_mode = FBD_HW_SIMULATED;
    pwm->params.pin = 5;
    pwm->params.frequency = 1000;
    pwm->params.resolution = 12;

    fbd_node_t *duty_const = fbd_graph_add_node(&g, "duty1", FBD_NODE_CONST);
    duty_const->params.const_value = fbd_make_float(75.0f); /* 75% duty */

    fbd_graph_add_link(&g, "duty1", 0, "pwm1", 0);

    fbd_node_t *servo = fbd_graph_add_node(&g, "servo1", FBD_NODE_SERVO);
    servo->params.hw_mode = FBD_HW_SIMULATED;
    servo->params.pin = 18;
    servo->params.min_us = 500;
    servo->params.max_us = 2500;

    fbd_node_t *angle_const = fbd_graph_add_node(&g, "angle1", FBD_NODE_CONST);
    angle_const->params.const_value = fbd_make_float(90.0f);

    fbd_graph_add_link(&g, "angle1", 0, "servo1", 0);

    CHECK(fbd_graph_compile(&g), "compile() sukses untuk rangkaian pwm_output & servo simulated");

    fbd_graph_execute_cycle(&g, 0);

    size_t pwm_idx = fbd_graph_find_node(&g, "pwm1");
    CHECK(fbd_to_float(g.nodes[pwm_idx].outputs[0]) == 75.0f,
          "pwm_output simulated: outputs[0] = 75 (duty% utk indikator UI)");

    size_t servo_idx = fbd_graph_find_node(&g, "servo1");
    CHECK(fbd_to_float(g.nodes[servo_idx].outputs[0]) == 90.0f,
          "servo simulated: outputs[0] = 90 (sudut utk indikator UI)");

    /* Servo clamp ke rentang 0-180. */
    angle_const->params.const_value = fbd_make_float(250.0f);
    fbd_graph_execute_cycle(&g, 20);
    CHECK(fbd_to_float(g.nodes[servo_idx].outputs[0]) == 180.0f,
          "servo: sudut di luar rentang di-clamp ke 180 (maks)");
}

/* ws2812 mode simulated: tidak menyentuh RMT, outputs[0] pass-through
 * channel R untuk indikator UI. Clamp rentang 0-255 juga dites di sini
 * (nilai luar rentang, mis. hasil scale, tidak boleh wrap-around). */
static void test_ws2812_simulated(void)
{
    fbd_graph_t g;
    fbd_graph_init(&g);

    fbd_node_t *led = fbd_graph_add_node(&g, "led1", FBD_NODE_WS2812);
    led->params.hw_mode = FBD_HW_SIMULATED;
    led->params.pin = 8;
    led->params.ws2812_count = 10;

    fbd_node_t *r = fbd_graph_add_node(&g, "r1", FBD_NODE_CONST);
    r->params.const_value = fbd_make_float(255.0f);
    fbd_node_t *gr = fbd_graph_add_node(&g, "g1", FBD_NODE_CONST);
    gr->params.const_value = fbd_make_float(128.0f);
    fbd_node_t *b = fbd_graph_add_node(&g, "b1", FBD_NODE_CONST);
    b->params.const_value = fbd_make_float(64.0f);

    fbd_graph_add_link(&g, "r1", 0, "led1", 0);
    fbd_graph_add_link(&g, "g1", 0, "led1", 1);
    fbd_graph_add_link(&g, "b1", 0, "led1", 2);

    CHECK(fbd_graph_compile(&g), "compile() sukses untuk rangkaian ws2812 simulated (3 input R/G/B)");

    fbd_graph_execute_cycle(&g, 0);

    size_t idx = fbd_graph_find_node(&g, "led1");
    CHECK(g.nodes[idx].outputs[0].i == 255, "ws2812 simulated: outputs[0] pass-through R=255");

    /* Clamp: input di luar rentang 0-255 tidak boleh wrap-around/overflow. */
    r->params.const_value = fbd_make_float(999.0f);
    b->params.const_value = fbd_make_float(-50.0f);
    fbd_graph_execute_cycle(&g, 20);
    CHECK(g.nodes[idx].outputs[0].i == 255, "ws2812: R di luar rentang (999) di-clamp ke 255");
}

/* ultrasonic mode simulated: pakai sim_distance_cm langsung, tidak
 * menyentuh GPIO/timing sama sekali - outputs[1] (error) harus selalu
 * false karena mode simulated tidak pernah "gagal ukur". */
static void test_ultrasonic_simulated(void)
{
    fbd_graph_t g;
    fbd_graph_init(&g);

    fbd_node_t *us = fbd_graph_add_node(&g, "us1", FBD_NODE_ULTRASONIC);
    us->params.hw_mode = FBD_HW_SIMULATED;
    us->params.pin = 4;
    us->params.ultrasonic_echo_pin = 5;
    us->params.ultrasonic_sim_distance_cm = 42.5f;

    CHECK(fbd_graph_compile(&g), "compile() sukses untuk rangkaian ultrasonic simulated");
    fbd_graph_execute_cycle(&g, 0);

    size_t idx = fbd_graph_find_node(&g, "us1");
    CHECK(fbd_to_float(g.nodes[idx].outputs[0]) == 42.5f,
          "ultrasonic simulated: outputs[0] = sim_distance_cm (42.5), tidak menyentuh GPIO");
    CHECK(fbd_to_bool(g.nodes[idx].outputs[1]) == false,
          "ultrasonic simulated: outputs[1] (error) selalu false, mode simulated tidak pernah gagal ukur");
}

int main(void)
{
    test_analog_scale_compare_digital_out();
    test_pwm_and_servo_simulated();
    test_ws2812_simulated();
    test_ultrasonic_simulated();

    printf("\n%s (%d gagal)\n", g_fail == 0 ? "SEMUA TEST LOLOS" : "ADA TEST GAGAL", g_fail);
    return g_fail == 0 ? 0 : 1;
}

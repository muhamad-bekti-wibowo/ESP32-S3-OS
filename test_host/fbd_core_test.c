/* Test host murni C, tanpa ESP-IDF/Arduino. Compile & run di PC.
 * Lihat specs/01-fbdvalue-core.md untuk kriteria selesai. */
#include <stdio.h>
#include <assert.h>
#include "fbd_value.h"
#include "fbd_nodes.h"

static int g_fail = 0;

#define CHECK(cond, msg) do { \
    if (cond) { printf("PASS: %s\n", msg); } \
    else { printf("FAIL: %s\n", msg); g_fail++; } \
} while (0)

static void test_sizeof(void)
{
    printf("sizeof(fbd_value_t) = %zu bytes\n", sizeof(fbd_value_t));
    /* Diharapkan kecil (union terbesar: bytes[8]+len, plus tag) -> ~12-16 byte
     * tergantung alignment. Tidak boleh puluhan byte karena padding aneh. */
    CHECK(sizeof(fbd_value_t) <= 16, "sizeof(fbd_value_t) <= 16 bytes");
}

static void test_conversions(void)
{
    fbd_value_t vb = fbd_make_bool(true);
    fbd_value_t vi = fbd_make_int(42);
    fbd_value_t vf = fbd_make_float(3.5f);

    CHECK(fbd_to_float(vb) == 1.0f, "bool(true) -> float 1.0");
    CHECK(fbd_to_float(vi) == 42.0f, "int(42) -> float 42.0");
    CHECK(fbd_to_bool(vi) == true, "int(42) -> bool true");
    CHECK(fbd_to_bool(fbd_make_int(0)) == false, "int(0) -> bool false");
    CHECK(fbd_to_bool(vf) == true, "float(3.5) -> bool true");
}

static void test_logic_math_compare_chain(void)
{
    /* Constant(10) + Constant(5) = 15, lalu Compare(15 > 12) => true */
    fbd_value_t c1 = fbd_eval_constant(fbd_make_float(10.0f));
    fbd_value_t c2 = fbd_eval_constant(fbd_make_float(5.0f));
    fbd_value_t sum = fbd_eval_math(c1, c2, OP_ADD);
    CHECK(fbd_to_float(sum) == 15.0f, "Constant(10)+Constant(5) = 15");

    fbd_value_t cmp = fbd_eval_compare(sum, fbd_make_float(12.0f), CMP_GT);
    CHECK(fbd_to_bool(cmp) == true, "15 > 12 = true");

    fbd_value_t a = fbd_make_bool(true);
    fbd_value_t b = fbd_make_bool(false);
    CHECK(fbd_to_bool(fbd_eval_and(a, b)) == false, "AND(true,false) = false");
    CHECK(fbd_to_bool(fbd_eval_or(a, b)) == true, "OR(true,false) = true");
    CHECK(fbd_to_bool(fbd_eval_xor(a, b)) == true, "XOR(true,false) = true");
    CHECK(fbd_to_bool(fbd_eval_not(a)) == false, "NOT(true) = false");
}

static void test_scale_clamp(void)
{
    /* 50.0 di rentang [0,100] -> [0,1] harus jadi 0.5, sesuai contoh plan.md */
    fbd_value_t in = fbd_make_float(50.0f);
    fbd_value_t out = fbd_eval_scale(in, 0.0f, 100.0f, 0.0f, 1.0f);
    CHECK(fbd_to_float(out) == 0.5f, "SCALE(50, 0-100 -> 0-1) = 0.5");

    fbd_value_t clamped = fbd_eval_clamp(fbd_make_float(150.0f), 0.0f, 100.0f);
    CHECK(fbd_to_float(clamped) == 100.0f, "CLAMP(150, 0-100) = 100");
}

static void test_variable_store(void)
{
    fbd_var_store_t store;
    fbd_var_store_init(&store);
    fbd_var_set(&store, "counter_a", fbd_make_int(7));
    fbd_value_t got = fbd_var_get(&store, "counter_a");
    CHECK(fbd_to_float(got) == 7.0f, "variable_set/get counter_a = 7");

    fbd_value_t missing = fbd_var_get(&store, "does_not_exist");
    CHECK(missing.type == FBD_EMPTY, "variable_get nama tidak ada -> EMPTY");
}

static void test_timer_ton(void)
{
    fbd_timer_state_t st = {0};
    uint32_t now_ms = 0;
    fbd_value_t on = fbd_make_bool(true);

    /* Simulasi manual maju waktu, bukan sleep() asli. */
    fbd_value_t out;
    out = fbd_eval_ton(on, 2000, now_ms, &st);
    CHECK(fbd_to_bool(out) == false, "TON: t=0, delay belum lewat -> false");

    now_ms = 1000;
    out = fbd_eval_ton(on, 2000, now_ms, &st);
    CHECK(fbd_to_bool(out) == false, "TON: t=1000ms < delay 2000ms -> false");

    now_ms = 2500;
    out = fbd_eval_ton(on, 2000, now_ms, &st);
    CHECK(fbd_to_bool(out) == true, "TON: t=2500ms >= delay 2000ms -> true");

    /* Input jatuh ke OFF harus reset timer */
    fbd_value_t off = fbd_make_bool(false);
    now_ms = 2600;
    out = fbd_eval_ton(off, 2000, now_ms, &st);
    CHECK(fbd_to_bool(out) == false, "TON: input OFF -> output langsung false");
}

static void test_timer_tof_tp(void)
{
    fbd_timer_state_t st_tof = {0};
    uint32_t now_ms = 0;
    fbd_value_t on = fbd_make_bool(true);
    fbd_value_t off = fbd_make_bool(false);

    fbd_value_t out = fbd_eval_tof(on, 1000, now_ms, &st_tof);
    CHECK(fbd_to_bool(out) == true, "TOF: input ON -> output ON");

    now_ms = 100;
    out = fbd_eval_tof(off, 1000, now_ms, &st_tof);
    CHECK(fbd_to_bool(out) == true, "TOF: input baru OFF, delay belum lewat -> masih ON");

    now_ms = 1200;
    out = fbd_eval_tof(off, 1000, now_ms, &st_tof);
    CHECK(fbd_to_bool(out) == false, "TOF: delay lewat -> output OFF");

    fbd_timer_state_t st_tp = {0};
    now_ms = 0;
    out = fbd_eval_tp(on, 500, now_ms, &st_tp);
    CHECK(fbd_to_bool(out) == true, "TP: trigger OFF->ON -> pulsa ON");

    now_ms = 600;
    out = fbd_eval_tp(on, 500, now_ms, &st_tp);
    CHECK(fbd_to_bool(out) == false, "TP: setelah durasi pulsa lewat -> OFF walau input masih ON");
}

static void test_counter_ctu(void)
{
    fbd_counter_state_t st = {0};
    fbd_value_t reset = fbd_make_bool(false);
    fbd_value_t out;

    out = fbd_eval_ctu(fbd_make_bool(true), reset, 3, &st);
    CHECK(fbd_to_bool(out) == false, "CTU: count=1 < preset 3 -> false");
    fbd_eval_ctu(fbd_make_bool(false), reset, 3, &st); /* tepi turun, tidak nambah */
    out = fbd_eval_ctu(fbd_make_bool(true), reset, 3, &st);
    CHECK(fbd_to_bool(out) == false, "CTU: count=2 < preset 3 -> false");
    fbd_eval_ctu(fbd_make_bool(false), reset, 3, &st);
    out = fbd_eval_ctu(fbd_make_bool(true), reset, 3, &st);
    CHECK(fbd_to_bool(out) == true, "CTU: count=3 >= preset 3 -> true");

    out = fbd_eval_ctu(fbd_make_bool(true), fbd_make_bool(true), 3, &st);
    CHECK(fbd_to_bool(out) == false, "CTU: reset -> count kembali 0, output false");
}

int main(void)
{
    test_sizeof();
    test_conversions();
    test_logic_math_compare_chain();
    test_scale_clamp();
    test_variable_store();
    test_timer_ton();
    test_timer_tof_tp();
    test_counter_ctu();

    printf("\n%s (%d gagal)\n", g_fail == 0 ? "SEMUA TEST LOLOS" : "ADA TEST GAGAL", g_fail);
    return g_fail == 0 ? 0 : 1;
}

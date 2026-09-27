#pragma once

#include <stddef.h>
#include "fbd_value.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Logic ---- */
fbd_value_t fbd_eval_and(fbd_value_t a, fbd_value_t b);
fbd_value_t fbd_eval_or(fbd_value_t a, fbd_value_t b);
fbd_value_t fbd_eval_not(fbd_value_t a);
fbd_value_t fbd_eval_xor(fbd_value_t a, fbd_value_t b);
fbd_value_t fbd_eval_nand(fbd_value_t a, fbd_value_t b);
fbd_value_t fbd_eval_nor(fbd_value_t a, fbd_value_t b);

typedef enum {
    CMP_GT = 0, CMP_LT, CMP_EQ, CMP_NEQ, CMP_GTE, CMP_LTE
} compare_op_t;

fbd_value_t fbd_eval_compare(fbd_value_t a, fbd_value_t b, compare_op_t op);

/* ---- Data ---- */
fbd_value_t fbd_eval_constant(fbd_value_t configured_value);

#define FBD_VAR_NAME_LEN 16
#define FBD_MAX_VARS 32

typedef struct {
    char name[FBD_VAR_NAME_LEN];
    fbd_value_t value;
} fbd_var_slot_t;

typedef struct {
    fbd_var_slot_t slots[FBD_MAX_VARS];
    size_t count;
} fbd_var_store_t;

void fbd_var_store_init(fbd_var_store_t *store);
void fbd_var_set(fbd_var_store_t *store, const char *name, fbd_value_t value);
/* Return FBD_EMPTY value kalau nama belum pernah di-set. */
fbd_value_t fbd_var_get(const fbd_var_store_t *store, const char *name);

/* ---- Math ---- */
typedef enum {
    OP_ADD = 0, OP_SUB, OP_MUL, OP_DIV
} math_op_t;

fbd_value_t fbd_eval_math(fbd_value_t a, fbd_value_t b, math_op_t op);
fbd_value_t fbd_eval_min(fbd_value_t a, fbd_value_t b);
fbd_value_t fbd_eval_max(fbd_value_t a, fbd_value_t b);
fbd_value_t fbd_eval_abs(fbd_value_t a);
fbd_value_t fbd_eval_scale(fbd_value_t in, float in_min, float in_max, float out_min, float out_max);
fbd_value_t fbd_eval_clamp(fbd_value_t in, float min_val, float max_val);

/* ---- Timing ---- */
typedef struct {
    uint32_t start_ms;
    bool running;
    bool prev_input;
} fbd_timer_state_t;

/* TON: output ON setelah input ON selama >= delay_ms terus-menerus. */
fbd_value_t fbd_eval_ton(fbd_value_t input, uint32_t delay_ms, uint32_t now_ms, fbd_timer_state_t *state);

/* TOF: output tetap ON sesaat setelah input jatuh ke OFF. */
fbd_value_t fbd_eval_tof(fbd_value_t input, uint32_t delay_ms, uint32_t now_ms, fbd_timer_state_t *state);

/* TP: trigger OFF->ON menghasilkan pulsa ON berdurasi tetap, mengabaikan input setelahnya
 * sampai pulsa selesai. */
fbd_value_t fbd_eval_tp(fbd_value_t input, uint32_t pulse_ms, uint32_t now_ms, fbd_timer_state_t *state);

typedef struct {
    int32_t count;
    bool prev_clk;
} fbd_counter_state_t;

/* CTU: naik tiap tepi naik (OFF->ON) di clk. Output true saat count >= preset_value. */
fbd_value_t fbd_eval_ctu(fbd_value_t clk, fbd_value_t reset, int32_t preset_value, fbd_counter_state_t *state);

#ifdef __cplusplus
}
#endif

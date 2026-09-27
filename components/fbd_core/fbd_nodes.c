#include "fbd_nodes.h"
#include <string.h>

/* ---- Logic ---- */

fbd_value_t fbd_eval_and(fbd_value_t a, fbd_value_t b)
{
    return fbd_make_bool(fbd_to_bool(a) && fbd_to_bool(b));
}

fbd_value_t fbd_eval_or(fbd_value_t a, fbd_value_t b)
{
    return fbd_make_bool(fbd_to_bool(a) || fbd_to_bool(b));
}

fbd_value_t fbd_eval_not(fbd_value_t a)
{
    return fbd_make_bool(!fbd_to_bool(a));
}

fbd_value_t fbd_eval_xor(fbd_value_t a, fbd_value_t b)
{
    return fbd_make_bool(fbd_to_bool(a) != fbd_to_bool(b));
}

fbd_value_t fbd_eval_nand(fbd_value_t a, fbd_value_t b)
{
    return fbd_make_bool(!(fbd_to_bool(a) && fbd_to_bool(b)));
}

fbd_value_t fbd_eval_nor(fbd_value_t a, fbd_value_t b)
{
    return fbd_make_bool(!(fbd_to_bool(a) || fbd_to_bool(b)));
}

fbd_value_t fbd_eval_compare(fbd_value_t a, fbd_value_t b, compare_op_t op)
{
    float fa = fbd_to_float(a);
    float fb = fbd_to_float(b);
    bool result = false;
    switch (op) {
        case CMP_GT:  result = fa > fb;  break;
        case CMP_LT:  result = fa < fb;  break;
        case CMP_EQ:  result = fa == fb; break;
        case CMP_NEQ: result = fa != fb; break;
        case CMP_GTE: result = fa >= fb; break;
        case CMP_LTE: result = fa <= fb; break;
    }
    return fbd_make_bool(result);
}

/* ---- Data ---- */

fbd_value_t fbd_eval_constant(fbd_value_t configured_value)
{
    return configured_value;
}

void fbd_var_store_init(fbd_var_store_t *store)
{
    memset(store, 0, sizeof(*store));
}

void fbd_var_set(fbd_var_store_t *store, const char *name, fbd_value_t value)
{
    for (size_t i = 0; i < store->count; ++i) {
        if (strncmp(store->slots[i].name, name, FBD_VAR_NAME_LEN) == 0) {
            store->slots[i].value = value;
            return;
        }
    }
    if (store->count < FBD_MAX_VARS) {
        fbd_var_slot_t *slot = &store->slots[store->count++];
        strncpy(slot->name, name, FBD_VAR_NAME_LEN - 1);
        slot->name[FBD_VAR_NAME_LEN - 1] = '\0';
        slot->value = value;
    }
}

fbd_value_t fbd_var_get(const fbd_var_store_t *store, const char *name)
{
    for (size_t i = 0; i < store->count; ++i) {
        if (strncmp(store->slots[i].name, name, FBD_VAR_NAME_LEN) == 0) {
            return store->slots[i].value;
        }
    }
    return fbd_make_empty();
}

/* ---- Math ---- */

fbd_value_t fbd_eval_math(fbd_value_t a, fbd_value_t b, math_op_t op)
{
    float fa = fbd_to_float(a);
    float fb = fbd_to_float(b);
    switch (op) {
        case OP_ADD: return fbd_make_float(fa + fb);
        case OP_SUB: return fbd_make_float(fa - fb);
        case OP_MUL: return fbd_make_float(fa * fb);
        case OP_DIV: return fbd_make_float(fb == 0.0f ? 0.0f : fa / fb);
        default:     return fbd_make_float(0.0f);
    }
}

fbd_value_t fbd_eval_min(fbd_value_t a, fbd_value_t b)
{
    float fa = fbd_to_float(a);
    float fb = fbd_to_float(b);
    return fbd_make_float(fa < fb ? fa : fb);
}

fbd_value_t fbd_eval_max(fbd_value_t a, fbd_value_t b)
{
    float fa = fbd_to_float(a);
    float fb = fbd_to_float(b);
    return fbd_make_float(fa > fb ? fa : fb);
}

fbd_value_t fbd_eval_abs(fbd_value_t a)
{
    float fa = fbd_to_float(a);
    return fbd_make_float(fa < 0.0f ? -fa : fa);
}

fbd_value_t fbd_eval_scale(fbd_value_t in, float in_min, float in_max, float out_min, float out_max)
{
    float val = fbd_to_float(in);
    float in_span = in_max - in_min;
    if (in_span == 0.0f) {
        return fbd_make_float(out_min);
    }
    float out_span = out_max - out_min;
    return fbd_make_float(out_min + ((val - in_min) / in_span) * out_span);
}

fbd_value_t fbd_eval_clamp(fbd_value_t in, float min_val, float max_val)
{
    float val = fbd_to_float(in);
    if (val < min_val) {
        val = min_val;
    }
    if (val > max_val) {
        val = max_val;
    }
    return fbd_make_float(val);
}

/* ---- Timing ---- */

fbd_value_t fbd_eval_ton(fbd_value_t input, uint32_t delay_ms, uint32_t now_ms, fbd_timer_state_t *state)
{
    bool in = fbd_to_bool(input);
    if (in && !state->prev_input) {
        state->start_ms = now_ms;
        state->running = true;
    } else if (!in) {
        state->running = false;
    }
    state->prev_input = in;
    bool out = state->running && (now_ms - state->start_ms >= delay_ms);
    return fbd_make_bool(out);
}

fbd_value_t fbd_eval_tof(fbd_value_t input, uint32_t delay_ms, uint32_t now_ms, fbd_timer_state_t *state)
{
    bool in = fbd_to_bool(input);
    if (!in && state->prev_input) {
        state->start_ms = now_ms;
        state->running = true;
    } else if (in) {
        state->running = false;
    }
    state->prev_input = in;
    bool out = in || (state->running && (now_ms - state->start_ms < delay_ms));
    return fbd_make_bool(out);
}

fbd_value_t fbd_eval_tp(fbd_value_t input, uint32_t pulse_ms, uint32_t now_ms, fbd_timer_state_t *state)
{
    bool in = fbd_to_bool(input);
    if (in && !state->prev_input && !state->running) {
        state->start_ms = now_ms;
        state->running = true;
    }
    state->prev_input = in;
    if (state->running && (now_ms - state->start_ms >= pulse_ms)) {
        state->running = false;
    }
    return fbd_make_bool(state->running);
}

fbd_value_t fbd_eval_ctu(fbd_value_t clk, fbd_value_t reset, int32_t preset_value, fbd_counter_state_t *state)
{
    if (fbd_to_bool(reset)) {
        state->count = 0;
        state->prev_clk = false;
        return fbd_make_bool(false);
    }
    bool clk_now = fbd_to_bool(clk);
    if (clk_now && !state->prev_clk) {
        state->count++;
    }
    state->prev_clk = clk_now;
    return fbd_make_bool(state->count >= preset_value);
}

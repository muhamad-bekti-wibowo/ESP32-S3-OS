#include "logic_engine.h"
#include <string.h>
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "logic_engine";

void logic_engine_init(logic_program_t *prog)
{
    memset(prog, 0, sizeof(*prog));
    for (int i = 0; i < LOGIC_MAX_BLOCKS; i++) {
        for (int j = 0; j < LOGIC_MAX_INPUTS; j++) {
            prog->blocks[i].in[j].block_id = -1;
        }
    }
}

static double input_value(const logic_program_t *prog, link_ref_t ref)
{
    if (ref.block_id < 0 || ref.block_id >= prog->block_count) {
        return 0.0;
    }
    return prog->blocks[ref.block_id].out_value;
}

static block_type_t parse_type(const char *s)
{
    if (!s) return BLOCK_CONST;
    if (strcmp(s, "const") == 0)    return BLOCK_CONST;
    if (strcmp(s, "input") == 0)    return BLOCK_INPUT;
    if (strcmp(s, "output") == 0)   return BLOCK_OUTPUT;
    if (strcmp(s, "compare") == 0)  return BLOCK_COMPARE;
    if (strcmp(s, "if_else") == 0)  return BLOCK_IF_ELSE;
    if (strcmp(s, "counter") == 0)  return BLOCK_COUNTER;
    if (strcmp(s, "math") == 0)     return BLOCK_MATH;
    return BLOCK_CONST;
}

static compare_op_t parse_cmp_op(const char *s)
{
    if (!s) return CMP_EQ;
    if (strcmp(s, ">") == 0)  return CMP_GT;
    if (strcmp(s, "<") == 0)  return CMP_LT;
    if (strcmp(s, "==") == 0) return CMP_EQ;
    if (strcmp(s, "!=") == 0) return CMP_NEQ;
    if (strcmp(s, ">=") == 0) return CMP_GTE;
    if (strcmp(s, "<=") == 0) return CMP_LTE;
    return CMP_EQ;
}

static math_op_t parse_math_op(const char *s)
{
    if (!s) return OP_ADD;
    if (strcmp(s, "+") == 0) return OP_ADD;
    if (strcmp(s, "-") == 0) return OP_SUB;
    if (strcmp(s, "*") == 0) return OP_MUL;
    if (strcmp(s, "/") == 0) return OP_DIV;
    return OP_ADD;
}

bool logic_engine_load_json(logic_program_t *prog, const cJSON *json)
{
    logic_engine_init(prog);

    const cJSON *blocks = cJSON_GetObjectItem(json, "blocks");
    if (!cJSON_IsArray(blocks)) {
        ESP_LOGE(TAG, "field 'blocks' hilang atau bukan array");
        return false;
    }

    int idx = 0;
    const cJSON *b = NULL;
    cJSON_ArrayForEach(b, blocks) {
        if (idx >= LOGIC_MAX_BLOCKS) {
            ESP_LOGE(TAG, "melebihi LOGIC_MAX_BLOCKS (%d)", LOGIC_MAX_BLOCKS);
            return false;
        }
        logic_block_t *blk = &prog->blocks[idx];

        const cJSON *name = cJSON_GetObjectItem(b, "name");
        if (cJSON_IsString(name)) {
            strncpy(blk->name, name->valuestring, LOGIC_NAME_LEN - 1);
        }

        blk->type = parse_type(cJSON_GetObjectItem(b, "type") ? cJSON_GetObjectItem(b, "type")->valuestring : NULL);

        const cJSON *inputs = cJSON_GetObjectItem(b, "in");
        if (cJSON_IsArray(inputs)) {
            int j = 0;
            const cJSON *ref = NULL;
            cJSON_ArrayForEach(ref, inputs) {
                if (j >= LOGIC_MAX_INPUTS) break;
                blk->in[j].block_id = cJSON_IsNumber(ref) ? ref->valueint : -1;
                j++;
            }
        }

        const cJSON *cfg = cJSON_GetObjectItem(b, "cfg");
        switch (blk->type) {
        case BLOCK_CONST: {
            const cJSON *v = cfg ? cJSON_GetObjectItem(cfg, "value") : NULL;
            blk->cfg.konst.value = cJSON_IsNumber(v) ? v->valuedouble : 0.0;
            break;
        }
        case BLOCK_INPUT: {
            const cJSON *g = cfg ? cJSON_GetObjectItem(cfg, "gpio") : NULL;
            const cJSON *a = cfg ? cJSON_GetObjectItem(cfg, "analog") : NULL;
            blk->cfg.input.gpio = cJSON_IsNumber(g) ? g->valueint : -1;
            blk->cfg.input.analog = cJSON_IsBool(a) ? cJSON_IsTrue(a) : false;
            break;
        }
        case BLOCK_OUTPUT: {
            const cJSON *g = cfg ? cJSON_GetObjectItem(cfg, "gpio") : NULL;
            blk->cfg.output.gpio = cJSON_IsNumber(g) ? g->valueint : -1;
            break;
        }
        case BLOCK_COMPARE: {
            const cJSON *op = cfg ? cJSON_GetObjectItem(cfg, "op") : NULL;
            blk->cfg.compare.op = parse_cmp_op(cJSON_IsString(op) ? op->valuestring : NULL);
            break;
        }
        case BLOCK_MATH: {
            const cJSON *op = cfg ? cJSON_GetObjectItem(cfg, "op") : NULL;
            blk->cfg.math.op = parse_math_op(cJSON_IsString(op) ? op->valuestring : NULL);
            break;
        }
        case BLOCK_COUNTER: {
            const cJSON *preset = cfg ? cJSON_GetObjectItem(cfg, "preset") : NULL;
            const cJSON *up = cfg ? cJSON_GetObjectItem(cfg, "count_up") : NULL;
            blk->cfg.counter.preset = cJSON_IsNumber(preset) ? preset->valueint : 0;
            blk->cfg.counter.count_up = cJSON_IsBool(up) ? cJSON_IsTrue(up) : true;
            break;
        }
        default:
            break;
        }

        idx++;
    }
    prog->block_count = idx;

    /* Siapkan GPIO yang dipakai block input/output. */
    for (int i = 0; i < prog->block_count; i++) {
        logic_block_t *blk = &prog->blocks[i];
        if (blk->type == BLOCK_INPUT && blk->cfg.input.gpio >= 0) {
            gpio_config_t io = {
                .pin_bit_mask = 1ULL << blk->cfg.input.gpio,
                .mode = GPIO_MODE_INPUT,
                .pull_up_en = GPIO_PULLUP_ENABLE,
            };
            gpio_config(&io);
        } else if (blk->type == BLOCK_OUTPUT && blk->cfg.output.gpio >= 0) {
            gpio_config_t io = {
                .pin_bit_mask = 1ULL << blk->cfg.output.gpio,
                .mode = GPIO_MODE_OUTPUT,
            };
            gpio_config(&io);
        }
    }

    ESP_LOGI(TAG, "program dimuat: %d block", prog->block_count);
    return true;
}

static void eval_block(logic_program_t *prog, logic_block_t *blk)
{
    switch (blk->type) {
    case BLOCK_CONST:
        blk->out_value = blk->cfg.konst.value;
        break;

    case BLOCK_INPUT:
        if (blk->cfg.input.gpio >= 0) {
            blk->out_value = blk->cfg.input.analog
                ? 0.0 /* TODO: ADC belum diimplementasi di versi awal */
                : (double)gpio_get_level(blk->cfg.input.gpio);
        }
        break;

    case BLOCK_OUTPUT: {
        double v = input_value(prog, blk->in[0]);
        blk->out_value = v;
        if (blk->cfg.output.gpio >= 0) {
            gpio_set_level(blk->cfg.output.gpio, v != 0.0);
        }
        break;
    }

    case BLOCK_COMPARE: {
        double a = input_value(prog, blk->in[0]);
        double b = input_value(prog, blk->in[1]);
        bool result = false;
        switch (blk->cfg.compare.op) {
        case CMP_GT:  result = a > b;  break;
        case CMP_LT:  result = a < b;  break;
        case CMP_EQ:  result = a == b; break;
        case CMP_NEQ: result = a != b; break;
        case CMP_GTE: result = a >= b; break;
        case CMP_LTE: result = a <= b; break;
        }
        blk->out_value = result ? 1.0 : 0.0;
        break;
    }

    case BLOCK_IF_ELSE: {
        double cond = input_value(prog, blk->in[0]);
        double if_true = input_value(prog, blk->in[1]);
        double if_false = input_value(prog, blk->in[2]);
        blk->out_value = (cond != 0.0) ? if_true : if_false;
        break;
    }

    case BLOCK_MATH: {
        double a = input_value(prog, blk->in[0]);
        double b = input_value(prog, blk->in[1]);
        switch (blk->cfg.math.op) {
        case OP_ADD: blk->out_value = a + b; break;
        case OP_SUB: blk->out_value = a - b; break;
        case OP_MUL: blk->out_value = a * b; break;
        case OP_DIV: blk->out_value = (b != 0.0) ? a / b : 0.0; break;
        }
        break;
    }

    case BLOCK_COUNTER: {
        /* in[0] = clock (naik di tepi 0->1), in[1] = reset */
        double clk = input_value(prog, blk->in[0]);
        double reset = input_value(prog, blk->in[1]);
        bool clk_now = clk != 0.0;

        if (reset != 0.0) {
            blk->counter_value = 0;
        } else if (clk_now && !blk->counter_prev_clk) {
            blk->counter_value += blk->cfg.counter.count_up ? 1 : -1;
        }
        blk->counter_prev_clk = clk_now;
        blk->out_value = (double)blk->counter_value;
        break;
    }

    default:
        break;
    }
}

void logic_engine_scan(logic_program_t *prog)
{
    for (int i = 0; i < prog->block_count; i++) {
        eval_block(prog, &prog->blocks[i]);
    }
}

double logic_engine_get_output(const logic_program_t *prog, int block_id)
{
    if (block_id < 0 || block_id >= prog->block_count) {
        return 0.0;
    }
    return prog->blocks[block_id].out_value;
}

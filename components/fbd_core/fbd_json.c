#include "fbd_json.h"
#include "i2c_bridge.h"
#include <string.h>
#include <stdio.h>

static void set_err(char *err, size_t err_len, const char *msg)
{
    if (err && err_len > 0) {
        snprintf(err, err_len, "%s", msg);
    }
}

/* Validasi pin GPIO ESP32-S3 - HANYA diterapkan saat hw_mode == real
 * (mode simulated tidak pernah menyentuh GPIO fisik sama sekali, jadi
 * angka pin di sana murni informasional). Berdasarkan datasheet ESP32-S3:
 * - GPIO0, 3, 45, 46: strapping pin (boot mode) - mengubahnya saat boot
 *   bisa membuat device gagal boot atau masuk mode download tak sengaja.
 * - GPIO19, 20: default dipakai USB-JTAG (debugging via USB bawaan).
 * - GPIO26-37: dipakai SPI flash/PSRAM pada board dengan Octal PSRAM
 *   (device proyek ini pakai PSRAM Octal 8MB, dikonfirmasi dari log boot
 *   "Embedded PSRAM 8MB") - dipakai untuk GPIO lain bisa corrupt flash/PSRAM.
 * Rentang valid GPIO ESP32-S3: 0-48, tapi tidak semua nomor itu ada fisik
 * di tiap breakout board - validasi ini hanya soal keamanan elektrikal,
 * bukan jaminan pin itu ke-broke-out di board tertentu. */
static bool is_pin_safe_for_real_gpio(int pin, char *err, size_t err_len)
{
    if (pin < 0 || pin > 48) {
        set_err(err, err_len, "pin di luar rentang valid ESP32-S3 (0-48)");
        return false;
    }
    if (pin == 0 || pin == 3 || pin == 45 || pin == 46) {
        set_err(err, err_len, "pin adalah strapping pin ESP32-S3 (0/3/45/46) - jangan dipakai hw_mode real, berisiko device gagal boot");
        return false;
    }
    if (pin == 19 || pin == 20) {
        set_err(err, err_len, "pin 19/20 dipakai USB-JTAG bawaan - pakai hw_mode real di sini akan mematikan debugging USB");
        return false;
    }
    if (pin >= 26 && pin <= 37) {
        set_err(err, err_len, "pin 26-37 dipakai SPI flash/PSRAM (device ini pakai PSRAM Octal) - hw_mode real di sini bisa corrupt flash/PSRAM");
        return false;
    }
    return true;
}

/* ---- type <-> string ---- */

static const char *node_type_to_str(fbd_node_type_t type)
{
    switch (type) {
        case FBD_NODE_CONST:        return "const";
        case FBD_NODE_VAR_GET:      return "var_get";
        case FBD_NODE_VAR_SET:      return "var_set";
        case FBD_NODE_AND:          return "and";
        case FBD_NODE_OR:           return "or";
        case FBD_NODE_NOT:          return "not";
        case FBD_NODE_XOR:          return "xor";
        case FBD_NODE_NAND:         return "nand";
        case FBD_NODE_NOR:          return "nor";
        case FBD_NODE_COMPARE:      return "compare";
        case FBD_NODE_MATH:         return "math";
        case FBD_NODE_MIN:          return "min";
        case FBD_NODE_MAX:          return "max";
        case FBD_NODE_ABS:          return "abs";
        case FBD_NODE_SCALE:        return "scale";
        case FBD_NODE_CLAMP:        return "clamp";
        case FBD_NODE_TON:          return "ton";
        case FBD_NODE_TOF:          return "tof";
        case FBD_NODE_TP:           return "tp";
        case FBD_NODE_OSC:          return "osc";
        case FBD_NODE_CTU:          return "ctu";
        case FBD_NODE_DIGITAL_IN:   return "digital_input";
        case FBD_NODE_DIGITAL_OUT:  return "digital_output";
        case FBD_NODE_ANALOG_IN:    return "analog_input";
        case FBD_NODE_PWM_OUT:      return "pwm_output";
        case FBD_NODE_SERVO:        return "servo";
        case FBD_NODE_WS2812:       return "ws2812";
        case FBD_NODE_ULTRASONIC:   return "ultrasonic";
        case FBD_NODE_I2C_READ_REG:  return "i2c_read_reg";
        case FBD_NODE_I2C_WRITE_REG: return "i2c_write_reg";
        case FBD_NODE_I2C_WRITE_BURST: return "i2c_write_burst";
        case FBD_NODE_SYS_VAR_GET:   return "sys_var_get";
        case FBD_NODE_HTTP_ENDPOINT: return "http_endpoint";
        default:                    return NULL;
    }
}

static bool str_to_node_type(const char *s, fbd_node_type_t *out)
{
    for (fbd_node_type_t t = 0; t < FBD_NODE_TYPE_COUNT; ++t) {
        const char *name = node_type_to_str(t);
        if (name && strcmp(name, s) == 0) {
            *out = t;
            return true;
        }
    }
    return false;
}

static const char *compare_op_to_str(fbd_compare_op_t op)
{
    switch (op) {
        case FBD_CMP_GT:  return "gt";
        case FBD_CMP_LT:  return "lt";
        case FBD_CMP_EQ:  return "eq";
        case FBD_CMP_NEQ: return "neq";
        case FBD_CMP_GTE: return "gte";
        case FBD_CMP_LTE: return "lte";
        default:          return "gt";
    }
}

static bool str_to_compare_op(const char *s, fbd_compare_op_t *out)
{
    if (strcmp(s, "gt") == 0)  { *out = FBD_CMP_GT;  return true; }
    if (strcmp(s, "lt") == 0)  { *out = FBD_CMP_LT;  return true; }
    if (strcmp(s, "eq") == 0)  { *out = FBD_CMP_EQ;  return true; }
    if (strcmp(s, "neq") == 0) { *out = FBD_CMP_NEQ; return true; }
    if (strcmp(s, "gte") == 0) { *out = FBD_CMP_GTE; return true; }
    if (strcmp(s, "lte") == 0) { *out = FBD_CMP_LTE; return true; }
    return false;
}

static const char *math_op_to_str(fbd_math_op_t op)
{
    switch (op) {
        case FBD_OP_ADD: return "add";
        case FBD_OP_SUB: return "sub";
        case FBD_OP_MUL: return "mul";
        case FBD_OP_DIV: return "div";
        default:         return "add";
    }
}

static bool str_to_math_op(const char *s, fbd_math_op_t *out)
{
    if (strcmp(s, "add") == 0) { *out = FBD_OP_ADD; return true; }
    if (strcmp(s, "sub") == 0) { *out = FBD_OP_SUB; return true; }
    if (strcmp(s, "mul") == 0) { *out = FBD_OP_MUL; return true; }
    if (strcmp(s, "div") == 0) { *out = FBD_OP_DIV; return true; }
    return false;
}

static const char *pin_mode_to_str(fbd_pin_mode_t mode)
{
    switch (mode) {
        case FBD_PIN_MODE_PULLUP:   return "pullup";
        case FBD_PIN_MODE_PULLDOWN: return "pulldown";
        case FBD_PIN_MODE_FLOATING: return "floating";
        default:                    return "pullup";
    }
}

static bool str_to_pin_mode(const char *s, fbd_pin_mode_t *out)
{
    if (strcmp(s, "pullup") == 0)   { *out = FBD_PIN_MODE_PULLUP;   return true; }
    if (strcmp(s, "pulldown") == 0) { *out = FBD_PIN_MODE_PULLDOWN; return true; }
    if (strcmp(s, "floating") == 0) { *out = FBD_PIN_MODE_FLOATING; return true; }
    return false;
}

static const char *hw_mode_to_str(fbd_hw_mode_t mode)
{
    return (mode == FBD_HW_REAL) ? "real" : "simulated";
}

static bool str_to_hw_mode(const char *s, fbd_hw_mode_t *out)
{
    if (strcmp(s, "simulated") == 0) { *out = FBD_HW_SIMULATED; return true; }
    if (strcmp(s, "real") == 0)      { *out = FBD_HW_REAL;      return true; }
    return false;
}

/* params.hw_mode opsional - default simulated kalau tidak dikirim (dual
 * backend, plan.md §3: perilaku aman-default adalah tidak menyentuh
 * hardware sampai user eksplisit set "real"). */
static bool parse_hw_mode(const cJSON *params, fbd_hw_mode_t *out, char *err, size_t err_len)
{
    const cJSON *hw_mode = cJSON_GetObjectItem(params, "hw_mode");
    if (!hw_mode) {
        *out = FBD_HW_SIMULATED;
        return true;
    }
    if (!cJSON_IsString(hw_mode) || !str_to_hw_mode(hw_mode->valuestring, out)) {
        set_err(err, err_len, "params.hw_mode tidak valid (simulated/real)");
        return false;
    }
    return true;
}

/* ---- parse params per node type ---- */

/* params boleh NULL untuk node type yang tidak butuh params (and/or/not/dst) -
 * cJSON_GetObjectItem(NULL, ...) aman, return NULL, jadi cukup treat params
 * NULL sebagai object kosong tanpa perlu alokasi dummy. */
static bool parse_params(const cJSON *params, fbd_node_t *node, char *err, size_t err_len)
{
    switch (node->type) {
        case FBD_NODE_CONST: {
            const cJSON *datatype = cJSON_GetObjectItem(params, "datatype");
            const cJSON *value = cJSON_GetObjectItem(params, "value");
            if (!cJSON_IsString(datatype) || !value) {
                set_err(err, err_len, "const: params.datatype/value tidak valid");
                return false;
            }
            if (strcmp(datatype->valuestring, "bool") == 0) {
                node->params.const_value = fbd_make_bool(cJSON_IsTrue(value));
            } else if (strcmp(datatype->valuestring, "int32") == 0) {
                node->params.const_value = fbd_make_int((int32_t)cJSON_GetNumberValue(value));
            } else if (strcmp(datatype->valuestring, "float") == 0) {
                node->params.const_value = fbd_make_float((float)cJSON_GetNumberValue(value));
            } else {
                set_err(err, err_len, "const: datatype harus bool/int32/float");
                return false;
            }
            break;
        }
        case FBD_NODE_VAR_GET:
        case FBD_NODE_VAR_SET: {
            const cJSON *name = cJSON_GetObjectItem(params, "name");
            if (!cJSON_IsString(name)) {
                set_err(err, err_len, "var_get/var_set: params.name wajib string");
                return false;
            }
            strncpy(node->params.var_name, name->valuestring, FBD_VAR_NAME_LEN - 1);
            node->params.var_name[FBD_VAR_NAME_LEN - 1] = '\0';
            break;
        }
        case FBD_NODE_COMPARE: {
            const cJSON *op = cJSON_GetObjectItem(params, "op");
            if (!cJSON_IsString(op) || !str_to_compare_op(op->valuestring, &node->params.compare_op)) {
                set_err(err, err_len, "compare: params.op tidak valid (gt/lt/eq/neq/gte/lte)");
                return false;
            }
            break;
        }
        case FBD_NODE_MATH: {
            const cJSON *op = cJSON_GetObjectItem(params, "op");
            if (!cJSON_IsString(op) || !str_to_math_op(op->valuestring, &node->params.math_op)) {
                set_err(err, err_len, "math: params.op tidak valid (add/sub/mul/div)");
                return false;
            }
            break;
        }
        case FBD_NODE_SCALE: {
            const cJSON *in_min = cJSON_GetObjectItem(params, "in_min");
            const cJSON *in_max = cJSON_GetObjectItem(params, "in_max");
            const cJSON *out_min = cJSON_GetObjectItem(params, "out_min");
            const cJSON *out_max = cJSON_GetObjectItem(params, "out_max");
            if (!in_min || !in_max || !out_min || !out_max) {
                set_err(err, err_len, "scale: params.in_min/in_max/out_min/out_max wajib");
                return false;
            }
            node->params.in_min = (float)cJSON_GetNumberValue(in_min);
            node->params.in_max = (float)cJSON_GetNumberValue(in_max);
            node->params.out_min = (float)cJSON_GetNumberValue(out_min);
            node->params.out_max = (float)cJSON_GetNumberValue(out_max);
            break;
        }
        case FBD_NODE_CLAMP: {
            const cJSON *min_v = cJSON_GetObjectItem(params, "min");
            const cJSON *max_v = cJSON_GetObjectItem(params, "max");
            if (!min_v || !max_v) {
                set_err(err, err_len, "clamp: params.min/max wajib");
                return false;
            }
            node->params.clamp_min = (float)cJSON_GetNumberValue(min_v);
            node->params.clamp_max = (float)cJSON_GetNumberValue(max_v);
            break;
        }
        case FBD_NODE_TON:
        case FBD_NODE_TOF: {
            const cJSON *delay_ms = cJSON_GetObjectItem(params, "delay_ms");
            if (!delay_ms) {
                set_err(err, err_len, "ton/tof: params.delay_ms wajib");
                return false;
            }
            node->params.delay_ms = (uint32_t)cJSON_GetNumberValue(delay_ms);
            break;
        }
        case FBD_NODE_TP: {
            const cJSON *pulse_ms = cJSON_GetObjectItem(params, "pulse_ms");
            if (!pulse_ms) {
                set_err(err, err_len, "tp: params.pulse_ms wajib");
                return false;
            }
            node->params.delay_ms = (uint32_t)cJSON_GetNumberValue(pulse_ms);
            break;
        }
        case FBD_NODE_OSC: {
            const cJSON *on_ms = cJSON_GetObjectItem(params, "on_ms");
            const cJSON *off_ms = cJSON_GetObjectItem(params, "off_ms");
            if (!on_ms || !off_ms) {
                set_err(err, err_len, "osc: params.on_ms/off_ms wajib");
                return false;
            }
            node->params.osc_on_ms = (uint32_t)cJSON_GetNumberValue(on_ms);
            node->params.osc_off_ms = (uint32_t)cJSON_GetNumberValue(off_ms);
            break;
        }
        case FBD_NODE_CTU: {
            /* reset_value BUKAN params - itu port input ke-4 (in3), bisa
             * datang dari node lain (mis. Constant), bukan angka tetap di
             * JSON node ini sendiri, dipakai di KEDUA mode auto_reset.
             * Lihat fbd_graph.c evaluate_node(). */
            const cJSON *preset = cJSON_GetObjectItem(params, "preset");
            if (!preset) {
                set_err(err, err_len, "ctu: params.preset wajib");
                return false;
            }
            node->params.preset = (int32_t)cJSON_GetNumberValue(preset);
            /* auto_reset opsional, default false - kompatibel dengan
             * dokumen lama yang belum tahu field ini (perilaku sama
             * seperti sebelumnya: reset manual lewat port). */
            const cJSON *auto_reset = cJSON_GetObjectItem(params, "auto_reset");
            node->params.ctu_auto_reset = auto_reset ? cJSON_IsTrue(auto_reset) : false;
            break;
        }
        case FBD_NODE_DIGITAL_IN: {
            const cJSON *pin = cJSON_GetObjectItem(params, "pin");
            if (!pin) {
                set_err(err, err_len, "digital_input: params.pin wajib");
                return false;
            }
            node->params.pin = (int)cJSON_GetNumberValue(pin);
            const cJSON *invert = cJSON_GetObjectItem(params, "invert");
            node->params.invert = invert ? cJSON_IsTrue(invert) : false;
            const cJSON *mode = cJSON_GetObjectItem(params, "mode");
            if (!cJSON_IsString(mode) || !str_to_pin_mode(mode->valuestring, &node->params.pin_mode)) {
                set_err(err, err_len, "digital_input: params.mode tidak valid (pullup/pulldown/floating)");
                return false;
            }
            if (!parse_hw_mode(params, &node->params.hw_mode, err, err_len)) return false;
            if (node->params.hw_mode == FBD_HW_REAL &&
                !is_pin_safe_for_real_gpio(node->params.pin, err, err_len)) return false;
            break;
        }
        case FBD_NODE_DIGITAL_OUT: {
            const cJSON *pin = cJSON_GetObjectItem(params, "pin");
            if (!pin) {
                set_err(err, err_len, "digital_output: params.pin wajib");
                return false;
            }
            node->params.pin = (int)cJSON_GetNumberValue(pin);
            const cJSON *invert = cJSON_GetObjectItem(params, "invert");
            node->params.invert = invert ? cJSON_IsTrue(invert) : false;
            if (!parse_hw_mode(params, &node->params.hw_mode, err, err_len)) return false;
            if (node->params.hw_mode == FBD_HW_REAL &&
                !is_pin_safe_for_real_gpio(node->params.pin, err, err_len)) return false;
            break;
        }
        case FBD_NODE_ANALOG_IN: {
            const cJSON *pin = cJSON_GetObjectItem(params, "pin");
            const cJSON *resolution = cJSON_GetObjectItem(params, "resolution");
            const cJSON *attenuation = cJSON_GetObjectItem(params, "attenuation");
            if (!pin || !resolution || !attenuation) {
                set_err(err, err_len, "analog_input: params.pin/resolution/attenuation wajib");
                return false;
            }
            node->params.pin = (int)cJSON_GetNumberValue(pin);
            node->params.resolution = (int)cJSON_GetNumberValue(resolution);
            node->params.attenuation = (int)cJSON_GetNumberValue(attenuation);
            const cJSON *sim_value = cJSON_GetObjectItem(params, "sim_value");
            node->params.sim_value = sim_value ? fbd_make_int((int32_t)cJSON_GetNumberValue(sim_value))
                                                : fbd_make_int(0);
            if (!parse_hw_mode(params, &node->params.hw_mode, err, err_len)) return false;
            if (node->params.hw_mode == FBD_HW_REAL &&
                !is_pin_safe_for_real_gpio(node->params.pin, err, err_len)) return false;
            break;
        }
        case FBD_NODE_PWM_OUT: {
            const cJSON *pin = cJSON_GetObjectItem(params, "pin");
            const cJSON *frequency = cJSON_GetObjectItem(params, "frequency");
            const cJSON *resolution = cJSON_GetObjectItem(params, "resolution");
            if (!pin || !frequency || !resolution) {
                set_err(err, err_len, "pwm_output: params.pin/frequency/resolution wajib");
                return false;
            }
            node->params.pin = (int)cJSON_GetNumberValue(pin);
            node->params.frequency = (uint32_t)cJSON_GetNumberValue(frequency);
            node->params.resolution = (int)cJSON_GetNumberValue(resolution);
            if (!parse_hw_mode(params, &node->params.hw_mode, err, err_len)) return false;
            if (node->params.hw_mode == FBD_HW_REAL &&
                !is_pin_safe_for_real_gpio(node->params.pin, err, err_len)) return false;
            break;
        }
        case FBD_NODE_SERVO: {
            const cJSON *pin = cJSON_GetObjectItem(params, "pin");
            const cJSON *min_us = cJSON_GetObjectItem(params, "min_us");
            const cJSON *max_us = cJSON_GetObjectItem(params, "max_us");
            if (!pin || !min_us || !max_us) {
                set_err(err, err_len, "servo: params.pin/min_us/max_us wajib");
                return false;
            }
            node->params.pin = (int)cJSON_GetNumberValue(pin);
            node->params.min_us = (uint32_t)cJSON_GetNumberValue(min_us);
            node->params.max_us = (uint32_t)cJSON_GetNumberValue(max_us);
            if (!parse_hw_mode(params, &node->params.hw_mode, err, err_len)) return false;
            if (node->params.hw_mode == FBD_HW_REAL &&
                !is_pin_safe_for_real_gpio(node->params.pin, err, err_len)) return false;
            break;
        }
        case FBD_NODE_WS2812: {
            const cJSON *pin = cJSON_GetObjectItem(params, "pin");
            const cJSON *count = cJSON_GetObjectItem(params, "count");
            if (!pin || !count) {
                set_err(err, err_len, "ws2812: params.pin/count wajib");
                return false;
            }
            int count_val = (int)cJSON_GetNumberValue(count);
            if (count_val < 1 || count_val > 256) {
                set_err(err, err_len, "ws2812: params.count harus 1-256");
                return false;
            }
            node->params.pin = (int)cJSON_GetNumberValue(pin);
            node->params.ws2812_count = count_val;
            if (!parse_hw_mode(params, &node->params.hw_mode, err, err_len)) return false;
            if (node->params.hw_mode == FBD_HW_REAL &&
                !is_pin_safe_for_real_gpio(node->params.pin, err, err_len)) return false;
            break;
        }
        case FBD_NODE_ULTRASONIC: {
            const cJSON *pin = cJSON_GetObjectItem(params, "pin");
            const cJSON *echo_pin = cJSON_GetObjectItem(params, "echo_pin");
            if (!pin || !echo_pin) {
                set_err(err, err_len, "ultrasonic: params.pin (trig)/echo_pin wajib");
                return false;
            }
            node->params.pin = (int)cJSON_GetNumberValue(pin);
            node->params.ultrasonic_echo_pin = (int)cJSON_GetNumberValue(echo_pin);
            if (node->params.pin == node->params.ultrasonic_echo_pin) {
                set_err(err, err_len, "ultrasonic: params.pin (trig) dan echo_pin harus berbeda");
                return false;
            }
            const cJSON *sim_distance = cJSON_GetObjectItem(params, "sim_distance_cm");
            node->params.ultrasonic_sim_distance_cm = sim_distance ? (float)cJSON_GetNumberValue(sim_distance) : 0.0f;
            if (!parse_hw_mode(params, &node->params.hw_mode, err, err_len)) return false;
            if (node->params.hw_mode == FBD_HW_REAL) {
                if (!is_pin_safe_for_real_gpio(node->params.pin, err, err_len)) return false;
                if (!is_pin_safe_for_real_gpio(node->params.ultrasonic_echo_pin, err, err_len)) return false;
            }
            break;
        }
        case FBD_NODE_I2C_READ_REG: {
            const cJSON *bus = cJSON_GetObjectItem(params, "bus");
            const cJSON *address = cJSON_GetObjectItem(params, "address");
            const cJSON *reg = cJSON_GetObjectItem(params, "register");
            const cJSON *length = cJSON_GetObjectItem(params, "length");
            if (!address || !reg || !length) {
                set_err(err, err_len, "i2c_read_reg: params.address/register/length wajib");
                return false;
            }
            node->params.i2c_bus = bus ? (int)cJSON_GetNumberValue(bus) : 0;
            node->params.i2c_address = (uint8_t)cJSON_GetNumberValue(address);
            node->params.i2c_register = (uint8_t)cJSON_GetNumberValue(reg);
            int len = (int)cJSON_GetNumberValue(length);
            if (len < 1 || len > I2C_BRIDGE_MAX_DATA_LEN) {
                set_err(err, err_len, "i2c_read_reg: params.length harus 1-8");
                return false;
            }
            node->params.i2c_data_len = (uint8_t)len;
            break;
        }
        case FBD_NODE_I2C_WRITE_REG: {
            const cJSON *bus = cJSON_GetObjectItem(params, "bus");
            const cJSON *address = cJSON_GetObjectItem(params, "address");
            const cJSON *reg = cJSON_GetObjectItem(params, "register");
            const cJSON *data = cJSON_GetObjectItem(params, "data");
            if (!address || !reg || !cJSON_IsArray(data)) {
                set_err(err, err_len, "i2c_write_reg: params.address/register/data (array) wajib");
                return false;
            }
            int len = cJSON_GetArraySize(data);
            if (len < 1 || len > I2C_BRIDGE_MAX_DATA_LEN - 1) {
                set_err(err, err_len, "i2c_write_reg: params.data panjangnya harus 1-7");
                return false;
            }
            node->params.i2c_bus = bus ? (int)cJSON_GetNumberValue(bus) : 0;
            node->params.i2c_address = (uint8_t)cJSON_GetNumberValue(address);
            node->params.i2c_register = (uint8_t)cJSON_GetNumberValue(reg);
            node->params.i2c_data_len = (uint8_t)len;
            for (int i = 0; i < len; ++i) {
                node->params.i2c_data[i] = (uint8_t)cJSON_GetNumberValue(cJSON_GetArrayItem(data, i));
            }
            break;
        }
        case FBD_NODE_I2C_WRITE_BURST: {
            const cJSON *bus = cJSON_GetObjectItem(params, "bus");
            const cJSON *address = cJSON_GetObjectItem(params, "address");
            const cJSON *commands = cJSON_GetObjectItem(params, "commands");
            const cJSON *delay_us = cJSON_GetObjectItem(params, "delay_us");
            if (!address || !cJSON_IsArray(commands)) {
                set_err(err, err_len, "i2c_write_burst: params.address/commands (array) wajib");
                return false;
            }
            int cmd_count = cJSON_GetArraySize(commands);
            if (cmd_count < 1 || cmd_count > FBD_I2C_BURST_MAX_CMDS) {
                set_err(err, err_len, "i2c_write_burst: params.commands panjangnya harus 1-8");
                return false;
            }
            node->params.i2c_bus = bus ? (int)cJSON_GetNumberValue(bus) : 0;
            node->params.i2c_address = (uint8_t)cJSON_GetNumberValue(address);
            node->params.i2c_burst_delay_us = delay_us ? (uint32_t)cJSON_GetNumberValue(delay_us) : 0;
            node->params.i2c_burst_cmd_count = (uint8_t)cmd_count;
            for (int i = 0; i < cmd_count; ++i) {
                const cJSON *cmd = cJSON_GetArrayItem(commands, i);
                const cJSON *reg = cJSON_GetObjectItem(cmd, "register");
                const cJSON *data = cJSON_GetObjectItem(cmd, "data");
                if (!reg || !cJSON_IsArray(data)) {
                    set_err(err, err_len, "i2c_write_burst: tiap commands[] wajib punya register+data (array)");
                    return false;
                }
                int data_len = cJSON_GetArraySize(data);
                if (data_len < 1 || data_len > FBD_I2C_BURST_MAX_DATA) {
                    set_err(err, err_len, "i2c_write_burst: tiap commands[].data panjangnya harus 1-4");
                    return false;
                }
                node->params.i2c_burst_cmds[i].reg = (uint8_t)cJSON_GetNumberValue(reg);
                node->params.i2c_burst_cmds[i].data_len = (uint8_t)data_len;
                for (int j = 0; j < data_len; ++j) {
                    node->params.i2c_burst_cmds[i].data[j] =
                        (uint8_t)cJSON_GetNumberValue(cJSON_GetArrayItem(data, j));
                }
            }
            break;
        }
        case FBD_NODE_SYS_VAR_GET: {
            const cJSON *name = cJSON_GetObjectItem(params, "name");
            if (!cJSON_IsString(name)) {
                set_err(err, err_len, "sys_var_get: params.name wajib string");
                return false;
            }
            strncpy(node->params.sys_var_name, name->valuestring, sizeof(node->params.sys_var_name) - 1);
            node->params.sys_var_name[sizeof(node->params.sys_var_name) - 1] = '\0';
            break;
        }
        case FBD_NODE_HTTP_ENDPOINT: {
            const cJSON *path = cJSON_GetObjectItem(params, "path");
            const cJSON *file = cJSON_GetObjectItem(params, "file");
            if (!cJSON_IsString(path) || !cJSON_IsString(file)) {
                set_err(err, err_len, "http_endpoint: params.path/file wajib string");
                return false;
            }
            if (path->valuestring[0] != '/') {
                set_err(err, err_len, "http_endpoint: params.path harus mulai dengan '/'");
                return false;
            }
            /* file BOLEH kosong sekarang - kalau input port (response
             * dinamis) tersambung, file statis tidak pernah dipakai.
             * Validasi "file wajib ADA kalau input kosong" tidak
             * dilakukan di sini (parse time) karena link belum tentu
             * sudah diproses saat case ini dieksekusi - fallback ke
             * balasan "file endpoint tidak ditemukan" di runtime kalau
             * user memang salah konfigurasi (bukan kesalahan fatal). */
            strncpy(node->params.http_path, path->valuestring, sizeof(node->params.http_path) - 1);
            node->params.http_path[sizeof(node->params.http_path) - 1] = '\0';
            strncpy(node->params.http_file, file->valuestring, sizeof(node->params.http_file) - 1);
            node->params.http_file[sizeof(node->params.http_file) - 1] = '\0';
            const cJSON *content_type = cJSON_GetObjectItem(params, "content_type");
            node->params.http_content_type_html =
                !(content_type && cJSON_IsString(content_type) && strcmp(content_type->valuestring, "text/plain") == 0);
            const cJSON *query_a_name = cJSON_GetObjectItem(params, "query_a_name");
            const cJSON *query_b_name = cJSON_GetObjectItem(params, "query_b_name");
            if (query_a_name && cJSON_IsString(query_a_name)) {
                strncpy(node->params.http_query_a_name, query_a_name->valuestring, sizeof(node->params.http_query_a_name) - 1);
                node->params.http_query_a_name[sizeof(node->params.http_query_a_name) - 1] = '\0';
            } else {
                node->params.http_query_a_name[0] = '\0';
            }
            if (query_b_name && cJSON_IsString(query_b_name)) {
                strncpy(node->params.http_query_b_name, query_b_name->valuestring, sizeof(node->params.http_query_b_name) - 1);
                node->params.http_query_b_name[sizeof(node->params.http_query_b_name) - 1] = '\0';
            } else {
                node->params.http_query_b_name[0] = '\0';
            }
            break;
        }
        case FBD_NODE_AND:
        case FBD_NODE_OR:
        case FBD_NODE_NOT:
        case FBD_NODE_XOR:
        case FBD_NODE_NAND:
        case FBD_NODE_NOR:
        case FBD_NODE_MIN:
        case FBD_NODE_MAX:
        case FBD_NODE_ABS:
            /* tidak ada params khusus */
            break;
        default:
            set_err(err, err_len, "type node tidak dikenal / belum didukung");
            return false;
    }
    return true;
}

/* ---- parse ---- */

bool fbd_json_parse(const cJSON *root, fbd_graph_t *out_graph, char *err, size_t err_len)
{
    if (!cJSON_IsObject(root)) {
        set_err(err, err_len, "root harus object JSON");
        return false;
    }

    const cJSON *version = cJSON_GetObjectItem(root, "version");
    if (!version || (int)cJSON_GetNumberValue(version) != 1) {
        set_err(err, err_len, "version tidak dikenal (harus 1)");
        return false;
    }

    const cJSON *nodes = cJSON_GetObjectItem(root, "nodes");
    if (!cJSON_IsArray(nodes)) {
        set_err(err, err_len, "nodes harus array");
        return false;
    }

    /* Tulis LANGSUNG ke *out_graph, jangan pakai local fbd_graph_t di stack -
     * sizeof(fbd_graph_t) ~19KB, jauh lebih besar dari stack task manapun
     * yang wajar (misal task httpd 4-8KB). Pernah menyebabkan stack overflow
     * yang merusak heap TLSF ESP32 secara diam-diam - baru kelihatan jauh
     * setelah titik overflow sebenarnya (di cJSON_Delete berikutnya). Kalau
     * parse gagal di tengah jalan, *out_graph boleh dalam keadaan tidak
     * lengkap - caller tidak boleh memakainya saat return false. */
    fbd_graph_init(out_graph);

    int idx = 0;
    const cJSON *node_json;
    cJSON_ArrayForEach(node_json, nodes) {
        const cJSON *id = cJSON_GetObjectItem(node_json, "id");
        const cJSON *type_str = cJSON_GetObjectItem(node_json, "type");
        if (!cJSON_IsString(id) || !cJSON_IsString(type_str)) {
            set_err(err, err_len, "nodes[]: id/type wajib string");
            return false;
        }
        if (fbd_graph_find_node(out_graph, id->valuestring) != (size_t)-1) {
            set_err(err, err_len, "nodes[]: id duplikat");
            return false;
        }

        fbd_node_type_t type;
        if (!str_to_node_type(type_str->valuestring, &type)) {
            set_err(err, err_len, "nodes[]: type tidak dikenal");
            return false;
        }

        fbd_node_t *node = fbd_graph_add_node(out_graph, id->valuestring, type);
        if (!node) {
            set_err(err, err_len, "nodes[]: melebihi FBD_MAX_NODES");
            return false;
        }

        const cJSON *params = cJSON_GetObjectItem(node_json, "params");
        char local_err[FBD_JSON_ERR_LEN];
        if (!parse_params(params, node, local_err, sizeof(local_err))) {
            set_err(err, err_len, local_err);
            return false;
        }
        idx++;
    }

    const cJSON *links = cJSON_GetObjectItem(root, "links");
    if (links && !cJSON_IsArray(links)) {
        set_err(err, err_len, "links harus array");
        return false;
    }

    if (links) {
        const cJSON *link_json;
        cJSON_ArrayForEach(link_json, links) {
            const cJSON *from = cJSON_GetObjectItem(link_json, "from");
            const cJSON *to = cJSON_GetObjectItem(link_json, "to");
            if (!cJSON_IsObject(from) || !cJSON_IsObject(to)) {
                set_err(err, err_len, "links[]: from/to wajib object");
                return false;
            }
            const cJSON *from_node = cJSON_GetObjectItem(from, "node");
            const cJSON *from_port = cJSON_GetObjectItem(from, "port");
            const cJSON *to_node = cJSON_GetObjectItem(to, "node");
            const cJSON *to_port = cJSON_GetObjectItem(to, "port");
            if (!cJSON_IsString(from_node) || !from_port || !cJSON_IsString(to_node) || !to_port) {
                set_err(err, err_len, "links[]: from.node/from.port/to.node/to.port wajib");
                return false;
            }

            if (fbd_graph_find_node(out_graph, from_node->valuestring) == (size_t)-1 ||
                fbd_graph_find_node(out_graph, to_node->valuestring) == (size_t)-1) {
                set_err(err, err_len, "links[]: from.node/to.node merujuk id yang tidak ada");
                return false;
            }

            uint8_t fp = (uint8_t)cJSON_GetNumberValue(from_port);
            uint8_t tp = (uint8_t)cJSON_GetNumberValue(to_port);
            if (fp >= FBD_MAX_NODE_OUTPUTS || tp >= FBD_MAX_NODE_INPUTS) {
                set_err(err, err_len, "links[]: port di luar rentang valid");
                return false;
            }

            if (!fbd_graph_add_link(out_graph, from_node->valuestring, fp, to_node->valuestring, tp)) {
                set_err(err, err_len, "links[]: gagal menambahkan link (melebihi FBD_MAX_LINKS?)");
                return false;
            }
        }
    }

    return true;
}

/* ---- serialize ---- */

static cJSON *serialize_params(const fbd_node_t *node)
{
    cJSON *params = cJSON_CreateObject();

    switch (node->type) {
        case FBD_NODE_CONST: {
            const char *dt = "float";
            switch (node->params.const_value.type) {
                case FBD_BOOL:  dt = "bool"; break;
                case FBD_INT32: dt = "int32"; break;
                default:        dt = "float"; break;
            }
            cJSON_AddStringToObject(params, "datatype", dt);
            if (dt[0] == 'b') {
                cJSON_AddBoolToObject(params, "value", node->params.const_value.b);
            } else if (dt[0] == 'i') {
                cJSON_AddNumberToObject(params, "value", node->params.const_value.i);
            } else {
                cJSON_AddNumberToObject(params, "value", node->params.const_value.f);
            }
            break;
        }
        case FBD_NODE_VAR_GET:
        case FBD_NODE_VAR_SET:
            cJSON_AddStringToObject(params, "name", node->params.var_name);
            break;
        case FBD_NODE_COMPARE:
            cJSON_AddStringToObject(params, "op", compare_op_to_str(node->params.compare_op));
            break;
        case FBD_NODE_MATH:
            cJSON_AddStringToObject(params, "op", math_op_to_str(node->params.math_op));
            break;
        case FBD_NODE_SCALE:
            cJSON_AddNumberToObject(params, "in_min", node->params.in_min);
            cJSON_AddNumberToObject(params, "in_max", node->params.in_max);
            cJSON_AddNumberToObject(params, "out_min", node->params.out_min);
            cJSON_AddNumberToObject(params, "out_max", node->params.out_max);
            break;
        case FBD_NODE_CLAMP:
            cJSON_AddNumberToObject(params, "min", node->params.clamp_min);
            cJSON_AddNumberToObject(params, "max", node->params.clamp_max);
            break;
        case FBD_NODE_TON:
        case FBD_NODE_TOF:
            cJSON_AddNumberToObject(params, "delay_ms", node->params.delay_ms);
            break;
        case FBD_NODE_TP:
            cJSON_AddNumberToObject(params, "pulse_ms", node->params.delay_ms);
            break;
        case FBD_NODE_OSC:
            cJSON_AddNumberToObject(params, "on_ms", node->params.osc_on_ms);
            cJSON_AddNumberToObject(params, "off_ms", node->params.osc_off_ms);
            break;
        case FBD_NODE_CTU:
            cJSON_AddNumberToObject(params, "preset", node->params.preset);
            cJSON_AddBoolToObject(params, "auto_reset", node->params.ctu_auto_reset);
            break;
        case FBD_NODE_DIGITAL_IN:
            cJSON_AddNumberToObject(params, "pin", node->params.pin);
            cJSON_AddStringToObject(params, "mode", pin_mode_to_str(node->params.pin_mode));
            cJSON_AddBoolToObject(params, "invert", node->params.invert);
            cJSON_AddStringToObject(params, "hw_mode", hw_mode_to_str(node->params.hw_mode));
            break;
        case FBD_NODE_DIGITAL_OUT:
            cJSON_AddNumberToObject(params, "pin", node->params.pin);
            cJSON_AddBoolToObject(params, "invert", node->params.invert);
            cJSON_AddStringToObject(params, "hw_mode", hw_mode_to_str(node->params.hw_mode));
            break;
        case FBD_NODE_ANALOG_IN:
            cJSON_AddNumberToObject(params, "pin", node->params.pin);
            cJSON_AddNumberToObject(params, "resolution", node->params.resolution);
            cJSON_AddNumberToObject(params, "attenuation", node->params.attenuation);
            cJSON_AddNumberToObject(params, "sim_value", (double)node->params.sim_value.i);
            cJSON_AddStringToObject(params, "hw_mode", hw_mode_to_str(node->params.hw_mode));
            break;
        case FBD_NODE_PWM_OUT:
            cJSON_AddNumberToObject(params, "pin", node->params.pin);
            cJSON_AddNumberToObject(params, "frequency", node->params.frequency);
            cJSON_AddNumberToObject(params, "resolution", node->params.resolution);
            cJSON_AddStringToObject(params, "hw_mode", hw_mode_to_str(node->params.hw_mode));
            break;
        case FBD_NODE_SERVO:
            cJSON_AddNumberToObject(params, "pin", node->params.pin);
            cJSON_AddNumberToObject(params, "min_us", node->params.min_us);
            cJSON_AddNumberToObject(params, "max_us", node->params.max_us);
            cJSON_AddStringToObject(params, "hw_mode", hw_mode_to_str(node->params.hw_mode));
            break;
        case FBD_NODE_WS2812:
            cJSON_AddNumberToObject(params, "pin", node->params.pin);
            cJSON_AddNumberToObject(params, "count", node->params.ws2812_count);
            cJSON_AddStringToObject(params, "hw_mode", hw_mode_to_str(node->params.hw_mode));
            break;
        case FBD_NODE_ULTRASONIC:
            cJSON_AddNumberToObject(params, "pin", node->params.pin);
            cJSON_AddNumberToObject(params, "echo_pin", node->params.ultrasonic_echo_pin);
            cJSON_AddNumberToObject(params, "sim_distance_cm", node->params.ultrasonic_sim_distance_cm);
            cJSON_AddStringToObject(params, "hw_mode", hw_mode_to_str(node->params.hw_mode));
            break;
        case FBD_NODE_I2C_READ_REG:
            cJSON_AddNumberToObject(params, "bus", node->params.i2c_bus);
            cJSON_AddNumberToObject(params, "address", node->params.i2c_address);
            cJSON_AddNumberToObject(params, "register", node->params.i2c_register);
            cJSON_AddNumberToObject(params, "length", node->params.i2c_data_len);
            break;
        case FBD_NODE_I2C_WRITE_REG: {
            cJSON_AddNumberToObject(params, "bus", node->params.i2c_bus);
            cJSON_AddNumberToObject(params, "address", node->params.i2c_address);
            cJSON_AddNumberToObject(params, "register", node->params.i2c_register);
            cJSON *data_arr = cJSON_CreateArray();
            for (int i = 0; i < node->params.i2c_data_len; ++i) {
                cJSON_AddItemToArray(data_arr, cJSON_CreateNumber(node->params.i2c_data[i]));
            }
            cJSON_AddItemToObject(params, "data", data_arr);
            break;
        }
        case FBD_NODE_I2C_WRITE_BURST: {
            cJSON_AddNumberToObject(params, "bus", node->params.i2c_bus);
            cJSON_AddNumberToObject(params, "address", node->params.i2c_address);
            cJSON_AddNumberToObject(params, "delay_us", node->params.i2c_burst_delay_us);
            cJSON *commands_arr = cJSON_CreateArray();
            for (int i = 0; i < node->params.i2c_burst_cmd_count; ++i) {
                const fbd_i2c_burst_cmd_t *cmd = &node->params.i2c_burst_cmds[i];
                cJSON *cmd_obj = cJSON_CreateObject();
                cJSON_AddNumberToObject(cmd_obj, "register", cmd->reg);
                cJSON *data_arr = cJSON_CreateArray();
                for (int j = 0; j < cmd->data_len; ++j) {
                    cJSON_AddItemToArray(data_arr, cJSON_CreateNumber(cmd->data[j]));
                }
                cJSON_AddItemToObject(cmd_obj, "data", data_arr);
                cJSON_AddItemToArray(commands_arr, cmd_obj);
            }
            cJSON_AddItemToObject(params, "commands", commands_arr);
            break;
        }
        case FBD_NODE_SYS_VAR_GET:
            cJSON_AddStringToObject(params, "name", node->params.sys_var_name);
            break;
        case FBD_NODE_HTTP_ENDPOINT:
            cJSON_AddStringToObject(params, "path", node->params.http_path);
            cJSON_AddStringToObject(params, "file", node->params.http_file);
            cJSON_AddStringToObject(params, "content_type", node->params.http_content_type_html ? "text/html" : "text/plain");
            cJSON_AddStringToObject(params, "query_a_name", node->params.http_query_a_name);
            cJSON_AddStringToObject(params, "query_b_name", node->params.http_query_b_name);
            break;
        default:
            break;
    }
    return params;
}

/* Serialize satu fbd_value_t ke bentuk JSON generik, dipakai untuk live
 * monitor ("outputs" tiap node di GET /api/program - lihat specs/
 * 06-level2-i2c-wifi.md, polling minimal pengganti WebSocket). Bukan bagian
 * dari "params" node manapun - hanya untuk observasi read-only. */
static cJSON *serialize_fbd_value(fbd_value_t v)
{
    switch (v.type) {
        case FBD_BOOL:  return cJSON_CreateBool(v.b);
        case FBD_INT32: return cJSON_CreateNumber(v.i);
        case FBD_FLOAT:  return cJSON_CreateNumber(v.f);
        case FBD_BYTES: {
            cJSON *arr = cJSON_CreateArray();
            for (uint8_t i = 0; i < v.bytes.len; ++i) {
                cJSON_AddItemToArray(arr, cJSON_CreateNumber(v.bytes.data[i]));
            }
            return arr;
        }
        default: return cJSON_CreateNull();
    }
}

cJSON *fbd_json_serialize(const fbd_graph_t *g)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "version", 1);

    cJSON *nodes = cJSON_CreateArray();
    for (size_t i = 0; i < g->node_count; ++i) {
        const fbd_node_t *node = &g->nodes[i];
        cJSON *node_json = cJSON_CreateObject();
        cJSON_AddStringToObject(node_json, "id", node->id);
        cJSON_AddStringToObject(node_json, "type", node_type_to_str(node->type));
        cJSON_AddItemToObject(node_json, "params", serialize_params(node));

        /* "outputs": nilai runtime read-only utk live monitor (mis.
         * raw_bytes/error dari i2c_read_reg) - BUKAN bagian schema untuk
         * di-load balik, cuma observasi. Selalu 2 elemen sesuai
         * FBD_MAX_NODE_OUTPUTS, walau kebanyakan node cuma pakai outputs[0]. */
        cJSON *outputs = cJSON_CreateArray();
        cJSON_AddItemToArray(outputs, serialize_fbd_value(node->outputs[0]));
        cJSON_AddItemToArray(outputs, serialize_fbd_value(node->outputs[1]));
        cJSON_AddItemToObject(node_json, "outputs", outputs);

        cJSON_AddItemToArray(nodes, node_json);
    }
    cJSON_AddItemToObject(root, "nodes", nodes);

    cJSON *links = cJSON_CreateArray();
    for (size_t i = 0; i < g->link_count; ++i) {
        const fbd_link_t *link = &g->links[i];
        cJSON *link_json = cJSON_CreateObject();

        cJSON *from = cJSON_CreateObject();
        cJSON_AddStringToObject(from, "node", g->nodes[link->from_idx].id);
        cJSON_AddNumberToObject(from, "port", link->from_port);
        cJSON_AddItemToObject(link_json, "from", from);

        cJSON *to = cJSON_CreateObject();
        cJSON_AddStringToObject(to, "node", g->nodes[link->to_idx].id);
        cJSON_AddNumberToObject(to, "port", link->to_port);
        cJSON_AddItemToObject(link_json, "to", to);

        cJSON_AddItemToArray(links, link_json);
    }
    cJSON_AddItemToObject(root, "links", links);

    return root;
}

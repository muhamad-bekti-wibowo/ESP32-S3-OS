#include "fbd_graph.h"
#include "fbd_hw_backend.h"
#include "i2c_bridge.h"
#include "fbd_sys_vars.h"
#include <string.h>

void fbd_graph_init(fbd_graph_t *g)
{
    memset(g, 0, sizeof(*g));
    fbd_var_store_init(&g->vars);
}

size_t fbd_graph_find_node(const fbd_graph_t *g, const char *id)
{
    for (size_t i = 0; i < g->node_count; ++i) {
        if (strncmp(g->nodes[i].id, id, FBD_MAX_ID_LEN) == 0) {
            return i;
        }
    }
    return (size_t)-1;
}

fbd_node_t *fbd_graph_add_node(fbd_graph_t *g, const char *id, fbd_node_type_t type)
{
    if (g->node_count >= FBD_MAX_NODES) {
        return NULL;
    }
    fbd_node_t *node = &g->nodes[g->node_count++];
    memset(node, 0, sizeof(*node));
    strncpy(node->id, id, FBD_MAX_ID_LEN - 1);
    node->id[FBD_MAX_ID_LEN - 1] = '\0';
    node->type = type;
    return node;
}

bool fbd_graph_add_link(fbd_graph_t *g, const char *from_id, uint8_t from_port,
                         const char *to_id, uint8_t to_port)
{
    if (g->link_count >= FBD_MAX_LINKS) {
        return false;
    }
    size_t from_idx = fbd_graph_find_node(g, from_id);
    size_t to_idx = fbd_graph_find_node(g, to_id);
    if (from_idx == (size_t)-1 || to_idx == (size_t)-1) {
        return false;
    }
    if (from_port >= FBD_MAX_NODE_OUTPUTS || to_port >= FBD_MAX_NODE_INPUTS) {
        return false;
    }
    fbd_link_t *link = &g->links[g->link_count++];
    link->from_idx = from_idx;
    link->from_port = from_port;
    link->to_idx = to_idx;
    link->to_port = to_port;
    return true;
}

bool fbd_graph_compile(fbd_graph_t *g)
{
    size_t n = g->node_count;
    int in_degree[FBD_MAX_NODES] = {0};

    for (size_t i = 0; i < g->link_count; ++i) {
        in_degree[g->links[i].to_idx]++;
    }

    size_t queue[FBD_MAX_NODES];
    size_t q_head = 0, q_tail = 0;

    for (size_t i = 0; i < n; ++i) {
        if (in_degree[i] == 0) {
            queue[q_tail++] = i;
        }
    }

    g->order_count = 0;
    while (q_head < q_tail) {
        size_t u = queue[q_head++];
        g->execution_order[g->order_count++] = u;

        for (size_t i = 0; i < g->link_count; ++i) {
            if (g->links[i].from_idx == u) {
                size_t v = g->links[i].to_idx;
                if (--in_degree[v] == 0) {
                    queue[q_tail++] = v;
                }
            }
        }
    }

    return g->order_count == n;
}

/* out_secondary diisi untuk node yang punya 2 output (saat ini hanya
 * FBD_NODE_I2C_READ_REG: outputs[0]=raw_bytes, outputs[1]=error). Node lain
 * mengabaikan parameter ini - default *out_secondary tetap FBD_EMPTY. */
static fbd_value_t evaluate_node(fbd_node_t *node, fbd_var_store_t *vars, uint32_t now_ms,
                                  fbd_value_t *out_secondary)
{
    fbd_value_t *in = node->inputs;

    switch (node->type) {
        case FBD_NODE_CONST:
            return fbd_eval_constant(node->params.const_value);
        case FBD_NODE_VAR_GET:
            return fbd_var_get(vars, node->params.var_name);
        case FBD_NODE_VAR_SET:
            fbd_var_set(vars, node->params.var_name, in[0]);
            return in[0];
        case FBD_NODE_AND:
            return fbd_eval_and(in[0], in[1]);
        case FBD_NODE_OR:
            return fbd_eval_or(in[0], in[1]);
        case FBD_NODE_NOT:
            return fbd_eval_not(in[0]);
        case FBD_NODE_XOR:
            return fbd_eval_xor(in[0], in[1]);
        case FBD_NODE_NAND:
            return fbd_eval_nand(in[0], in[1]);
        case FBD_NODE_NOR:
            return fbd_eval_nor(in[0], in[1]);
        case FBD_NODE_COMPARE:
            return fbd_eval_compare(in[0], in[1], node->params.compare_op);
        case FBD_NODE_MATH:
            return fbd_eval_math(in[0], in[1], node->params.math_op);
        case FBD_NODE_MIN:
            return fbd_eval_min(in[0], in[1]);
        case FBD_NODE_MAX:
            return fbd_eval_max(in[0], in[1]);
        case FBD_NODE_ABS:
            return fbd_eval_abs(in[0]);
        case FBD_NODE_SCALE:
            return fbd_eval_scale(in[0], node->params.in_min, node->params.in_max,
                                   node->params.out_min, node->params.out_max);
        case FBD_NODE_CLAMP:
            return fbd_eval_clamp(in[0], node->params.clamp_min, node->params.clamp_max);
        case FBD_NODE_TON:
            return fbd_eval_ton(in[0], node->params.delay_ms, now_ms, &node->state.timer);
        case FBD_NODE_TOF:
            return fbd_eval_tof(in[0], node->params.delay_ms, now_ms, &node->state.timer);
        case FBD_NODE_TP:
            return fbd_eval_tp(in[0], node->params.delay_ms, now_ms, &node->state.timer);
        case FBD_NODE_OSC:
            return fbd_eval_osc(node->params.osc_on_ms, node->params.osc_off_ms, now_ms, &node->state.timer);
        case FBD_NODE_CTU: {
            /* Port: in0=up, in1=down, in2=reset (diabaikan kalau
             * params.ctu_auto_reset=true), in3=reset_value (angka tujuan
             * reset di KEDUA mode, BUKAN selalu 0). reset_value dibaca
             * live tiap cycle supaya nilainya selalu konsisten kapan pun
             * reset dipicu (manual) atau tercapai (auto).
             * outputs[0]=bool (count>=preset), outputs[1]=angka count
             * aktual (state->count dibaca SETELAH fbd_eval_ctud() selesai
             * memutasinya, supaya reflect nilai paling baru cycle ini). */
            int32_t reset_value = (int32_t)fbd_to_float(in[3]);
            fbd_value_t result = fbd_eval_ctud(in[0], in[1], in[2], reset_value, node->params.preset,
                                                node->params.ctu_auto_reset, &node->state.counter);
            *out_secondary = fbd_make_int(node->state.counter.count);
            return result;
        }
        case FBD_NODE_DIGITAL_IN: {
            /* Backend simulated: no-op, nilai tetap datang dari inputs[0]
             * yang di-set scan task/test (pass-through) - supaya test host
             * lama (set inputs[0] manual) tidak perlu berubah. Backend real:
             * init sekali (gpio_config), lalu baca GPIO fisik sungguhan
             * tiap cycle, abaikan inputs[0]. */
            if (node->params.hw_mode != FBD_HW_REAL) {
                return in[0];
            }
            const fbd_hw_backend_t *hw = fbd_hw_get_backend();
            if (!node->state.hw_initialized) {
                hw->digital_init_input(node->params.pin, node->params.pin_mode);
                node->state.hw_initialized = true;
            }
            bool level = hw->digital_read(node->params.pin);
            return fbd_make_bool(node->params.invert ? !level : level);
        }
        case FBD_NODE_DIGITAL_OUT: {
            bool level = fbd_to_bool(in[0]);
            if (node->params.hw_mode == FBD_HW_REAL) {
                const fbd_hw_backend_t *hw = fbd_hw_get_backend();
                if (!node->state.hw_initialized) {
                    hw->digital_init_output(node->params.pin);
                    node->state.hw_initialized = true;
                }
                hw->digital_write(node->params.pin, node->params.invert ? !level : level);
            }
            return in[0];
        }
        case FBD_NODE_ANALOG_IN: {
            const fbd_hw_backend_t *hw = fbd_hw_get_backend();
            if (node->params.hw_mode == FBD_HW_REAL) {
                if (!node->state.hw_initialized) {
                    hw->analog_init(node->params.pin, node->params.resolution, node->params.attenuation);
                    node->state.hw_initialized = true;
                }
                fbd_value_t dummy = fbd_make_empty();
                return hw->analog_read(node->params.pin, node->params.resolution,
                                        node->params.attenuation, dummy);
            }
            return hw->analog_read(node->params.pin, node->params.resolution,
                                    node->params.attenuation, node->params.sim_value);
        }
        case FBD_NODE_PWM_OUT: {
            float duty_percent = fbd_to_float(in[0]);
            uint32_t max_duty = (1u << node->params.resolution) - 1u;
            uint32_t duty = (uint32_t)((duty_percent / 100.0f) * (float)max_duty);
            if (node->params.hw_mode == FBD_HW_REAL) {
                const fbd_hw_backend_t *hw = fbd_hw_get_backend();
                if (!node->state.hw_initialized) {
                    hw->pwm_init(node->params.pin, node->params.frequency, node->params.resolution);
                    node->state.hw_initialized = true;
                }
                hw->pwm_write(node->params.pin, duty);
            }
            /* outputs[0] tetap nilai duty% asli (bukan raw duty register) -
             * dipakai UI utk indikator, sesuai spec 05 "tampilkan angka Freq/Duty". */
            return in[0];
        }
        case FBD_NODE_SERVO: {
            float angle = fbd_to_float(in[0]);
            if (angle < 0.0f) angle = 0.0f;
            if (angle > 180.0f) angle = 180.0f;
            if (node->params.hw_mode == FBD_HW_REAL) {
                const fbd_hw_backend_t *hw = fbd_hw_get_backend();
                if (!node->state.hw_initialized) {
                    hw->servo_init(node->params.pin, node->params.min_us, node->params.max_us);
                    node->state.hw_initialized = true;
                }
                hw->servo_write(node->params.pin, angle, node->params.min_us, node->params.max_us);
            }
            return fbd_make_float(angle);
        }
        case FBD_NODE_WS2812: {
            /* Input R,G,B (0-255) - di-clamp supaya nilai di luar rentang
             * (mis. hasil scale/math) tidak wrap-around jadi warna acak. */
            int r = (int)fbd_to_float(in[0]);
            int g = (int)fbd_to_float(in[1]);
            int b = (int)fbd_to_float(in[2]);
            if (r < 0) r = 0;
            if (r > 255) r = 255;
            if (g < 0) g = 0;
            if (g > 255) g = 255;
            if (b < 0) b = 0;
            if (b > 255) b = 255;
            if (node->params.hw_mode == FBD_HW_REAL) {
                const fbd_hw_backend_t *hw = fbd_hw_get_backend();
                if (!node->state.hw_initialized) {
                    hw->ws2812_init(node->params.pin, node->params.ws2812_count);
                    node->state.hw_initialized = true;
                }
                hw->ws2812_write(node->params.pin, node->params.ws2812_count,
                                  (uint8_t)r, (uint8_t)g, (uint8_t)b);
            }
            /* outputs[0] pass-through r (indikator UI paling representatif
             * dari 3 channel - outputs[] cuma 2 slot, tidak cukup utk r,g,b
             * sekaligus; live monitor GET /api/program tetap bisa lihat
             * inputs[] lengkap kalau perlu r/g/b masing-masing). */
            return fbd_make_int(r);
        }
        case FBD_NODE_ULTRASONIC: {
            /* outputs[0] = jarak (cm), outputs[1] = error (bool, true
             * kalau timeout/out-of-range) - konsisten pola dengan
             * i2c_read_reg (raw_bytes + error), supaya "0 cm valid" bisa
             * dibedakan dari "gagal ukur" tanpa harus menebak dari angka
             * 0 itu sendiri (0 cm secara fisik memang mungkin terjadi
             * kalau objek nempel persis di sensor). */
            if (node->params.hw_mode != FBD_HW_REAL) {
                *out_secondary = fbd_make_bool(false);
                return fbd_make_float(node->params.ultrasonic_sim_distance_cm);
            }
            const fbd_hw_backend_t *hw = fbd_hw_get_backend();
            if (!node->state.hw_initialized) {
                hw->ultrasonic_init(node->params.pin, node->params.ultrasonic_echo_pin);
                node->state.hw_initialized = true;
            }
            float distance_cm = 0.0f;
            bool ok = hw->ultrasonic_read(node->params.pin, node->params.ultrasonic_echo_pin, &distance_cm);
            *out_secondary = fbd_make_bool(!ok);
            return fbd_make_float(ok ? distance_cm : 0.0f);
        }
        case FBD_NODE_I2C_READ_REG: {
            uint8_t buf[I2C_BRIDGE_MAX_DATA_LEN];
            uint8_t len = node->params.i2c_data_len;
            if (len > sizeof(buf)) len = sizeof(buf);
            bool ok = i2c_bridge_read_reg(node->params.i2c_bus, node->params.i2c_address,
                                           node->params.i2c_register, buf, len);
            *out_secondary = fbd_make_bool(!ok);
            return fbd_make_bytes(buf, len);
        }
        case FBD_NODE_I2C_WRITE_REG: {
            bool ok = i2c_bridge_write_reg(node->params.i2c_bus, node->params.i2c_address,
                                            node->params.i2c_register,
                                            node->params.i2c_data, node->params.i2c_data_len);
            return fbd_make_bool(ok);
        }
        case FBD_NODE_I2C_WRITE_BURST: {
            /* Kirim commands[] berurutan dalam SATU scan cycle. Berhenti di
             * command pertama yang gagal (NACK/timeout) - output = false,
             * sisa command TIDAK dicoba (device kemungkinan sudah dalam
             * state tidak diketahui, melanjutkan berisiko salah kirim). */
            bool ok = true;
            uint8_t count = node->params.i2c_burst_cmd_count;
            if (count > FBD_I2C_BURST_MAX_CMDS) count = FBD_I2C_BURST_MAX_CMDS;
            for (uint8_t i = 0; i < count; ++i) {
                const fbd_i2c_burst_cmd_t *cmd = &node->params.i2c_burst_cmds[i];
                ok = i2c_bridge_write_reg(node->params.i2c_bus, node->params.i2c_address,
                                           cmd->reg, cmd->data, cmd->data_len);
                if (!ok) break;
                if (node->params.i2c_burst_delay_us > 0 && i + 1 < count) {
                    i2c_bridge_delay_us(node->params.i2c_burst_delay_us);
                }
            }
            return fbd_make_bool(ok);
        }
        case FBD_NODE_SYS_VAR_GET:
            return fbd_sys_vars_get(node->params.sys_var_name);
        default:
            return fbd_make_empty();
    }
}

void fbd_graph_execute_cycle(fbd_graph_t *g, uint32_t now_ms)
{
    for (size_t k = 0; k < g->order_count; ++k) {
        size_t idx = g->execution_order[k];
        fbd_node_t *node = &g->nodes[idx];
        node->outputs[1] = fbd_make_empty();
        node->outputs[0] = evaluate_node(node, &g->vars, now_ms, &node->outputs[1]);

        for (size_t i = 0; i < g->link_count; ++i) {
            fbd_link_t *link = &g->links[i];
            if (link->from_idx == idx) {
                g->nodes[link->to_idx].inputs[link->to_port] = node->outputs[link->from_port];
            }
        }
    }
}

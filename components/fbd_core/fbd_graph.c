#include "fbd_graph.h"
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

static fbd_value_t evaluate_node(fbd_node_t *node, fbd_var_store_t *vars, uint32_t now_ms)
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
        case FBD_NODE_CTU:
            return fbd_eval_ctu(in[0], in[1], node->params.preset, &node->state.counter);
        case FBD_NODE_DIGITAL_IN:
        case FBD_NODE_DIGITAL_OUT:
            /* Level 1 - implementasi hardware ditambahkan di spec 05.
             * Untuk sekarang cukup pass-through supaya bisa dites di host
             * lewat manipulasi outputs[0]/inputs[0] langsung. */
            return in[0];
        default:
            return fbd_make_empty();
    }
}

void fbd_graph_execute_cycle(fbd_graph_t *g, uint32_t now_ms)
{
    for (size_t k = 0; k < g->order_count; ++k) {
        size_t idx = g->execution_order[k];
        fbd_node_t *node = &g->nodes[idx];
        node->outputs[0] = evaluate_node(node, &g->vars, now_ms);

        for (size_t i = 0; i < g->link_count; ++i) {
            fbd_link_t *link = &g->links[i];
            if (link->from_idx == idx) {
                g->nodes[link->to_idx].inputs[link->to_port] = node->outputs[link->from_port];
            }
        }
    }
}

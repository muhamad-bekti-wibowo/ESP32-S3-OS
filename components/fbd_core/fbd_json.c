#include "fbd_json.h"
#include <string.h>
#include <stdio.h>

static void set_err(char *err, size_t err_len, const char *msg)
{
    if (err && err_len > 0) {
        snprintf(err, err_len, "%s", msg);
    }
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
        case FBD_NODE_CTU:          return "ctu";
        case FBD_NODE_DIGITAL_IN:   return "digital_input";
        case FBD_NODE_DIGITAL_OUT:  return "digital_output";
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
        case FBD_NODE_CTU: {
            const cJSON *preset = cJSON_GetObjectItem(params, "preset");
            if (!preset) {
                set_err(err, err_len, "ctu: params.preset wajib");
                return false;
            }
            node->params.preset = (int32_t)cJSON_GetNumberValue(preset);
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
        case FBD_NODE_CTU:
            cJSON_AddNumberToObject(params, "preset", node->params.preset);
            break;
        case FBD_NODE_DIGITAL_IN:
            cJSON_AddNumberToObject(params, "pin", node->params.pin);
            cJSON_AddStringToObject(params, "mode", pin_mode_to_str(node->params.pin_mode));
            cJSON_AddBoolToObject(params, "invert", node->params.invert);
            break;
        case FBD_NODE_DIGITAL_OUT:
            cJSON_AddNumberToObject(params, "pin", node->params.pin);
            cJSON_AddBoolToObject(params, "invert", node->params.invert);
            break;
        default:
            break;
    }
    return params;
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

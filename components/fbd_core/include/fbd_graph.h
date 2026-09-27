#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "fbd_value.h"
#include "fbd_nodes.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FBD_MAX_NODES 64
#define FBD_MAX_LINKS 128
#define FBD_MAX_NODE_INPUTS 4
#define FBD_MAX_NODE_OUTPUTS 2
#define FBD_MAX_ID_LEN 16

typedef enum {
    FBD_NODE_CONST = 0,
    FBD_NODE_VAR_GET,
    FBD_NODE_VAR_SET,
    FBD_NODE_AND,
    FBD_NODE_OR,
    FBD_NODE_NOT,
    FBD_NODE_XOR,
    FBD_NODE_NAND,
    FBD_NODE_NOR,
    FBD_NODE_COMPARE,
    FBD_NODE_MATH,
    FBD_NODE_MIN,
    FBD_NODE_MAX,
    FBD_NODE_ABS,
    FBD_NODE_SCALE,
    FBD_NODE_CLAMP,
    FBD_NODE_TON,
    FBD_NODE_TOF,
    FBD_NODE_TP,
    FBD_NODE_CTU,
    FBD_NODE_DIGITAL_IN,
    FBD_NODE_DIGITAL_OUT,
    FBD_NODE_TYPE_COUNT
} fbd_node_type_t;

typedef enum {
    FBD_PIN_MODE_PULLUP = 0,
    FBD_PIN_MODE_PULLDOWN,
    FBD_PIN_MODE_FLOATING
} fbd_pin_mode_t;

typedef struct {
    fbd_value_t const_value;         /* FBD_NODE_CONST */
    char var_name[FBD_VAR_NAME_LEN]; /* FBD_NODE_VAR_GET / FBD_NODE_VAR_SET */
    fbd_compare_op_t compare_op;         /* FBD_NODE_COMPARE */
    fbd_math_op_t math_op;               /* FBD_NODE_MATH */
    float in_min, in_max, out_min, out_max; /* FBD_NODE_SCALE */
    float clamp_min, clamp_max;       /* FBD_NODE_CLAMP */
    uint32_t delay_ms;                 /* FBD_NODE_TON/TOF/TP */
    int32_t preset;                    /* FBD_NODE_CTU */
    int pin;                            /* FBD_NODE_DIGITAL_IN/OUT */
    bool invert;                        /* FBD_NODE_DIGITAL_IN/OUT */
    fbd_pin_mode_t pin_mode;            /* FBD_NODE_DIGITAL_IN saja */
} fbd_node_params_t;

typedef struct {
    fbd_timer_state_t timer;
    fbd_counter_state_t counter;
} fbd_node_state_t;

typedef struct {
    char id[FBD_MAX_ID_LEN];
    fbd_node_type_t type;
    fbd_node_params_t params;
    fbd_node_state_t state;
    fbd_value_t inputs[FBD_MAX_NODE_INPUTS];
    fbd_value_t outputs[FBD_MAX_NODE_OUTPUTS];
} fbd_node_t;

typedef struct {
    size_t from_idx; uint8_t from_port;
    size_t to_idx;   uint8_t to_port;
} fbd_link_t;

typedef struct {
    fbd_node_t nodes[FBD_MAX_NODES];
    size_t node_count;

    fbd_link_t links[FBD_MAX_LINKS];
    size_t link_count;

    size_t execution_order[FBD_MAX_NODES];
    size_t order_count;

    fbd_var_store_t vars;
} fbd_graph_t;

void fbd_graph_init(fbd_graph_t *g);

/* Return NULL kalau graph penuh (FBD_MAX_NODES tercapai). */
fbd_node_t *fbd_graph_add_node(fbd_graph_t *g, const char *id, fbd_node_type_t type);

/* Return false kalau id tidak ditemukan atau link penuh (FBD_MAX_LINKS). */
bool fbd_graph_add_link(fbd_graph_t *g, const char *from_id, uint8_t from_port,
                         const char *to_id, uint8_t to_port);

/* Cari index node by id. Return (size_t)-1 kalau tidak ditemukan. */
size_t fbd_graph_find_node(const fbd_graph_t *g, const char *id);

/* Topological sort (Kahn's algorithm). Return false kalau ada cyclic
 * dependency - execution_order tidak dipakai kalau gagal. */
bool fbd_graph_compile(fbd_graph_t *g);

/* Jalankan satu scan cycle sesuai execution_order. Panggil fbd_graph_compile()
 * dulu minimal sekali sebelum ini. */
void fbd_graph_execute_cycle(fbd_graph_t *g, uint32_t now_ms);

#ifdef __cplusplus
}
#endif

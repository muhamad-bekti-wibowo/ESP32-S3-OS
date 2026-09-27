#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LOGIC_MAX_BLOCKS   64
#define LOGIC_MAX_INPUTS   4
#define LOGIC_NAME_LEN     16

/* Tipe block yang didukung. Ditambah bertahap menuju set block ala FBD PLC. */
typedef enum {
    BLOCK_CONST = 0,   /* nilai tetap */
    BLOCK_INPUT,       /* baca GPIO digital/analog */
    BLOCK_OUTPUT,      /* tulis GPIO digital */
    BLOCK_COMPARE,     /* >, <, ==, !=, >=, <= */
    BLOCK_IF_ELSE,     /* pilih salah satu dari 2 input berdasar kondisi */
    BLOCK_COUNTER,     /* counter naik/turun dengan reset */
    BLOCK_MATH,        /* +, -, *, / */
    BLOCK_TYPE_COUNT
} block_type_t;

typedef enum {
    OP_ADD = 0, OP_SUB, OP_MUL, OP_DIV
} math_op_t;

typedef enum {
    CMP_GT = 0, CMP_LT, CMP_EQ, CMP_NEQ, CMP_GTE, CMP_LTE
} compare_op_t;

/* Referensi ke output block lain, dipakai sebagai "wire". id == -1 berarti tidak terhubung. */
typedef struct {
    int16_t block_id;
} link_ref_t;

typedef struct {
    char name[LOGIC_NAME_LEN];
    block_type_t type;

    link_ref_t in[LOGIC_MAX_INPUTS];   /* wiring dari block lain */

    /* parameter spesifik per tipe */
    union {
        struct { double value; } konst;
        struct { int gpio; bool analog; } input;
        struct { int gpio; } output;
        struct { compare_op_t op; } compare;
        struct { math_op_t op; } math;
        struct { int32_t preset; bool count_up; bool reset_state; } counter;
    } cfg;

    /* runtime state */
    double out_value;
    int32_t counter_value;
    bool counter_prev_clk;
} logic_block_t;

typedef struct {
    logic_block_t blocks[LOGIC_MAX_BLOCKS];
    int block_count;
} logic_program_t;

/* Kosongkan program (semua block dihapus). */
void logic_engine_init(logic_program_t *prog);

/* Muat program dari JSON (format didokumentasikan di komponen web_ui).
 * Return true jika berhasil di-parse. */
bool logic_engine_load_json(logic_program_t *prog, const cJSON *json);

/* Jalankan satu siklus scan: evaluasi semua block sesuai urutan definisi
 * (asumsi: block input harus didefinisikan sebelum block yang memakainya). */
void logic_engine_scan(logic_program_t *prog);

/* Ambil nilai output block by index, dipakai untuk debug/monitor via web. */
double logic_engine_get_output(const logic_program_t *prog, int block_id);

#ifdef __cplusplus
}
#endif

/* Test host murni C untuk fbd_graph: topological sort + execute cycle.
 * Lihat specs/02-topo-sort-runtime.md untuk kriteria selesai. */
#include <stdio.h>
#include <string.h>
#include "fbd_graph.h"

static int g_fail = 0;

#define CHECK(cond, msg) do { \
    if (cond) { printf("PASS: %s\n", msg); } \
    else { printf("FAIL: %s\n", msg); g_fail++; } \
} while (0)

/* const_1(10) -> add_1 <- const_2(5) -> scale_1 [0-100]->[0-1]
 * Node ditambah SENGAJA TERBALIK: scale_1, add_1, const_2, const_1.
 * execution_order harus tetap ikut dependency (const dulu, scale terakhir),
 * bukan urutan penambahan ke array node. */
static void test_topo_sort_reversed_order(void)
{
    fbd_graph_t g;
    fbd_graph_init(&g);

    fbd_node_t *scale_1 = fbd_graph_add_node(&g, "scale_1", FBD_NODE_SCALE);
    scale_1->params.in_min = 0.0f; scale_1->params.in_max = 100.0f;
    scale_1->params.out_min = 0.0f; scale_1->params.out_max = 1.0f;

    fbd_graph_add_node(&g, "add_1", FBD_NODE_MATH)->params.math_op = FBD_OP_ADD;

    fbd_node_t *const_2 = fbd_graph_add_node(&g, "const_2", FBD_NODE_CONST);
    const_2->params.const_value = fbd_make_float(5.0f);

    fbd_node_t *const_1 = fbd_graph_add_node(&g, "const_1", FBD_NODE_CONST);
    const_1->params.const_value = fbd_make_float(45.0f);

    bool ok = true;
    ok &= fbd_graph_add_link(&g, "const_1", 0, "add_1", 0);
    ok &= fbd_graph_add_link(&g, "const_2", 0, "add_1", 1);
    ok &= fbd_graph_add_link(&g, "add_1", 0, "scale_1", 0);
    CHECK(ok, "semua link berhasil ditambahkan (id valid)");

    bool compiled = fbd_graph_compile(&g);
    CHECK(compiled, "compile() sukses (tidak cyclic)");
    CHECK(g.order_count == g.node_count, "execution_order mencakup semua node");

    printf("Urutan eksekusi node:\n");
    for (size_t i = 0; i < g.order_count; ++i) {
        printf(" -> %s\n", g.nodes[g.execution_order[i]].id);
    }

    /* const_1/const_2 harus dieksekusi sebelum add_1, add_1 sebelum scale_1 -
     * dicek lewat posisi di execution_order, bukan urutan penambahan. */
    size_t pos_const1 = 0, pos_const2 = 0, pos_add1 = 0, pos_scale1 = 0;
    for (size_t i = 0; i < g.order_count; ++i) {
        const char *id = g.nodes[g.execution_order[i]].id;
        if (strcmp(id, "const_1") == 0) pos_const1 = i;
        if (strcmp(id, "const_2") == 0) pos_const2 = i;
        if (strcmp(id, "add_1") == 0)   pos_add1 = i;
        if (strcmp(id, "scale_1") == 0) pos_scale1 = i;
    }
    CHECK(pos_const1 < pos_add1, "const_1 dieksekusi sebelum add_1");
    CHECK(pos_const2 < pos_add1, "const_2 dieksekusi sebelum add_1");
    CHECK(pos_add1 < pos_scale1, "add_1 dieksekusi sebelum scale_1");

    fbd_graph_execute_cycle(&g, 0);

    size_t scale_idx = fbd_graph_find_node(&g, "scale_1");
    float result = fbd_to_float(g.nodes[scale_idx].outputs[0]);
    printf("\nHasil output akhir scale_1 (50.0 di-scale ke 0-1):\nValue: %g\n\n", result);
    CHECK(result == 0.5f, "hasil akhir scale_1 = 0.5 (const_1=45 + const_2=5 = 50, scale ke 0-1)");
}

static void test_cyclic_rejected(void)
{
    fbd_graph_t g;
    fbd_graph_init(&g);

    fbd_graph_add_node(&g, "n1", FBD_NODE_NOT);
    fbd_graph_add_node(&g, "n2", FBD_NODE_NOT);

    /* n1 -> n2 -> n1 : cyclic */
    fbd_graph_add_link(&g, "n1", 0, "n2", 0);
    fbd_graph_add_link(&g, "n2", 0, "n1", 0);

    bool compiled = fbd_graph_compile(&g);
    CHECK(compiled == false, "graph cyclic ditolak oleh fbd_graph_compile()");
}

static void test_and_gate_chain(void)
{
    /* digital_input(true) -> AND <- timer_on_delay(input=true) -> digital_output
     * Meniru rangkaian dasar dari plan.md §7.1. */
    fbd_graph_t g;
    fbd_graph_init(&g);

    fbd_graph_add_node(&g, "n1", FBD_NODE_DIGITAL_IN);
    fbd_node_t *n2 = fbd_graph_add_node(&g, "n2", FBD_NODE_TON);
    n2->params.delay_ms = 0; /* langsung ON supaya bisa dites 1 cycle */
    fbd_graph_add_node(&g, "n3", FBD_NODE_AND);
    fbd_graph_add_node(&g, "n4", FBD_NODE_DIGITAL_OUT);

    fbd_graph_add_link(&g, "n1", 0, "n3", 0);
    fbd_graph_add_link(&g, "n2", 0, "n3", 1);
    fbd_graph_add_link(&g, "n3", 0, "n4", 0);

    CHECK(fbd_graph_compile(&g), "compile() sukses untuk rangkaian AND");

    size_t n1_idx = fbd_graph_find_node(&g, "n1");
    size_t n2_idx = fbd_graph_find_node(&g, "n2");
    g.nodes[n1_idx].inputs[0] = fbd_make_bool(true);
    g.nodes[n2_idx].inputs[0] = fbd_make_bool(true);

    fbd_graph_execute_cycle(&g, 0);

    size_t n4_idx = fbd_graph_find_node(&g, "n4");
    CHECK(fbd_to_bool(g.nodes[n4_idx].outputs[0]) == true,
          "digital_output true setelah AND(digital_input=true, timer_on_delay=true)");
}

int main(void)
{
    test_topo_sort_reversed_order();
    test_cyclic_rejected();
    test_and_gate_chain();

    printf("\n%s (%d gagal)\n", g_fail == 0 ? "SEMUA TEST LOLOS" : "ADA TEST GAGAL", g_fail);
    return g_fail == 0 ? 0 : 1;
}

/* Test host murni C untuk i2c_read_reg/i2c_write_reg/sys_var_get.
 * TANPA bus I2C fisik - dipakai i2c_bridge_stub.c yang selalu return error
 * (i2c_bridge_stub.c) sesuai kontrak "TIDAK ada I2C simulated" di
 * i2c_bridge.h - stub cuma untuk isolasi test host, bukan mode "simulated"
 * yang sah secara desain. Lihat specs/06-level2-i2c-wifi.md. */
#include <stdio.h>
#include <string.h>
#include "fbd_graph.h"
#include "fbd_sys_vars.h"

static int g_fail = 0;

#define CHECK(cond, msg) do { \
    if (cond) { printf("PASS: %s\n", msg); } \
    else { printf("FAIL: %s\n", msg); g_fail++; } \
} while (0)

/* Kriteria selesai 6a: i2c_read_reg gagal (stub selalu NACK) -> error=true,
 * scan cycle TIDAK berhenti/crash, node lain di graph tetap dieksekusi. */
static void test_i2c_read_reg_error_does_not_block_scan_cycle(void)
{
    fbd_graph_t g;
    fbd_graph_init(&g);

    fbd_node_t *i2c_node = fbd_graph_add_node(&g, "i2c1", FBD_NODE_I2C_READ_REG);
    i2c_node->params.i2c_bus = 0;
    i2c_node->params.i2c_address = 0x27; /* alamat khas LCD I2C backpack PCF8574 */
    i2c_node->params.i2c_register = 0;
    i2c_node->params.i2c_data_len = 2;

    /* Node lain yang tidak berhubungan dengan I2C - membuktikan scan cycle
     * tetap menjalankan SEMUA node walau i2c1 gagal, bukan berhenti di situ. */
    fbd_node_t *cst = fbd_graph_add_node(&g, "cst1", FBD_NODE_CONST);
    cst->params.const_value = fbd_make_float(42.0f);

    CHECK(fbd_graph_compile(&g), "compile() sukses untuk graph dengan i2c_read_reg");

    fbd_graph_execute_cycle(&g, 0);

    size_t i2c_idx = fbd_graph_find_node(&g, "i2c1");
    CHECK(fbd_to_bool(g.nodes[i2c_idx].outputs[1]) == true,
          "i2c_read_reg gagal (stub NACK) -> outputs[1] (error) = true");
    CHECK(g.nodes[i2c_idx].outputs[0].type == FBD_BYTES,
          "i2c_read_reg outputs[0] (raw_bytes) tetap FBD_BYTES walau gagal (fallback kosong)");

    size_t cst_idx = fbd_graph_find_node(&g, "cst1");
    CHECK(fbd_to_float(g.nodes[cst_idx].outputs[0]) == 42.0f,
          "node lain (cst1) tetap dieksekusi normal - scan cycle tidak berhenti karena i2c1 gagal");

    /* Jalankan beberapa cycle lagi - membuktikan tidak ada crash/hang berulang. */
    for (int i = 0; i < 5; ++i) {
        fbd_graph_execute_cycle(&g, (uint32_t)(i * 20));
    }
    CHECK(fbd_to_bool(g.nodes[i2c_idx].outputs[1]) == true,
          "setelah beberapa cycle lagi, i2c_read_reg tetap error=true (konsisten, tidak hang)");
}

static void test_i2c_write_reg_error(void)
{
    fbd_graph_t g;
    fbd_graph_init(&g);

    fbd_node_t *w = fbd_graph_add_node(&g, "w1", FBD_NODE_I2C_WRITE_REG);
    w->params.i2c_bus = 0;
    w->params.i2c_address = 0x27;
    w->params.i2c_register = 0;
    w->params.i2c_data[0] = 0xFF;
    w->params.i2c_data_len = 1;

    CHECK(fbd_graph_compile(&g), "compile() sukses untuk graph dengan i2c_write_reg");
    fbd_graph_execute_cycle(&g, 0);

    size_t w_idx = fbd_graph_find_node(&g, "w1");
    CHECK(fbd_to_bool(g.nodes[w_idx].outputs[0]) == false,
          "i2c_write_reg gagal (stub NACK) -> outputs[0] = false (bukan crash)");
}

/* ---- sys_var_get (6b) ---- */

static fbd_value_t test_sys_var_provider(const char *name)
{
    if (strcmp(name, "SYS.WIFI_CONNECTED") == 0) {
        return fbd_make_bool(true);
    }
    if (strcmp(name, "SYS.WIFI_RSSI") == 0) {
        return fbd_make_int(-85); /* sengaja lemah, utk uji Compare < -80 */
    }
    return fbd_make_empty();
}

static void test_sys_var_get_wifi_rssi_compare(void)
{
    fbd_sys_vars_set_provider(test_sys_var_provider);

    fbd_graph_t g;
    fbd_graph_init(&g);

    fbd_node_t *rssi = fbd_graph_add_node(&g, "rssi1", FBD_NODE_SYS_VAR_GET);
    strncpy(rssi->params.sys_var_name, "SYS.WIFI_RSSI", sizeof(rssi->params.sys_var_name) - 1);

    fbd_node_t *threshold = fbd_graph_add_node(&g, "th1", FBD_NODE_CONST);
    threshold->params.const_value = fbd_make_float(-80.0f);

    fbd_node_t *cmp = fbd_graph_add_node(&g, "cmp1", FBD_NODE_COMPARE);
    cmp->params.compare_op = FBD_CMP_LT;

    fbd_node_t *dout = fbd_graph_add_node(&g, "dout1", FBD_NODE_DIGITAL_OUT);
    dout->params.hw_mode = FBD_HW_SIMULATED;

    fbd_graph_add_link(&g, "rssi1", 0, "cmp1", 0);
    fbd_graph_add_link(&g, "th1", 0, "cmp1", 1);
    fbd_graph_add_link(&g, "cmp1", 0, "dout1", 0);

    CHECK(fbd_graph_compile(&g), "compile() sukses untuk rangkaian sys_var_get(WIFI_RSSI) -> Compare -> digital_out");

    fbd_graph_execute_cycle(&g, 0);

    size_t rssi_idx = fbd_graph_find_node(&g, "rssi1");
    CHECK(g.nodes[rssi_idx].outputs[0].i == -85, "sys_var_get(SYS.WIFI_RSSI) = -85 (dari provider)");

    size_t dout_idx = fbd_graph_find_node(&g, "dout1");
    CHECK(fbd_to_bool(g.nodes[dout_idx].outputs[0]) == true,
          "RSSI -85 < threshold -80 -> digital_output (warning LED) true");

    fbd_sys_vars_set_provider(NULL); /* reset supaya tidak bocor ke test lain */
}

static void test_sys_var_get_unknown_name(void)
{
    fbd_sys_vars_set_provider(test_sys_var_provider);

    fbd_graph_t g;
    fbd_graph_init(&g);
    fbd_node_t *node = fbd_graph_add_node(&g, "n1", FBD_NODE_SYS_VAR_GET);
    strncpy(node->params.sys_var_name, "SYS.TIDAK_ADA", sizeof(node->params.sys_var_name) - 1);

    fbd_graph_compile(&g);
    fbd_graph_execute_cycle(&g, 0);

    size_t idx = fbd_graph_find_node(&g, "n1");
    CHECK(g.nodes[idx].outputs[0].type == FBD_EMPTY,
          "sys_var_get nama tidak dikenal -> FBD_EMPTY (bukan crash)");

    fbd_sys_vars_set_provider(NULL);
}

static void test_sys_var_get_no_provider_registered(void)
{
    /* Belum pernah panggil fbd_sys_vars_set_provider() - harus aman
     * (bukan crash null pointer). */
    fbd_value_t v = fbd_sys_vars_get("SYS.WIFI_CONNECTED");
    CHECK(v.type == FBD_EMPTY, "fbd_sys_vars_get tanpa provider terdaftar -> FBD_EMPTY, bukan crash");
}

int main(void)
{
    test_i2c_read_reg_error_does_not_block_scan_cycle();
    test_i2c_write_reg_error();
    test_sys_var_get_no_provider_registered();
    test_sys_var_get_wifi_rssi_compare();
    test_sys_var_get_unknown_name();

    printf("\n%s (%d gagal)\n", g_fail == 0 ? "SEMUA TEST LOLOS" : "ADA TEST GAGAL", g_fail);
    return g_fail == 0 ? 0 : 1;
}

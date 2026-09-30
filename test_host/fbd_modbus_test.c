/* Test host murni C untuk modbus_tcp_read/modbus_tcp_write/
 * modbus_slave_reg. TANPA socket/UART fisik - dipakai modbus_tcp_bridge_
 * stub.c/modbus_slave_bridge_stub.c yang tidak pernah "menjalankan"
 * request sungguhan (tidak ada task background di stub, cuma slot table
 * biasa) - cukup untuk memverifikasi logic evaluate_node() membaca/
 * menulis bridge dengan benar, bukan menguji network/RS485 sungguhan. */
#include <stdio.h>
#include <string.h>
#include "fbd_graph.h"
#include "modbus_tcp_bridge.h"
#include "modbus_slave_bridge.h"

static int g_fail = 0;

#define CHECK(cond, msg) do { \
    if (cond) { printf("PASS: %s\n", msg); } \
    else { printf("FAIL: %s\n", msg); g_fail++; } \
} while (0)

/* modbus_tcp_read: sebelum bridge PERNAH diisi hasil (stub tidak punya
 * task yang benar-benar merespons), outputs[1] (error) harus true dan
 * outputs[0] tetap 0 - membuktikan evaluate_node() tidak crash/hang
 * menunggu response yang tidak akan pernah datang di test host. */
static void test_modbus_tcp_read_no_response_yet(void)
{
    fbd_graph_t g;
    fbd_graph_init(&g);

    fbd_node_t *r = fbd_graph_add_node(&g, "r1", FBD_NODE_MODBUS_TCP_READ);
    strncpy(r->params.modbus_ip, "192.168.1.50", sizeof(r->params.modbus_ip) - 1);
    r->params.modbus_port = 502;
    r->params.modbus_unit_id = 1;
    r->params.modbus_reg_type = FBD_MODBUS_REG_HOLDING;
    r->params.modbus_address = 100;
    r->params.modbus_count = 1;

    CHECK(fbd_graph_compile(&g), "compile() sukses untuk graph dengan modbus_tcp_read");
    fbd_graph_execute_cycle(&g, 0);

    size_t idx = fbd_graph_find_node(&g, "r1");
    CHECK(fbd_to_bool(g.nodes[idx].outputs[1]) == true,
          "modbus_tcp_read: belum ada response (stub) -> outputs[1] (error) = true");
    CHECK(g.nodes[idx].outputs[0].i == 0,
          "modbus_tcp_read: outputs[0] tetap 0 selama belum ada response");

    /* Jalankan beberapa cycle lagi - membuktikan tidak hang/crash. */
    for (int i = 0; i < 5; ++i) {
        fbd_graph_execute_cycle(&g, (uint32_t)(i * 20));
    }
    CHECK(fbd_to_bool(g.nodes[idx].outputs[1]) == true,
          "modbus_tcp_read: tetap konsisten error=true setelah beberapa cycle (tidak hang)");
}

/* Simulasikan task modbus_tcp_task "menjawab" request lewat
 * modbus_tcp_bridge_set_result() langsung (meniru apa yang firmware
 * lakukan setelah request TCP sukses) - membuktikan evaluate_node()
 * membaca hasil itu di scan cycle SETELAHNYA dengan benar. */
static void test_modbus_tcp_read_after_simulated_response(void)
{
    fbd_graph_t g;
    fbd_graph_init(&g);

    fbd_node_t *r = fbd_graph_add_node(&g, "r1", FBD_NODE_MODBUS_TCP_READ);
    strncpy(r->params.modbus_ip, "192.168.1.51", sizeof(r->params.modbus_ip) - 1);
    r->params.modbus_port = 502;
    r->params.modbus_unit_id = 1;
    r->params.modbus_reg_type = FBD_MODBUS_REG_HOLDING;
    r->params.modbus_address = 10;
    r->params.modbus_count = 1;

    CHECK(fbd_graph_compile(&g), "compile() sukses untuk modbus_tcp_read (count=1)");

    /* Cycle pertama: kirim request (bikin key di bridge). */
    fbd_graph_execute_cycle(&g, 0);

    char key[MODBUS_TCP_BRIDGE_KEY_LEN];
    modbus_tcp_bridge_make_key(key, "192.168.1.51", 502, 1, FBD_MODBUS_REG_HOLDING, 10, 1);
    uint16_t values[1] = { 1234 };
    modbus_tcp_bridge_set_result(key, values, 1, true);

    /* Cycle kedua: evaluate_node() baca hasil yang baru "datang". */
    fbd_graph_execute_cycle(&g, 20);

    size_t idx = fbd_graph_find_node(&g, "r1");
    CHECK(g.nodes[idx].outputs[0].i == 1234,
          "modbus_tcp_read: outputs[0] = 1234 setelah bridge diisi hasil sukses");
    CHECK(fbd_to_bool(g.nodes[idx].outputs[1]) == false,
          "modbus_tcp_read: outputs[1] (error) = false setelah response sukses");
}

/* count>1 -> outputs[0] harus FBD_BYTES (raw uint16 little-endian per
 * register), bukan FBD_INT32 - beda dari kasus count==1. */
static void test_modbus_tcp_read_multi_count_returns_bytes(void)
{
    fbd_graph_t g;
    fbd_graph_init(&g);

    fbd_node_t *r = fbd_graph_add_node(&g, "r1", FBD_NODE_MODBUS_TCP_READ);
    strncpy(r->params.modbus_ip, "192.168.1.52", sizeof(r->params.modbus_ip) - 1);
    r->params.modbus_port = 502;
    r->params.modbus_unit_id = 1;
    r->params.modbus_reg_type = FBD_MODBUS_REG_HOLDING;
    r->params.modbus_address = 0;
    r->params.modbus_count = 3;

    CHECK(fbd_graph_compile(&g), "compile() sukses untuk modbus_tcp_read (count=3)");
    fbd_graph_execute_cycle(&g, 0);

    char key[MODBUS_TCP_BRIDGE_KEY_LEN];
    modbus_tcp_bridge_make_key(key, "192.168.1.52", 502, 1, FBD_MODBUS_REG_HOLDING, 0, 3);
    uint16_t values[3] = { 0x0102, 0x0304, 0x0506 };
    modbus_tcp_bridge_set_result(key, values, 3, true);
    fbd_graph_execute_cycle(&g, 20);

    size_t idx = fbd_graph_find_node(&g, "r1");
    CHECK(g.nodes[idx].outputs[0].type == FBD_BYTES,
          "modbus_tcp_read: count=3 -> outputs[0] tipe FBD_BYTES");
    CHECK(g.nodes[idx].outputs[0].bytes.len == 6,
          "modbus_tcp_read: count=3 -> 6 byte (3x uint16)");
    CHECK(g.nodes[idx].outputs[0].bytes.data[0] == 0x02 && g.nodes[idx].outputs[0].bytes.data[1] == 0x01,
          "modbus_tcp_read: register pertama (0x0102) di-pack little-endian di byte 0-1");
}

static void test_modbus_tcp_write_coil(void)
{
    fbd_graph_t g;
    fbd_graph_init(&g);

    fbd_node_t *cst = fbd_graph_add_node(&g, "cst1", FBD_NODE_CONST);
    cst->params.const_value = fbd_make_bool(true);

    fbd_node_t *w = fbd_graph_add_node(&g, "w1", FBD_NODE_MODBUS_TCP_WRITE);
    strncpy(w->params.modbus_ip, "192.168.1.53", sizeof(w->params.modbus_ip) - 1);
    w->params.modbus_port = 502;
    w->params.modbus_unit_id = 1;
    w->params.modbus_reg_type = FBD_MODBUS_REG_COIL;
    w->params.modbus_address = 5;

    fbd_graph_add_link(&g, "cst1", 0, "w1", 0);

    CHECK(fbd_graph_compile(&g), "compile() sukses untuk cst1(true) -> modbus_tcp_write(coil)");
    fbd_graph_execute_cycle(&g, 0);

    size_t idx = fbd_graph_find_node(&g, "w1");
    CHECK(fbd_to_bool(g.nodes[idx].outputs[0]) == false,
          "modbus_tcp_write: belum ada response (stub) -> outputs[0] = false (bukan crash)");
}

/* modbus_slave_reg: nilai dari node lain (inputs[0]) harus terekspos ke
 * bridge "to_master" (yang akan dibaca master via RS485), DAN nilai
 * yang "ditulis master" (from_master, disimulasikan langsung lewat
 * modbus_slave_bridge_set_from_master()) harus muncul di outputs[0]
 * node di cycle berikutnya - membuktikan dua arah bekerja independen. */
static void test_modbus_slave_reg_bidirectional(void)
{
    fbd_graph_t g;
    fbd_graph_init(&g);

    fbd_node_t *cst = fbd_graph_add_node(&g, "cst1", FBD_NODE_CONST);
    cst->params.const_value = fbd_make_float(77.0f);

    fbd_node_t *slave = fbd_graph_add_node(&g, "sl1", FBD_NODE_MODBUS_SLAVE_REG);
    slave->params.modbus_slave_address = 200;
    slave->params.modbus_slave_reg_type = FBD_MODBUS_REG_HOLDING;

    fbd_graph_add_link(&g, "cst1", 0, "sl1", 0);

    CHECK(fbd_graph_compile(&g), "compile() sukses untuk cst1(77) -> modbus_slave_reg(holding,200)");
    fbd_graph_execute_cycle(&g, 0);

    char key[MODBUS_SLAVE_BRIDGE_KEY_LEN];
    modbus_slave_bridge_make_key(key, FBD_MODBUS_REG_HOLDING, 200);
    uint16_t to_master = 0;
    CHECK(modbus_slave_bridge_get_to_master(key, &to_master) && to_master == 77,
          "modbus_slave_reg: nilai dari inputs[0] (77) terdorong ke bridge to_master (dibaca master luar)");

    /* Simulasikan master eksternal menulis FC06 ke register ini. */
    CHECK(modbus_slave_bridge_set_from_master(key, 999),
          "modbus_slave_reg: simulasi master menulis FC06 (nilai 999) ke bridge from_master");

    fbd_graph_execute_cycle(&g, 20);

    size_t idx = fbd_graph_find_node(&g, "sl1");
    CHECK(g.nodes[idx].outputs[0].i == 999,
          "modbus_slave_reg: outputs[0] = 999 (dari master) di cycle berikutnya, TERPISAH dari to_master");
}

static void test_modbus_slave_reg_coil_bool(void)
{
    fbd_graph_t g;
    fbd_graph_init(&g);

    fbd_node_t *slave = fbd_graph_add_node(&g, "sl1", FBD_NODE_MODBUS_SLAVE_REG);
    slave->params.modbus_slave_address = 50;
    slave->params.modbus_slave_reg_type = FBD_MODBUS_REG_COIL;

    CHECK(fbd_graph_compile(&g), "compile() sukses untuk modbus_slave_reg(coil,50) tanpa input");
    fbd_graph_execute_cycle(&g, 0);

    size_t idx = fbd_graph_find_node(&g, "sl1");
    CHECK(g.nodes[idx].outputs[0].type == FBD_BOOL,
          "modbus_slave_reg: reg_type coil -> outputs[0] tipe FBD_BOOL");
    CHECK(fbd_to_bool(g.nodes[idx].outputs[0]) == false,
          "modbus_slave_reg: coil belum pernah ditulis master -> outputs[0] = false");
}

int main(void)
{
    test_modbus_tcp_read_no_response_yet();
    test_modbus_tcp_read_after_simulated_response();
    test_modbus_tcp_read_multi_count_returns_bytes();
    test_modbus_tcp_write_coil();
    test_modbus_slave_reg_bidirectional();
    test_modbus_slave_reg_coil_bool();

    printf("\n%s (%d gagal)\n", g_fail == 0 ? "SEMUA TEST LOLOS" : "ADA TEST GAGAL", g_fail);
    return g_fail == 0 ? 0 : 1;
}

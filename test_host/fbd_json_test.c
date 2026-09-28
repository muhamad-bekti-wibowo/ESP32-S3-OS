/* Test host murni C untuk fbd_json: parse & serialize sesuai schema.md.
 * Lihat specs/03-schema-freeze.md untuk kriteria selesai. */
#include <stdio.h>
#include <string.h>
#include "cJSON.h"
#include "fbd_json.h"

static int g_fail = 0;

#define CHECK(cond, msg) do { \
    if (cond) { printf("PASS: %s\n", msg); } \
    else { printf("FAIL: %s\n", msg); g_fail++; } \
} while (0)

static const char *k_example_json =
"{"
"  \"version\": 1,"
"  \"nodes\": ["
"    { \"id\": \"n1\", \"type\": \"digital_input\",  \"params\": { \"pin\": 4, \"mode\": \"pullup\", \"invert\": false } },"
"    { \"id\": \"n2\", \"type\": \"ton\", \"params\": { \"delay_ms\": 2000 } },"
"    { \"id\": \"n3\", \"type\": \"and\", \"params\": {} },"
"    { \"id\": \"n4\", \"type\": \"digital_output\", \"params\": { \"pin\": 2, \"invert\": false } }"
"  ],"
"  \"links\": ["
"    { \"from\": { \"node\": \"n1\", \"port\": 0 }, \"to\": { \"node\": \"n3\", \"port\": 0 } },"
"    { \"from\": { \"node\": \"n2\", \"port\": 0 }, \"to\": { \"node\": \"n3\", \"port\": 1 } },"
"    { \"from\": { \"node\": \"n3\", \"port\": 0 }, \"to\": { \"node\": \"n4\", \"port\": 0 } }"
"  ]"
"}";

static void test_parse_example_from_schema(void)
{
    cJSON *root = cJSON_Parse(k_example_json);
    CHECK(root != NULL, "contoh JSON dari schema.md berhasil di-cJSON_Parse");

    fbd_graph_t g;
    char err[FBD_JSON_ERR_LEN] = {0};
    bool ok = fbd_json_parse(root, &g, err, sizeof(err));
    CHECK(ok, "fbd_json_parse() sukses untuk contoh schema.md");
    if (!ok) {
        printf("  error: %s\n", err);
    }

    CHECK(g.node_count == 4, "4 node ter-parse");
    CHECK(g.link_count == 3, "3 link ter-parse");

    size_t n2_idx = fbd_graph_find_node(&g, "n2");
    CHECK(n2_idx != (size_t)-1, "node n2 ditemukan");
    CHECK(g.nodes[n2_idx].type == FBD_NODE_TON, "n2 bertipe FBD_NODE_TON");
    CHECK(g.nodes[n2_idx].params.delay_ms == 2000, "n2 params.delay_ms = 2000");

    CHECK(fbd_graph_compile(&g), "graph hasil parse berhasil di-compile (tidak cyclic)");

    cJSON_Delete(root);
}

static void test_roundtrip(void)
{
    cJSON *root = cJSON_Parse(k_example_json);
    fbd_graph_t g;
    fbd_json_parse(root, &g, NULL, 0);
    cJSON_Delete(root);

    cJSON *serialized = fbd_json_serialize(&g);
    char *serialized_str = cJSON_PrintUnformatted(serialized);
    printf("Hasil serialize:\n%s\n\n", serialized_str);

    /* Parse ulang hasil serialize, bandingkan graph-nya (bukan string-nya -
     * urutan key JSON tidak dijamin identik, tapi struktur graph harus setara). */
    cJSON *root2 = cJSON_Parse(serialized_str);
    fbd_graph_t g2;
    char err[FBD_JSON_ERR_LEN] = {0};
    bool ok2 = fbd_json_parse(root2, &g2, err, sizeof(err));
    CHECK(ok2, "hasil serialize bisa di-parse ulang (round-trip)");
    if (!ok2) printf("  error: %s\n", err);

    CHECK(g2.node_count == g.node_count, "round-trip: node_count sama");
    CHECK(g2.link_count == g.link_count, "round-trip: link_count sama");

    size_t n2_idx = fbd_graph_find_node(&g2, "n2");
    CHECK(g2.nodes[n2_idx].type == FBD_NODE_TON, "round-trip: n2 masih FBD_NODE_TON");
    CHECK(g2.nodes[n2_idx].params.delay_ms == 2000, "round-trip: n2 params.delay_ms tetap 2000");

    size_t n1_idx = fbd_graph_find_node(&g2, "n1");
    CHECK(g2.nodes[n1_idx].params.pin == 4, "round-trip: n1 (digital_input) params.pin tetap 4");
    CHECK(g2.nodes[n1_idx].params.pin_mode == FBD_PIN_MODE_PULLUP,
          "round-trip: n1 params.mode tetap pullup (bukan hilang/default salah)");

    free(serialized_str);
    cJSON_Delete(serialized);
    cJSON_Delete(root2);
}

static void test_level1_analog_pwm_servo_roundtrip(void)
{
    const char *json =
    "{ \"version\": 1, \"nodes\": ["
    "  { \"id\": \"ai1\", \"type\": \"analog_input\", \"params\": { \"pin\": 4, \"resolution\": 12, \"attenuation\": 11, \"hw_mode\": \"simulated\", \"sim_value\": 2048 } },"
    "  { \"id\": \"pwm1\", \"type\": \"pwm_output\", \"params\": { \"pin\": 5, \"frequency\": 1000, \"resolution\": 12, \"hw_mode\": \"simulated\" } },"
    "  { \"id\": \"servo1\", \"type\": \"servo\", \"params\": { \"pin\": 18, \"min_us\": 500, \"max_us\": 2500, \"hw_mode\": \"real\" } }"
    "], \"links\": [] }";

    cJSON *root = cJSON_Parse(json);
    fbd_graph_t g;
    char err[FBD_JSON_ERR_LEN] = {0};
    bool ok = fbd_json_parse(root, &g, err, sizeof(err));
    CHECK(ok, "analog_input/pwm_output/servo berhasil di-parse");
    if (!ok) printf("  error: %s\n", err);
    cJSON_Delete(root);

    size_t ai_idx = fbd_graph_find_node(&g, "ai1");
    CHECK(g.nodes[ai_idx].type == FBD_NODE_ANALOG_IN, "ai1 bertipe FBD_NODE_ANALOG_IN");
    CHECK(g.nodes[ai_idx].params.pin == 4, "ai1 params.pin = 4");
    CHECK(g.nodes[ai_idx].params.resolution == 12, "ai1 params.resolution = 12");
    CHECK(g.nodes[ai_idx].params.attenuation == 11, "ai1 params.attenuation = 11");
    CHECK(g.nodes[ai_idx].params.hw_mode == FBD_HW_SIMULATED, "ai1 params.hw_mode = simulated");
    CHECK(g.nodes[ai_idx].params.sim_value.i == 2048, "ai1 params.sim_value = 2048");

    size_t pwm_idx = fbd_graph_find_node(&g, "pwm1");
    CHECK(g.nodes[pwm_idx].params.frequency == 1000, "pwm1 params.frequency = 1000");

    size_t servo_idx = fbd_graph_find_node(&g, "servo1");
    CHECK(g.nodes[servo_idx].params.min_us == 500, "servo1 params.min_us = 500");
    CHECK(g.nodes[servo_idx].params.max_us == 2500, "servo1 params.max_us = 2500");
    CHECK(g.nodes[servo_idx].params.hw_mode == FBD_HW_REAL, "servo1 params.hw_mode = real");

    cJSON *serialized = fbd_json_serialize(&g);
    char *serialized_str = cJSON_PrintUnformatted(serialized);

    cJSON *root2 = cJSON_Parse(serialized_str);
    fbd_graph_t g2;
    bool ok2 = fbd_json_parse(root2, &g2, err, sizeof(err));
    CHECK(ok2, "hasil serialize analog_input/pwm_output/servo bisa di-parse ulang (round-trip)");

    size_t ai2_idx = fbd_graph_find_node(&g2, "ai1");
    CHECK(g2.nodes[ai2_idx].params.sim_value.i == 2048, "round-trip: ai1 sim_value tetap 2048");
    size_t servo2_idx = fbd_graph_find_node(&g2, "servo1");
    CHECK(g2.nodes[servo2_idx].params.hw_mode == FBD_HW_REAL, "round-trip: servo1 hw_mode tetap real");

    free(serialized_str);
    cJSON_Delete(serialized);
    cJSON_Delete(root2);
}

static void test_ws2812_roundtrip(void)
{
    const char *json =
    "{ \"version\": 1, \"nodes\": ["
    "  { \"id\": \"led1\", \"type\": \"ws2812\", \"params\": { \"pin\": 8, \"count\": 30, \"hw_mode\": \"real\" } }"
    "], \"links\": [] }";

    cJSON *root = cJSON_Parse(json);
    fbd_graph_t g;
    char err[FBD_JSON_ERR_LEN] = {0};
    bool ok = fbd_json_parse(root, &g, err, sizeof(err));
    CHECK(ok, "ws2812 berhasil di-parse");
    if (!ok) printf("  error: %s\n", err);
    cJSON_Delete(root);

    size_t idx = fbd_graph_find_node(&g, "led1");
    CHECK(g.nodes[idx].type == FBD_NODE_WS2812, "led1 bertipe FBD_NODE_WS2812");
    CHECK(g.nodes[idx].params.pin == 8, "led1 params.pin = 8");
    CHECK(g.nodes[idx].params.ws2812_count == 30, "led1 params.count = 30");
    CHECK(g.nodes[idx].params.hw_mode == FBD_HW_REAL, "led1 params.hw_mode = real");

    cJSON *serialized = fbd_json_serialize(&g);
    char *serialized_str = cJSON_PrintUnformatted(serialized);

    cJSON *root2 = cJSON_Parse(serialized_str);
    fbd_graph_t g2;
    bool ok2 = fbd_json_parse(root2, &g2, err, sizeof(err));
    CHECK(ok2, "hasil serialize ws2812 bisa di-parse ulang (round-trip)");

    size_t idx2 = fbd_graph_find_node(&g2, "led1");
    CHECK(g2.nodes[idx2].params.ws2812_count == 30, "round-trip: led1 count tetap 30");

    free(serialized_str);
    cJSON_Delete(serialized);
    cJSON_Delete(root2);
}

static void test_ws2812_count_out_of_range_rejected(void)
{
    const char *json =
    "{ \"version\": 1, \"nodes\": ["
    "  { \"id\": \"led1\", \"type\": \"ws2812\", \"params\": { \"pin\": 8, \"count\": 0 } }"
    "], \"links\": [] }";
    cJSON *root = cJSON_Parse(json);
    fbd_graph_t g;
    char err[FBD_JSON_ERR_LEN] = {0};
    bool ok = fbd_json_parse(root, &g, err, sizeof(err));
    CHECK(ok == false, "ws2812: params.count=0 ditolak (di luar rentang 1-256)");
    cJSON_Delete(root);
}

static void test_osc_roundtrip(void)
{
    const char *json =
    "{ \"version\": 1, \"nodes\": ["
    "  { \"id\": \"osc1\", \"type\": \"osc\", \"params\": { \"on_ms\": 300, \"off_ms\": 700 } }"
    "], \"links\": [] }";

    cJSON *root = cJSON_Parse(json);
    fbd_graph_t g;
    char err[FBD_JSON_ERR_LEN] = {0};
    bool ok = fbd_json_parse(root, &g, err, sizeof(err));
    CHECK(ok, "osc berhasil di-parse");
    if (!ok) printf("  error: %s\n", err);
    cJSON_Delete(root);

    size_t idx = fbd_graph_find_node(&g, "osc1");
    CHECK(g.nodes[idx].type == FBD_NODE_OSC, "osc1 bertipe FBD_NODE_OSC");
    CHECK(g.nodes[idx].params.osc_on_ms == 300, "osc1 params.on_ms = 300");
    CHECK(g.nodes[idx].params.osc_off_ms == 700, "osc1 params.off_ms = 700");

    cJSON *serialized = fbd_json_serialize(&g);
    char *serialized_str = cJSON_PrintUnformatted(serialized);

    cJSON *root2 = cJSON_Parse(serialized_str);
    fbd_graph_t g2;
    bool ok2 = fbd_json_parse(root2, &g2, err, sizeof(err));
    CHECK(ok2, "hasil serialize osc bisa di-parse ulang (round-trip)");

    size_t idx2 = fbd_graph_find_node(&g2, "osc1");
    CHECK(g2.nodes[idx2].params.osc_on_ms == 300, "round-trip: osc1 on_ms tetap 300");
    CHECK(g2.nodes[idx2].params.osc_off_ms == 700, "round-trip: osc1 off_ms tetap 700");

    free(serialized_str);
    cJSON_Delete(serialized);
    cJSON_Delete(root2);
}

static void test_hw_mode_default_simulated_when_omitted(void)
{
    /* JSON lama (sebelum spec 05) tanpa field hw_mode sama sekali harus
     * tetap ter-parse - backward compatible, default ke simulated. */
    const char *json =
    "{ \"version\": 1, \"nodes\": ["
    "  { \"id\": \"n1\", \"type\": \"digital_input\", \"params\": { \"pin\": 4, \"mode\": \"pullup\", \"invert\": false } }"
    "], \"links\": [] }";

    cJSON *root = cJSON_Parse(json);
    fbd_graph_t g;
    char err[FBD_JSON_ERR_LEN] = {0};
    bool ok = fbd_json_parse(root, &g, err, sizeof(err));
    CHECK(ok, "digital_input tanpa hw_mode (JSON lama) tetap ter-parse");
    if (!ok) printf("  error: %s\n", err);

    size_t n1_idx = fbd_graph_find_node(&g, "n1");
    CHECK(g.nodes[n1_idx].params.hw_mode == FBD_HW_SIMULATED,
          "hw_mode default simulated saat tidak dikirim di JSON");

    cJSON_Delete(root);
}

static void test_i2c_and_sys_var_roundtrip(void)
{
    const char *json =
    "{ \"version\": 1, \"nodes\": ["
    "  { \"id\": \"r1\", \"type\": \"i2c_read_reg\", \"params\": { \"bus\": 0, \"address\": 39, \"register\": 1, \"length\": 2 } },"
    "  { \"id\": \"w1\", \"type\": \"i2c_write_reg\", \"params\": { \"bus\": 0, \"address\": 39, \"register\": 0, \"data\": [16, 32] } },"
    "  { \"id\": \"s1\", \"type\": \"sys_var_get\", \"params\": { \"name\": \"SYS.WIFI_RSSI\" } }"
    "], \"links\": [] }";

    cJSON *root = cJSON_Parse(json);
    fbd_graph_t g;
    char err[FBD_JSON_ERR_LEN] = {0};
    bool ok = fbd_json_parse(root, &g, err, sizeof(err));
    CHECK(ok, "i2c_read_reg/i2c_write_reg/sys_var_get berhasil di-parse");
    if (!ok) printf("  error: %s\n", err);
    cJSON_Delete(root);

    size_t r1_idx = fbd_graph_find_node(&g, "r1");
    CHECK(g.nodes[r1_idx].params.i2c_address == 39, "r1 params.address = 39 (0x27)");
    CHECK(g.nodes[r1_idx].params.i2c_register == 1, "r1 params.register = 1");
    CHECK(g.nodes[r1_idx].params.i2c_data_len == 2, "r1 params.length = 2");

    size_t w1_idx = fbd_graph_find_node(&g, "w1");
    CHECK(w1_idx != (size_t)-1, "w1 ditemukan");
    CHECK(g.nodes[w1_idx].params.i2c_data_len == 2, "w1 params.data panjang 2");
    CHECK(g.nodes[w1_idx].params.i2c_data[0] == 16 && g.nodes[w1_idx].params.i2c_data[1] == 32,
          "w1 params.data = [16, 32]");

    size_t s1_idx = fbd_graph_find_node(&g, "s1");
    CHECK(strcmp(g.nodes[s1_idx].params.sys_var_name, "SYS.WIFI_RSSI") == 0,
          "s1 params.name = SYS.WIFI_RSSI");

    cJSON *serialized = fbd_json_serialize(&g);
    char *serialized_str = cJSON_PrintUnformatted(serialized);

    cJSON *root2 = cJSON_Parse(serialized_str);
    fbd_graph_t g2;
    bool ok2 = fbd_json_parse(root2, &g2, err, sizeof(err));
    CHECK(ok2, "hasil serialize i2c/sys_var bisa di-parse ulang (round-trip)");

    size_t w2_idx = fbd_graph_find_node(&g2, "w1");
    CHECK(g2.nodes[w2_idx].params.i2c_data[0] == 16 && g2.nodes[w2_idx].params.i2c_data[1] == 32,
          "round-trip: w1 params.data tetap [16, 32]");

    free(serialized_str);
    cJSON_Delete(serialized);
    cJSON_Delete(root2);
}

static void test_reject_unsafe_pin_when_real(void)
{
    /* Strapping pin (GPIO0) dengan hw_mode real HARUS ditolak - berisiko
     * device gagal boot kalau benar-benar dipakai gpio_config(). */
    const char *json_strapping =
    "{ \"version\": 1, \"nodes\": ["
    "  { \"id\": \"n1\", \"type\": \"digital_output\", \"params\": { \"pin\": 0, \"invert\": false, \"hw_mode\": \"real\" } }"
    "], \"links\": [] }";
    cJSON *root1 = cJSON_Parse(json_strapping);
    fbd_graph_t g1;
    char err1[FBD_JSON_ERR_LEN] = {0};
    bool ok1 = fbd_json_parse(root1, &g1, err1, sizeof(err1));
    CHECK(ok1 == false, "digital_output pin=0 (strapping) hw_mode=real -> ditolak");
    printf("  error (diharapkan): %s\n", err1);
    cJSON_Delete(root1);

    /* PSRAM/flash pin (GPIO28) dengan hw_mode real HARUS ditolak. */
    const char *json_psram =
    "{ \"version\": 1, \"nodes\": ["
    "  { \"id\": \"n1\", \"type\": \"digital_output\", \"params\": { \"pin\": 28, \"invert\": false, \"hw_mode\": \"real\" } }"
    "], \"links\": [] }";
    cJSON *root2 = cJSON_Parse(json_psram);
    fbd_graph_t g2;
    char err2[FBD_JSON_ERR_LEN] = {0};
    bool ok2 = fbd_json_parse(root2, &g2, err2, sizeof(err2));
    CHECK(ok2 == false, "digital_output pin=28 (PSRAM/flash) hw_mode=real -> ditolak");
    cJSON_Delete(root2);

    /* Pin yang sama (GPIO0) TAPI hw_mode simulated -> HARUS tetap diterima,
     * karena mode simulated tidak pernah menyentuh GPIO fisik sama sekali. */
    const char *json_sim_ok =
    "{ \"version\": 1, \"nodes\": ["
    "  { \"id\": \"n1\", \"type\": \"digital_output\", \"params\": { \"pin\": 0, \"invert\": false, \"hw_mode\": \"simulated\" } }"
    "], \"links\": [] }";
    cJSON *root3 = cJSON_Parse(json_sim_ok);
    fbd_graph_t g3;
    char err3[FBD_JSON_ERR_LEN] = {0};
    bool ok3 = fbd_json_parse(root3, &g3, err3, sizeof(err3));
    CHECK(ok3 == true, "digital_output pin=0 hw_mode=simulated -> tetap diterima (tidak menyentuh GPIO fisik)");
    cJSON_Delete(root3);

    /* Pin aman (GPIO4) dengan hw_mode real -> HARUS diterima. */
    const char *json_safe =
    "{ \"version\": 1, \"nodes\": ["
    "  { \"id\": \"n1\", \"type\": \"digital_input\", \"params\": { \"pin\": 4, \"mode\": \"pullup\", \"invert\": false, \"hw_mode\": \"real\" } }"
    "], \"links\": [] }";
    cJSON *root4 = cJSON_Parse(json_safe);
    fbd_graph_t g4;
    char err4[FBD_JSON_ERR_LEN] = {0};
    bool ok4 = fbd_json_parse(root4, &g4, err4, sizeof(err4));
    CHECK(ok4 == true, "digital_input pin=4 hw_mode=real -> diterima (pin aman)");
    if (!ok4) printf("  error: %s\n", err4);
    cJSON_Delete(root4);
}

static void test_reject_unknown_id_in_link(void)
{
    const char *bad_json =
    "{ \"version\": 1, \"nodes\": ["
    "  { \"id\": \"n1\", \"type\": \"and\", \"params\": {} }"
    "], \"links\": ["
    "  { \"from\": { \"node\": \"n1\", \"port\": 0 }, \"to\": { \"node\": \"tidak_ada\", \"port\": 0 } }"
    "] }";

    cJSON *root = cJSON_Parse(bad_json);
    fbd_graph_t g;
    char err[FBD_JSON_ERR_LEN] = {0};
    bool ok = fbd_json_parse(root, &g, err, sizeof(err));
    CHECK(ok == false, "link merujuk id yang tidak ada -> ditolak fbd_json_parse()");
    printf("  error (diharapkan): %s\n", err);
    cJSON_Delete(root);
}

static void test_reject_unknown_type(void)
{
    const char *bad_json =
    "{ \"version\": 1, \"nodes\": ["
    "  { \"id\": \"n1\", \"type\": \"tipe_tidak_ada\", \"params\": {} }"
    "], \"links\": [] }";

    cJSON *root = cJSON_Parse(bad_json);
    fbd_graph_t g;
    char err[FBD_JSON_ERR_LEN] = {0};
    bool ok = fbd_json_parse(root, &g, err, sizeof(err));
    CHECK(ok == false, "type tidak dikenal -> ditolak fbd_json_parse()");
    cJSON_Delete(root);
}

static void test_reject_unknown_version(void)
{
    const char *bad_json = "{ \"version\": 999, \"nodes\": [], \"links\": [] }";
    cJSON *root = cJSON_Parse(bad_json);
    fbd_graph_t g;
    bool ok = fbd_json_parse(root, &g, NULL, 0);
    CHECK(ok == false, "version tidak dikenal -> ditolak fbd_json_parse()");
    cJSON_Delete(root);
}

int main(void)
{
    test_parse_example_from_schema();
    test_roundtrip();
    test_level1_analog_pwm_servo_roundtrip();
    test_osc_roundtrip();
    test_ws2812_roundtrip();
    test_ws2812_count_out_of_range_rejected();
    test_hw_mode_default_simulated_when_omitted();
    test_i2c_and_sys_var_roundtrip();
    test_reject_unsafe_pin_when_real();
    test_reject_unknown_id_in_link();
    test_reject_unknown_type();
    test_reject_unknown_version();

    printf("\n%s (%d gagal)\n", g_fail == 0 ? "SEMUA TEST LOLOS" : "ADA TEST GAGAL", g_fail);
    return g_fail == 0 ? 0 : 1;
}

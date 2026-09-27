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
    test_reject_unknown_id_in_link();
    test_reject_unknown_type();
    test_reject_unknown_version();

    printf("\n%s (%d gagal)\n", g_fail == 0 ? "SEMUA TEST LOLOS" : "ADA TEST GAGAL", g_fail);
    return g_fail == 0 ? 0 : 1;
}

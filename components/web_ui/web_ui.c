#include "web_ui.h"
#include <string.h>
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "cJSON.h"
#include "fbd_json.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "web_ui";
static logic_program_t *s_legacy_prog = NULL;
static fbd_graph_t *s_active_graph = NULL;

/* sizeof(fbd_graph_t) ~19KB - JANGAN taruh sebagai local variable di stack
 * handler httpd (stack task httpd hanya beberapa KB, overflow merusak heap
 * TLSF secara diam-diam dan crash di tempat yang jauh dari akar masalahnya).
 * Dipakai sebagai scratch buffer statis untuk parse+compile sebelum
 * diterapkan ke *s_active_graph. */
static fbd_graph_t s_scratch_graph;

/* Melindungi *s_active_graph & s_scratch_graph dari race condition write
 * (POST /api/program, Core 0, dan antar POST bersamaan) vs read
 * (fbd_scan_task, Core 1). Ini BUKAN dual-buffer swap yang proper (itu
 * spec 07) - cukup mencegah corruption saat write terjadi di tengah scan
 * cycle sedang membaca graph yang sama. */
static SemaphoreHandle_t s_graph_mutex = NULL;

/* ---- Serve file statis (editor Drawflow) dari SPIFFS ---- */

static const char *guess_content_type(const char *path)
{
    const char *ext = strrchr(path, '.');
    if (!ext) return "application/octet-stream";
    if (strcmp(ext, ".html") == 0) return "text/html";
    if (strcmp(ext, ".js") == 0)   return "application/javascript";
    if (strcmp(ext, ".css") == 0)  return "text/css";
    if (strcmp(ext, ".json") == 0) return "application/json";
    return "application/octet-stream";
}

/* Handler wildcard untuk semua GET selain /api/... : "/" -> index.html,
 * selain itu diambil apa adanya dari /spiffs (misal /app.js -> /spiffs/app.js).
 * Diperlukan karena editor Drawflow terdiri dari beberapa file
 * (index.html, app.js, node-types.js, style.css, drawflow.min.js/css),
 * bukan cuma satu index.html seperti versi editor v1. */
static esp_err_t static_get_handler(httpd_req_t *req)
{
    char path[160] = "/spiffs";
    const char *uri = req->uri;
    if (strcmp(uri, "/") == 0) {
        strncat(path, "/index.html", sizeof(path) - strlen(path) - 1);
    } else {
        strncat(path, uri, sizeof(path) - strlen(path) - 1);
    }

    FILE *f = fopen(path, "r");
    if (!f) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "file tidak ditemukan di SPIFFS");
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, guess_content_type(path));
    /* Editor masih sering berubah selama development - cegah browser
     * menyimpan versi lama app.js/style.css di cache (pernah bikin
     * perubahan CSS/JS terlihat "tidak kepakai" padahal firmware sudah
     * di-reflash dengan file baru). */
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    char buf[512];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        httpd_resp_send_chunk(req, buf, n);
    }
    httpd_resp_send_chunk(req, NULL, 0);
    fclose(f);
    return ESP_OK;
}

static esp_err_t send_json_error(httpd_req_t *req, const char *msg)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", "error");
    cJSON_AddStringToObject(root, "message", msg);
    char *out = cJSON_PrintUnformatted(root);
    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, out);
    free(out);
    cJSON_Delete(root);
    return ESP_FAIL;
}

/* ---- POST /api/program : terima JSON sesuai schema.md, parse+compile ke
 * graph baru, baru diterapkan ke active_graph kalau sukses (tidak ada
 * partial-load: gagal validasi -> active_graph tidak disentuh). ---- */

static esp_err_t program_post_handler(httpd_req_t *req)
{
    int total = req->content_len;
    if (total <= 0 || total > 16384) {
        return send_json_error(req, "ukuran body tidak valid");
    }

    char *buf = malloc(total + 1);
    if (!buf) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "out of memory");
        return ESP_FAIL;
    }

    int received = 0;
    while (received < total) {
        int r = httpd_req_recv(req, buf + received, total - received);
        if (r <= 0) {
            free(buf);
            return send_json_error(req, "gagal membaca body");
        }
        received += r;
    }
    buf[total] = '\0';

    cJSON *json = cJSON_Parse(buf);
    free(buf);
    if (!json) {
        return send_json_error(req, "JSON tidak valid (parse error)");
    }

    /* Lock dipegang sepanjang parse+compile+swap supaya tidak ada POST lain
     * yang menimpa s_scratch_graph di tengah proses (scratch buffer statis
     * dipakai bersama, bukan per-request). */
    xSemaphoreTake(s_graph_mutex, portMAX_DELAY);

    char err[FBD_JSON_ERR_LEN] = {0};
    bool ok = fbd_json_parse(json, &s_scratch_graph, err, sizeof(err));
    cJSON_Delete(json);

    if (!ok) {
        xSemaphoreGive(s_graph_mutex);
        ESP_LOGW(TAG, "POST /api/program ditolak: %s", err);
        return send_json_error(req, err);
    }

    if (!fbd_graph_compile(&s_scratch_graph)) {
        xSemaphoreGive(s_graph_mutex);
        ESP_LOGW(TAG, "POST /api/program ditolak: graph cyclic/invalid");
        return send_json_error(req, "graph cyclic dependency - tidak bisa dikompilasi");
    }

    /* Validasi sukses - baru sekarang active_graph disentuh. */
    *s_active_graph = s_scratch_graph;
    unsigned node_count = (unsigned)s_scratch_graph.node_count;
    unsigned link_count = (unsigned)s_scratch_graph.link_count;
    xSemaphoreGive(s_graph_mutex);

    ESP_LOGI(TAG, "POST /api/program sukses: %u node, %u link", node_count, link_count);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

/* ---- GET /api/program : dump active_graph sesuai schema.md ---- */

static esp_err_t program_get_handler(httpd_req_t *req)
{
    xSemaphoreTake(s_graph_mutex, portMAX_DELAY);
    cJSON *root = fbd_json_serialize(s_active_graph);
    xSemaphoreGive(s_graph_mutex);

    char *out = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, out);
    free(out);
    cJSON_Delete(root);
    return ESP_OK;
}

/* ---- GET /api/status : nilai output tiap block (v1/logic_engine, dipertahankan
 * untuk debug selama migrasi bertahap ke fbd_graph). ---- */

static esp_err_t status_get_handler(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *outputs = cJSON_CreateArray();
    for (int i = 0; i < s_legacy_prog->block_count; i++) {
        cJSON_AddItemToArray(outputs, cJSON_CreateNumber(logic_engine_get_output(s_legacy_prog, i)));
    }
    cJSON_AddItemToObject(root, "outputs", outputs);
    cJSON_AddNumberToObject(root, "block_count", s_legacy_prog->block_count);

    char *out = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, out);
    free(out);
    cJSON_Delete(root);
    return ESP_OK;
}

static void mount_spiffs(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = true,
    };
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "gagal mount SPIFFS (%s)", esp_err_to_name(ret));
    }
}

void web_ui_start(logic_program_t *legacy_prog, fbd_graph_t *active_graph)
{
    s_legacy_prog = legacy_prog;
    s_active_graph = active_graph;
    s_graph_mutex = xSemaphoreCreateMutex();

    mount_spiffs();

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.max_uri_handlers = 8;
    /* Default 4096 terlalu kecil untuk program_post_handler/program_get_handler
     * yang memanggil cJSON print/parse rekursif atas dokumen berisi puluhan
     * node - pernah menyebabkan stack overflow yang merusak heap TLSF secara
     * diam-diam (crash muncul jauh setelah request yang sebenarnya bermasalah). */
    config.stack_size = 8192;

    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "gagal start httpd");
        return;
    }

    httpd_uri_t program_post_uri = { .uri = "/api/program", .method = HTTP_POST, .handler = program_post_handler };
    httpd_uri_t program_get_uri = { .uri = "/api/program", .method = HTTP_GET, .handler = program_get_handler };
    httpd_uri_t status_uri = { .uri = "/api/status", .method = HTTP_GET, .handler = status_get_handler };
    /* Wildcard, harus didaftarkan setelah /api/... supaya tidak menutupi -
     * httpd_uri_match_wildcard cocokkan URI paling spesifik dulu terlepas
     * urutan register, tapi tetap didaftarkan terakhir untuk kejelasan. */
    httpd_uri_t static_uri = { .uri = "/*", .method = HTTP_GET, .handler = static_get_handler };

    httpd_register_uri_handler(server, &program_post_uri);
    httpd_register_uri_handler(server, &program_get_uri);
    httpd_register_uri_handler(server, &status_uri);
    httpd_register_uri_handler(server, &static_uri);

    ESP_LOGI(TAG, "web server siap");
}

SemaphoreHandle_t web_ui_get_graph_mutex(void)
{
    return s_graph_mutex;
}

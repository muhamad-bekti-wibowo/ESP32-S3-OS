#include "web_ui.h"
#include <string.h>
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "cJSON.h"

static const char *TAG = "web_ui";
static logic_program_t *s_prog = NULL;

/* ---- Serve halaman editor dari SPIFFS ---- */

static esp_err_t index_get_handler(httpd_req_t *req)
{
    FILE *f = fopen("/spiffs/index.html", "r");
    if (!f) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "index.html tidak ditemukan di SPIFFS");
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "text/html");
    char buf[512];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        httpd_resp_send_chunk(req, buf, n);
    }
    httpd_resp_send_chunk(req, NULL, 0);
    fclose(f);
    return ESP_OK;
}

/* ---- POST /api/program : terima JSON definisi block, muat ke logic_engine ---- */

static esp_err_t program_post_handler(httpd_req_t *req)
{
    int total = req->content_len;
    if (total <= 0 || total > 16384) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "ukuran body tidak valid");
        return ESP_FAIL;
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
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "gagal membaca body");
            return ESP_FAIL;
        }
        received += r;
    }
    buf[total] = '\0';

    cJSON *json = cJSON_Parse(buf);
    free(buf);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "JSON tidak valid");
        return ESP_FAIL;
    }

    bool ok = logic_engine_load_json(s_prog, json);
    cJSON_Delete(json);

    if (!ok) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "gagal memuat program");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

/* ---- GET /api/status : nilai output tiap block, untuk debug/monitor ---- */

static esp_err_t status_get_handler(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *outputs = cJSON_CreateArray();
    for (int i = 0; i < s_prog->block_count; i++) {
        cJSON_AddItemToArray(outputs, cJSON_CreateNumber(logic_engine_get_output(s_prog, i)));
    }
    cJSON_AddItemToObject(root, "outputs", outputs);
    cJSON_AddNumberToObject(root, "block_count", s_prog->block_count);

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

void web_ui_start(logic_program_t *prog)
{
    s_prog = prog;
    mount_spiffs();

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.max_uri_handlers = 8;

    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "gagal start httpd");
        return;
    }

    httpd_uri_t index_uri = { .uri = "/", .method = HTTP_GET, .handler = index_get_handler };
    httpd_uri_t program_uri = { .uri = "/api/program", .method = HTTP_POST, .handler = program_post_handler };
    httpd_uri_t status_uri = { .uri = "/api/status", .method = HTTP_GET, .handler = status_get_handler };

    httpd_register_uri_handler(server, &index_uri);
    httpd_register_uri_handler(server, &program_uri);
    httpd_register_uri_handler(server, &status_uri);

    ESP_LOGI(TAG, "web server siap");
}

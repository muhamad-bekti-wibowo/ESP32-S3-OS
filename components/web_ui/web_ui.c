#include "web_ui.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "esp_timer.h"
#include "cJSON.h"
#include "fbd_json.h"
#include "wifi_mgr.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "web_ui";
static logic_program_t *s_legacy_prog = NULL;

/* Dual-buffer graph (spec 07): pointer-ke-pointer supaya bisa mengikuti
 * swap yang dilakukan scan task - lihat komentar lengkap di web_ui.h. */
static fbd_graph_t **s_active_graph_ptr = NULL;
static fbd_graph_t **s_standby_graph_ptr = NULL;
static volatile bool *s_reload_requested_ptr = NULL;

/* sizeof(fbd_graph_t) ~19KB - JANGAN taruh sebagai local variable di stack
 * handler httpd (stack task httpd hanya beberapa KB, overflow merusak heap
 * TLSF secara diam-diam dan crash di tempat yang jauh dari akar masalahnya).
 * Ini scratch buffer TERPISAH dari *standby_graph_ptr - parse dulu ke sini,
 * baru di-copy ke standby setelah validasi lolos, supaya standby graph yang
 * scan task lihat tidak pernah dalam keadaan setengah-jadi. */
static fbd_graph_t s_scratch_graph;

/* File program aktif + backup, di SPIFFS (spec 07 plan.md §11.2 "Auto-backup").
 * Retensi dibatasi (bukan tumbuh tanpa batas) supaya tidak memenuhi partisi
 * storage 2MB (lihat partitions.csv). */
#define PROGRAM_FILE_PATH "/spiffs/program.json"
#define PROGRAM_BACKUP_PREFIX "/spiffs/program_backup_"
#define PROGRAM_BACKUP_MAX_COUNT 5

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

/* ---- Auto-backup program.json (spec 07 plan.md §11.2) ---- */

/* Hapus backup tertua kalau sudah mencapai PROGRAM_BACKUP_MAX_COUNT, supaya
 * backup tidak menumpuk tanpa batas dan memenuhi partisi storage. Nama file
 * pakai counter monoton (uptime ms) - urut secara leksikografis = urut
 * waktu, jadi cukup baca direktori & hapus yang angkanya paling kecil kalau
 * jumlahnya sudah melebihi batas. */
static void prune_old_backups(void)
{
    DIR *dir = opendir("/spiffs");
    if (!dir) {
        return;
    }

    char oldest_name[64] = {0};
    long oldest_ts = -1;
    int count = 0;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "program_backup_", 15) != 0) {
            continue;
        }
        count++;
        long ts = atol(entry->d_name + 15);
        if (oldest_ts < 0 || ts < oldest_ts) {
            oldest_ts = ts;
            strncpy(oldest_name, entry->d_name, sizeof(oldest_name) - 1);
        }
    }
    closedir(dir);

    if (count > PROGRAM_BACKUP_MAX_COUNT && oldest_name[0] != '\0') {
        char path[80];
        snprintf(path, sizeof(path), "/spiffs/%s", oldest_name);
        if (remove(path) == 0) {
            ESP_LOGI(TAG, "backup lama dihapus: %s (retensi maks %d)", path, PROGRAM_BACKUP_MAX_COUNT);
        }
    }
}

/* Copy file program aktif lama ke program_backup_<uptime_ms>.json sebelum
 * ditimpa. Aman dipanggil walau program.json belum pernah ada (belum pernah
 * di-POST sebelumnya) - langsung return, tidak ada yang perlu di-backup. */
static void backup_program_file(void)
{
    FILE *src = fopen(PROGRAM_FILE_PATH, "r");
    if (!src) {
        return; /* belum pernah ada program tersimpan - tidak ada yang di-backup */
    }

    char backup_path[64];
    long uptime_ms = (long)(esp_timer_get_time() / 1000);
    snprintf(backup_path, sizeof(backup_path), "%s%ld.json", PROGRAM_BACKUP_PREFIX, uptime_ms);

    FILE *dst = fopen(backup_path, "w");
    if (!dst) {
        ESP_LOGW(TAG, "gagal buat file backup %s", backup_path);
        fclose(src);
        return;
    }

    char buf[512];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), src)) > 0) {
        fwrite(buf, 1, n, dst);
    }
    fclose(src);
    fclose(dst);

    ESP_LOGI(TAG, "backup program lama disimpan: %s", backup_path);
    prune_old_backups();
}

/* Tulis program.json baru ke SPIFFS - dipanggil SETELAH backup_program_file()
 * dan SETELAH validasi (parse+compile) standby graph sukses. Kegagalan tulis
 * di sini TIDAK membatalkan swap graph di RAM (device tetap jalan dengan
 * logic baru), hanya berarti program hilang lagi kalau device reboot -
 * di-log sebagai warning, bukan error fatal. */
static void save_program_file(const char *json_str)
{
    FILE *f = fopen(PROGRAM_FILE_PATH, "w");
    if (!f) {
        ESP_LOGW(TAG, "gagal simpan program.json - program tidak akan bertahan setelah reboot");
        return;
    }
    fwrite(json_str, 1, strlen(json_str), f);
    fclose(f);
}

/* ---- POST /api/program : terima JSON sesuai schema.md, parse+compile ke
 * standby graph, baru di-swap ke active kalau sukses (dual-buffer, spec 07 -
 * TIDAK ADA lagi modifikasi langsung ke active_graph seperti spec 03/06).
 * TIDAK ADA partial-load: gagal validasi -> active_graph TIDAK disentuh,
 * TIDAK ADA downtime. ---- */

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
    if (!json) {
        free(buf);
        return send_json_error(req, "JSON tidak valid (parse error)");
    }

    char err[FBD_JSON_ERR_LEN] = {0};
    bool ok = fbd_json_parse(json, &s_scratch_graph, err, sizeof(err));
    cJSON_Delete(json);

    if (!ok) {
        free(buf);
        ESP_LOGW(TAG, "POST /api/program ditolak: %s", err);
        return send_json_error(req, err);
    }

    if (!fbd_graph_compile(&s_scratch_graph)) {
        free(buf);
        ESP_LOGW(TAG, "POST /api/program ditolak: graph cyclic/invalid");
        return send_json_error(req, "graph cyclic dependency - tidak bisa dikompilasi");
    }

    /* Validasi sukses. Salin ke standby graph, lalu set flag - scan task
     * yang melakukan swap sesungguhnya di awal cycle berikutnya (lihat
     * main.c). Handler ini TIDAK PERNAH menunggu scan task memproses flag
     * ini - request langsung dijawab sukses begitu standby graph siap. */
    *(*s_standby_graph_ptr) = s_scratch_graph;
    *s_reload_requested_ptr = true;

    unsigned node_count = (unsigned)s_scratch_graph.node_count;
    unsigned link_count = (unsigned)s_scratch_graph.link_count;

    backup_program_file();
    save_program_file(buf);
    free(buf);

    ESP_LOGI(TAG, "POST /api/program sukses: %u node, %u link (live update, tanpa reboot)", node_count, link_count);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

/* ---- GET /api/program : dump graph aktif sesuai schema.md.
 * *s_active_graph_ptr bisa berubah kapan saja (scan task menukar pointer
 * di awal tiap cycle) - dereference SEKALI ke variabel lokal sebelum
 * serialize, supaya konsisten sepanjang satu request walau swap terjadi
 * di tengah proses serialize (fbd_json_serialize baca dari snapshot
 * pointer, bukan re-read *s_active_graph_ptr berulang kali). ---- */

static esp_err_t program_get_handler(httpd_req_t *req)
{
    const fbd_graph_t *graph = *s_active_graph_ptr;
    cJSON *root = fbd_json_serialize(graph);

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

/* ---- GET/POST /api/network : konfigurasi WiFi STA (SSID/password/hostname),
 * tab "System > Network" terpisah dari canvas Drawflow (spec 06 - WiFi
 * BUKAN node yang dikonfigurasi di canvas). ---- */

static esp_err_t network_get_handler(httpd_req_t *req)
{
    char ssid[33] = {0};
    char hostname[32] = {0};
    wifi_mgr_get_sta_config(ssid, sizeof(ssid), hostname, sizeof(hostname));

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "ssid", ssid);
    cJSON_AddStringToObject(root, "hostname", hostname);
    cJSON_AddBoolToObject(root, "connected", wifi_mgr_is_sta_connected());
    cJSON_AddNumberToObject(root, "rssi", wifi_mgr_get_sta_rssi());
    char ip[16];
    wifi_mgr_get_sta_ip(ip, sizeof(ip));
    cJSON_AddStringToObject(root, "ip", ip);
    /* password SENGAJA tidak diikutkan - lihat wifi_mgr_get_sta_config(). */

    char *out = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, out);
    free(out);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t network_post_handler(httpd_req_t *req)
{
    int total = req->content_len;
    if (total <= 0 || total > 512) {
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

    const cJSON *ssid = cJSON_GetObjectItem(json, "ssid");
    const cJSON *password = cJSON_GetObjectItem(json, "password");
    const cJSON *hostname = cJSON_GetObjectItem(json, "hostname");
    if (!cJSON_IsString(ssid) || strlen(ssid->valuestring) == 0) {
        cJSON_Delete(json);
        return send_json_error(req, "ssid wajib diisi");
    }

    bool ok = wifi_mgr_save_sta_config(
        ssid->valuestring,
        cJSON_IsString(password) ? password->valuestring : "",
        cJSON_IsString(hostname) ? hostname->valuestring : NULL);
    cJSON_Delete(json);

    if (!ok) {
        return send_json_error(req, "gagal menyimpan config ke NVS");
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\",\"message\":\"Config tersimpan. Reboot device untuk menerapkan.\"}");
    return ESP_OK;
}

static void mount_spiffs(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 8, /* dinaikkan dari 5: webroot + program.json + beberapa backup sekaligus terbuka mungkin terjadi */
        .format_if_mount_failed = true,
    };
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "gagal mount SPIFFS (%s)", esp_err_to_name(ret));
    }
}

/* Muat program.json dari SPIFFS ke standby graph, lalu minta swap - dipanggil
 * SEKALI saat startup (spec 07: program yang di-Save harus bertahan setelah
 * reboot). Kalau file tidak ada/rusak, biarkan graph default dari main.c
 * yang tetap dipakai - tidak fatal. */
static void load_program_file_at_startup(void)
{
    FILE *f = fopen(PROGRAM_FILE_PATH, "r");
    if (!f) {
        ESP_LOGI(TAG, "program.json belum ada, pakai graph default");
        return;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0 || size > 16384) {
        fclose(f);
        ESP_LOGW(TAG, "program.json ukurannya tidak wajar (%ld byte), diabaikan", size);
        return;
    }

    char *buf = malloc(size + 1);
    if (!buf) {
        fclose(f);
        return;
    }
    fread(buf, 1, size, f);
    buf[size] = '\0';
    fclose(f);

    cJSON *json = cJSON_Parse(buf);
    free(buf);
    if (!json) {
        ESP_LOGW(TAG, "program.json tersimpan tapi tidak valid sebagai JSON, diabaikan");
        return;
    }

    char err[FBD_JSON_ERR_LEN] = {0};
    bool ok = fbd_json_parse(json, &s_scratch_graph, err, sizeof(err));
    cJSON_Delete(json);
    if (!ok || !fbd_graph_compile(&s_scratch_graph)) {
        ESP_LOGW(TAG, "program.json tersimpan tapi gagal di-load (%s), pakai graph default", err);
        return;
    }

    *(*s_standby_graph_ptr) = s_scratch_graph;
    *s_reload_requested_ptr = true;
    ESP_LOGI(TAG, "program.json dimuat dari SPIFFS (%u node)", (unsigned)s_scratch_graph.node_count);
}

void web_ui_start(logic_program_t *legacy_prog,
                   fbd_graph_t **active_graph_ptr,
                   fbd_graph_t **standby_graph_ptr,
                   volatile bool *reload_requested_ptr)
{
    s_legacy_prog = legacy_prog;
    s_active_graph_ptr = active_graph_ptr;
    s_standby_graph_ptr = standby_graph_ptr;
    s_reload_requested_ptr = reload_requested_ptr;

    mount_spiffs();
    load_program_file_at_startup();

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.max_uri_handlers = 10;
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
    httpd_uri_t network_get_uri = { .uri = "/api/network", .method = HTTP_GET, .handler = network_get_handler };
    httpd_uri_t network_post_uri = { .uri = "/api/network", .method = HTTP_POST, .handler = network_post_handler };
    /* Wildcard, harus didaftarkan setelah /api/... supaya tidak menutupi -
     * httpd_uri_match_wildcard cocokkan URI paling spesifik dulu terlepas
     * urutan register, tapi tetap didaftarkan terakhir untuk kejelasan. */
    httpd_uri_t static_uri = { .uri = "/*", .method = HTTP_GET, .handler = static_get_handler };

    httpd_register_uri_handler(server, &program_post_uri);
    httpd_register_uri_handler(server, &program_get_uri);
    httpd_register_uri_handler(server, &status_uri);
    httpd_register_uri_handler(server, &network_get_uri);
    httpd_register_uri_handler(server, &network_post_uri);
    httpd_register_uri_handler(server, &static_uri);

    ESP_LOGI(TAG, "web server siap");
}

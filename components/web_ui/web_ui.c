#include "web_ui.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "esp_timer.h"
#include "cJSON.h"
#include "fbd_json.h"
#include "wifi_mgr.h"
#include "endpoint_mgr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"

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

/* ---- POST /api/ota : upload firmware .bin lewat browser (input type=file),
 * tulis ke partisi app yang SEDANG TIDAK AKTIF (esp_ota_get_next_update_
 * partition - otomatis pilih ota_0/ota_1 mana pun yang bukan sedang jalan),
 * lalu set sebagai boot partition berikutnya dan reboot.
 *
 * Rollback otomatis (CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE, lihat
 * sdkconfig.defaults + partitions.csv): kalau firmware baru gagal boot
 * sampai app_main() memanggil esp_ota_mark_app_valid_cancel_rollback()
 * (lihat main.c confirm_ota_boot_healthy()), bootloader otomatis kembali
 * ke firmware LAMA di boot berikutnya - device TIDAK PERNAH ter-brick
 * hanya karena upload firmware yang salah/corrupt, selama bootloader
 * sendiri masih hidup (partisi bootloader/partition-table tidak disentuh
 * OTA sama sekali).
 *
 * TIDAK ADA validasi isi .bin di sini selain yang esp_ota_ops sendiri
 * lakukan (magic byte esp_image_header_t, checksum) - kalau user upload
 * file yang bukan build project ini, boot berikutnya kemungkinan besar
 * gagal dan otomatis rollback (bukan jaminan aman, tapi tidak bisa
 * mem-brick device permanen). */
#define OTA_MAX_SIZE (3 * 1024 * 1024) /* muat di partisi ota_0/ota_1 3MB */

static esp_err_t ota_post_handler(httpd_req_t *req)
{
    int total = req->content_len;
    if (total <= 0 || total > OTA_MAX_SIZE) {
        return send_json_error(req, "ukuran firmware tidak valid (maks 3MB, sesuai partisi ota_0/ota_1)");
    }

    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    if (!update_partition) {
        return send_json_error(req, "tidak ada partisi OTA tersedia (partition table tidak dual app?)");
    }

    esp_ota_handle_t ota_handle;
    esp_err_t err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin gagal: %s", esp_err_to_name(err));
        return send_json_error(req, "gagal memulai OTA (esp_ota_begin)");
    }

    char buf[1024];
    int received = 0;
    bool write_failed = false;
    while (received < total) {
        int to_read = total - received;
        if (to_read > (int)sizeof(buf)) to_read = sizeof(buf);
        int r = httpd_req_recv(req, buf, to_read);
        if (r <= 0) {
            write_failed = true;
            break;
        }
        if (esp_ota_write(ota_handle, buf, r) != ESP_OK) {
            write_failed = true;
            break;
        }
        received += r;
    }

    if (write_failed) {
        esp_ota_abort(ota_handle);
        ESP_LOGW(TAG, "OTA gagal: hanya %d/%d byte diterima/ditulis", received, total);
        return send_json_error(req, "gagal menerima/menulis firmware di tengah upload - partisi lama TIDAK disentuh, aman dicoba lagi");
    }

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end gagal: %s (kemungkinan file .bin corrupt/bukan firmware valid)", esp_err_to_name(err));
        return send_json_error(req, "firmware tidak valid (gagal validasi image) - partisi lama TIDAK disentuh");
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition gagal: %s", esp_err_to_name(err));
        return send_json_error(req, "gagal set boot partition");
    }

    ESP_LOGI(TAG, "OTA sukses (%d byte) ke partisi %s, reboot dalam 1 detik...",
             total, update_partition->label);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\",\"message\":\"Firmware diterima, device reboot sekarang.\"}");

    /* Beri waktu response HTTP terkirim penuh ke browser sebelum reboot -
     * kalau restart() dipanggil langsung, browser sering melihat koneksi
     * putus tanpa response (terlihat seperti gagal padahal sebenarnya
     * sukses). vTaskDelay di task httpd aman (blocking task ini saja,
     * bukan seluruh sistem - fbd_scan_task tetap jalan normal). */
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    return ESP_OK; /* tidak pernah tercapai - esp_restart() tidak return */
}

/* ---- GET /api/ota/status : info versi/partisi firmware yang sedang
 * jalan, dipakai UI System > Firmware untuk tampilkan status sebelum
 * user upload .bin baru. ---- */

static esp_err_t ota_status_get_handler(httpd_req_t *req)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_app_desc_t app_desc;
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    if (running) {
        esp_ota_get_state_partition(running, &state);
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "running_partition", running ? running->label : "?");
    if (esp_ota_get_partition_description(running, &app_desc) == ESP_OK) {
        cJSON_AddStringToObject(root, "version", app_desc.version);
        cJSON_AddStringToObject(root, "compile_time", app_desc.time);
        cJSON_AddStringToObject(root, "compile_date", app_desc.date);
    }
    const char *state_str = "unknown";
    switch (state) {
        case ESP_OTA_IMG_VALID:          state_str = "valid"; break;
        case ESP_OTA_IMG_PENDING_VERIFY: state_str = "pending_verify"; break;
        case ESP_OTA_IMG_UNDEFINED:      state_str = "undefined"; break;
        case ESP_OTA_IMG_NEW:            state_str = "new"; break;
        case ESP_OTA_IMG_INVALID:        state_str = "invalid"; break;
        case ESP_OTA_IMG_ABORTED:        state_str = "aborted"; break;
        default: break;
    }
    cJSON_AddStringToObject(root, "state", state_str);

    char *out = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, out);
    free(out);
    cJSON_Delete(root);
    return ESP_OK;
}

/* ---- Node http_endpoint (lihat schema.md): route HTTP statis di server
 * httpd KEDUA, port terpisah (endpoint_mgr) dari server utama (editor +
 * /api/...). File HTML/teks-nya disimpan di /spiffs/endpoints/<file>,
 * di-upload lewat POST /api/endpoint_file (mirip pola upload OTA).
 * Server kedua didaftarkan SEKALI saat boot dari graph yang tersimpan -
 * TIDAK bisa ditambah/dihapus route secara dinamis tanpa reboot (batasan
 * esp_http_server ESP-IDF), makanya UI System > HTTP Endpoints wajib
 * kasih peringatan reboot setelah Save program berisi node baru/berubah. ---- */

#define ENDPOINT_FILE_DIR "/spiffs/endpoints"
#define ENDPOINT_FILE_MAX_SIZE (64 * 1024) /* 64KB cukup longgar utk halaman HTML+CSS sederhana */

/* Handler generik dipakai server KEDUA - membaca file dari /spiffs/endpoints/
 * sesuai http_file yang di-bind lewat httpd_uri_t.user_ctx (nama file,
 * bukan path lengkap - dihitung di sini). content_type juga lewat user_ctx
 * (di-encode di string statis "file|content_type" saat registrasi, lihat
 * register_http_endpoints()). */
static esp_err_t endpoint_get_handler(httpd_req_t *req)
{
    const char *user_ctx = (const char *)req->user_ctx;
    char file[32] = {0};
    char content_type[16] = "text/html";
    const char *sep = strchr(user_ctx, '|');
    if (sep) {
        size_t file_len = (size_t)(sep - user_ctx);
        if (file_len >= sizeof(file)) file_len = sizeof(file) - 1;
        memcpy(file, user_ctx, file_len);
        strncpy(content_type, sep + 1, sizeof(content_type) - 1);
    } else {
        strncpy(file, user_ctx, sizeof(file) - 1);
    }

    char path[80];
    snprintf(path, sizeof(path), "%s/%s", ENDPOINT_FILE_DIR, file);
    FILE *f = fopen(path, "r");
    if (!f) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "file endpoint tidak ditemukan di SPIFFS");
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, content_type);
    char buf[512];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        httpd_resp_send_chunk(req, buf, n);
    }
    httpd_resp_send_chunk(req, NULL, 0);
    fclose(f);
    return ESP_OK;
}

/* ---- POST /api/endpoint_file : upload file HTML/teks lewat browser
 * (input type=file), disimpan ke /spiffs/endpoints/<nama file dari query
 * string ?name=...>. Dipakai UI System > HTTP Endpoints sebelum user
 * menyambungkan nama file itu ke params.file node http_endpoint. ---- */

static esp_err_t endpoint_file_post_handler(httpd_req_t *req)
{
    char filename[32] = {0};
    size_t qs_len = httpd_req_get_url_query_len(req);
    if (qs_len == 0 || qs_len >= 128) {
        return send_json_error(req, "query string ?name=<file> wajib diisi");
    }
    char qs[128];
    httpd_req_get_url_query_str(req, qs, sizeof(qs));
    if (httpd_query_key_value(qs, "name", filename, sizeof(filename)) != ESP_OK || strlen(filename) == 0) {
        return send_json_error(req, "query string ?name=<file> wajib diisi");
    }
    /* Cegah path traversal sederhana - nama file tidak boleh mengandung
     * '/' (harus nama file polos, bukan path bersarang) atau '..'. */
    if (strchr(filename, '/') || strstr(filename, "..")) {
        return send_json_error(req, "nama file tidak valid (tidak boleh mengandung '/' atau '..')");
    }

    int total = req->content_len;
    if (total <= 0 || total > ENDPOINT_FILE_MAX_SIZE) {
        return send_json_error(req, "ukuran file tidak valid (maks 64KB)");
    }

    mkdir(ENDPOINT_FILE_DIR, 0755); /* aman dipanggil walau folder sudah ada */

    char path[80];
    snprintf(path, sizeof(path), "%s/%s", ENDPOINT_FILE_DIR, filename);
    FILE *f = fopen(path, "w");
    if (!f) {
        return send_json_error(req, "gagal buat file di SPIFFS");
    }

    char buf[512];
    int received = 0;
    bool write_failed = false;
    while (received < total) {
        int to_read = total - received;
        if (to_read > (int)sizeof(buf)) to_read = sizeof(buf);
        int r = httpd_req_recv(req, buf, to_read);
        if (r <= 0) {
            write_failed = true;
            break;
        }
        fwrite(buf, 1, r, f);
        received += r;
    }
    fclose(f);

    if (write_failed) {
        remove(path);
        return send_json_error(req, "gagal menerima file di tengah upload");
    }

    ESP_LOGI(TAG, "file endpoint tersimpan: %s (%d byte)", path, total);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

/* ---- GET/POST /api/endpoints_config : port server httpd KEDUA
 * (endpoint_mgr, NVS) - beda dari /api/network (WiFi) tapi pola sama:
 * simpan ke NVS, baru berlaku setelah reboot. ---- */

static esp_err_t endpoints_config_get_handler(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "port", endpoint_mgr_get_port());
    char *out = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, out);
    free(out);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t endpoints_config_post_handler(httpd_req_t *req)
{
    int total = req->content_len;
    if (total <= 0 || total > 128) {
        return send_json_error(req, "ukuran body tidak valid");
    }
    char buf[128];
    int received = 0;
    while (received < total) {
        int r = httpd_req_recv(req, buf + received, total - received);
        if (r <= 0) {
            return send_json_error(req, "gagal membaca body");
        }
        received += r;
    }
    buf[total] = '\0';

    cJSON *json = cJSON_Parse(buf);
    if (!json) {
        return send_json_error(req, "JSON tidak valid (parse error)");
    }
    const cJSON *port = cJSON_GetObjectItem(json, "port");
    if (!port || !cJSON_IsNumber(port) || port->valuedouble < 1 || port->valuedouble > 65535) {
        cJSON_Delete(json);
        return send_json_error(req, "port harus angka 1-65535");
    }
    bool ok = endpoint_mgr_save_port((uint16_t)port->valuedouble);
    cJSON_Delete(json);

    if (!ok) {
        return send_json_error(req, "gagal menyimpan port ke NVS");
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\",\"message\":\"Port tersimpan. Reboot device untuk menerapkan.\"}");
    return ESP_OK;
}

/* Daftarkan route HTTP kustom (node http_endpoint) di server httpd KEDUA,
 * dibaca dari graph aktif SAAT INI (dipanggil sekali setelah
 * load_program_file_at_startup(), sebelum server kedua di-start).
 * user_ctx per handler diisi string statis "file|content_type" yang
 * SENGAJA tidak pernah di-free (hidup selama program berjalan, jumlahnya
 * dibatasi FBD_MAX_NODES sehingga tidak bisa leak tanpa batas). */
static void register_http_endpoints(httpd_handle_t server, const fbd_graph_t *graph)
{
    for (size_t i = 0; i < graph->node_count; ++i) {
        const fbd_node_t *node = &graph->nodes[i];
        if (node->type != FBD_NODE_HTTP_ENDPOINT) continue;

        char *user_ctx = malloc(80);
        if (!user_ctx) continue;
        snprintf(user_ctx, 80, "%s|%s", node->params.http_file,
                 node->params.http_content_type_html ? "text/html" : "text/plain");

        httpd_uri_t *uri = malloc(sizeof(httpd_uri_t));
        if (!uri) { free(user_ctx); continue; }
        uri->uri = strdup(node->params.http_path);
        uri->method = HTTP_GET;
        uri->handler = endpoint_get_handler;
        uri->user_ctx = user_ctx;

        if (httpd_register_uri_handler(server, uri) == ESP_OK) {
            ESP_LOGI(TAG, "http_endpoint terdaftar: GET %s -> %s", node->params.http_path, node->params.http_file);
        } else {
            ESP_LOGW(TAG, "gagal daftar http_endpoint: %s (path bentrok/handler penuh?)", node->params.http_path);
        }
        /* uri/user_ctx SENGAJA tidak di-free - httpd_uri_t harus tetap
         * hidup selama server berjalan (esp_http_server tidak copy
         * struct-nya). Bukan leak tanpa batas karena dibatasi FBD_MAX_NODES
         * dan cuma terjadi sekali saat boot, tidak berulang. */
    }
}

/* Server httpd KEDUA, port dari endpoint_mgr (NVS). Dipanggil sekali saat
 * boot SETELAH server utama & load_program_file_at_startup() selesai -
 * membaca graph AKTIF saat itu (yang barusan dimuat dari program.json)
 * untuk tahu route apa saja yang perlu didaftarkan. */
static void start_endpoint_server(const fbd_graph_t *graph)
{
    uint16_t port = endpoint_mgr_get_port();

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = port;
    /* HTTPD_DEFAULT_CONFIG() set ctrl_port ke konstanta tetap
     * (ESP_HTTPD_DEF_CTRL_PORT=32768) - kalau dua instance httpd jalan di
     * proses yang sama (server utama + server kedua ini) dan sama-sama
     * pakai ctrl_port default, instance kedua GAGAL start (errno 112,
     * "error in creating ctrl socket") karena ctrl socket internal itu
     * sendiri bentrok - BUKAN soal server_port (8080) yang dipilih user.
     * Ditemukan dari log nyata di hardware, bukan dugaan. Server kedua
     * WAJIB ctrl_port beda dari default supaya bisa jalan berdampingan
     * dengan server utama. */
    config.ctrl_port = ESP_HTTPD_DEF_CTRL_PORT + 1;
    config.max_uri_handlers = 16; /* cukup untuk FBD_MAX_NODES/4 endpoint realistis */

    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "gagal start httpd endpoint server di port %u", port);
        return;
    }

    register_http_endpoints(server, graph);
    ESP_LOGI(TAG, "http endpoint server siap di port %u", port);
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
    config.max_uri_handlers = 16;
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
    httpd_uri_t ota_post_uri = { .uri = "/api/ota", .method = HTTP_POST, .handler = ota_post_handler };
    httpd_uri_t ota_status_uri = { .uri = "/api/ota/status", .method = HTTP_GET, .handler = ota_status_get_handler };
    httpd_uri_t endpoint_file_post_uri = { .uri = "/api/endpoint_file", .method = HTTP_POST, .handler = endpoint_file_post_handler };
    httpd_uri_t endpoints_config_get_uri = { .uri = "/api/endpoints_config", .method = HTTP_GET, .handler = endpoints_config_get_handler };
    httpd_uri_t endpoints_config_post_uri = { .uri = "/api/endpoints_config", .method = HTTP_POST, .handler = endpoints_config_post_handler };
    /* Wildcard, harus didaftarkan setelah /api/... supaya tidak menutupi -
     * httpd_uri_match_wildcard cocokkan URI paling spesifik dulu terlepas
     * urutan register, tapi tetap didaftarkan terakhir untuk kejelasan. */
    httpd_uri_t static_uri = { .uri = "/*", .method = HTTP_GET, .handler = static_get_handler };

    httpd_register_uri_handler(server, &program_post_uri);
    httpd_register_uri_handler(server, &program_get_uri);
    httpd_register_uri_handler(server, &status_uri);
    httpd_register_uri_handler(server, &network_get_uri);
    httpd_register_uri_handler(server, &network_post_uri);
    httpd_register_uri_handler(server, &ota_post_uri);
    httpd_register_uri_handler(server, &ota_status_uri);
    httpd_register_uri_handler(server, &endpoint_file_post_uri);
    httpd_register_uri_handler(server, &endpoints_config_get_uri);
    httpd_register_uri_handler(server, &endpoints_config_post_uri);
    httpd_register_uri_handler(server, &static_uri);

    ESP_LOGI(TAG, "web server siap");

    /* Server httpd KEDUA (port terpisah, node http_endpoint) - dibaca dari
     * *STANDBY* graph, BUKAN *active_graph_ptr. Titik ini (dalam
     * web_ui_start(), dipanggil dari app_main() SEBELUM fbd_scan_task
     * dibuat - lihat main.c) terjadi SEBELUM scan task pernah sempat
     * jalan sama sekali, jadi swap active<->standby belum pernah terjadi:
     * *active_graph_ptr masih graph default kosong dari main.c, sedangkan
     * program.json yang BARU SAJA dimuat oleh load_program_file_at_startup()
     * (dipanggil beberapa baris di atas) ada di *standby_graph_ptr,
     * menunggu di-swap oleh scan task nanti. Baris ini SETELAH server
     * utama start supaya kalau start server kedua gagal (mis. port
     * dipakai proses lain), server utama tetap jalan normal - user masih
     * bisa akses editor untuk perbaiki config port lewat System > HTTP
     * Endpoints. Ditemukan dari log nyata di hardware (endpoint tidak
     * pernah terdaftar walau program.json 1 node berhasil dimuat), bukan
     * dugaan. */
    start_endpoint_server(*s_standby_graph_ptr);
}

#include "wifi_mgr.h"
#include <string.h>
#include <stdio.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_random.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#define AP_SSID "ESP32-WebLogic"
#define AP_PASS_LEN 10
#define AP_CHANNEL 1
#define AP_MAX_CONN 4

/* SSID rumah tanpa password - dipakai sebagai FALLBACK kalau NVS belum
 * pernah diisi lewat tab System>Network (wifi_mgr_save_sta_config()).
 * Fallback ini WAJIB tetap ada supaya koneksi dev tidak putus kalau user
 * belum pernah simpan config apa pun - device tidak boleh gagal boot WiFi
 * hanya karena NVS kosong. */
#define STA_SSID_FALLBACK "MIFON"
#define STA_PASS_FALLBACK ""

#define NVS_NAMESPACE "wifi_cfg"
#define NVS_KEY_STA_SSID "sta_ssid"
#define NVS_KEY_STA_PASS "sta_pass"
#define NVS_KEY_HOSTNAME "hostname"
#define NVS_KEY_AP_PASS "ap_pass"

static const char *TAG = "wifi_mgr";
static EventGroupHandle_t s_sta_event_group = NULL;
#define STA_CONNECTED_BIT BIT0

/* Status STA untuk getter SYS.* - diupdate dari sta_event_handler(),
 * dibaca dari task mana pun (termasuk fbd_scan_task). volatile cukup untuk
 * bool/uint32 sederhana ini, tidak butuh mutex (tidak ada invariant
 * multi-field yang harus konsisten bersamaan). */
static volatile bool s_sta_connected = false;
static volatile uint32_t s_sta_ip = 0; /* network byte order, dari esp_ip4_addr_t.addr */

/* SSID/password STA yang benar-benar dipakai saat wifi_mgr_start_apsta() -
 * dibaca dari NVS di load_sta_config_from_nvs(), fallback ke *_FALLBACK
 * kalau NVS kosong/belum pernah diisi. Disimpan di sini (bukan langsung
 * dipakai inline) supaya wifi_mgr_get_sta_ssid() bisa melapor SSID yang
 * SEDANG dipakai (berguna utk endpoint GET /api/network menampilkan
 * config aktif). */
static char s_sta_ssid[33] = STA_SSID_FALLBACK;
static char s_sta_pass[65] = STA_PASS_FALLBACK;

static void load_sta_config_from_nvs(void)
{
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        ESP_LOGI(TAG, "NVS wifi_cfg belum pernah diisi, pakai fallback SSID=%s", STA_SSID_FALLBACK);
        return; /* s_sta_ssid/s_sta_pass tetap nilai fallback dari inisialisasi statis */
    }

    size_t len = sizeof(s_sta_ssid);
    if (nvs_get_str(handle, NVS_KEY_STA_SSID, s_sta_ssid, &len) != ESP_OK) {
        strncpy(s_sta_ssid, STA_SSID_FALLBACK, sizeof(s_sta_ssid) - 1);
    }

    len = sizeof(s_sta_pass);
    if (nvs_get_str(handle, NVS_KEY_STA_PASS, s_sta_pass, &len) != ESP_OK) {
        strncpy(s_sta_pass, STA_PASS_FALLBACK, sizeof(s_sta_pass) - 1);
    }

    nvs_close(handle);
    ESP_LOGI(TAG, "Config WiFi STA dimuat dari NVS: SSID=%s", s_sta_ssid);
}

/* Password AP unik per perangkat: dibuat acak sekali (esp_fill_random,
 * bukan turunan MAC - MAC/BSSID AP terlihat di udara oleh siapa pun, jadi
 * password turunan MAC bisa ditebak) lalu disimpan di NVS supaya tetap
 * sama setelah reboot. Dicetak di log serial saat boot. Alfabet 32
 * karakter tanpa yang mudah tertukar (0/O, 1/I/l), 256 % 32 == 0 jadi
 * tidak ada bias modulo. */
static char s_ap_pass[AP_PASS_LEN + 1];

static void load_or_create_ap_pass(void)
{
    static const char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    nvs_handle_t handle = 0;
    bool nvs_ok = (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK);
    if (!nvs_ok) {
        ESP_LOGE(TAG, "gagal buka NVS untuk password AP, pakai password sementara (tidak tersimpan)");
    } else {
        size_t len = sizeof(s_ap_pass);
        if (nvs_get_str(handle, NVS_KEY_AP_PASS, s_ap_pass, &len) == ESP_OK && strlen(s_ap_pass) >= 8) {
            nvs_close(handle);
            return;
        }
    }

    uint8_t rnd[AP_PASS_LEN];
    esp_fill_random(rnd, sizeof(rnd));
    for (int i = 0; i < AP_PASS_LEN; ++i) {
        s_ap_pass[i] = alphabet[rnd[i] % 32];
    }
    s_ap_pass[AP_PASS_LEN] = '\0';

    if (nvs_ok) {
        nvs_set_str(handle, NVS_KEY_AP_PASS, s_ap_pass);
        nvs_commit(handle);
        nvs_close(handle);
    }
    ESP_LOGI(TAG, "Password AP baru dibuat untuk perangkat ini (boot pertama)");
}

static void nvs_init_once(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
}

void wifi_mgr_start_ap(void)
{
    nvs_init_once();
    load_or_create_ap_pass();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid_len = strlen(AP_SSID),
            .channel = AP_CHANNEL,
            .max_connection = AP_MAX_CONN,
            .authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    strncpy((char *)wifi_config.ap.ssid, AP_SSID, sizeof(wifi_config.ap.ssid));
    strncpy((char *)wifi_config.ap.password, s_ap_pass, sizeof(wifi_config.ap.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "AP aktif: SSID=%s PASS=%s, buka http://192.168.4.1", AP_SSID, s_ap_pass);
}

static void sta_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "STA terputus dari %s, mencoba reconnect...", s_sta_ssid);
        s_sta_connected = false;
        s_sta_ip = 0;
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "STA terhubung ke %s, IP: " IPSTR, s_sta_ssid, IP2STR(&event->ip_info.ip));
        s_sta_connected = true;
        s_sta_ip = event->ip_info.ip.addr;
        xEventGroupSetBits(s_sta_event_group, STA_CONNECTED_BIT);
    }
}

void wifi_mgr_start_apsta(void)
{
    nvs_init_once();
    load_or_create_ap_pass();
    load_sta_config_from_nvs();
    s_sta_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &sta_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &sta_event_handler, NULL));

    wifi_config_t ap_config = {
        .ap = {
            .ssid_len = strlen(AP_SSID),
            .channel = AP_CHANNEL,
            .max_connection = AP_MAX_CONN,
            .authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    strncpy((char *)ap_config.ap.ssid, AP_SSID, sizeof(ap_config.ap.ssid));
    strncpy((char *)ap_config.ap.password, s_ap_pass, sizeof(ap_config.ap.password));

    /* Password kosong -> jaringan open (fallback MIFON), password terisi
     * (dari NVS lewat tab System>Network) -> WPA2-PSK. */
    wifi_config_t sta_config = {
        .sta = {
            .threshold.authmode = (strlen(s_sta_pass) == 0) ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK,
        },
    };
    strncpy((char *)sta_config.sta.ssid, s_sta_ssid, sizeof(sta_config.sta.ssid));
    strncpy((char *)sta_config.sta.password, s_sta_pass, sizeof(sta_config.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "AP aktif: SSID=%s PASS=%s, buka http://192.168.4.1", AP_SSID, s_ap_pass);
    ESP_LOGI(TAG, "Menyambungkan STA ke %s...", s_sta_ssid);

    /* Tunggu STA connect maksimal 10s supaya log IP sempat tercetak sebelum
     * lanjut - tidak fatal kalau timeout, AP tetap jalan seperti biasa. */
    EventBits_t bits = xEventGroupWaitBits(s_sta_event_group, STA_CONNECTED_BIT,
                                            pdFALSE, pdFALSE, pdMS_TO_TICKS(10000));
    if (!(bits & STA_CONNECTED_BIT)) {
        ESP_LOGW(TAG, "STA belum connect ke %s dalam 10s, akan tetap retry di background", s_sta_ssid);
    }
}

bool wifi_mgr_is_sta_connected(void)
{
    return s_sta_connected;
}

int32_t wifi_mgr_get_sta_rssi(void)
{
    if (!s_sta_connected) {
        return -127; /* "sangat lemah" - aman dipakai Compare < -80 tanpa STA connect dianggap warning */
    }
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) != ESP_OK) {
        return -127;
    }
    return ap_info.rssi;
}

bool wifi_mgr_get_sta_ip(char *buf, size_t buf_len)
{
    if (!s_sta_connected || s_sta_ip == 0) {
        snprintf(buf, buf_len, "0.0.0.0");
        return false;
    }
    esp_ip4_addr_t ip = { .addr = s_sta_ip };
    snprintf(buf, buf_len, IPSTR, IP2STR(&ip));
    return true;
}

void wifi_mgr_get_sta_config(char *ssid_buf, size_t ssid_len, char *hostname_buf, size_t hostname_len)
{
    strncpy(ssid_buf, s_sta_ssid, ssid_len - 1);
    ssid_buf[ssid_len - 1] = '\0';

    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) == ESP_OK) {
        size_t len = hostname_len;
        if (nvs_get_str(handle, NVS_KEY_HOSTNAME, hostname_buf, &len) != ESP_OK) {
            snprintf(hostname_buf, hostname_len, "esp32-fbd");
        }
        nvs_close(handle);
    } else {
        snprintf(hostname_buf, hostname_len, "esp32-fbd");
    }
    /* Password TIDAK PERNAH diekspos lewat getter ini - kalau dibutuhkan
     * UI untuk konfirmasi "sudah ada password tersimpan", tab System>Network
     * cukup tampilkan placeholder, bukan password asli (keamanan dasar -
     * jangan kirim balik credential yang sudah tersimpan lewat GET). */
}

bool wifi_mgr_save_sta_config(const char *ssid, const char *password, const char *hostname)
{
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        ESP_LOGE(TAG, "gagal buka NVS untuk simpan config WiFi");
        return false;
    }

    esp_err_t err = ESP_OK;
    err |= nvs_set_str(handle, NVS_KEY_STA_SSID, ssid);
    err |= nvs_set_str(handle, NVS_KEY_STA_PASS, password);
    if (hostname && strlen(hostname) > 0) {
        err |= nvs_set_str(handle, NVS_KEY_HOSTNAME, hostname);
    }
    err |= nvs_commit(handle);
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gagal simpan config WiFi ke NVS");
        return false;
    }

    ESP_LOGI(TAG, "Config WiFi disimpan ke NVS (SSID=%s). Reboot device untuk menerapkan.", ssid);
    return true;
}

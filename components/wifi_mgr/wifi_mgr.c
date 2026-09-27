#include "wifi_mgr.h"
#include <string.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#define AP_SSID "ESP32-WebLogic"
#define AP_PASS "logic1234"
#define AP_CHANNEL 1
#define AP_MAX_CONN 4

/* SSID rumah tanpa password, dipakai wifi_mgr_start_apsta() supaya PC dev
 * bisa akses device lewat jaringan yang sama tanpa pindah WiFi manual. */
#define STA_SSID "MIFON"
#define STA_PASS ""

static const char *TAG = "wifi_mgr";
static EventGroupHandle_t s_sta_event_group = NULL;
#define STA_CONNECTED_BIT BIT0

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
    strncpy((char *)wifi_config.ap.password, AP_PASS, sizeof(wifi_config.ap.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "AP aktif: SSID=%s PASS=%s, buka http://192.168.4.1", AP_SSID, AP_PASS);
}

static void sta_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "STA terputus dari %s, mencoba reconnect...", STA_SSID);
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "STA terhubung ke %s, IP: " IPSTR, STA_SSID, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_sta_event_group, STA_CONNECTED_BIT);
    }
}

void wifi_mgr_start_apsta(void)
{
    nvs_init_once();
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
    strncpy((char *)ap_config.ap.password, AP_PASS, sizeof(ap_config.ap.password));

    wifi_config_t sta_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_OPEN,
        },
    };
    strncpy((char *)sta_config.sta.ssid, STA_SSID, sizeof(sta_config.sta.ssid));
    strncpy((char *)sta_config.sta.password, STA_PASS, sizeof(sta_config.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "AP aktif: SSID=%s PASS=%s, buka http://192.168.4.1", AP_SSID, AP_PASS);
    ESP_LOGI(TAG, "Menyambungkan STA ke %s (tanpa password)...", STA_SSID);

    /* Tunggu STA connect maksimal 10s supaya log IP sempat tercetak sebelum
     * lanjut - tidak fatal kalau timeout, AP tetap jalan seperti biasa. */
    EventBits_t bits = xEventGroupWaitBits(s_sta_event_group, STA_CONNECTED_BIT,
                                            pdFALSE, pdFALSE, pdMS_TO_TICKS(10000));
    if (!(bits & STA_CONNECTED_BIT)) {
        ESP_LOGW(TAG, "STA belum connect ke %s dalam 10s, akan tetap retry di background", STA_SSID);
    }
}

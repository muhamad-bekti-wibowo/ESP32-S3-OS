#include "sys_reset.h"
#include "wifi_mgr.h"
#include "fbd_hw_backend.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"

static const char *TAG = "sys_reset";

#define RESET_BUTTON_PIN 0   /* tombol BOOT, aktif low, pull-up bawaan board */
#define RESET_LED_PIN 48     /* WS2812 onboard */
#define POLL_MS 50
#define WARN_AFTER_MS 1000   /* di bawah ini dianggap tekan tak sengaja */
#define RESET_AFTER_MS 5000
#define RELEASE_TIMEOUT_MS 10000

static void led(uint8_t r, uint8_t g, uint8_t b)
{
    fbd_hw_ws2812_write_override(RESET_LED_PIN, 1, r, g, b);
}

static bool button_pressed(void)
{
    return gpio_get_level(RESET_BUTTON_PIN) == 0;
}

static void sys_reset_task(void *arg)
{
    (void)arg;

    /* Init channel RMT pin 48 sekarang (LED mati) - lihat sys_reset.h. */
    led(0, 0, 0);

    uint32_t held_ms = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));

        if (!button_pressed()) {
            if (held_ms >= WARN_AFTER_MS) {
                led(0, 0, 0); /* dilepas sebelum 5 detik = batal */
                fbd_hw_ws2812_set_override(-1); /* LED kembali dikendalikan graph */
                ESP_LOGI(TAG, "tombol dilepas setelah %u ms, reset dibatalkan", (unsigned)held_ms);
            }
            held_ms = 0;
            continue;
        }

        held_ms += POLL_MS;
        if (held_ms < WARN_AFTER_MS) {
            continue;
        }
        /* Pinjam LED: node ws2812 pengguna di GPIO48 (kalau ada) berhenti
         * menulis selama umpan balik reset, supaya kedipan tidak tertimpa. */
        fbd_hw_ws2812_set_override(RESET_LED_PIN);

        if (held_ms < RESET_AFTER_MS) {
            /* kuning kedip lambat: 500 ms nyala / 500 ms mati */
            bool on = ((held_ms / 500) % 2) == 0;
            led(on ? 40 : 0, on ? 30 : 0, 0);
            continue;
        }

        ESP_LOGW(TAG, "tombol BOOT ditahan %d detik: reset config WiFi lalu reboot", RESET_AFTER_MS / 1000);
        wifi_mgr_erase_config();

        /* Merah kedip cepat sampai tombol dilepas (maks RELEASE_TIMEOUT_MS).
         * Menunggu dilepas supaya GPIO0 tidak sedang low saat reboot
         * (low saat reset = masuk mode download ROM). */
        uint32_t waited_ms = 0;
        while (button_pressed() && waited_ms < RELEASE_TIMEOUT_MS) {
            bool on = ((waited_ms / 150) % 2) == 0;
            led(on ? 60 : 0, 0, 0);
            vTaskDelay(pdMS_TO_TICKS(POLL_MS));
            waited_ms += POLL_MS;
        }
        led(0, 0, 0);
        ESP_LOGW(TAG, "reboot");
        vTaskDelay(pdMS_TO_TICKS(100));
        esp_restart();
    }
}

void sys_reset_start(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << RESET_BUTTON_PIN,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
    xTaskCreate(sys_reset_task, "sys_reset", 4096, NULL, 3, NULL);
}

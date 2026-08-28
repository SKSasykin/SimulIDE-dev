#include <stdint.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_defaults.h"
#include "esp_rom_lldesc.h"
#include "soc/soc.h"

#include "virtual_wifi.h"

#if CONFIG_IDF_TARGET_ESP32
#define VIRTUAL_WIFI_BASE 0x3ff58000u
#elif CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32C3
#define VIRTUAL_WIFI_BASE 0x60058000u
#else
#error "Unsupported virtual WiFi target"
#endif

#define VIRTUAL_WIFI_TX_LINK       (VIRTUAL_WIFI_BASE + 0x40)
#define VIRTUAL_WIFI_RX_LINK       (VIRTUAL_WIFI_BASE + 0x3c)
#define VIRTUAL_WIFI_TX_DESC       (VIRTUAL_WIFI_BASE + 0xec)
#define VIRTUAL_WIFI_RX_DESC       (VIRTUAL_WIFI_BASE + 0xf4)
#define VIRTUAL_WIFI_LINK_START    (1u << 29)

#define VIRTUAL_WIFI_FRAME_SIZE 1600
#define VIRTUAL_WIFI_RX_COUNT 8

typedef struct {
    esp_netif_t *netif;
    SemaphoreHandle_t tx_lock;
    lldesc_t tx_desc;
    uint8_t tx_buffer[VIRTUAL_WIFI_FRAME_SIZE];
    lldesc_t rx_desc[VIRTUAL_WIFI_RX_COUNT];
    uint8_t rx_buffer[VIRTUAL_WIFI_RX_COUNT][VIRTUAL_WIFI_FRAME_SIZE];
} virtual_wifi_t;

static const char *TAG = "virtual_wifi";
static DRAM_ATTR virtual_wifi_t s_wifi;

static void virtual_wifi_free_rx_buffer(void *handle, void *buffer)
{
    (void)handle;
    heap_caps_free(buffer);
}

static void descriptor_init(lldesc_t *desc, uint8_t *buffer, size_t size,
                            lldesc_t *next)
{
    memset(desc, 0, sizeof(*desc));
    desc->size = size;
    desc->owner = 1;
    desc->buf = buffer;
    desc->qe.stqe_next = next;
}

static esp_err_t virtual_wifi_transmit(void *handle, void *buffer, size_t len)
{
    virtual_wifi_t *wifi = handle;

    if (!buffer || len == 0 || len > sizeof(wifi->tx_buffer)) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (xSemaphoreTake(wifi->tx_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    memcpy(wifi->tx_buffer, buffer, len);
    descriptor_init(&wifi->tx_desc, wifi->tx_buffer, len, NULL);
    wifi->tx_desc.length = len;
    wifi->tx_desc.eof = 1;
    __sync_synchronize();

    REG_WRITE(VIRTUAL_WIFI_TX_DESC, (uint32_t)&wifi->tx_desc);
    REG_WRITE(VIRTUAL_WIFI_TX_LINK, VIRTUAL_WIFI_LINK_START);

    xSemaphoreGive(wifi->tx_lock);
    return ESP_OK;
}

static void virtual_wifi_rx_task(void *arg)
{
    virtual_wifi_t *wifi = arg;

    while (true) {
        for (size_t i = 0; i < VIRTUAL_WIFI_RX_COUNT; ++i) {
            lldesc_t *desc = &wifi->rx_desc[i];
            if (desc->owner == 0 && desc->length > 0) {
                size_t len = desc->length;
                void *frame = heap_caps_malloc(len, MALLOC_CAP_8BIT);
                if (frame) {
                    memcpy(frame, wifi->rx_buffer[i], len);
                    if (esp_netif_receive(wifi->netif, frame, len, NULL) != ESP_OK) {
                        heap_caps_free(frame);
                    }
                }
                desc->length = 0;
                desc->eof = 0;
                __sync_synchronize();
                desc->owner = 1;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

esp_err_t virtual_wifi_start(esp_netif_t **out_netif)
{
    static uint8_t mac[6] = { 0x02, 0x53, 0x49, 0x4d, 0x00, 0x01 };
    esp_netif_inherent_config_t base = ESP_NETIF_INHERENT_DEFAULT_WIFI_STA();
    esp_netif_driver_ifconfig_t driver = {
        .handle = &s_wifi,
        .transmit = virtual_wifi_transmit,
        .driver_free_rx_buffer = virtual_wifi_free_rx_buffer,
    };
    esp_netif_config_t config = {
        .base = &base,
        .driver = &driver,
        .stack = ESP_NETIF_NETSTACK_DEFAULT_ETH,
    };

    memset(&s_wifi, 0, sizeof(s_wifi));
    base.if_key = "WIFI_STA_SIM";
    base.if_desc = "SimulIDE virtual WiFi";
    base.route_prio = 128;

    s_wifi.tx_lock = xSemaphoreCreateMutex();
    if (!s_wifi.tx_lock) {
        return ESP_ERR_NO_MEM;
    }
    s_wifi.netif = esp_netif_new(&config);
    if (!s_wifi.netif) {
        return ESP_ERR_NO_MEM;
    }
    ESP_ERROR_CHECK(esp_netif_set_mac(s_wifi.netif, mac));

    for (size_t i = 0; i < VIRTUAL_WIFI_RX_COUNT; ++i) {
        descriptor_init(&s_wifi.rx_desc[i], s_wifi.rx_buffer[i],
                        sizeof(s_wifi.rx_buffer[i]),
                        &s_wifi.rx_desc[(i + 1) % VIRTUAL_WIFI_RX_COUNT]);
    }
    __sync_synchronize();
    REG_WRITE(VIRTUAL_WIFI_RX_DESC, (uint32_t)&s_wifi.rx_desc[0]);
    REG_WRITE(VIRTUAL_WIFI_RX_LINK, VIRTUAL_WIFI_LINK_START);

    if (xTaskCreate(virtual_wifi_rx_task, "virtual_wifi_rx", 3072,
                    &s_wifi, 1, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    esp_netif_action_start(s_wifi.netif, NULL, 0, NULL);
    esp_netif_action_connected(s_wifi.netif, NULL, 0, NULL);
    ESP_LOGI(TAG, "virtual WiFi link is up; waiting for DHCP");

    if (out_netif) {
        *out_netif = s_wifi.netif;
    }
    return ESP_OK;
}

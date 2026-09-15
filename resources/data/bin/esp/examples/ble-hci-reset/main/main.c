#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "soc/soc.h"

#if CONFIG_IDF_TARGET_ESP32
#define VBLE_HCI_BASE 0x3ff52000u
#elif CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32C3
#define VBLE_HCI_BASE 0x60012000u
#else
#error "Unsupported virtual BLE HCI target"
#endif

#define VBLE_CONTROL  (VBLE_HCI_BASE + 0x08)
#define VBLE_INT_ENA  (VBLE_HCI_BASE + 0x14)
#define VBLE_INT_CLR  (VBLE_HCI_BASE + 0x1c)
#define VBLE_TX_DESC  (VBLE_HCI_BASE + 0x20)
#define VBLE_RX_DESC  (VBLE_HCI_BASE + 0x24)
#define VBLE_TX_KICK  (VBLE_HCI_BASE + 0x28)

#define VBLE_CONTROL_ENABLE (1u << 0)
#define VBLE_CONTROL_RESET  (1u << 1)
#define VBLE_INT_ALL         0x7u
#define VBLE_DESC_OWN        (1u << 31)

typedef struct __attribute__((aligned(16))) {
    uint32_t buffer;
    uint32_t length;
    uint32_t next;
    volatile uint32_t flags;
} vble_desc_t;

static const char *TAG = "ble_hci_reset";
static DRAM_ATTR uint8_t s_tx_packet[] = { 0x01, 0x03, 0x0c, 0x00 };
static DRAM_ATTR uint8_t s_rx_packet[32];
static DRAM_ATTR vble_desc_t s_tx_desc __attribute__((aligned(16)));
static DRAM_ATTR vble_desc_t s_rx_desc __attribute__((aligned(16)));

static void init_desc(vble_desc_t *desc, uint8_t *buffer, uint32_t length)
{
    desc->buffer = (uint32_t)(uintptr_t)buffer;
    desc->length = length;
    desc->next = 0;
    desc->flags = VBLE_DESC_OWN;
}

void app_main(void)
{
    static const uint8_t expected[] = {
        0x04, 0x0e, 0x04, 0x01, 0x03, 0x0c, 0x00,
    };

    memset(s_rx_packet, 0, sizeof(s_rx_packet));
    init_desc(&s_rx_desc, s_rx_packet, sizeof(s_rx_packet));
    init_desc(&s_tx_desc, s_tx_packet, sizeof(s_tx_packet));
    __sync_synchronize();

    REG_WRITE(VBLE_CONTROL, VBLE_CONTROL_RESET);
    REG_WRITE(VBLE_RX_DESC, (uint32_t)(uintptr_t)&s_rx_desc);
    REG_WRITE(VBLE_TX_DESC, (uint32_t)(uintptr_t)&s_tx_desc);
    REG_WRITE(VBLE_INT_ENA, VBLE_INT_ALL);
    REG_WRITE(VBLE_CONTROL, VBLE_CONTROL_ENABLE);
    REG_WRITE(VBLE_TX_KICK, 1);

    for (unsigned attempt = 0; attempt < 200; ++attempt) {
        __sync_synchronize();
        if (!(s_rx_desc.flags & VBLE_DESC_OWN)) {
            bool valid = s_rx_desc.length == sizeof(expected) &&
                         memcmp(s_rx_packet, expected, sizeof(expected)) == 0;
            REG_WRITE(VBLE_INT_CLR, VBLE_INT_ALL);
            if (valid) {
                ESP_LOGI(TAG, "BLE_HCI_RESET_PASS opcode=0x0c03 status=0x00");
            } else {
                ESP_LOGE(TAG, "BLE_HCI_RESET_FAIL malformed response length=%u",
                         (unsigned)s_rx_desc.length);
            }
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    ESP_LOGE(TAG, "BLE_HCI_RESET_FAIL timeout");
}

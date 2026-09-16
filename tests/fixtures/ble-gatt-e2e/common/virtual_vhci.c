#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "esp_attr.h"
#include "esp_bt.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "soc/soc.h"

#include "virtual_vhci.h"

#if CONFIG_IDF_TARGET_ESP32
#define VBLE_HCI_BASE 0x3ff52000u
#elif CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32C3
#define VBLE_HCI_BASE 0x60012000u
#else
#error "Unsupported virtual BLE HCI target"
#endif

#define VBLE_ID       (VBLE_HCI_BASE + 0x00)
#define VBLE_CONTROL  (VBLE_HCI_BASE + 0x08)
#define VBLE_INT_ENA  (VBLE_HCI_BASE + 0x14)
#define VBLE_INT_CLR  (VBLE_HCI_BASE + 0x1c)
#define VBLE_TX_DESC  (VBLE_HCI_BASE + 0x20)
#define VBLE_RX_DESC  (VBLE_HCI_BASE + 0x24)
#define VBLE_TX_KICK  (VBLE_HCI_BASE + 0x28)
#define VBLE_RX_KICK  (VBLE_HCI_BASE + 0x2c)

#define VBLE_ID_VALUE       0x424c4548u
#define VBLE_CONTROL_ENABLE (1u << 0)
#define VBLE_CONTROL_RESET  (1u << 1)
#define VBLE_INT_ALL         0x7u
#define VBLE_DESC_OWN        (1u << 31)
#define VBLE_FRAME_MAX       1536u

typedef struct __attribute__((aligned(16))) {
    uint32_t buffer;
    uint32_t length;
    uint32_t next;
    volatile uint32_t flags;
} vble_desc_t;

static DRAM_ATTR uint8_t s_tx_buffer[VBLE_FRAME_MAX];
static DRAM_ATTR uint8_t s_rx_buffer[VBLE_FRAME_MAX];
static DRAM_ATTR vble_desc_t s_tx_desc __attribute__((aligned(16)));
static DRAM_ATTR vble_desc_t s_rx_desc __attribute__((aligned(16)));
static const esp_vhci_host_callback_t *s_callback;
static SemaphoreHandle_t s_tx_lock;
static TaskHandle_t s_rx_task;
static bool s_started;

esp_err_t __wrap_esp_bt_controller_init(esp_bt_controller_config_t *cfg)
{
    (void)cfg;
    return ESP_OK;
}

esp_err_t __wrap_esp_bt_controller_enable(esp_bt_mode_t mode)
{
    (void)mode;
    return ESP_OK;
}

static void post_rx(void)
{
    s_rx_desc.buffer = (uint32_t)(uintptr_t)s_rx_buffer;
    s_rx_desc.length = sizeof(s_rx_buffer);
    s_rx_desc.next = 0;
    __sync_synchronize();
    s_rx_desc.flags = VBLE_DESC_OWN;
    __sync_synchronize();
    REG_WRITE(VBLE_RX_DESC, (uint32_t)(uintptr_t)&s_rx_desc);
    REG_WRITE(VBLE_RX_KICK, 1);
}

static void rx_task(void *arg)
{
    (void)arg;
    for (;;) {
        __sync_synchronize();
        if (!(s_rx_desc.flags & VBLE_DESC_OWN)) {
            const uint32_t length = s_rx_desc.length;
            if (length && length <= sizeof(s_rx_buffer) && s_callback &&
                s_callback->notify_host_recv) {
                s_callback->notify_host_recv(s_rx_buffer, (uint16_t)length);
            }
            REG_WRITE(VBLE_INT_CLR, VBLE_INT_ALL);
            post_rx();
        }
        vTaskDelay(1);
    }
}

esp_err_t virtual_vhci_start(void)
{
    if (s_started) return ESP_OK;
    if (REG_READ(VBLE_ID) != VBLE_ID_VALUE) return ESP_ERR_NOT_FOUND;

    s_tx_lock = xSemaphoreCreateMutex();
    if (!s_tx_lock) return ESP_ERR_NO_MEM;

    REG_WRITE(VBLE_CONTROL, VBLE_CONTROL_RESET);
    REG_WRITE(VBLE_INT_ENA, VBLE_INT_ALL);
    REG_WRITE(VBLE_CONTROL, VBLE_CONTROL_ENABLE);
    post_rx();
    if (xTaskCreate(rx_task, "vble_rx", 3072, NULL, 10, &s_rx_task) != pdPASS)
        return ESP_ERR_NO_MEM;
    s_started = true;
    return ESP_OK;
}

esp_err_t __wrap_esp_vhci_host_register_callback(
    const esp_vhci_host_callback_t *callback)
{
    if (!callback) return ESP_ERR_INVALID_ARG;
    s_callback = callback;
    return virtual_vhci_start();
}

bool __wrap_esp_vhci_host_check_send_available(void)
{
    return s_started && !(s_tx_desc.flags & VBLE_DESC_OWN);
}

void __wrap_esp_vhci_host_send_packet(uint8_t *data, uint16_t len)
{
    if (!data || !len || len > sizeof(s_tx_buffer) || !s_started) return;
    if (xSemaphoreTake(s_tx_lock, pdMS_TO_TICKS(2000)) != pdTRUE) return;

    memcpy(s_tx_buffer, data, len);
    s_tx_desc.buffer = (uint32_t)(uintptr_t)s_tx_buffer;
    s_tx_desc.length = len;
    s_tx_desc.next = 0;
    __sync_synchronize();
    s_tx_desc.flags = VBLE_DESC_OWN;
    __sync_synchronize();
    REG_WRITE(VBLE_TX_DESC, (uint32_t)(uintptr_t)&s_tx_desc);
    REG_WRITE(VBLE_TX_KICK, 1);

    for (unsigned attempt = 0; attempt < 2000; ++attempt) {
        __sync_synchronize();
        if (!(s_tx_desc.flags & VBLE_DESC_OWN)) break;
        vTaskDelay(1);
    }
    REG_WRITE(VBLE_INT_CLR, VBLE_INT_ALL);
    xSemaphoreGive(s_tx_lock);
    if (s_callback && s_callback->notify_host_send_available)
        s_callback->notify_host_send_available();
}

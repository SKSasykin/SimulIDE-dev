#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"

static const char *TAG = "max31855";

/* VSPI default pins routed via GPIO Matrix — any free GPIOs would work. */
#define PIN_SCK  18
#define PIN_MISO 19
#define PIN_MOSI 23
#define PIN_CS    5

static void decode_and_log(const uint8_t *rx)
{
    uint32_t w = ((uint32_t)rx[0] << 24) | ((uint32_t)rx[1] << 16) |
                 ((uint32_t)rx[2] << 8)  |  (uint32_t)rx[3];

    bool fault = (w >> 16) & 1;
    bool scv   = (w >> 2) & 1;
    bool scg   = (w >> 1) & 1;
    bool oc    =  w       & 1;

    if (fault) {
        if (oc)  ESP_LOGW(TAG, "MAX31855 fault: open circuit");
        if (scg) ESP_LOGW(TAG, "MAX31855 fault: short to GND");
        if (scv) ESP_LOGW(TAG, "MAX31855 fault: short to VCC");
        if (!oc && !scg && !scv) ESP_LOGW(TAG, "MAX31855 fault: unknown (raw=0x%08lX)", (unsigned long)w);
    }

    int16_t tc_raw = (int16_t)((w >> 18) & 0x3FFF);
    if (tc_raw & 0x2000) tc_raw |= 0xC000; /* sign-extend 14 bit */
    float tc = tc_raw * 0.25f;

    int16_t cj_raw = (int16_t)((w >> 4) & 0x0FFF);
    if (cj_raw & 0x0800) cj_raw |= 0xF000; /* sign-extend 12 bit */
    float cj = cj_raw * 0.0625f;

    ESP_LOGI(TAG, "MAX31855: TC=%.2f C  CJ=%.2f C  raw=0x%08lX%s",
             tc, cj, (unsigned long)w, fault ? " FAULT" : "");
}

void app_main(void)
{
    ESP_LOGI(TAG, "MAX31855 SPI example — SCK=%d MISO=%d MOSI=%d CS=%d",
             PIN_SCK, PIN_MISO, PIN_MOSI, PIN_CS);

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_MOSI,
        .miso_io_num = PIN_MISO,
        .sclk_io_num = PIN_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_DISABLED));

    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = 500 * 1000, /* MAX31855 up to 5 MHz */
        .mode = 0,                    /* CPOL=0 CPHA=0 */
        .spics_io_num = PIN_CS,
        .queue_size = 1,
        .command_bits = 0,
        .address_bits = 0,
        .dummy_bits = 0,
    };
    spi_device_handle_t spi;
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &dev_cfg, &spi));

    uint8_t rx_buf[4];
    spi_transaction_t t = {
        .length = 32,
        .rxlength = 32,
        .tx_buffer = NULL,
        .rx_buffer = rx_buf,
        .flags = 0,
    };

    while (1) {
        memset(rx_buf, 0, sizeof(rx_buf));
        /* NOTE: SimulIDE QEMU emulation does not deliver SPI interrupts to the
         * guest, so the interrupt-driven spi_device_transmit() would block
         * forever. Use the polling API, which only polls CMD.USR. */
        esp_err_t ret = spi_device_polling_transmit(spi, &t);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "SPI transmit failed: %s", esp_err_to_name(ret));
        } else {
            decode_and_log(rx_buf);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

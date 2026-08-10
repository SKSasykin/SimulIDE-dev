/*
 * SimulIDE default ESP blink firmware source.
 *
 * The bundled ESP32/ESP32-S3/ESP32-C3 images are ESP-IDF flash images which
 * blink GPIO2 using gpio_set_level() and vTaskDelay(). The bundled ESP8266
 * image is a minimal raw Xtensa binary; the SIMULIDE_ESP8266_RAW section below
 * is the C equivalent of that algorithm, but the shipped 82-byte binary was
 * produced from straight-line assembly to avoid literal pools and calls.
 */

#ifdef SIMULIDE_ESP8266_RAW

#include <stdint.h>

#define GPIO2_MASK (1u << 2)

static inline uint32_t ccount( void )
{
    uint32_t value;
    __asm__ volatile ( "rsr.ccount %0" : "=a" ( value ) );
    return value;
}

static void delay_cycles( uint32_t cycles )
{
    const uint32_t deadline = ccount() + cycles;
    while ( ccount() < deadline ) {}
}

void call_user_start( void )
{
    volatile uint32_t* const gpio_enable = (volatile uint32_t*)0x6000030C;
    volatile uint32_t* const gpio_out = (volatile uint32_t*)0x60000300;
    volatile uint32_t* const gpio_out_w1tc = (volatile uint32_t*)0x60000308;
    const uint32_t delay = (1u << 22) - (1u << 19);

    *gpio_enable |= GPIO2_MASK;

    for (;;) {
        *gpio_out = GPIO2_MASK;
        delay_cycles( delay );
        *gpio_out_w1tc = GPIO2_MASK;
        delay_cycles( delay );
    }
}

#else

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define BLINK_GPIO GPIO_NUM_2
#define BLINK_DELAY_MS 250

void app_main( void )
{
    gpio_reset_pin( BLINK_GPIO );
    gpio_set_direction( BLINK_GPIO, GPIO_MODE_OUTPUT );

    for (;;) {
        gpio_set_level( BLINK_GPIO, 1 );
        vTaskDelay( pdMS_TO_TICKS( BLINK_DELAY_MS ) );
        gpio_set_level( BLINK_GPIO, 0 );
        vTaskDelay( pdMS_TO_TICKS( BLINK_DELAY_MS ) );
    }
}

#endif

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include "virtual_wifi.h"

/* Blink-style LED output. Adjust for boards that do not use GPIO2. */
#define LED_GPIO 2

static const char *TAG = "wifi_http";

static void blink_task(void *arg)
{
    (void)arg;
    gpio_reset_pin(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
    while (1) {
        gpio_set_level(LED_GPIO, 1);
        vTaskDelay(pdMS_TO_TICKS(500));
        gpio_set_level(LED_GPIO, 0);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

static esp_err_t hello_get_handler(httpd_req_t *req)
{
    const char *resp = "Hello World!\n";
    httpd_resp_send(req, resp, strlen(resp));
    return ESP_OK;
}

static httpd_handle_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t uri = {
            .uri      = "/",
            .method   = HTTP_GET,
            .handler  = hello_get_handler,
            .user_ctx = NULL,
        };
        httpd_register_uri_handler(server, &uri);
        ESP_LOGI(TAG, "HTTP server started; host URL: http://127.0.0.1:8080/");
    }
    return server;
}

static void ip_event_handler(void *arg, esp_event_base_t base,
                             int32_t id, void *data)
{
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = data;
        ESP_LOGI(TAG, "virtual WiFi got IP: " IPSTR,
                 IP2STR(&event->ip_info.ip));
    }
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &ip_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(virtual_wifi_start(NULL));
    start_webserver();

    xTaskCreate(blink_task, "blink", 2048, NULL, 5, NULL);
}

#include <assert.h>
#include <string.h>

#include "esp_log.h"
#include "esp_idf_version.h"
#include "esp_nimble_hci.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs_flash.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "virtual_vhci.h"

static const char *TAG = "ble_gatt_central";
static const uint8_t kTestByte = 0x19;

static const ble_uuid128_t s_service_uuid = BLE_UUID128_INIT(
    0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe,
    0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe);
static const ble_uuid128_t s_characteristic_uuid = BLE_UUID128_INIT(
    0x11, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe,
    0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe);
static const ble_uuid16_t s_cccd_uuid = BLE_UUID16_INIT(0x2902);

static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_svc_start;
static uint16_t s_svc_end;
static uint16_t s_value_handle;
static uint16_t s_cccd_handle;
static bool s_notified;
static bool s_done;

static int gap_event(struct ble_gap_event *event, void *arg);

static int on_dsc_discovered(uint16_t conn_handle,
                             const struct ble_gatt_error *error,
                             uint16_t chr_val_handle,
                             const struct ble_gatt_dsc *dsc,
                             void *arg);
static int on_chr_discovered(uint16_t conn_handle,
                             const struct ble_gatt_error *error,
                             const struct ble_gatt_chr *chr,
                             void *arg);
static int on_svc_discovered(uint16_t conn_handle,
                             const struct ble_gatt_error *error,
                             const struct ble_gatt_svc *svc,
                             void *arg);
static int on_attr_op(uint16_t conn_handle,
                      const struct ble_gatt_error *error,
                      struct ble_gatt_attr *attr,
                      void *arg);

static void finish(bool ok)
{
    if (s_done) return;
    s_done = true;
    if (ok) ESP_LOGI(TAG, "BLE_GATT_E2E_PASS byte=0x%02x", kTestByte);
    else ESP_LOGE(TAG, "BLE_GATT_E2E_FAIL");
}

static int on_attr_op(uint16_t conn_handle,
                      const struct ble_gatt_error *error,
                      struct ble_gatt_attr *attr,
                      void *arg)
{
    int step = (int)(intptr_t)arg;
    if (error->status != 0) {
        ESP_LOGE(TAG, "attr op step %d failed status=%d", step, error->status);
        finish(false);
        return 0;
    }
    if (step == 1) {
        int rc = ble_gattc_write_flat(conn_handle, s_value_handle,
                                      &kTestByte, 1, on_attr_op,
                                      (void *)(intptr_t)2);
        if (rc != 0) finish(false);
    } else if (step == 2) {
        int rc = ble_gattc_read(conn_handle, s_value_handle,
                                on_attr_op, (void *)(intptr_t)3);
        if (rc != 0) finish(false);
    } else if (step == 3) {
        if (attr && attr->om && OS_MBUF_PKTLEN(attr->om) == 1) {
            uint8_t value = 0;
            if (ble_hs_mbuf_to_flat(attr->om, &value, 1, NULL) == 0 &&
                value == kTestByte && s_notified) {
                finish(true);
                return 0;
            }
        }
        ESP_LOGE(TAG, "readback mismatch");
        finish(false);
    }
    return 0;
}

static int on_dsc_discovered(uint16_t conn_handle,
                             const struct ble_gatt_error *error,
                             uint16_t chr_val_handle,
                             const struct ble_gatt_dsc *dsc,
                             void *arg)
{
    (void)chr_val_handle;
    (void)arg;
    if (error->status == BLE_HS_EDONE) {
        if (s_cccd_handle == 0) {
            ESP_LOGE(TAG, "CCCD not found");
            finish(false);
            return 0;
        }
        int rc = ble_gattc_write_flat(conn_handle, s_cccd_handle,
                                      (const uint8_t[]){0x01, 0x00}, 2,
                                      on_attr_op, (void *)(intptr_t)1);
        if (rc != 0) finish(false);
        return 0;
    }
    if (error->status != 0) {
        ESP_LOGE(TAG, "dsc discovery failed status=%d", error->status);
        finish(false);
        return 0;
    }
    if (dsc && ble_uuid_cmp(&dsc->uuid.u, &s_cccd_uuid.u) == 0)
        s_cccd_handle = dsc->handle;
    return 0;
}

static int on_chr_discovered(uint16_t conn_handle,
                             const struct ble_gatt_error *error,
                             const struct ble_gatt_chr *chr,
                             void *arg)
{
    (void)arg;
    if (error->status == BLE_HS_EDONE) {
        if (s_value_handle == 0) {
            ESP_LOGE(TAG, "characteristic not found");
            finish(false);
            return 0;
        }
        int rc = ble_gattc_disc_all_dscs(conn_handle, s_value_handle,
                                           s_svc_end,
                                           on_dsc_discovered, NULL);
        if (rc != 0) finish(false);
        return 0;
    }
    if (error->status != 0) {
        ESP_LOGE(TAG, "chr discovery failed status=%d", error->status);
        finish(false);
        return 0;
    }
    if (chr && ble_uuid_cmp(&chr->uuid.u, &s_characteristic_uuid.u) == 0)
        s_value_handle = chr->val_handle;
    return 0;
}

static int on_svc_discovered(uint16_t conn_handle,
                             const struct ble_gatt_error *error,
                             const struct ble_gatt_svc *svc,
                             void *arg)
{
    (void)arg;
    if (error->status == BLE_HS_EDONE) {
        if (s_svc_start == 0) {
            ESP_LOGE(TAG, "service not found");
            finish(false);
            return 0;
        }
        int rc = ble_gattc_disc_chrs_by_uuid(conn_handle, s_svc_start,
                                             s_svc_end,
                                             &s_characteristic_uuid.u,
                                             on_chr_discovered, NULL);
        if (rc != 0) finish(false);
        return 0;
    }
    if (error->status != 0) {
        ESP_LOGE(TAG, "svc discovery failed status=%d", error->status);
        finish(false);
        return 0;
    }
    if (svc && ble_uuid_cmp(&svc->uuid.u, &s_service_uuid.u) == 0) {
        s_svc_start = svc->start_handle;
        s_svc_end = svc->end_handle;
        return 0;
    }
    return 0;
}

static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_DISC: {
        if (event->disc.event_type != BLE_HCI_ADV_RPT_EVTYPE_ADV_IND &&
            event->disc.event_type != BLE_HCI_ADV_RPT_EVTYPE_SCAN_IND)
            return 0;
        ble_gap_disc_cancel();
        int rc = ble_gap_connect(BLE_OWN_ADDR_PUBLIC, &event->disc.addr,
                                 BLE_HS_FOREVER, NULL, gap_event, NULL);
        if (rc != 0) {
            ESP_LOGE(TAG, "connect failed rc=%d", rc);
            finish(false);
        }
        return 0;
    }
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status != 0) {
            ESP_LOGE(TAG, "connect failed status=%d", event->connect.status);
            finish(false);
            return 0;
        }
        s_conn_handle = event->connect.conn_handle;
        {
            int rc = ble_gattc_disc_svc_by_uuid(s_conn_handle,
                                                &s_service_uuid.u,
                                                on_svc_discovered, NULL);
            if (rc != 0) finish(false);
        }
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGE(TAG, "disconnected reason=%d", event->disconnect.reason);
        finish(false);
        return 0;
    case BLE_GAP_EVENT_NOTIFY_RX: {
        if (event->notify_rx.attr_handle == s_value_handle &&
            event->notify_rx.om &&
            OS_MBUF_PKTLEN(event->notify_rx.om) == 1) {
            uint8_t value = 0;
            if (ble_hs_mbuf_to_flat(event->notify_rx.om, &value, 1, NULL) == 0 &&
                value == kTestByte) {
                s_notified = true;
            }
        }
        return 0;
    }
    default:
        return 0;
    }
}

static void on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    assert(rc == 0);
    struct ble_gap_disc_params params = {0};
    params.filter_duplicates = 1;
    rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, BLE_HS_FOREVER, &params,
                      gap_event, NULL);
    assert(rc == 0);
    ESP_LOGI(TAG, "BLE_GATT_CENTRAL_SCANNING");
}

static void host_task(void *arg)
{
    (void)arg;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(virtual_vhci_start());
#if ESP_IDF_VERSION_MAJOR < 5
    ESP_ERROR_CHECK(esp_nimble_hci_init());
    nimble_port_init();
#else
    ESP_ERROR_CHECK(nimble_port_init());
#endif
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_hs_cfg.sync_cb = on_sync;
    nimble_port_freertos_init(host_task);
}

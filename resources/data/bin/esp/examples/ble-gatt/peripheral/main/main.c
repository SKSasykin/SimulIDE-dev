#include <assert.h>
#include <string.h>

#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_nimble_hci.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs_flash.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "virtual_vhci.h"

static const char *TAG = "ble_gatt_peripheral";
static uint8_t s_value = 0x01;
static uint16_t s_value_handle;

static const ble_uuid128_t s_service_uuid = BLE_UUID128_INIT(
    0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe,
    0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe);
static const ble_uuid128_t s_characteristic_uuid = BLE_UUID128_INIT(
    0x11, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe,
    0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe);

static int access_value(uint16_t conn_handle, uint16_t attr_handle,
                        struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        ESP_LOGI(TAG, "Characteristic read: 0x%02x", s_value);
        return os_mbuf_append(ctxt->om, &s_value, sizeof(s_value));
    }
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        if (OS_MBUF_PKTLEN(ctxt->om) != 1) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        if (ble_hs_mbuf_to_flat(ctxt->om, &s_value, sizeof(s_value), NULL))
            return BLE_ATT_ERR_UNLIKELY;
        ESP_LOGI(TAG, "Characteristic written: 0x%02x; sending notification", s_value);
        ble_gatts_chr_updated(s_value_handle);
        return 0;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static const struct ble_gatt_svc_def s_services[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_service_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {{
            .uuid = &s_characteristic_uuid.u,
            .access_cb = access_value,
            .val_handle = &s_value_handle,
            .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE |
                     BLE_GATT_CHR_F_NOTIFY,
        }, {0}},
    },
    {0},
};

static int gap_event(struct ble_gap_event *event, void *arg);

static void advertise(void)
{
    struct ble_hs_adv_fields fields = {0};
    struct ble_gap_adv_params params = {0};
    uint8_t own_addr_type;
    int rc = ble_hs_id_infer_auto(0, &own_addr_type);
    assert(rc == 0);

    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128 = (ble_uuid128_t *)&s_service_uuid;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;
    rc = ble_gap_adv_set_fields(&fields);
    assert(rc == 0);

    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &params,
                           gap_event, NULL);
    assert(rc == 0);
    ESP_LOGI(TAG, "BLE GATT peripheral started and advertising");
    ESP_LOGI(TAG, "BLE_GATT_PERIPHERAL_READY");
}

static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    if (event->type == BLE_GAP_EVENT_CONNECT && event->connect.status == 0)
        ESP_LOGI(TAG, "Central connected");
    if (event->type == BLE_GAP_EVENT_DISCONNECT) {
        ESP_LOGI(TAG, "Central disconnected; restarting advertising");
        advertise();
    }
    return 0;
}

static void on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    assert(rc == 0);
    advertise();
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
    ble_svc_gap_device_name_set("SimulIDE GATT Demo");
    assert(ble_gatts_count_cfg(s_services) == 0);
    assert(ble_gatts_add_svcs(s_services) == 0);
    ble_hs_cfg.sync_cb = on_sync;
    nimble_port_freertos_init(host_task);
}

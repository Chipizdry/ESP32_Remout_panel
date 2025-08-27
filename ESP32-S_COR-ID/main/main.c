


#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/ble_gatt.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "HID_BLE";

static void ble_app_on_sync(void);

/* HID Report Map for Keyboard + Mouse */
static const uint8_t hid_report_map[] = {
    // Keyboard (Report ID 1)
    0x05,0x01,0x09,0x06,0xA1,0x01,0x85,0x01,
    0x05,0x07,0x19,0xE0,0x29,0xE7,0x15,0x00,
    0x25,0x01,0x75,0x01,0x95,0x08,0x81,0x02,
    0x95,0x01,0x75,0x08,0x81,0x01,
    // Mouse (Report ID 2)
    0x05,0x01,0x09,0x02,0xA1,0x01,0x85,0x02,
    0x09,0x01,0xA1,0x00,0x05,0x09,0x19,0x01,
    0x29,0x03,0x15,0x00,0x25,0x01,0x95,0x03,
    0x75,0x01,0x81,0x02,0x95,0x01,0x75,0x05,
    0x81,0x01,0x05,0x01,0x09,0x30,0x09,0x31,
    0x09,0x38,0x15,0x81,0x25,0x7F,0x75,0x08,
    0x95,0x03,0x81,0x06,0xC0,0xC0
};

/* Buffers */
static uint8_t keyboard_report[8] = {0};
static uint8_t mouse_report[4] = {0};
static uint8_t battery_level = 100;
static uint8_t hid_protocol_mode = 1; // Report Protocol
static uint8_t hid_control_point = 0;

/* CCCD */
static uint16_t kbd_cccd = 0;
static uint16_t mouse_cccd = 0;

/* Report Reference */
static const uint8_t kbd_report_ref[] = {0x01,0x01};
static const uint8_t mouse_report_ref[] = {0x02,0x01};

/* Access Callbacks */
static int battery_cb(uint16_t conn_handle, uint16_t attr_handle,
                      struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    os_mbuf_append(ctxt->om, &battery_level, sizeof(battery_level));
    return 0;
}

static int keyboard_cb(uint16_t conn_handle, uint16_t attr_handle,
                       struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    os_mbuf_append(ctxt->om, keyboard_report, sizeof(keyboard_report));
    return 0;
}

static int mouse_cb(uint16_t conn_handle, uint16_t attr_handle,
                    struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    os_mbuf_append(ctxt->om, mouse_report, sizeof(mouse_report));
    return 0;
}

static int hid_info_cb(uint16_t conn_handle, uint16_t attr_handle,
                       struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    uint8_t hid_info[] = {0x11,0x01,0x00,0x02};
    os_mbuf_append(ctxt->om, hid_info, sizeof(hid_info));
    return 0;
}

static int hid_ctrl_cb(uint16_t conn_handle, uint16_t attr_handle,
                       struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    os_mbuf_append(ctxt->om, &hid_control_point, sizeof(hid_control_point));
    return 0;
}

static int hid_protocol_cb(uint16_t conn_handle, uint16_t attr_handle,
                           struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    os_mbuf_append(ctxt->om, &hid_protocol_mode, sizeof(hid_protocol_mode));
    return 0;
}

static int hid_report_map_cb(uint16_t conn_handle, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    os_mbuf_append(ctxt->om, hid_report_map, sizeof(hid_report_map));
    return 0;
}

static int report_ref_cb(uint16_t conn_handle, uint16_t attr_handle,
                         struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    const uint8_t *data = arg;
    os_mbuf_append(ctxt->om, data, 2);
    return 0;
}

static int cccd_cb(uint16_t conn_handle, uint16_t attr_handle,
                   struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    uint16_t *cccd = (uint16_t*)arg;
    if(ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        os_mbuf_append(ctxt->om, cccd, sizeof(uint16_t));
    } else if(ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        uint16_t val;
        os_mbuf_copydata(ctxt->om, 0, sizeof(uint16_t), (uint8_t*)&val);
        *cccd = val;
    }
    return 0;
}

/* GATT Services */
static const struct ble_gatt_svc_def gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x180F),
        .characteristics = (struct ble_gatt_chr_def[]){
            {
                .uuid = BLE_UUID16_DECLARE(0x2A19),
                .access_cb = battery_cb,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY
            },
            {0}
        }
    },
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x1812),
        .characteristics = (struct ble_gatt_chr_def[]){
            { .uuid = BLE_UUID16_DECLARE(0x2A4A), .access_cb = hid_info_cb, .flags = BLE_GATT_CHR_F_READ },
            { .uuid = BLE_UUID16_DECLARE(0x2A4C), .access_cb = hid_ctrl_cb, .flags = BLE_GATT_CHR_F_WRITE_NO_RSP },
            { .uuid = BLE_UUID16_DECLARE(0x2A4E), .access_cb = hid_protocol_cb, .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE_NO_RSP },
            { .uuid = BLE_UUID16_DECLARE(0x2A4B), .access_cb = hid_report_map_cb, .flags = BLE_GATT_CHR_F_READ },
            { .uuid = BLE_UUID16_DECLARE(0x2A4D), .access_cb = keyboard_cb,
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
              .descriptors = (struct ble_gatt_dsc_def[]){
                  { .uuid = BLE_UUID16_DECLARE(0x2908), .att_flags = BLE_ATT_F_READ,
                    .access_cb = report_ref_cb, .arg = (void*)kbd_report_ref },
                  { .uuid = BLE_UUID16_DECLARE(0x2902), .att_flags = BLE_ATT_F_READ | BLE_ATT_F_WRITE,
                    .access_cb = cccd_cb, .arg = (void*)&kbd_cccd },
                  {0}
              }
            },
            { .uuid = BLE_UUID16_DECLARE(0x2A4D), .access_cb = mouse_cb,
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
              .descriptors = (struct ble_gatt_dsc_def[]){
                  { .uuid = BLE_UUID16_DECLARE(0x2908), .att_flags = BLE_ATT_F_READ,
                    .access_cb = report_ref_cb, .arg = (void*)mouse_report_ref },
                  { .uuid = BLE_UUID16_DECLARE(0x2902), .att_flags = BLE_ATT_F_READ | BLE_ATT_F_WRITE,
                    .access_cb = cccd_cb, .arg = (void*)&mouse_cccd },
                  {0}
              }
            },
            {0}
        }
    },
    {0}
};

/* GAP Event */
static int gap_event_cb(struct ble_gap_event *event, void *arg)
{
    switch(event->type) {
        case BLE_GAP_EVENT_CONNECT:
            if(event->connect.status == 0) ESP_LOGI(TAG,"Connected!");
            else ESP_LOGI(TAG,"Connect failed; status=%d", event->connect.status);
            break;
        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI(TAG,"Disconnected; reason=%d", event->disconnect.reason);
            ble_app_on_sync();
            break;
        default: break;
    }
    return 0;
}

/* BLE Init */
static void ble_app_on_sync(void)
{
    int rc;
    const char *dev_name = "ESP32_HID";
    ble_svc_gap_device_name_set(dev_name);

    rc = ble_gatts_count_cfg(gatt_svcs);
    if(rc) { ESP_LOGE(TAG,"ble_gatts_count_cfg failed %d", rc); return; }
    rc = ble_gatts_add_svcs(gatt_svcs);
    if(rc) { ESP_LOGE(TAG,"ble_gatts_add_svcs failed %d", rc); return; }

    uint8_t own_addr_type;
    ble_hs_id_infer_auto(0, &own_addr_type);

    struct ble_hs_adv_fields adv_fields = {0};
    adv_fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    ble_uuid16_t uuids16[] = {BLE_UUID16_INIT(0x1812), BLE_UUID16_INIT(0x180F)};
    adv_fields.uuids16 = uuids16;
    adv_fields.num_uuids16 = 2;
    adv_fields.uuids16_is_complete = 1;

    adv_fields.appearance = 961;

    rc = ble_gap_adv_set_fields(&adv_fields);
    if(rc) { ESP_LOGE(TAG,"ble_gap_adv_set_fields failed %d", rc); return; }

    struct ble_hs_adv_fields scan_rsp_fields = {0};
    scan_rsp_fields.name = (uint8_t*)dev_name;
    scan_rsp_fields.name_len = strlen(dev_name);
    scan_rsp_fields.name_is_complete = 1;
    rc = ble_gap_adv_rsp_set_fields(&scan_rsp_fields);
    if(rc) { ESP_LOGE(TAG,"ble_gap_adv_rsp_set_fields failed %d", rc); return; }

    struct ble_gap_adv_params adv_params = {0};
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    adv_params.itvl_min = 0x60;
    adv_params.itvl_max = 0x60;
    adv_params.channel_map = 7;
    adv_params.filter_policy = 0;

    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &adv_params, gap_event_cb, NULL);
    if(rc) ESP_LOGE(TAG,"ble_gap_adv_start failed %d", rc);

    ESP_LOGI(TAG, "Advertising as HID device started");
}

/* Host task */
static void host_task(void *param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
    vTaskDelete(NULL);
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if(ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND){
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    nimble_port_init();
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_hs_cfg.sync_cb = ble_app_on_sync;

    nimble_port_freertos_init(host_task);
}

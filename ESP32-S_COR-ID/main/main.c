


#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/ble_gatt.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"

static const char *TAG = "nimble_app";

/* ------------------ Handles ------------------ */
static uint16_t battery_level_handle = 0;
static uint16_t hid_input_kbd_handle = 0;
static uint16_t hid_input_mouse_handle = 0;
static uint16_t custom_char_handle = 0;
static uint8_t own_addr_type;
/* ------------------ HID Report Map ------------------ */
static const uint8_t hid_report_map[] = {
    0x05,0x01,0x09,0x06,0xA1,0x01,0x85,0x01,
    0x05,0x07,0x19,0xE0,0x29,0xE7,0x15,0x00,
    0x25,0x01,0x75,0x01,0x95,0x08,0x81,0x02,
    0x95,0x01,0x75,0x08,0x81,0x01,
    0x95,0x05,0x75,0x01,0x05,0x08,0x19,0x01,
    0x29,0x05,0x91,0x02,0x95,0x01,0x75,0x03,
    0x91,0x01,0x95,0x06,0x75,0x08,0x15,0x00,
    0x25,0x65,0x05,0x07,0x19,0x00,0x29,0x65,
    0x81,0x00,0xC0,0x05,0x01,0x09,0x02,0xA1,
    0x01,0x85,0x02,0x09,0x01,0xA1,0x00,0x05,
    0x09,0x19,0x01,0x29,0x03,0x15,0x00,0x25,
    0x01,0x95,0x03,0x75,0x01,0x81,0x02,0x95,
    0x01,0x75,0x05,0x81,0x01,0x05,0x01,0x09,
    0x30,0x09,0x31,0x09,0x38,0x15,0x81,0x25,
    0x7F,0x75,0x08,0x95,0x03,0x81,0x06,0xC0,0xC0
};

/*

static int simple_access_cb(uint16_t conn_handle, uint16_t attr_handle,
    struct ble_gatt_access_ctxt *ctxt, void *arg)
{
const char *resp = "hello";
if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
os_mbuf_append(ctxt->om, resp, strlen(resp));
return 0;
} else if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
// просто принять запись
return 0;
}
return BLE_ATT_ERR_UNLIKELY;
}

*/

/* ------------------ Custom Characteristic callback ------------------ */
static int custom_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                            struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;

    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        os_mbuf_append(ctxt->om, hid_report_map, sizeof(hid_report_map));
        return 0;
    } else if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        struct os_mbuf *om = ctxt->om;
        /* size_t len = OS_MBUF_PKTLEN(om); // можно использовать, если нужно */
        return 0;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

/* ------------------ Battery callback ------------------ */
static int battery_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle; (void)attr_handle; (void)arg;
    uint8_t level = 100;
    os_mbuf_append(ctxt->om, &level, sizeof(level));
    return 0;
}

/* ------------------ HID callbacks ------------------ */
static int hid_report_map_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                                    struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle; (void)attr_handle; (void)arg;
    os_mbuf_append(ctxt->om, hid_report_map, sizeof(hid_report_map));
    return 0;
}

static int hid_kbd_input_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                                   struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle; (void)attr_handle; (void)arg;
    uint8_t report[8] = {0};
    os_mbuf_append(ctxt->om, report, sizeof(report));
    return 0;
}

static int hid_mouse_input_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                                     struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle; (void)attr_handle; (void)arg;
    uint8_t report[4] = {0};
    os_mbuf_append(ctxt->om, report, sizeof(report));
    return 0;
}

/* ------------------ GATT services ------------------ */

static const struct ble_gatt_svc_def gatt_svcs[] = {
   // End of primary service list placeholder

    // Battery Service
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x180F),
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = BLE_UUID16_DECLARE(0x2A19),
                .access_cb = battery_access_cb,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &battery_level_handle,
            },
            {0}
        }
    },

    // HID Service
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x1812),
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = BLE_UUID16_DECLARE(0x2A4B), // Report Map
                .access_cb = hid_report_map_access_cb,
                .flags = BLE_GATT_CHR_F_READ,
            },
            {
                .uuid = BLE_UUID16_DECLARE(0x2A4D), // Keyboard Input
                .access_cb = hid_kbd_input_access_cb,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &hid_input_kbd_handle,
            },
            {
                .uuid = BLE_UUID16_DECLARE(0x2A4D), // Mouse Input
                .access_cb = hid_mouse_input_access_cb,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &hid_input_mouse_handle,
            },
            {0}
        }
    },

    // Custom Service
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = (ble_uuid_t *)&(ble_uuid128_t)BLE_UUID128_INIT(
            0xab,0x90,0x78,0x56,0x34,0x12,0x34,0x12,
            0x34,0x12,0x34,0x12,0x78,0x56,0x34,0x12).u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = (ble_uuid_t *)&(ble_uuid128_t)BLE_UUID128_INIT(
                    0x21,0x43,0x65,0x87,0x21,0x43,0x21,0x43,
                    0x21,0x43,0x21,0x43,0x21,0x43,0x87,0xba).u,
                .access_cb = custom_access_cb,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &custom_char_handle,
            },
            {0}
        }
    },
    {0} // End of services
};


/*
static const struct ble_gatt_svc_def gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0xFFF0),
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = BLE_UUID16_DECLARE(0xFFF1),
                .access_cb = simple_access_cb,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_NOTIFY,
            },
            {0}
        }
    },
    {0}
};
*/


/* ------------------ GAP Event Callback ------------------ */
static int
gap_event_cb(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                printf("Connected!\n");
            } else {
                printf("Connection failed; status=%d\n", event->connect.status);
            }
            break;
        case BLE_GAP_EVENT_DISCONNECT:
            printf("Disconnected; reason=%d\n", event->disconnect.reason);
            break;
        case BLE_GAP_EVENT_ADV_COMPLETE:
            printf("Advertisement complete\n");
            break;
        default:
            break;
    }
    return 0;
}


static void ble_app_on_sync(void)
{
    int rc;

    ESP_LOGI(TAG, "ble_app_on_sync: start");

    // Имя устройства
    ble_svc_gap_device_name_set("ESP32_HID");

    // Зарегистрировать свои GATT-сервисы
    rc = ble_gatts_count_cfg(gatt_svcs);
    if (rc) {
        ESP_LOGE(TAG, "ble_gatts_count_cfg failed: %d", rc);
        return;
    }
    rc = ble_gatts_add_svcs(gatt_svcs);
    if (rc) {
        ESP_LOGE(TAG, "ble_gatts_add_svcs failed: %d", rc);
        return;
    }

    // Определить тип адреса
    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc) {
        ESP_LOGE(TAG, "ble_hs_id_infer_auto failed: %d", rc);
        return;
    }

    // Параметры рекламы
    struct ble_gap_adv_params adv_params;
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    // Старт рекламы
    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER,
                           &adv_params, gap_event_cb, NULL);
    if (rc) {
        ESP_LOGE(TAG, "ble_gap_adv_start failed: %d", rc);
        return;
    }

    ESP_LOGI(TAG, "ble_app_on_sync: done, advertising started");
}



static void host_task(void *param)
{
    ESP_LOGI(TAG, "host_task: running nimble_port_run");
    nimble_port_run();                // Блокирующий цикл NimBLE
    nimble_port_freertos_deinit();    // Если когда-нибудь выйдет
    vTaskDelete(NULL);
}

/* ------------------ Main Application ------------------ */
void app_main(void)
{
    // NVS как было
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Инициализация NimBLE
    nimble_port_init();

    // Инициализация стандартных сервисов NimBLE
    ble_svc_gap_init();
    ble_svc_gatt_init();

    // Назначить колбэки стека
    ble_hs_cfg.sync_cb = ble_app_on_sync;

    // **Правильно:** запускаем host_task, а не ble_app_on_sync
    nimble_port_freertos_init(host_task);
}
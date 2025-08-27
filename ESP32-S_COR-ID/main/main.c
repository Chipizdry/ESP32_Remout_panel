


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




// UUIDs
#define UUID_PRIMARY_SERVICE           ESP_GATT_UUID_PRI_SERVICE
#define UUID_CHAR                      ESP_GATT_UUID_CHAR_DECLARE
#define UUID_CHAR_CLIENT_CONFIG        ESP_GATT_UUID_CHAR_CLIENT_CONFIG
/* ------------------ UUIDs ------------------ */
#define UUID_HID_SERVICE         0x1812
#define UUID_BATTERY_SERVICE     0x180F
#define UUID_HID_INFORMATION     0x2A4A
#define UUID_HID_REPORT_MAP      0x2A4B
#define UUID_HID_CONTROL_POINT   0x2A4C
#define UUID_HID_PROTOCOL_MODE   0x2A4E
#define UUID_HID_REPORT          0x2A4D
#define UUID_BATTERY_LEVEL       0x2A19
#define UUID_TELEMETRY_SERVICE   0xFFF0
#define UUID_TELEMETRY_DATA      0xFFF1

static const char *TAG = "nimble_app";



struct report_ref {
    uint8_t id;   // Report ID
    uint8_t type; // 1 = Input, 2 = Output, 3 = Feature
};


/* ------------------ Buffers ------------------ */
static uint8_t hid_kbd_report[8] = {0};
static uint8_t hid_mouse_report[4] = {0};
static uint8_t custom_report[20] = {0};
static uint8_t battery_level = 100;

/* ------------------ HID Constants ------------------ */
static const uint8_t kbd_report_ref[]   = {0x01, 0x01}; // ID 1, Input
static const uint8_t mouse_report_ref[] = {0x02, 0x01}; // ID 2, Input
static const uint8_t hid_info[]         = {0x11, 0x01, 0x00, 0x02}; // ver=1.11, country=0, flags=2
static const uint8_t hid_protocol_mode[]= {1}; // Report Protocol
static const uint8_t hid_control_point[]= {0};
uint8_t adv_data[] = {
    0x02, 0x01, 0x06,       // Flags
    0x03, 0x03, 0x12,0x18, // Complete List of 16-bit Service UUIDs (HID 0x1812)
    0x0F, 0x09, 'E','S','P','3','2','_','H','I','D' // Complete Local Name
};

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

/* ------------------ Custom Characteristic callback ------------------ */

/*
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
       
        return 0;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

*/

/* ------------------ Access Callbacks ------------------ */
static int battery_access_cb(uint16_t conn_handle, uint16_t attr_handle,
    struct ble_gatt_access_ctxt *ctxt, void *arg) {
(void)conn_handle; (void)attr_handle; (void)arg;
os_mbuf_append(ctxt->om, &battery_level, sizeof(battery_level));
return 0;
}

static int custom_access_cb(uint16_t conn_handle, uint16_t attr_handle,
   struct ble_gatt_access_ctxt *ctxt, void *arg) {
(void)conn_handle; (void)attr_handle; (void)arg;

if(ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
os_mbuf_append(ctxt->om, custom_report, sizeof(custom_report));
} else if(ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
struct os_mbuf *om = ctxt->om;
os_mbuf_copydata(om, 0, OS_MBUF_PKTLEN(om), custom_report);
}
return 0;
}

static int hid_info_cb(uint16_t conn_handle, uint16_t attr_handle,
struct ble_gatt_access_ctxt *ctxt, void *arg) {
os_mbuf_append(ctxt->om, hid_info, sizeof(hid_info));
return 0;
}

static int hid_ctrl_point_cb(uint16_t conn_handle, uint16_t attr_handle,
    struct ble_gatt_access_ctxt *ctxt, void *arg) {
os_mbuf_append(ctxt->om, hid_control_point, sizeof(hid_control_point));
return 0;
}

static int hid_protocol_cb(uint16_t conn_handle, uint16_t attr_handle,
  struct ble_gatt_access_ctxt *ctxt, void *arg) {
os_mbuf_append(ctxt->om, hid_protocol_mode, sizeof(hid_protocol_mode));
return 0;
}

static int hid_report_map_cb(uint16_t conn_handle, uint16_t attr_handle,
    struct ble_gatt_access_ctxt *ctxt, void *arg) {
os_mbuf_append(ctxt->om, hid_report_map, sizeof(hid_report_map));
return 0;
}

static int hid_kbd_cb(uint16_t conn_handle, uint16_t attr_handle,
struct ble_gatt_access_ctxt *ctxt, void *arg) {
os_mbuf_append(ctxt->om, hid_kbd_report, sizeof(hid_kbd_report));
return 0;
}

static int hid_mouse_cb(uint16_t conn_handle, uint16_t attr_handle,
struct ble_gatt_access_ctxt *ctxt, void *arg) {
os_mbuf_append(ctxt->om, hid_mouse_report, sizeof(hid_mouse_report));
return 0;
}

static int hid_kbd_report_ref_cb(uint16_t conn_handle, uint16_t attr_handle,
        struct ble_gatt_access_ctxt *ctxt, void *arg) {
os_mbuf_append(ctxt->om, kbd_report_ref, sizeof(kbd_report_ref));
return 0;
}

static int hid_mouse_report_ref_cb(uint16_t conn_handle, uint16_t attr_handle,
          struct ble_gatt_access_ctxt *ctxt, void *arg) {
os_mbuf_append(ctxt->om, mouse_report_ref, sizeof(mouse_report_ref));
return 0;
}



/* ------------------ HID Descriptors ------------------ */
static const struct ble_gatt_dsc_def hid_kbd_report_ref_desc[] = {
    {
        .uuid = BLE_UUID16_DECLARE(0x2904),
        .att_flags = BLE_ATT_F_READ,
        .access_cb  = hid_kbd_report_ref_cb,
    },
    {0}
};

static const struct ble_gatt_dsc_def hid_mouse_report_ref_desc[] = {
    {
        .uuid = BLE_UUID16_DECLARE(0x2904),
        .att_flags = BLE_ATT_F_READ,
        .access_cb  = hid_mouse_report_ref_cb,
    },
    {0}
};




/* ------------------ GATT Services ------------------ */
static const struct ble_gatt_svc_def gatt_svcs[] = {
    // Battery Service
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(UUID_BATTERY_SERVICE),
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = BLE_UUID16_DECLARE(UUID_BATTERY_LEVEL),
                .access_cb = battery_access_cb,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            },
            {0}
        }
    },
    // HID Service
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(UUID_HID_SERVICE),
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = BLE_UUID16_DECLARE(UUID_HID_INFORMATION),
                .access_cb = hid_info_cb,
                .flags = BLE_GATT_CHR_F_READ,
            },
            {
                .uuid = BLE_UUID16_DECLARE(UUID_HID_CONTROL_POINT),
                .access_cb = hid_ctrl_point_cb,
                .flags = BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {
                .uuid = BLE_UUID16_DECLARE(UUID_HID_PROTOCOL_MODE),
                .access_cb = hid_protocol_cb,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {
                .uuid = BLE_UUID16_DECLARE(UUID_HID_REPORT_MAP),
                .access_cb = hid_report_map_cb,
                .flags = BLE_GATT_CHR_F_READ,
            },
            {
                .uuid = BLE_UUID16_DECLARE(UUID_HID_REPORT),
                .access_cb = hid_kbd_cb,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = NULL,
                .descriptors = hid_kbd_report_ref_desc,
            },
            {
                .uuid = BLE_UUID16_DECLARE(UUID_HID_REPORT),
                .access_cb = hid_mouse_cb,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = NULL,
                .descriptors = hid_mouse_report_ref_desc,
            },
            {0}
        }
    },
    // Custom Telemetry Service
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID128_DECLARE(0xFFF0),
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = BLE_UUID128_DECLARE(0xFFF1),
                .access_cb = custom_access_cb,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = NULL,
            },
            {0}
        }
    },
    {0} // End of services
};


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


    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids16 = (ble_uuid16_t[]) { BLE_UUID16_INIT(UUID_HID_SERVICE) };
    fields.num_uuids16 = 1;
    fields.name = (uint8_t*)"ESP32_HID";
    fields.name_len = strlen("ESP32_HID");
    fields.name_is_complete = 1;
    fields.appearance = 961; // HID Keyboard
    ble_gap_adv_set_fields(&fields);


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
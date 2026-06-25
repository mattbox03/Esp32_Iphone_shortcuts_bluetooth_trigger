/*
 * ============================================================
 *   ESP32 BT TRIGGER  -  Classic Bluetooth HID mouse (not BLE)
 * ============================================================
 *
 *   The ESP32 appears to an iPhone as a Classic Bluetooth MOUSE.
 *   It is used purely as a "Bluetooth trigger": the act of CONNECTING
 *   fires an iOS Shortcuts automation set to "When <device> connects".
 *   No movement or click is ever sent.
 *
 *   Why a mouse and not a keyboard?
 *   iOS hides the on-screen keyboard while an HID keyboard is connected.
 *   A pointing device does not, so the on-screen keyboard stays usable.
 *
 *   Auto-reconnect:
 *   With BLE the phone reconnects on its own; with Classic HID the device
 *   must initiate. After a reboot or a dropped link, the ESP32 actively
 *   calls esp_bt_hid_device_connect() to the last paired phone, retrying
 *   periodically until it is back in range.
 *
 *   Build: ESP-IDF (required - the BT HID Device role is disabled in the
 *   precompiled Arduino libraries). See platformio.ini.
 * ============================================================
 */

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_hidd_api.h"

/* ============================================================
 *   >>>  DEVICE NAME  <<<
 *   Change the name shown in the phone's Bluetooth list HERE.
 *   This is the only thing you need to edit to personalize it.
 * ============================================================ */
#define DEVICE_NAME        "ESP32-BT-Trigger"

#define DEVICE_PROVIDER    "ESP32 DIY"
#define RECONNECT_PERIOD_S 8        // seconds between reconnect attempts

static const char *TAG = "BTHID";

// HID report descriptor: a standard MOUSE (3 buttons + X/Y + wheel).
// We never send reports - it only makes the device a pointing device so
// iOS does NOT hide the on-screen keyboard.
static const uint8_t hid_descriptor[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x02,        // Usage (Mouse)
    0xA1, 0x01,        // Collection (Application)
    0x09, 0x01,        //   Usage (Pointer)
    0xA1, 0x00,        //   Collection (Physical)
    0x05, 0x09,        //     Usage Page (Buttons)
    0x19, 0x01,        //     Usage Minimum (1)
    0x29, 0x03,        //     Usage Maximum (3)
    0x15, 0x00,        //     Logical Minimum (0)
    0x25, 0x01,        //     Logical Maximum (1)
    0x95, 0x03,        //     Report Count (3)
    0x75, 0x01,        //     Report Size (1)
    0x81, 0x02,        //     Input (Data,Var,Abs) - 3 buttons
    0x95, 0x01,        //     Report Count (1)
    0x75, 0x05,        //     Report Size (5)
    0x81, 0x03,        //     Input (Const) - padding
    0x05, 0x01,        //     Usage Page (Generic Desktop)
    0x09, 0x30,        //     Usage (X)
    0x09, 0x31,        //     Usage (Y)
    0x09, 0x38,        //     Usage (Wheel)
    0x15, 0x81,        //     Logical Minimum (-127)
    0x25, 0x7F,        //     Logical Maximum (127)
    0x75, 0x08,        //     Report Size (8)
    0x95, 0x03,        //     Report Count (3)
    0x81, 0x06,        //     Input (Data,Var,Rel) - X, Y, wheel
    0xC0,              //   End Collection
    0xC0               // End Collection
};

static esp_hidd_app_param_t app_param;
static esp_hidd_qos_param_t  qos;

static volatile bool s_connected = false;   // are we connected to the phone?
static volatile bool s_have_peer = false;   // do we know an already-paired phone?
static esp_bd_addr_t s_peer_addr;           // address of the last paired phone

// Make the ESP32 visible and connectable (for the first pairing)
static void make_discoverable(void)
{
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
}

// Fetch from NVS the address of an already-paired device.
// Pairings survive a reboot, so this is effectively "the last phone".
static bool load_paired_peer(esp_bd_addr_t out)
{
    int num = esp_bt_gap_get_bond_device_num();
    if (num <= 0) {
        return false;
    }
    if (num > 4) {
        num = 4;
    }
    esp_bd_addr_t list[4];
    if (esp_bt_gap_get_bond_device_list(&num, list) != ESP_OK || num <= 0) {
        return false;
    }
    memcpy(out, list[0], sizeof(esp_bd_addr_t));
    return true;
}

// HID Device profile callback
static void hidd_cb(esp_hidd_cb_event_t event, esp_hidd_cb_param_t *param)
{
    switch (event) {
        case ESP_HIDD_INIT_EVT:
            if (param->init.status == ESP_HIDD_SUCCESS) {
                ESP_LOGI(TAG, "HID init OK -> registering the mouse app");
                esp_bt_hid_device_register_app(&app_param, &qos, &qos);
            } else {
                ESP_LOGE(TAG, "HID init FAILED");
            }
            break;

        case ESP_HIDD_REGISTER_APP_EVT:
            if (param->register_app.status == ESP_HIDD_SUCCESS) {
                ESP_LOGI(TAG, "App registered -> becoming discoverable");
                make_discoverable();
                // If a phone is already paired, try to call it back right away
                if (load_paired_peer(s_peer_addr)) {
                    s_have_peer = true;
                    ESP_LOGI(TAG, "Found a paired phone -> attempting reconnect");
                    esp_bt_hid_device_connect(s_peer_addr);
                }
            }
            break;

        case ESP_HIDD_OPEN_EVT:
            s_connected = true;
            ESP_LOGI(TAG, ">>> PHONE CONNECTED <<<  (the Shortcuts automation can run)");
            break;

        case ESP_HIDD_CLOSE_EVT:
            s_connected = false;
            ESP_LOGI(TAG, "Phone disconnected -> will try to reconnect");
            make_discoverable();
            break;

        default:
            break;
    }
}

// GAP callback: handles pairing
static void gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    switch (event) {
        case ESP_BT_GAP_AUTH_CMPL_EVT:
            if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
                ESP_LOGI(TAG, "Pairing OK");
                // Remember this phone as the reconnect target
                if (load_paired_peer(s_peer_addr)) {
                    s_have_peer = true;
                }
            } else {
                ESP_LOGE(TAG, "Pairing FAILED");
            }
            break;

        case ESP_BT_GAP_CFM_REQ_EVT:
            // Auto-confirm pairing (Just Works, no PIN/code)
            esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
            break;

        default:
            break;
    }
}

extern "C" void app_main(void)
{
    // NVS: bluedroid uses it to store pairing keys
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "Starting %s (Classic Bluetooth MOUSE, not BLE)...", DEVICE_NAME);

    // HID app parameters: a pointing device (mouse)
    app_param.name         = DEVICE_NAME;
    app_param.description   = "BT Pointer";
    app_param.provider      = DEVICE_PROVIDER;
    app_param.subclass      = ESP_HID_CLASS_MIC;   // pointing device (mouse)
    app_param.desc_list     = (uint8_t *)hid_descriptor;
    app_param.desc_list_len = sizeof(hid_descriptor);

    memset(&qos, 0, sizeof(esp_hidd_qos_param_t));

    // Classic Bluetooth only: free the RAM reserved for BLE
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_BLE));

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT));
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());

    esp_bt_gap_register_callback(gap_cb);
    esp_bt_hid_device_register_callback(hidd_cb);
    esp_bt_hid_device_init();

    esp_bt_gap_set_device_name(DEVICE_NAME);

    // Pairing without PIN (Just Works)
    esp_bt_sp_param_t param_type = ESP_BT_SP_IOCAP_MODE;
    esp_bt_io_cap_t   iocap      = ESP_BT_IO_CAP_NONE;
    esp_bt_gap_set_security_param(param_type, &iocap, sizeof(uint8_t));

    ESP_LOGI(TAG, "Ready. Look for '%s' in the phone's Bluetooth settings.", DEVICE_NAME);

    // Loop: status + active reconnect to the last phone while disconnected
    int countdown = 0;
    while (true) {
        if (s_connected) {
            countdown = 0;
        } else if (s_have_peer) {
            if (countdown <= 0) {
                ESP_LOGI(TAG, "Reconnecting: calling the last paired phone...");
                esp_bt_hid_device_connect(s_peer_addr);
                countdown = RECONNECT_PERIOD_S;
            }
            countdown--;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

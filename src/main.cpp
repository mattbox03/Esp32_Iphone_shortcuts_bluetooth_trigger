/*
 * ============================================================
 *   ESP32 BT TRIGGER + FIND MY  -  dual mode (Classic + BLE)
 * ============================================================
 *
 *   Two things at once on one classic ESP32:
 *
 *   1) CLASSIC Bluetooth HID mouse  -> the "trigger": connecting to your
 *      iPhone fires an iOS Shortcuts automation (no input is sent).
 *      Auto-reconnects to the last paired phone.
 *
 *   2) BLE "Find My" beacon (OpenHaystack)  -> broadcasts a public key like
 *      a lost AirTag. Nearby Apple devices anonymously report its encrypted
 *      location to Apple; you fetch & decrypt them yourself (macless-haystack).
 *
 *   IMPORTANT - this is the UNOFFICIAL (reverse-engineered) Find My route.
 *   It is NOT a certified "Works with Find My" accessory. See README.
 *
 *   Build: ESP-IDF, dual mode (BTDM). See platformio.ini / sdkconfig.defaults.
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
#include "esp_gap_ble_api.h"

/* ============================================================
 *   >>>  DEVICE NAME (Classic, shown when pairing)  <<<
 * ============================================================ */

#define DEVICE_NAME        "ESP32-BT-Trigger"
#define DEVICE_PROVIDER    "ESP32 DIY"
#define RECONNECT_PERIOD_S 8

static const char *TAG = "BTHID";


/* ============================================================
 *   >>>  FIND MY PUBLIC KEY (28 bytes, P-224 X coordinate)  <<<
 *
 *   Generate it with tools/generate_keys.py and PASTE it here.
 *   Keep the matching PRIVATE key for macless-haystack (to read the
 *   location). The placeholder below (all zeros) compiles and advertises
 *   but is NOT locatable - you MUST replace it.
 * ============================================================ */

 static uint8_t findmy_public_key[28] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00
};


// ---------- Classic HID (mouse) ----------------------------------------

// Standard 3-button mouse report descriptor (never actually sent).
static const uint8_t hid_descriptor[] = {
    0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x09, 0x01, 0xA1, 0x00,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x03, 0x15, 0x00, 0x25, 0x01,
    0x95, 0x03, 0x75, 0x01, 0x81, 0x02, 0x95, 0x01, 0x75, 0x05,
    0x81, 0x03, 0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x09, 0x38,
    0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x03, 0x81, 0x06,
    0xC0, 0xC0
};

static esp_hidd_app_param_t app_param;
static esp_hidd_qos_param_t  qos;
static volatile bool s_connected = false;
static volatile bool s_have_peer = false;
static esp_bd_addr_t s_peer_addr;

static void make_discoverable(void)
{
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
}

static bool load_paired_peer(esp_bd_addr_t out)
{
    int num = esp_bt_gap_get_bond_device_num();
    if (num <= 0) return false;
    if (num > 4) num = 4;
    esp_bd_addr_t list[4];
    if (esp_bt_gap_get_bond_device_list(&num, list) != ESP_OK || num <= 0) return false;
    memcpy(out, list[0], sizeof(esp_bd_addr_t));
    return true;
}

static void hidd_cb(esp_hidd_cb_event_t event, esp_hidd_cb_param_t *param)
{
    switch (event) {
        case ESP_HIDD_INIT_EVT:
            if (param->init.status == ESP_HIDD_SUCCESS)
                esp_bt_hid_device_register_app(&app_param, &qos, &qos);
            else
                ESP_LOGE(TAG, "HID init FAILED");
            break;
        case ESP_HIDD_REGISTER_APP_EVT:
            if (param->register_app.status == ESP_HIDD_SUCCESS) {
                ESP_LOGI(TAG, "HID app registered -> discoverable");
                make_discoverable();
                if (load_paired_peer(s_peer_addr)) {
                    s_have_peer = true;
                    esp_bt_hid_device_connect(s_peer_addr);
                }
            }
            break;
        case ESP_HIDD_OPEN_EVT:
            s_connected = true;
            ESP_LOGI(TAG, ">>> PHONE CONNECTED <<<  (Shortcuts automation can run)");
            break;
        case ESP_HIDD_CLOSE_EVT:
            s_connected = false;
            ESP_LOGI(TAG, "Phone disconnected -> will retry");
            make_discoverable();
            break;
        default:
            break;
    }
}

static void gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    switch (event) {
        case ESP_BT_GAP_AUTH_CMPL_EVT:
            if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
                ESP_LOGI(TAG, "Pairing OK");
                if (load_paired_peer(s_peer_addr)) s_have_peer = true;
            } else {
                ESP_LOGE(TAG, "Pairing FAILED");
            }
            break;
        case ESP_BT_GAP_CFM_REQ_EVT:
            esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
            break;
        default:
            break;
    }
}

// ---------- BLE Find My beacon (OpenHaystack) --------------------------

// Apple "offline finding" advertisement template (31 bytes). The public key
// is injected into the BLE random address (bytes 0..5) and the payload.
static uint8_t findmy_adv_data[31] = {
    0x1e,             // length of the AD structure (30)
    0xff,             // manufacturer specific data
    0x4c, 0x00,       // company id: Apple (0x004C)
    0x12, 0x19,       // Apple type 0x12 (offline finding), length 0x19 (25)
    0x00,             // status
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  // key bytes 6..27 (filled in)
    0x00,             // public_key[0] >> 6
    0x00              // hint
};

static esp_ble_adv_params_t findmy_adv_params;   // filled in findmy_prepare()

static void findmy_prepare(void)
{
    // Advertising params: non-connectable beacon, random address, ~200 ms
    memset(&findmy_adv_params, 0, sizeof(findmy_adv_params));
    findmy_adv_params.adv_int_min       = 0x0140;
    findmy_adv_params.adv_int_max       = 0x0140;
    findmy_adv_params.adv_type          = ADV_TYPE_NONCONN_IND;
    findmy_adv_params.own_addr_type     = BLE_ADDR_TYPE_RANDOM;
    findmy_adv_params.channel_map       = ADV_CHNL_ALL;
    findmy_adv_params.adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY;

    // BLE random (static) address from the first 6 key bytes; top 2 bits = 11
    esp_bd_addr_t rnd_addr;
    rnd_addr[0] = findmy_public_key[0] | 0xC0;
    rnd_addr[1] = findmy_public_key[1];
    rnd_addr[2] = findmy_public_key[2];
    rnd_addr[3] = findmy_public_key[3];
    rnd_addr[4] = findmy_public_key[4];
    rnd_addr[5] = findmy_public_key[5];

    // payload: key bytes 6..27, then the top 2 bits of key byte 0
    memcpy(&findmy_adv_data[7], &findmy_public_key[6], 22);
    findmy_adv_data[29] = findmy_public_key[0] >> 6;

    ESP_ERROR_CHECK(esp_ble_gap_set_rand_addr(rnd_addr));
    ESP_ERROR_CHECK(esp_ble_gap_config_adv_data_raw(findmy_adv_data, sizeof(findmy_adv_data)));
}

static void ble_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
        case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
            esp_ble_gap_start_advertising(&findmy_adv_params);
            break;
        case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
            if (param->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS)
                ESP_LOGI(TAG, "Find My beacon: advertising");
            else
                ESP_LOGE(TAG, "Find My beacon: advertising FAILED");
            break;
        default:
            break;
    }
}

// ---------- main -------------------------------------------------------

extern "C" void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "Starting %s (dual mode: Classic HID + BLE Find My)...", DEVICE_NAME);

    // Classic HID app (mouse)
    app_param.name         = DEVICE_NAME;
    app_param.description   = "BT Pointer";
    app_param.provider      = DEVICE_PROVIDER;
    app_param.subclass      = ESP_HID_CLASS_MIC;
    app_param.desc_list     = (uint8_t *)hid_descriptor;
    app_param.desc_list_len = sizeof(hid_descriptor);
    memset(&qos, 0, sizeof(esp_hidd_qos_param_t));

    // DUAL MODE: keep BLE memory (do NOT release it) and enable BTDM
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BTDM));
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());

    // Classic side
    esp_bt_gap_register_callback(gap_cb);
    esp_bt_hid_device_register_callback(hidd_cb);
    esp_bt_hid_device_init();
    esp_bt_gap_set_device_name(DEVICE_NAME);

    esp_bt_sp_param_t param_type = ESP_BT_SP_IOCAP_MODE;
    esp_bt_io_cap_t   iocap      = ESP_BT_IO_CAP_NONE;
    esp_bt_gap_set_security_param(param_type, &iocap, sizeof(uint8_t));

    // BLE Find My side
    ESP_ERROR_CHECK(esp_ble_gap_register_callback(ble_gap_cb));
    findmy_prepare();   // advertising starts from the BLE callback

    ESP_LOGI(TAG, "Ready. Classic name '%s'. Find My beacon active.", DEVICE_NAME);

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

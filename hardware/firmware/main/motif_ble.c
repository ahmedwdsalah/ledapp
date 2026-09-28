#include "motif_ble.h"

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_random.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "motif_player.h"
#include "motif_state.h"
#include "motif_wifi.h"

void ble_store_config_init(void);
static uint8_t s_addr_type;
static char s_name[13];
static uint8_t s_provision[98];
static unsigned s_expected, s_received;

static const ble_uuid128_t service_uuid = BLE_UUID128_INIT(0xc7,0x45,0x02,0x1d,0x90,0x31,0xbb,0x83,0x12,0x43,0x01,0xf3,0x09,0x38,0x68,0xc4);
static const ble_uuid128_t info_uuid = BLE_UUID128_INIT(0xd3,0xb9,0xa2,0x68,0x7e,0x12,0x64,0x9f,0xfb,0x4c,0xf3,0xb1,0xce,0xde,0xdd,0x6e);
static const ble_uuid128_t provision_uuid = BLE_UUID128_INIT(0xd7,0x7f,0x04,0x73,0xfe,0xaf,0x19,0xb0,0x68,0x47,0x28,0x3e,0xab,0x6a,0xae,0x3e);
static const ble_uuid128_t token_uuid = BLE_UUID128_INIT(0xaf,0x7b,0xd4,0x01,0x74,0x8a,0xdb,0xa4,0x8b,0x49,0xfa,0xd8,0x42,0x60,0x2d,0x69);

static int access_characteristic(uint16_t connection, uint16_t handle, struct ble_gatt_access_ctxt *ctx, void *arg)
{
    int which = (int)(intptr_t)arg;
    if (which == 1 && ctx->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        uint8_t value[16] = {1};
        motif_state_get_ip(value + 2);
        value[1] = value[2] || value[3] || value[4] || value[5];
        motif_state_mac(value + 6);
        uint32_t max = MOTIF_MAX_GIF_BYTES;
        value[12] = max; value[13] = max >> 8; value[14] = max >> 16; value[15] = max >> 24;
        return os_mbuf_append(ctx->om, value, sizeof(value)) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    if (which == 3 && ctx->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        uint8_t key[16];
        motif_state_token(key);
        return os_mbuf_append(ctx->om, key, sizeof(key)) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    if (which != 2 || ctx->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return BLE_ATT_ERR_UNLIKELY;
    uint8_t chunk[100];
    unsigned length = OS_MBUF_PKTLEN(ctx->om);
    if (length < 1 || length > sizeof(chunk) || ble_hs_mbuf_to_flat(ctx->om, chunk, length, NULL) != 0) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    if (chunk[0] == 1 && length == 3) {
        s_expected = chunk[1] | (chunk[2] << 8);
        s_received = 0;
        return s_expected >= 3 && s_expected <= sizeof(s_provision) ? 0 : BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    if (chunk[0] == 2 && length > 3) {
        unsigned offset = chunk[1] | (chunk[2] << 8);
        unsigned count = length - 3;
        if (!s_expected || offset != s_received || count > s_expected - s_received) return BLE_ATT_ERR_INVALID_OFFSET;
        memcpy(s_provision + offset, chunk + 3, count);
        s_received += count;
        return 0;
    }
    if (chunk[0] == 3 && length == 1 && s_expected && s_received == s_expected) {
        unsigned ssid_len = s_provision[0], pass_len = s_provision[1];
        if (!ssid_len || ssid_len > 32 || pass_len > 63 || ssid_len + pass_len + 2 != s_expected) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        char ssid[33] = {0}, password[65] = {0};
        memcpy(ssid, s_provision + 2, ssid_len);
        memcpy(password, s_provision + 2 + ssid_len, pass_len);
        s_expected = s_received = 0;
        return motif_wifi_provision(ssid, password) == ESP_OK ? 0 : BLE_ATT_ERR_UNLIKELY;
    }
    return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
}

static const struct ble_gatt_svc_def services[] = {{
    .type = BLE_GATT_SVC_TYPE_PRIMARY,
    .uuid = &service_uuid.u,
    .characteristics = (struct ble_gatt_chr_def[]){
        {.uuid = &info_uuid.u, .access_cb = access_characteristic, .arg = (void *)1, .flags = BLE_GATT_CHR_F_READ},
        {.uuid = &provision_uuid.u, .access_cb = access_characteristic, .arg = (void *)2, .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_AUTHEN},
        {.uuid = &token_uuid.u, .access_cb = access_characteristic, .arg = (void *)3, .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_AUTHEN},
        {0},
    },
}, {0}};

static int gap_event(struct ble_gap_event *event, void *arg);

static void advertise(void)
{
    struct ble_hs_adv_fields fields = {0};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128 = (ble_uuid128_t *)&service_uuid;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;
    if (ble_gap_adv_set_fields(&fields) != 0) return;
    struct ble_hs_adv_fields scan = {0};
    scan.name = (uint8_t *)s_name;
    scan.name_len = strlen(s_name);
    scan.name_is_complete = 1;
    ble_gap_adv_rsp_set_fields(&scan);
    struct ble_gap_adv_params params = {.conn_mode = BLE_GAP_CONN_MODE_UND, .disc_mode = BLE_GAP_DISC_MODE_GEN};
    ble_gap_adv_start(s_addr_type, NULL, BLE_HS_FOREVER, &params, gap_event, NULL);
}

static int gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status != 0) advertise();
            return 0;
        case BLE_GAP_EVENT_DISCONNECT:
            s_expected = s_received = 0;
            motif_player_clear_passkey();
            advertise();
            return 0;
        case BLE_GAP_EVENT_PASSKEY_ACTION:
            if (event->passkey.params.action != BLE_SM_IOACT_DISP) return BLE_ATT_ERR_UNLIKELY;
            struct ble_sm_io io = {.action = BLE_SM_IOACT_DISP, .passkey = esp_random() % 1000000};
            motif_player_show_passkey(io.passkey);
            return ble_sm_inject_io(event->passkey.conn_handle, &io);
        case BLE_GAP_EVENT_ENC_CHANGE:
            motif_player_clear_passkey();
            return 0;
        case BLE_GAP_EVENT_ADV_COMPLETE:
            advertise();
            return 0;
        default: return 0;
    }
}

static void on_sync(void)
{
    if (ble_hs_id_infer_auto(0, &s_addr_type) == 0) advertise();
}

static void host_task(void *param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

esp_err_t motif_ble_start(void)
{
    char id[7];
    motif_state_device_id(id);
    snprintf(s_name, sizeof(s_name), "MOTIF-%s", id);
    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) return err;
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_DISP_ONLY;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 1;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_svc_gap_init();
    ble_svc_gatt_init();
    if (ble_gatts_count_cfg(services) != 0 || ble_gatts_add_svcs(services) != 0) return ESP_FAIL;
    ble_svc_gap_device_name_set(s_name);
    ble_store_config_init();
    nimble_port_freertos_init(host_task);
    return ESP_OK;
}

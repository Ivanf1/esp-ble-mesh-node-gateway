#include <stdio.h>
#include <string.h>

#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_config_model_api.h"
#include "esp_ble_mesh_generic_model_api.h"
#include "esp_ble_mesh_networking_api.h"
#include "esp_ble_mesh_provisioning_api.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include <sys/time.h>

#include "ble_mesh_init.h"
#include "ble_mesh_nvs.h"
#include "mqtt_app.h"
#include "mqtt_client.h"
#include "wifi_connect.h"

#include "sdkconfig.h"

#define TAG "GATEWAY"

#define CID_ESP 0x02E5

static uint8_t dev_uuid[16] = {0xdd, 0xdd};

static esp_ble_mesh_client_t onoff_client;

static esp_ble_mesh_cfg_srv_t config_server = {
    .relay = ESP_BLE_MESH_RELAY_DISABLED,
    .beacon = ESP_BLE_MESH_BEACON_ENABLED,
    .friend_state = ESP_BLE_MESH_FRIEND_NOT_SUPPORTED,
    .gatt_proxy = ESP_BLE_MESH_GATT_PROXY_ENABLED,
    .default_ttl = 7,
    /* 3 transmissions with 20ms interval */
    .net_transmit = ESP_BLE_MESH_TRANSMIT(2, 20),
    .relay_retransmit = ESP_BLE_MESH_TRANSMIT(2, 20),
};

ESP_BLE_MESH_MODEL_PUB_DEFINE(onoff_cli_pub, 2 + 1, ROLE_NODE);

static esp_ble_mesh_model_t root_models[] = {
    ESP_BLE_MESH_MODEL_CFG_SRV(&config_server),
    ESP_BLE_MESH_MODEL_GEN_ONOFF_CLI(&onoff_cli_pub, &onoff_client),
};

static esp_ble_mesh_elem_t elements[] = {
    ESP_BLE_MESH_ELEMENT(0, root_models, ESP_BLE_MESH_MODEL_NONE),
};

static esp_ble_mesh_comp_t composition = {
    .cid = CID_ESP,
    .elements = elements,
    .element_count = ARRAY_SIZE(elements),
};

static esp_ble_mesh_prov_t provision = {
    .uuid = dev_uuid,
    // No OOB
    .output_size = 0,
    .output_actions = 0,
};

static void prov_complete(uint16_t net_idx, uint16_t addr, uint8_t flags, uint32_t iv_index) {
  ESP_LOGI(TAG, "net_idx: 0x%04x, addr: 0x%04x", net_idx, addr);
  ESP_LOGI(TAG, "flags: 0x%02x, iv_index: 0x%08x", flags, iv_index);
}

static void ble_mesh_provisioning_cb(esp_ble_mesh_prov_cb_event_t event, esp_ble_mesh_prov_cb_param_t *param) {
  switch (event) {
  case ESP_BLE_MESH_PROV_REGISTER_COMP_EVT:
    ESP_LOGI(TAG, "ESP_BLE_MESH_PROV_REGISTER_COMP_EVT, err_code %d", param->prov_register_comp.err_code);
    break;
  case ESP_BLE_MESH_NODE_PROV_ENABLE_COMP_EVT:
    ESP_LOGI(TAG, "ESP_BLE_MESH_NODE_PROV_ENABLE_COMP_EVT, err_code %d", param->node_prov_enable_comp.err_code);
    break;
  case ESP_BLE_MESH_NODE_PROV_LINK_OPEN_EVT:
    ESP_LOGI(TAG, "ESP_BLE_MESH_NODE_PROV_LINK_OPEN_EVT, bearer %s",
             param->node_prov_link_open.bearer == ESP_BLE_MESH_PROV_ADV ? "PB-ADV" : "PB-GATT");
    break;
  case ESP_BLE_MESH_NODE_PROV_LINK_CLOSE_EVT:
    ESP_LOGI(TAG, "ESP_BLE_MESH_NODE_PROV_LINK_CLOSE_EVT, bearer %s",
             param->node_prov_link_close.bearer == ESP_BLE_MESH_PROV_ADV ? "PB-ADV" : "PB-GATT");
    break;
  case ESP_BLE_MESH_NODE_PROV_COMPLETE_EVT:
    ESP_LOGI(TAG, "ESP_BLE_MESH_NODE_PROV_COMPLETE_EVT");
    prov_complete(param->node_prov_complete.net_idx, param->node_prov_complete.addr, param->node_prov_complete.flags,
                  param->node_prov_complete.iv_index);
    break;
  case ESP_BLE_MESH_NODE_PROV_RESET_EVT:
    break;
  case ESP_BLE_MESH_NODE_SET_UNPROV_DEV_NAME_COMP_EVT:
    ESP_LOGI(TAG, "ESP_BLE_MESH_NODE_SET_UNPROV_DEV_NAME_COMP_EVT, err_code %d",
             param->node_set_unprov_dev_name_comp.err_code);
    break;
  default:
    break;
  }
}

static void ble_mesh_generic_client_cb(esp_ble_mesh_generic_client_cb_event_t event,
                                       esp_ble_mesh_generic_client_cb_param_t *param) {
  ESP_LOGI(TAG, "Generic client, event %u, error code %d, opcode is 0x%04x", event, param->error_code,
           param->params->opcode);

  switch (event) {
  case ESP_BLE_MESH_GENERIC_CLIENT_GET_STATE_EVT:
    ESP_LOGI(TAG, "ESP_BLE_MESH_GENERIC_CLIENT_GET_STATE_EVT");
    if (param->params->opcode == ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_GET) {
      ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_GET, onoff %d", param->status_cb.onoff_status.present_onoff);
    }
    break;
  case ESP_BLE_MESH_GENERIC_CLIENT_SET_STATE_EVT:
    ESP_LOGI(TAG, "ESP_BLE_MESH_GENERIC_CLIENT_SET_STATE_EVT");
    if (param->params->opcode == ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET) {
      ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET, onoff %d", param->status_cb.onoff_status.present_onoff);
    }
    break;
  case ESP_BLE_MESH_GENERIC_CLIENT_PUBLISH_EVT:
    ESP_LOGI(TAG, "ESP_BLE_MESH_GENERIC_CLIENT_PUBLISH_EVT");
    ESP_LOGI(TAG, "addr: %04x, status: %d", param->params->ctx.addr, param->status_cb.onoff_status.present_onoff);
    char topic[16];
    char status[2];
    snprintf(topic, 16, "ble_mesh/%04x", param->params->ctx.addr);
    snprintf(status, 2, "%d", param->status_cb.onoff_status.present_onoff);
    send_mqtt_message(topic, status);
    break;
  case ESP_BLE_MESH_GENERIC_CLIENT_TIMEOUT_EVT:
    break;
  default:
    break;
  }
}

static void ble_mesh_config_server_cb(esp_ble_mesh_cfg_server_cb_event_t event,
                                      esp_ble_mesh_cfg_server_cb_param_t *param) {
  if (event == ESP_BLE_MESH_CFG_SERVER_STATE_CHANGE_EVT) {
    switch (param->ctx.recv_op) {
    case ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD:
      ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD");
      ESP_LOGI(TAG, "net_idx 0x%04x, app_idx 0x%04x", param->value.state_change.appkey_add.net_idx,
               param->value.state_change.appkey_add.app_idx);
      ESP_LOG_BUFFER_HEX("AppKey", param->value.state_change.appkey_add.app_key, 16);
      break;
    case ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND:
      ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND");
      ESP_LOGI(TAG, "elem_addr 0x%04x, app_idx 0x%04x, cid 0x%04x, mod_id 0x%04x",
               param->value.state_change.mod_app_bind.element_addr, param->value.state_change.mod_app_bind.app_idx,
               param->value.state_change.mod_app_bind.company_id, param->value.state_change.mod_app_bind.model_id);
      if (param->value.state_change.mod_app_bind.company_id == 0xFFFF &&
          param->value.state_change.mod_app_bind.model_id == ESP_BLE_MESH_MODEL_ID_GEN_ONOFF_CLI) {
      }
      break;
    case ESP_BLE_MESH_MODEL_OP_MODEL_SUB_ADD:
      ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_MODEL_SUB_ADD");
      ESP_LOGI(TAG, "elem_addr 0x%04x, sub_addr 0x%04x, cid 0x%04x, mod_id 0x%04x",
               param->value.state_change.mod_sub_add.element_addr, param->value.state_change.mod_sub_add.sub_addr,
               param->value.state_change.mod_sub_add.company_id, param->value.state_change.mod_sub_add.model_id);
      break;
    case ESP_BLE_MESH_MODEL_OP_MODEL_SUB_DELETE:
      ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_MODEL_SUB_DELETE");
      ESP_LOGI(TAG, "elem_addr 0x%04x, del_addr 0x%04x, cid 0x%04x, mod_id 0x%04x",
               param->value.state_change.mod_sub_add.element_addr, param->value.state_change.mod_sub_add.sub_addr,
               param->value.state_change.mod_sub_add.company_id, param->value.state_change.mod_sub_add.model_id);

    default:
      break;
    }
  }
}

static esp_err_t ble_mesh_init(void) {
  esp_err_t err = ESP_OK;

  esp_ble_mesh_register_prov_callback(ble_mesh_provisioning_cb);
  esp_ble_mesh_register_generic_client_callback(ble_mesh_generic_client_cb);
  esp_ble_mesh_register_config_server_callback(ble_mesh_config_server_cb);

  err = esp_ble_mesh_init(&provision, &composition);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize mesh stack (err %d)", err);
    return err;
  }

  err = esp_ble_mesh_node_prov_enable(ESP_BLE_MESH_PROV_ADV | ESP_BLE_MESH_PROV_GATT);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to enable mesh node (err %d)", err);
    return err;
  }

  ESP_LOGI(TAG, "BLE Mesh Node initialized");

  return err;
}

void app_main(void) {
  esp_err_t err;

  ESP_LOGI(TAG, "Initializing...");

  err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);

  err = bluetooth_init();
  if (err) {
    ESP_LOGE(TAG, "esp32_bluetooth_init failed (err %d)", err);
    return;
  }

  ble_mesh_get_dev_uuid(dev_uuid);

  /* Initialize the Bluetooth Mesh Subsystem */
  err = ble_mesh_init();
  if (err) {
    ESP_LOGE(TAG, "Bluetooth mesh init failed (err %d)", err);
  }

  wifi_init_sta();
  mqtt_app_start();
}

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "lwip/dns.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"

#include "mqtt_app.h"
#include "mqtt_client.h"
#include "secrets.h"
#include "wifi_connect.h"

static const char *TAG = "MQTT";
esp_mqtt_client_handle_t client;
static EventGroupHandle_t s_mqtt_event_group;

#define MQTT_CONNECTED_BIT BIT0

/**
 * @brief Event handler registered to receive MQTT events
 *
 *  This function is called by the MQTT client event loop.
 *
 * @param handler_args user data registered to the event.
 * @param base Event base for the handler(always MQTT Base in this example).
 * @param event_id The id for the received event.
 * @param event_data The data for the event, esp_mqtt_event_handle_t.
 */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
  ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%d", base, event_id);
  esp_mqtt_event_handle_t event = event_data;
  switch ((esp_mqtt_event_id_t)event_id) {
  case MQTT_EVENT_CONNECTED:
    xEventGroupSetBits(s_mqtt_event_group, MQTT_CONNECTED_BIT);
    ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
    break;
  case MQTT_EVENT_DISCONNECTED:
    ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
    xEventGroupClearBits(s_mqtt_event_group, MQTT_CONNECTED_BIT);
    break;

  case MQTT_EVENT_SUBSCRIBED:
    ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
    break;
  case MQTT_EVENT_UNSUBSCRIBED:
    ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
    break;
  case MQTT_EVENT_PUBLISHED:
    ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
    break;
  case MQTT_EVENT_DATA:
    ESP_LOGI(TAG, "MQTT_EVENT_DATA");
    printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
    printf("DATA=%.*s\r\n", event->data_len, event->data);
    break;
  case MQTT_EVENT_ERROR:
    ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
    break;
  default:
    ESP_LOGI(TAG, "Other event id:%d", event->event_id);
    break;
  }
}

static void mqtt_init(void) {
  esp_mqtt_client_config_t mqtt_cfg = {
      .broker.address.uri = BLE_MESH_PROJ_MQTT_BROKER_URI,
      .credentials.username = BLE_MESH_PROJ_MQTT_BROKER_USERNAME,
      .credentials.authentication.password = BLE_MESH_PROJ_MQTT_BROKER_PASSWORD,
  };

  client = esp_mqtt_client_init(&mqtt_cfg);
  /* The last argument may be used to pass data to the event handler, in this example mqtt_event_handler */
  esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
  esp_mqtt_client_start(client);
}

void on_wifi_status_change(int status) {
  EventBits_t bits = xEventGroupGetBits(s_mqtt_event_group);
  if (status) {
    if (!(bits & MQTT_CONNECTED_BIT)) {
      ESP_LOGI(TAG, "Starting MQTT client");
      mqtt_init();
    }
  } else {
    if (bits & MQTT_CONNECTED_BIT) {
      ESP_LOGI(TAG, "Stopping MQTT client");
      esp_mqtt_client_destroy(client);
    }
  }
}

void send_mqtt_message(const char *topic, const char *data) { esp_mqtt_client_publish(client, topic, data, 0, 1, 0); }

void mqtt_app_start(void) {
  s_mqtt_event_group = xEventGroupCreate();
  mqtt_init();
  wifi_register_on_status_change_callback(on_wifi_status_change);
}
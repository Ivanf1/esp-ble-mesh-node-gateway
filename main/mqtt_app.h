#ifndef _MQTT_APP_H_
#define _MQTT_APP_H_

void mqtt_app_start(void);
void send_mqtt_message(const char *topic, const char *data);

#endif // _MQTT_APP_H_
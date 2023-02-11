#ifndef _WIFI_CONNECT_H_
#define _WIFI_CONNECT_H_

typedef void (*wifi_status_cb_t)(int status);

void wifi_init_sta(void);
int wifi_is_connected(void);
void wifi_register_on_status_change_callback(wifi_status_cb_t callback);

#endif // _WIFI_CONNECT_H_
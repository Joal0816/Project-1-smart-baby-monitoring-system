#ifndef WIFI_NET_H
#define WIFI_NET_H

#include "sensors.h"

void wifi_net_run(void);
void wifi_net_publish_sensors(const SensorSnapshot *snapshot);
bool wifi_net_connected(void);
const char *wifi_net_ip(void);
bool wifi_net_cry_alert(void);
void wifi_net_clear_cry_alert(void);

#endif

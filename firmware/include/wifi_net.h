#ifndef WIFI_NET_H
#define WIFI_NET_H

#include "sensors.h"

void wifi_net_run(void);
void wifi_net_publish_sensors(const SensorSnapshot *snapshot);

#endif

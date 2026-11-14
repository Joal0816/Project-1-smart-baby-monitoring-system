#ifndef SMART_BABY_MONITOR_UI_H
#define SMART_BABY_MONITOR_UI_H

#include "sensors.h"

#include <stdbool.h>
#include <stdint.h>

void monitor_ui_init(void);
void monitor_ui_step(const SensorSnapshot *snapshot);

#endif

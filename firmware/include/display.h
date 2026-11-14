#ifndef SMART_BABY_DISPLAY_H
#define SMART_BABY_DISPLAY_H

#include "sensors.h"

#include <stdbool.h>
#include <stdint.h>

#define DISPLAY_WIDTH 240U
#define DISPLAY_HEIGHT 240U

bool display_init(void);
bool display_is_ready(void);
void display_render(const SensorSnapshot *snapshot, bool wifi_up,
                    const char *ip, bool cry_alert, bool muted, uint8_t page,
                    uint32_t audio_rms, uint32_t audio_frequency_hz);

#endif

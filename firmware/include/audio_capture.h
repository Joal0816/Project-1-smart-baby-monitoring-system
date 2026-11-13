#ifndef SMART_BABY_AUDIO_CAPTURE_H
#define SMART_BABY_AUDIO_CAPTURE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void audio_capture_init(void);
void audio_capture_poll(void);
bool audio_capture_window_ready(void);
const int16_t *audio_capture_window_data(void);
size_t audio_capture_window_length(void);
void audio_capture_window_release(void);

#endif

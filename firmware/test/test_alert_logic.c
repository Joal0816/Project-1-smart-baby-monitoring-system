#include "alert_logic.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    MonitorAlertState state = {0};
    const MonitorAlertConfig config = monitor_alert_default_config();
    MonitorAlertInput sample = {
        .temperature_c = 32.0f,
        .humidity_pct = 55.0f,
        .inference_valid = true,
        .cry_score = 0.1f,
        .temperature_valid = true,
        .humidity_valid = true,
    };
    assert(monitor_alert_update(&state, &config, &sample) == MONITOR_ALERT_NONE);
    assert(monitor_alert_update(&state, &config, &sample) == MONITOR_ALERT_NONE);
    assert(monitor_alert_update(&state, &config, &sample) == MONITOR_ALERT_TEMPERATURE);
    sample.temperature_c = 24.0f;
    assert(monitor_alert_update(&state, &config, &sample) == MONITOR_ALERT_TEMPERATURE);
    assert(monitor_alert_update(&state, &config, &sample) == MONITOR_ALERT_TEMPERATURE);
    assert(monitor_alert_update(&state, &config, &sample) == MONITOR_ALERT_NONE);

    sample.cry_score = 0.8f;
    assert(monitor_alert_update(&state, &config, &sample) == MONITOR_ALERT_NONE);
    assert(monitor_alert_update(&state, &config, &sample) == MONITOR_ALERT_CRY);
    sample.cry_score = 0.1f;
    assert(monitor_alert_update(&state, &config, &sample) == MONITOR_ALERT_NONE);

    sample.temperature_valid = false;
    sample.humidity_valid = false;
    assert(monitor_alert_update(&state, &config, &sample) == MONITOR_ALERT_SENSOR_FAULT);
    sample.temperature_valid = true;
    sample.humidity_valid = true;
    sample.inference_valid = false;
    assert(monitor_alert_update(&state, &config, &sample) == MONITOR_ALERT_INFERENCE_UNAVAILABLE);
    sample.sensor_fault = true;
    assert(monitor_alert_update(&state, &config, &sample) == MONITOR_ALERT_SENSOR_FAULT);
    sample.sensor_fault = false;
    sample.network_fault = true;
    assert(monitor_alert_update(&state, &config, &sample) == MONITOR_ALERT_NETWORK_FAULT);

    sample.network_fault = false;
    sample.inference_valid = true;
    sample.cry_score = NAN;
    assert(monitor_alert_update(&state, &config, &sample) == MONITOR_ALERT_INFERENCE_UNAVAILABLE);
    sample.cry_score = 0.8f;
    assert(monitor_alert_update(&state, &config, &sample) == MONITOR_ALERT_NONE);
    sample.inference_valid = false;
    assert(monitor_alert_update(&state, &config, &sample) == MONITOR_ALERT_INFERENCE_UNAVAILABLE);
    sample.inference_valid = true;
    assert(monitor_alert_update(&state, &config, &sample) == MONITOR_ALERT_NONE);
    assert(monitor_alert_update(&state, &config, &sample) == MONITOR_ALERT_CRY);

    puts("PASS: environment debounce/hysteresis, cry confirmation, explicit unavailable/fault states");
    return 0;
}

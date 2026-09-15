#ifndef HOME_ASSISTANT_H
#define HOME_ASSISTANT_H

#include <Arduino.h>

void sendHomeAssistantVoiceAlert(const String &alertMessage);
void sendHomeAssistantVoiceClear(const String &sensorType);

// Posts local sensor + fan telemetry to HA, then pulls the configured
// network temperature sensor back and updates networkTempC/networkSensorHealthy.
void fetchHomeAssistantTemperature();

// Two-way tMin/tMax sync with HA input_number helpers (see config.h's
// haTMinEntity/haTMaxEntity). Poll fetchThresholdsFromHA() periodically
// (e.g. every 15s - deliberately slower than the telemetry cadence, since
// thresholds change rarely). Call pushThresholdsToHA() immediately whenever
// the web form changes tMin/tMax, so HA's stored value doesn't silently
// revert the web change on the next poll.
void fetchThresholdsFromHA();
void pushThresholdsToHA();

// Two-way manual-override sync with HA (input_boolean for on/off, input_number
// for speed 0-255 - see config.h's haOverrideSwitchEntity/haOverrideSpeedEntity).
// Poll fetchOverrideFromHA() periodically (e.g. every 60s, same reasoning as
// the threshold poll re: W5500 socket churn). Call pushOverrideToHA()
// immediately whenever the LCD touch UI changes override state, so HA's
// stored value doesn't fight the next poll. Override state itself is never
// persisted to LittleFS (see sensors.h) - only these entity IDs are.
void fetchOverrideFromHA();
void pushOverrideToHA();

#endif // HOME_ASSISTANT_H

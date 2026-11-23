// Bluetooth LE link to a phone running nRF Connect (or any BLE app).
//
// Service "SynapSense" (7a3e0001-5c1f-4d2b-9a61-53594e415053):
//   Alert   ...0002  read/notify  text: pre-alerts, alerts, updates, end, cancel
//   Status  ...0003  read/notify  text: state, battery, activity, faults
//   Live    ...0004  notify       text: per-second features (for tuning)
//   Command ...0005  write        text: cancel | ack | test | status
// Plus the standard Battery Service (0x180F) and Device Information.
#pragma once
#include <Arduino.h>

enum class BleCommand : uint8_t { None, Cancel, Ack, Test, Status };

namespace ble {
// wake: called from the BLE task when something needs the main loop's attention.
void begin(void (*wake)());
bool connected();
void sendAlert(const char* text);  // stores (readable later) + notifies
void sendStatus(const char* text);
void sendLive(const char* text);   // notify only, if someone is listening
void setBattery(uint8_t percent);
BleCommand takeCommand();          // next phone command, or None
void advertiseFast();              // restart fast advertising (after an alert)
}  // namespace ble

// Bluetooth LE link to a phone (nRF Connect or any BLE app).
//
// Service 7a3e0001-5c1f-4d2b-9a61-53594e415053 ("SynapSense"), all values text:
//   ...0002 Alert    read/notify  pre-alert, alert, updates, ended, cancelled
//   ...0003 Status   read/notify  state, battery, activity, faults
//   ...0004 Live     notify       per-second features (for tuning)
//   ...0005 Command  write        cancel | ack | test | status
// plus the standard Battery Service and Device Information.
#pragma once
#include <Arduino.h>

#include "commands.h"

namespace ble {
// wake() is called from the BLE task when a command arrives or a phone
// (dis)connects, so the main loop doesn't wait for its next IMU interrupt.
void begin(void (*wake)());
bool connected();
void sendAlert(const char* text);  // stays readable; resent if the phone missed it
void sendStatus(const char* text);
void sendLive(const char* text);   // only if someone subscribed
void setBattery(uint8_t percent);
Command takeCommand();             // latest phone command, or Command::None
void advertiseFast();              // reconnect faster after an alert
}  // namespace ble

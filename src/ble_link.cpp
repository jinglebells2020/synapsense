#include "ble_link.h"

#include <bluefruit.h>

namespace ble {
namespace {
// 7a3e000X-5c1f-4d2b-9a61-53594e415053, in Bluefruit's little-endian byte order.
#define SYNAPSENSE_UUID(x) {0x53, 0x50, 0x41, 0x4e, 0x59, 0x53, 0x61, 0x9a, 0x2b, 0x4d, 0x1f, 0x5c, x, 0x00, 0x3e, 0x7a}
const uint8_t kServiceUuid[16] = SYNAPSENSE_UUID(0x01);
const uint8_t kAlertUuid[16] = SYNAPSENSE_UUID(0x02);
const uint8_t kStatusUuid[16] = SYNAPSENSE_UUID(0x03);
const uint8_t kLiveUuid[16] = SYNAPSENSE_UUID(0x04);
const uint8_t kCommandUuid[16] = SYNAPSENSE_UUID(0x05);

BLEService service(kServiceUuid);
BLECharacteristic alertChar(kAlertUuid);
BLECharacteristic statusChar(kStatusUuid);
BLECharacteristic liveChar(kLiveUuid);
BLECharacteristic commandChar(kCommandUuid);
BLEDis deviceInfo;
BLEBas battery;

void (*wakeMainLoop)() = nullptr;
volatile Command pending = Command::None;
volatile bool alert_missed = false;  // an alert went out while no phone was listening

void onCommand(uint16_t, BLECharacteristic*, uint8_t* data, uint16_t len) {
  char text[24] = {0};  // data has no 0 at the end, so copy it into a 0-filled buffer
  if (len > sizeof(text) - 1) len = sizeof(text) - 1;
  memcpy(text, data, len);
  const Command c = parseCommand(text);
  if (c == Command::None) return;
  pending = c;
  if (wakeMainLoop) wakeMainLoop();
}

// When a phone subscribes to alerts, hand over the one it missed.
void onAlertSubscribe(uint16_t conn, BLECharacteristic* chr, uint16_t value) {
  if (!(value & BLE_GATT_HVX_NOTIFICATION) || !alert_missed) return;
  char text[128] = {0};
  chr->read(text, sizeof(text) - 1);
  if (chr->notify(conn, text)) alert_missed = false;
}

void onConnect(uint16_t conn) {
  Bluefruit.Connection(conn)->requestMtuExchange(247);  // room for long text
  if (wakeMainLoop) wakeMainLoop();
}

void onDisconnect(uint16_t, uint8_t) {
  if (wakeMainLoop) wakeMainLoop();
}

void addCharacteristic(BLECharacteristic& c, uint8_t props, const char* name, const char* initial) {
  c.setProperties(props);
  if (props & (CHR_PROPS_WRITE | CHR_PROPS_WRITE_WO_RESP))
    c.setPermission(SECMODE_NO_ACCESS, SECMODE_OPEN);
  else
    c.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
  c.setMaxLen(128);
  c.setUserDescriptor(name);  // the name nRF Connect shows
  c.begin();
  if (initial) c.write(initial);
}
}  // namespace

void begin(void (*wake)()) {
  wakeMainLoop = wake;
  Bluefruit.configPrphBandwidth(BANDWIDTH_MAX);  // allow long notifications
  Bluefruit.begin();
  Bluefruit.autoConnLed(false);  // the LED is ours
  Bluefruit.setTxPower(0);
  Bluefruit.setName("SynapSense");
  Bluefruit.Periph.setConnectCallback(onConnect);
  Bluefruit.Periph.setDisconnectCallback(onDisconnect);

  deviceInfo.setManufacturer("SynapSense (student prototype)");
  deviceInfo.setModel("XIAO nRF52840 Sense");
  deviceInfo.begin();
  battery.begin();
  battery.write(100);

  service.begin();
  addCharacteristic(alertChar, CHR_PROPS_READ | CHR_PROPS_NOTIFY, "Alert", "no events");
  alertChar.setCccdWriteCallback(onAlertSubscribe);
  addCharacteristic(statusChar, CHR_PROPS_READ | CHR_PROPS_NOTIFY, "Status", "starting");
  addCharacteristic(liveChar, CHR_PROPS_NOTIFY, "Live features", nullptr);
  addCharacteristic(commandChar, CHR_PROPS_WRITE | CHR_PROPS_WRITE_WO_RESP, "Command: cancel / ack / test / status",
                    nullptr);
  commandChar.setWriteCallback(onCommand);

  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();
  Bluefruit.Advertising.addService(service);
  Bluefruit.ScanResponse.addName();
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(160, 1636);  // 100 ms for the first 30 s, then ~1 s
  Bluefruit.Advertising.setFastTimeout(30);
  Bluefruit.Advertising.start(0);
}

bool connected() { return Bluefruit.connected() > 0; }

void sendAlert(const char* text) {
  alertChar.write(text);
  alert_missed = !alertChar.notify(text);
}

void sendStatus(const char* text) {
  statusChar.write(text);
  statusChar.notify(text);
}

void sendLive(const char* text) {
  if (liveChar.notifyEnabled()) liveChar.notify(text);
}

void setBattery(uint8_t percent) { battery.notify(percent); }

Command takeCommand() {
  const Command c = pending;
  pending = Command::None;
  return c;
}

void advertiseFast() {
  if (connected()) return;
  Bluefruit.Advertising.stop();
  Bluefruit.Advertising.start(0);  // restarting begins with the fast interval
}
}  // namespace ble

#include "ble_link.h"

#include <bluefruit.h>
#include <string.h>

namespace ble {
namespace {
// 7a3e000X-5c1f-4d2b-9a61-53594e415053, little-endian byte order.
#define SYN_UUID(x) \
  {0x53, 0x50, 0x41, 0x4e, 0x59, 0x53, 0x61, 0x9a, 0x2b, 0x4d, 0x1f, 0x5c, x, 0x00, 0x3e, 0x7a}
const uint8_t kSvcUuid[16] = SYN_UUID(0x01);
const uint8_t kAlertUuid[16] = SYN_UUID(0x02);
const uint8_t kStatusUuid[16] = SYN_UUID(0x03);
const uint8_t kLiveUuid[16] = SYN_UUID(0x04);
const uint8_t kCmdUuid[16] = SYN_UUID(0x05);

BLEService svc(kSvcUuid);
BLECharacteristic alertChr(kAlertUuid);
BLECharacteristic statusChr(kStatusUuid);
BLECharacteristic liveChr(kLiveUuid);
BLECharacteristic cmdChr(kCmdUuid);
BLEDis dis;
BLEBas bas;

void (*wake_fn)() = nullptr;
volatile BleCommand pending = BleCommand::None;
volatile bool alert_unsent = false;  // an alert happened while nobody listened

void onCommand(uint16_t, BLECharacteristic*, uint8_t* data, uint16_t len) {
  char s[24] = {0};
  memcpy(s, data, min<uint16_t>(len, sizeof(s) - 1));
  for (char* p = s; *p; p++) *p = tolower(*p);
  size_t n = strlen(s);
  while (n && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ')) s[--n] = 0;
  BleCommand c = BleCommand::None;
  if (!strcmp(s, "cancel") || !strcmp(s, "c")) c = BleCommand::Cancel;
  else if (!strcmp(s, "ack") || !strcmp(s, "a")) c = BleCommand::Ack;
  else if (!strcmp(s, "test") || !strcmp(s, "t")) c = BleCommand::Test;
  else if (!strcmp(s, "status") || !strcmp(s, "s")) c = BleCommand::Status;
  if (c == BleCommand::None) return;
  pending = c;
  if (wake_fn) wake_fn();
}

// When the phone subscribes to alerts, hand over any alert it missed.
void onAlertCccd(uint16_t conn, BLECharacteristic* chr, uint16_t value) {
  if ((value & BLE_GATT_HVX_NOTIFICATION) && alert_unsent) {
    char buf[128] = {0};
    chr->read(buf, sizeof(buf) - 1);
    if (chr->notify(conn, buf)) alert_unsent = false;
  }
}

void onConnect(uint16_t conn) {
  BLEConnection* c = Bluefruit.Connection(conn);
  c->requestMtuExchange(247);
  if (wake_fn) wake_fn();
}

void onDisconnect(uint16_t, uint8_t) {
  if (wake_fn) wake_fn();
}

void setupChr(BLECharacteristic& c, uint8_t props, const char* name, const char* initial) {
  c.setProperties(props);
  if (props & (CHR_PROPS_WRITE | CHR_PROPS_WRITE_WO_RESP))
    c.setPermission(SECMODE_NO_ACCESS, SECMODE_OPEN);
  else
    c.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
  c.setMaxLen(128);
  c.setUserDescriptor(name);
  c.begin();
  if (initial) c.write(initial);
}
}  // namespace

void begin(void (*wake)()) {
  wake_fn = wake;
  Bluefruit.configPrphBandwidth(BANDWIDTH_MAX);  // allow long text notifications
  Bluefruit.begin();
  Bluefruit.autoConnLed(false);  // we drive the LED ourselves
  Bluefruit.setTxPower(0);
  Bluefruit.setName("SynapSense");
  Bluefruit.Periph.setConnectCallback(onConnect);
  Bluefruit.Periph.setDisconnectCallback(onDisconnect);

  dis.setManufacturer("SynapSense (student prototype)");
  dis.setModel("XIAO nRF52840 Sense");
  dis.begin();
  bas.begin();
  bas.write(100);

  svc.begin();
  setupChr(alertChr, CHR_PROPS_READ | CHR_PROPS_NOTIFY, "Alert", "no events");
  alertChr.setCccdWriteCallback(onAlertCccd);
  setupChr(statusChr, CHR_PROPS_READ | CHR_PROPS_NOTIFY, "Status", "starting");
  setupChr(liveChr, CHR_PROPS_NOTIFY, "Live features", nullptr);
  setupChr(cmdChr, CHR_PROPS_WRITE | CHR_PROPS_WRITE_WO_RESP,
           "Command: cancel / ack / test / status", nullptr);
  cmdChr.setWriteCallback(onCommand);

  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();
  Bluefruit.Advertising.addService(svc);
  Bluefruit.ScanResponse.addName();
  Bluefruit.Advertising.restartOnDisconnect(true);
  // 100 ms for the first 30 s, then ~1 s to save power.
  Bluefruit.Advertising.setInterval(160, 1636);
  Bluefruit.Advertising.setFastTimeout(30);
  Bluefruit.Advertising.start(0);
}

bool connected() { return Bluefruit.connected() > 0; }

void sendAlert(const char* text) {
  alertChr.write(text);
  alert_unsent = !alertChr.notify(text);
}

void sendStatus(const char* text) {
  statusChr.write(text);
  statusChr.notify(text);
}

void sendLive(const char* text) {
  if (liveChr.notifyEnabled()) liveChr.notify(text);
}

void setBattery(uint8_t percent) { bas.notify(percent); }

BleCommand takeCommand() {
  BleCommand c = pending;
  pending = BleCommand::None;
  return c;
}

void advertiseFast() {
  if (connected()) return;
  Bluefruit.Advertising.stop();
  Bluefruit.Advertising.start(0);
}
}  // namespace ble

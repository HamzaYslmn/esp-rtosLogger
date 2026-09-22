// Lines to a BLE notify, a batch per packet, and to Serial when a host is there. Defining
// logReady and logSink in the sketch replaces the library's Serial defaults
#include <BLEDevice.h>
#include <BLEServer.h>
#include <rtosLogger.h>

static BLECharacteristic *out;
static bool linked;

bool logReady() { return linked || Serial; }

void logSink(const char *batch, size_t n) {
  if (linked) out->setValue((uint8_t *)batch, n), out->notify();  // the central should ask for MTU 517
  if (Serial) Serial.write(batch, n);
}

struct OnLink : BLEServerCallbacks {
  void onConnect(BLEServer *) override { linked = true; }
  void onDisconnect(BLEServer *s) override { linked = false, s->startAdvertising(); }
};

void setup() {
  Serial.begin(115200);
  BLEDevice::init("rtosLogger");
  BLEDevice::setMTU(517);
  BLEServer *server = BLEDevice::createServer();
  server->setCallbacks(new OnLink);
  BLEService *svc = server->createService("9b39be61-f76f-4549-8a18-3ff0c3a21191");
  out = svc->createCharacteristic("2693aea1-fbe5-4526-9016-fdb6ff47899c", BLECharacteristic::PROPERTY_NOTIFY);
  svc->start();
  server->getAdvertising()->addServiceUUID(svc->getUUID());
  server->startAdvertising();
  logI("advertising");
}

void loop() {
  static int n;
  logI("line %d, %.3f", n++, n * 0.001f);
  delay(200);
}

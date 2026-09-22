// Every line to Serial and to a BLE notify at once. logTo says where lines go
#include <BLEDevice.h>
#include <rtosLogger.h>

static BLECharacteristic *out;
static bool linked;

struct OnLink : BLEServerCallbacks {
  void onConnect(BLEServer *) override { linked = true; }
  void onDisconnect(BLEServer *s) override { linked = false, s->startAdvertising(); }
};

void setupBLE() {
  BLEDevice::init("rtosLogger");
  BLEDevice::setMTU(517);  // one notify carries a whole batch
  BLEServer *server = BLEDevice::createServer();
  server->setCallbacks(new OnLink);
  BLEService *svc = server->createService("9b39be61-f76f-4549-8a18-3ff0c3a21191");
  out = svc->createCharacteristic("2693aea1-fbe5-4526-9016-fdb6ff47899c", BLECharacteristic::PROPERTY_NOTIFY);
  svc->start();
  server->getAdvertising()->addServiceUUID(svc->getUUID());
  server->startAdvertising();
}

void setup() {
  Serial.begin(115200);
  setupBLE();
  logTo([](const char *text, size_t n) {
    if (Serial) Serial.write(text, n);  // a USB port with no host would wait out its timeout
    if (linked) out->setValue((uint8_t *)text, n), out->notify();
  });
}

void loop() {
  logI("uptime %lu ms", millis());
  delay(200);
}

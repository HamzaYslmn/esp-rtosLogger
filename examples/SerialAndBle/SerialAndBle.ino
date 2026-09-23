// Lines to Serial, lines and records to a BLE notify. logTo says where packets go
#include <BLEDevice.h>
#include <rtosLogger.h>

struct Sample {  // a record: declare it once, in a header the receiving end shares
  uint32_t ms;
  int16_t rpm, amps;
};

static BLECharacteristic *out;
static bool linked;

struct OnLink : BLEServerCallbacks {
  void onConnect(BLEServer *) override { linked = true; }
  void onDisconnect(BLEServer *s) override { linked = false, s->startAdvertising(); }
};

void setupBLE() {
  BLEDevice::init("rtosLogger");
  BLEDevice::setMTU(517);  // one notify carries a whole 512 byte packet
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
  logTo([](const uint8_t *data, size_t n, bool text) {
    if (text && Serial) Serial.write(data, n);  // a USB port with no host would wait out its timeout
    if (linked) out->setValue((uint8_t *)data, n), out->notify();
  });
}

void loop() {
  logI("uptime %lu ms", millis());
  logBin(Sample{millis(), 1200, 35});  // the receiver: logBinRead<Sample>(data, n, [](const Sample &s) {...})
  delay(200);
}

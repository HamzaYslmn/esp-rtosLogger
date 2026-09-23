// Receiver for SerialAndBle: finds it, subscribes, prints its lines and its newest struct
#include <BLEDevice.h>
#include <rtosLogger.h>

struct Sample {  // the sender's struct, the same name and fields
  uint32_t ms;
  int16_t rpm, amps;
};

static BLEUUID SERVICE("9b39be61-f76f-4549-8a18-3ff0c3a21191"), OUT("2693aea1-fbe5-4526-9016-fdb6ff47899c");
static BLEClient *client;

bool connectTo(BLEAdvertisedDevice &dev) {
  if (!client->connect(&dev)) return false;
  client->setMTU(517);  // a whole 512 byte packet per notify
  BLERemoteService *svc = client->getService(SERVICE);
  BLERemoteCharacteristic *out = svc ? svc->getCharacteristic(OUT) : nullptr;
  if (!out) return client->disconnect(), false;
  out->registerForNotify([](BLERemoteCharacteristic *, uint8_t *data, size_t n, bool) { logFrom(data, n); });
  return true;
}

void findSender() {
  BLEScanResults *found = BLEDevice::getScan()->start(3);  // seconds
  for (int i = 0; found && i < found->getCount(); i++) {
    BLEAdvertisedDevice dev = found->getDevice(i);
    if (dev.isAdvertisingService(SERVICE) && connectTo(dev)) break;
  }
  BLEDevice::getScan()->clearResults();
}

void setup() {
  Serial.begin(115200);
  BLEDevice::init("");
  client = BLEDevice::createClient();
  BLEDevice::getScan()->setActiveScan(true);
}

void loop() {
  if (!client->isConnected()) findSender();
  static Sample s;  // keeps the newest
  if (logBinRead(s)) Serial.printf("sample %lu ms: %d rpm, %d A\n", (unsigned long)s.ms, s.rpm, s.amps);
  delay(10);
}

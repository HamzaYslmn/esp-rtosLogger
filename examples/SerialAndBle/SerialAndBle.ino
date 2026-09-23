// Sender: lines to Serial, lines and structs to a BLE notify. BleReceiver on a second ESP32 prints
// them, or any BLE app subscribed to the characteristic below
#include <BLEDevice.h>
#include <rtosLogger.h>

struct Sample {  // the receiver declares the same struct; in a real project, one header both include
  uint32_t ms;
  int16_t rpm, amps;
};

static BLECharacteristic *out;
static bool linked;
static hw_timer_t *timer;
static volatile int rpm, amps;

struct OnLink : BLEServerCallbacks {
  void onConnect(BLEServer *) override { linked = true; }
  void onDisconnect(BLEServer *s) override { linked = false, s->startAdvertising(); }
};

void IRAM_ATTR onTimer() {  // an interrupt: logI is safe here, Serial.printf is not
  static uint32_t ticks;
  if (++ticks % 1000 == 0) logI("isr: %lu ticks", (unsigned long)ticks);
}

void IRAM_ATTR control() {  // the hot path, in RAM: about 1 us a call, never a flash fetch
  if (amps > 35) logW("over %d A at %d rpm", amps, rpm);
  logBin(Sample{millis(), (int16_t)rpm, (int16_t)amps});  // telemetry: a struct, nothing formatted
}

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
  timer = timerBegin(1000000);
  timerAttachInterrupt(timer, onTimer);
  timerAlarm(timer, 1000, true, 0);  // every 1 ms
}

void loop() {  // plain code runs from flash: fine where a few us more does not matter
  rpm = 1200 + random(-50, 50);
  amps = random(20, 40);
  control();
  static uint32_t last;
  if (millis() - last >= 1000) last = millis(), logI("uptime %lu ms, %lu dropped", last, (unsigned long)logDrops.load());
  delay(20);
}

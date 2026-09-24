// One board, no radio: wire() stands in for the air, so what it prints is what a second board gets.
// Shows the two kinds of packet and how the first byte tells them apart
#include <rtosLogger.h>

struct Pedal {  // in a header both boards include
  bool pressed;
  uint32_t at;
};

const int PIN = 0;  // the BOOT button on most ESP32 boards

void wire(const uint8_t *data, size_t n, bool text) {
  if (text) return (void)Serial.write(data, n);  // lines, each starting with '['
  Serial.printf("struct packet, %u bytes:", (unsigned)n);  // a 4 byte id, never starting with '[', then the structs
  for (size_t i = 0; i < n; i++) Serial.printf(" %02x", data[i]);
  Serial.println();
  logFrom(data, n);  // what the other board does. Lines stay out: logFrom would hand them straight back here
}

LogOutput air(wire);  // the only output, so nothing goes to Serial but what wire() prints

void setup() {
  Serial.begin(115200);
  pinMode(PIN, INPUT_PULLUP);
}

void loop() {
  logI("uptime %lu s", millis() / 1000);           // a line: formatted later, sent as text
  logBin(Pedal{digitalRead(PIN) == LOW, millis()});  // a struct: sent as its bytes

  static Pedal pedal;  // the receiving side: holds the newest
  if (logBinRead(pedal)) Serial.printf("read: pressed=%d at %lu ms\n", pedal.pressed, (unsigned long)pedal.at);
  delay(1000);
}

#ifndef ANTDEVICEMANAGER_H
#define ANTDEVICEMANAGER_H

#include <Arduino.h>
#include <ANT.h>

class ANTDeviceManager {
 public:
  static bool start();
  static void stop();
  static void update();

 private:
  static void parseMessage();
  static void parseEventMessage(uint8_t code);
  static ArduinoSerialAnt ant;
  static bool started;
};

#endif
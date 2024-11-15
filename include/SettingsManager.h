#ifndef SETTINGSMANAGER_H
#define SETTINGSMANAGER_H

// IoTWebConf configuration
// identifier for EEPROM storage
#define WIFI_CONFIG_VERSION "init"
// when CONFIG_PIN is pulled to ground on startup, the device will use the initial password to build an AP
#define WIFI_CONFIG_PIN IO12
// status indicator pin, first it will light up (kept LOW), on Wifi connection it will blink, when connected to the Wifi it will turn off (kept HIGH)
#define WIFI_STATUS_PIN IO14

#include <Arduino.h>
#include <IotWebConf.h>
#include <IotWebConfUsing.h>

class SettingsManager {
 public:
  static void initialize();
  static uint16_t getChainringTeeth();
  static uint16_t getSprocketTeeth();
  static void setChainringTeeth(uint16_t chainringTeeth);
  static void setSprocketTeeth(uint16_t sprocketTeeth);
  static bool isTrackResistanceEnabled();
  static bool isVirtualShiftingEnabled();
  static void setTrackResistanceEnabled(bool enabled);
  static void setVirtualShiftingEnabled(bool enabled);
  static std::string getTrainerDeviceName();
  static void setTrainerDeviceName(std::string trainerDevice);
  static IotWebConfParameterGroup* getIoTWebConfSettingsParameterGroup();

 private:
  static char iotWebConfChainringTeethParameterValue[];
  static char iotWebConfSprocketTeethParameterValue[];
  static char iotWebConfTrackResistanceParameterValue[];
  static char iotWebConfVirtualShiftingParameterValue[];
  static char iotWebConfTrainerDeviceParameterValue[];
  static IotWebConfParameterGroup iotWebConfSettingsGroup;
  static IotWebConfNumberParameter iotWebConfChainringTeethParameter;
  static IotWebConfNumberParameter iotWebConfSprocketTeethParameter;
  static IotWebConfCheckboxParameter iotWebConfTrackResistanceParameter;
  static IotWebConfCheckboxParameter iotWebConfVirtualShiftingParameter;
  static IotWebConfTextParameter iotWebConfTrainerDeviceParameter;
};

#endif

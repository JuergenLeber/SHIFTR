#include <Arduino.h>
#include <AsyncTCP.h>
#include <BTDeviceManager.h>
#include <Bounce2.h>
#include <Config.h>
#include <DirConManager.h>
#include <ESPmDNS.h>
#include <ETH.h>
#include <IotWebConf.h>
#include <IotWebConfESP32HTTPUpdateServer.h>
#include <IotWebConfUsing.h>
#include <NimBLEDevice.h>
#include <SettingsManager.h>
#include <UUIDs.h>
#include <Utils.h>
#include <Version.h>
#include <WebSocketsServer.h>
#include <WiFi.h>
#include <ANTDeviceManager.h>

void networkEvent(WiFiEvent_t event);
void handleWebServerFile(const String& fileName);
void handleWebServerStatus();
void handleWebServerSettings();
void handleWebServerSettingsPost();
void handleWebServerDebug();

void setupWiredShifting();
void loopWiredShifting();

void notifyWebSocketClients();
void handleWebSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length);

class DirConCallbacks : public DirConManagerCallbacks {
  void onGearChanged(uint8_t currentGear, double currentGearRatio) {
    notifyWebSocketClients();
  };
  void onTrainerModeChanged(TrainerMode trainerMode) {
    notifyWebSocketClients();
  };
};

Bounce gearUpDebouncer = Bounce();
Bounce gearDownDebouncer = Bounce();

bool isMDNSStarted = false;
bool isBLEConnected = false;
bool isEthernetConnected = false;
bool isWiFiConnected = false;

DNSServer dnsServer;
WebServer webServer(WEB_SERVER_PORT);
WebSocketsServer webSocketServer = WebSocketsServer((WEB_SERVER_PORT + 1));
HTTPUpdateServer updateServer;
IotWebConf iotWebConf(Utils::getHostName().c_str(), &dnsServer, &webServer, Utils::getHostName().c_str(), WIFI_CONFIG_VERSION);

ServiceManager serviceManager;

void setup() {
  sleep(2); // wait for serial monitor
  log_i(DEVICE_NAME_PREFIX " " VERSION " starting...");
  log_i("Device name: %s, host name: %s", Utils::getDeviceName().c_str(), Utils::getFQDN().c_str());

  // initialize ANT device manager
  if (!ANTDeviceManager::start()) {
    log_e("ANT device manager initialization failed");
  }
  log_i("ANT device manager initialized");

  // initialize bluetooth device manager
  BTDeviceManager::setLocalDeviceName(Utils::getDeviceName());
  BTDeviceManager::setServiceManager(&serviceManager);
  if (!BTDeviceManager::start()) {
    log_e("Startup failed: Unable to start bluetooth device manager");
    ESP.restart();
  }
  log_i("Bluetooth device manager initialized");

  // initialize network events
  WiFi.onEvent(networkEvent);
  log_i("Network events initialized");

  // initialize ethernet interface
  ETH.begin();
  log_i("Ethernet interface initialized");

  // initialize settings manager
  SettingsManager::initialize(&iotWebConf);

  // initialize wifi manager and web server
  iotWebConf.setStatusPin(WIFI_STATUS_PIN);
  iotWebConf.setConfigPin(WIFI_CONFIG_PIN);
  iotWebConf.addParameterGroup(SettingsManager::getIoTWebConfSettingsParameterGroup());
  iotWebConf.setupUpdateServer(
      [](const char* updatePath) { updateServer.setup(&webServer, updatePath, SettingsManager::getUsername().c_str(), SettingsManager::getAPPassword().c_str()); },
      [](const char* userName, char* password) { updateServer.updateCredentials(userName, password); });
  iotWebConf.init();

  // workaround for missing thing name
  strncpy(iotWebConf.getThingNameParameter()->valueBuffer, Utils::getHostName().c_str(), iotWebConf.getThingNameParameter()->getLength());

  webServer.on("/debug", handleWebServerDebug);
  webServer.on("/status", handleWebServerStatus);
  webServer.on("/favicon.ico", [] { handleWebServerFile("favicon.ico"); });
  webServer.on("/style.css", [] { handleWebServerFile("style.css"); });
  webServer.on("/script.js", [] { handleWebServerFile("script.js"); });
  webServer.on("/", [] { handleWebServerFile("index.html"); });
  webServer.on("/control", HTTP_GET, [] { 
    if (!webServer.authenticate(SettingsManager::getUsername().c_str(), SettingsManager::getAPPassword().c_str())) {
      return webServer.requestAuthentication();
    }
    handleWebServerFile("control.html"); });
  webServer.on("/settings", HTTP_GET, [] { 
    if (!webServer.authenticate(SettingsManager::getUsername().c_str(), SettingsManager::getAPPassword().c_str())) {
      return webServer.requestAuthentication();
    }
    handleWebServerFile("settings.html"); });
  webServer.on("/settings", HTTP_POST, [] { 
    if (!webServer.authenticate(SettingsManager::getUsername().c_str(), SettingsManager::getAPPassword().c_str())) {
      return webServer.requestAuthentication();
    }
    handleWebServerSettingsPost(); });
  webServer.on("/devicesettings", [] { 
    if (!webServer.authenticate(SettingsManager::getUsername().c_str(), SettingsManager::getAPPassword().c_str())) {
      return webServer.requestAuthentication();
    }
    handleWebServerSettings(); });
  webServer.on("/config", [] { 
    if (!webServer.authenticate(SettingsManager::getUsername().c_str(), SettingsManager::getAPPassword().c_str())) {
      return webServer.requestAuthentication();
    }
    iotWebConf.handleConfig(); });

  webServer.onNotFound([]() { iotWebConf.handleNotFound(); });

  webSocketServer.begin();
  webSocketServer.onEvent(handleWebSocketEvent);
  
  log_i("WiFi manager, web and web sockets server initialized");

  // initialize service manager internal service if enabled
  if (SettingsManager::isVirtualShiftingEnabled()) {
    Service* zwiftCustomService = new Service(NimBLEUUID(ZWIFT_CUSTOM_SERVICE_UUID), true, true);
    zwiftCustomService->addCharacteristic(new Characteristic(NimBLEUUID(ZWIFT_ASYNC_CHARACTERISTIC_UUID), NOTIFY));
    zwiftCustomService->addCharacteristic(new Characteristic(NimBLEUUID(ZWIFT_SYNCRX_CHARACTERISTIC_UUID), WRITE));
    zwiftCustomService->addCharacteristic(new Characteristic(NimBLEUUID(ZWIFT_SYNCTX_CHARACTERISTIC_UUID), INDICATE));
    serviceManager.addService(zwiftCustomService);
  }

  // initialize service manager FTMS service if enabled
  if (SettingsManager::isFTMSEnabled()) {
    Service* fitnessMachineService = new Service(NimBLEUUID(FITNESS_MACHINE_SERVICE_UUID), true, true);
    fitnessMachineService->addCharacteristic(new Characteristic(NimBLEUUID(FITNESS_MACHINE_FEATURE_CHARACTERISTIC_UUID), READ));
    fitnessMachineService->addCharacteristic(new Characteristic(NimBLEUUID(INDOOR_BIKE_DATA_CHARACTERISTIC_UUID), NOTIFY));
    fitnessMachineService->addCharacteristic(new Characteristic(NimBLEUUID(TRAINING_STATUS_CHARACTERISTIC_UUID), READ | NOTIFY));
    fitnessMachineService->addCharacteristic(new Characteristic(NimBLEUUID(FITNESS_MACHINE_CONTROL_POINT_CHARACTERISTIC_UUID), WRITE | INDICATE));
    fitnessMachineService->addCharacteristic(new Characteristic(NimBLEUUID(FITNESS_MACHINE_STATUS_CHARACTERISTIC_UUID), NOTIFY));
    serviceManager.addService(fitnessMachineService);
  }
  log_i("Service manager initialized");

  // set BTDeviceManager selected trainer device
  BTDeviceManager::setRemoteDeviceNameFilter(SettingsManager::getTrainerDeviceName());

  // initialize MDNS
  if (!MDNS.begin(Utils::getHostName().c_str())) {
    log_e("Startup failed: Unable to start MDNS");
    ESP.restart();
  }
  MDNS.setInstanceName(Utils::getDeviceName().c_str());
  isMDNSStarted = true;
  log_i("MDNS initialized");

  // initialize DirCon manager
  DirConManager::setServiceManager(&serviceManager);
  DirConManager::subscribeCallbacks(new DirConCallbacks);
  if (!DirConManager::start()) {
    log_e("Startup failed: Unable to start DirCon manager");
    ESP.restart();
  }
  log_i("DirCon Manager initialized");

  // initialize wired/manual shifting
  setupWiredShifting();
  log_i("Wired/Manual shifting initialized");

  log_i("Startup finished");
}

void loop() {
  ANTDeviceManager::update();
  BTDeviceManager::update();
  DirConManager::update();
  iotWebConf.doLoop();
  loopWiredShifting();
  webSocketServer.loop();
}

void networkEvent(WiFiEvent_t event) {
  switch (event) {
    case ARDUINO_EVENT_ETH_START:
      log_d("Ethernet started");
      ETH.setHostname(Utils::getHostName().c_str());
      break;
    case ARDUINO_EVENT_ETH_CONNECTED:
      log_i("Ethernet connected");
      break;
    case ARDUINO_EVENT_ETH_GOT_IP:
      log_i("Ethernet DHCP successful with IP %u.%u.%u.%u", ETH.localIP()[0], ETH.localIP()[1], ETH.localIP()[2], ETH.localIP()[3]);
      isEthernetConnected = true;
      break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
      log_i("Ethernet disconnected");
      isEthernetConnected = false;
      break;
    case ARDUINO_EVENT_ETH_STOP:
      log_d("Ethernet stopped");
      isEthernetConnected = false;
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      log_i("WiFi DHCP successful with IP %u.%u.%u.%u", WiFi.localIP()[0], WiFi.localIP()[1], WiFi.localIP()[2], WiFi.localIP()[3]);
      isWiFiConnected = true;
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      log_i("WiFi disconnected");
      isWiFiConnected = false;
      break;
    case ARDUINO_EVENT_WIFI_STA_STOP:
      log_d("WiFi stopped");
      isWiFiConnected = false;
      break;
    default:
      break;
  }
}

extern const uint8_t favicon_ico_start[] asm("_binary_src_web_favicon_ico_start");
extern const uint8_t favicon_ico_end[] asm("_binary_src_web_favicon_ico_end");

extern const uint8_t index_html_start[] asm("_binary_src_web_index_html_start");
extern const uint8_t index_html_end[] asm("_binary_src_web_index_html_end");

extern const uint8_t settings_html_start[] asm("_binary_src_web_settings_html_start");
extern const uint8_t settings_html_end[] asm("_binary_src_web_settings_html_end");

extern const uint8_t control_html_start[] asm("_binary_src_web_control_html_start");
extern const uint8_t control_html_end[] asm("_binary_src_web_control_html_end");

extern const uint8_t style_css_start[] asm("_binary_src_web_style_css_start");
extern const uint8_t style_css_end[] asm("_binary_src_web_style_css_end");

extern const uint8_t script_js_start[] asm("_binary_src_web_script_js_start");
extern const uint8_t script_js_end[] asm("_binary_src_web_script_js_end");

void handleWebServerFile(const String& fileName) {
  if (iotWebConf.handleCaptivePortal()) {
    return;
  }
  if (fileName.equals("index.html")) {
    webServer.send_P(200, "text/html", (char*)index_html_start, (index_html_end - index_html_start));
  }
  if (fileName.equals("settings.html")) {
    webServer.send_P(200, "text/html", (char*)settings_html_start, (settings_html_end - settings_html_start));
  }
  if (fileName.equals("control.html")) {
    webServer.send_P(200, "text/html", (char*)control_html_start, (control_html_end - control_html_start));
  }
  if (fileName.equals("style.css")) {
    webServer.send_P(200, "text/css", (char*)style_css_start, (style_css_end - style_css_start));
  }
  if (fileName.equals("favicon.ico")) {
    webServer.send_P(200, "image/x-icon", (char*)favicon_ico_start, (favicon_ico_end - favicon_ico_start));
  }
  if (fileName.equals("script.js")) {
    webServer.send_P(200, "text/javascript", (char*)script_js_start, (script_js_end - script_js_start));
  }
}

void handleWebServerSettings() {
  String json = "{";
  json += "\"device_name\": \"";
  json += Utils::getDeviceName().c_str();
  json += "\",";

  json += "\"trainer_device\": \"";
  json += SettingsManager::getTrainerDeviceName().c_str();
  json += "\",";

  String devices_json = "\"trainer_devices\": [";
  devices_json += "\"\",";
  for (size_t deviceIndex = 0; deviceIndex < BTDeviceManager::getScannedDevices()->size(); deviceIndex++) {
    if (BTDeviceManager::getScannedDevices()->at(deviceIndex).haveName()) {
      devices_json += "\"";
      devices_json += BTDeviceManager::getScannedDevices()->at(deviceIndex).getName().c_str();
      devices_json += "\",";
    }
  }

  if (devices_json.endsWith(",")) {
    devices_json.remove(devices_json.length() - 1);
  }
  devices_json += "],";

  json += devices_json;

  json += "\"virtual_shifting\": ";
  if (SettingsManager::isVirtualShiftingEnabled()) {
    json += "true";
  } else {
    json += "false";
  }
  json += ",";

  json += "\"virtual_shifting_mode\": ";
  json += SettingsManager::getVirtualShiftingMode();
  json += ",";

  std::map<size_t, std::string> modes = SettingsManager::getVirtualShiftingModes();
  String modes_json = "\"virtual_shifting_modes\": [";
  for (auto mode = modes.begin(); mode != modes.end(); mode++) {
    modes_json += "{\"name\": \"";
    modes_json += mode->second.c_str();
    modes_json += "\", \"value\": ";
    modes_json += mode->first;
    modes_json += "},";
  }
  if (modes_json.endsWith(",")) {
    modes_json.remove(modes_json.length() - 1);
  }
  modes_json += "],";

  json += modes_json;

  json += "\"chainring_teeth\": ";
  json += SettingsManager::getChainringTeeth();
  json += ",";

  json += "\"sprocket_teeth\": ";
  json += SettingsManager::getSprocketTeeth();
  json += ",";

  json += "\"grade_smoothing\": ";
  if (SettingsManager::isGradeSmoothingEnabled()) {
    json += "true";
  } else {
    json += "false";
  }
  json += ",";

  json += "\"difficulty\": ";
  json += SettingsManager::getDifficulty();
  json += ",";

  json += "\"ftms_emulation\": ";
  if (SettingsManager::isFTMSEnabled()) {
    json += "true";
  } else {
    json += "false";
  }
  json += ",";

  json += "\"zwiftless_virtual_shifting\": ";
  if (SettingsManager::isZwiftlessVirtualShiftingEnabled()) {
    json += "true";
  } else {
    json += "false";
  }
  json += "";

  json += "}";

  webServer.send(200, "application/json", json);
}

void handleWebServerSettingsPost() {
  if (webServer.args() > 0) {
    if (webServer.hasArg("trainer_device")) {
      SettingsManager::setTrainerDeviceName(webServer.arg("trainer_device").c_str());
    }
    if (webServer.hasArg("virtual_shifting")) {
      SettingsManager::setVirtualShiftingEnabled(true);
      SettingsManager::setFTMSEnabled(false);
    } else {
      SettingsManager::setVirtualShiftingEnabled(false);
    }
    if (webServer.hasArg("virtual_shifting_mode")) {
      SettingsManager::setVirtualShiftingMode((VirtualShiftingMode)std::atoi(webServer.arg("virtual_shifting_mode").c_str()));
    }
    if (webServer.hasArg("chainring_teeth")) {
      SettingsManager::setChainringTeeth(std::atoi(webServer.arg("chainring_teeth").c_str()));
    }
    if (webServer.hasArg("sprocket_teeth")) {
      SettingsManager::setSprocketTeeth(std::atoi(webServer.arg("sprocket_teeth").c_str()));
    }
    if (webServer.hasArg("grade_smoothing")) {
      SettingsManager::setGradeSmoothingEnabled(true);
    } else {
      SettingsManager::setGradeSmoothingEnabled(false);
    }
    if (webServer.hasArg("difficulty")) {
      SettingsManager::setDifficulty(std::atoi(webServer.arg("difficulty").c_str()));
    }
    if (webServer.hasArg("ftms_emulation")) {
      SettingsManager::setFTMSEnabled(true);
      SettingsManager::setVirtualShiftingEnabled(false);
    } else {
      SettingsManager::setFTMSEnabled(false);
    }
    if (webServer.hasArg("zwiftless_virtual_shifting")) {
      SettingsManager::setZwiftlessVirtualShiftingEnabled(true);
      SettingsManager::setVirtualShiftingEnabled(false);
    } else {
      SettingsManager::setZwiftlessVirtualShiftingEnabled(false);
    }
    iotWebConf.saveConfig();
    delay(500);
    ESP.restart();
  }
}

void handleWebServerStatus() {
  String json = "{";
  json += "\"device_name\": \"";
  json += Utils::getDeviceName().c_str();
  json += "\",";

  json += "\"version\": \"";
  json += VERSION;
  json += "\",";

  json += "\"build_timestamp\": \"";
  json += VERSION_TIMESTAMP;
  json += "\",";

  json += "\"hostname\": \"";
  json += Utils::getFQDN().c_str();
  json += "\",";

  json += "\"ethernet_status\": \"";
  if (isEthernetConnected) {
    json += "Connected";
    json += ", IP: ";
    json += ETH.localIP().toString();
  } else {
    json += "Not connected";
  }
  json += "\",";

  json += "\"wifi_status\": \"";
  switch (iotWebConf.getState()) {
    case iotwebconf::NetworkState::ApMode:
      json += "Access-Point mode";
      break;
    case iotwebconf::NetworkState::Boot:
      json += "Booting";
      break;
    case iotwebconf::NetworkState::Connecting:
      json += "Connecting";
      break;
    case iotwebconf::NetworkState::NotConfigured:
      json += "Not configured";
      break;
    case iotwebconf::NetworkState::OffLine:
      json += "Disconnected";
      break;
    case iotwebconf::NetworkState::OnLine:
      json += "Connected";
      json += ", SSID: ";
      json += WiFi.SSID();
      json += ", IP: ";
      json += WiFi.localIP().toString();
      break;
    default:
      json += "Unknown";
      break;
  }
  json += "\",";

  json += "\"service_status\": \"";
  json += serviceManager.getStatusMessage().c_str();
  json += "\",";

  json += "\"dircon_status\": \"";
  json += DirConManager::getStatusMessage().c_str();
  json += "\",";

  json += "\"ble_status\": \"";
  json += BTDeviceManager::getStatusMessage().c_str();
  json += "\",";

  json += "\"mode\": \"";
  json += "Pass-through";
  if (SettingsManager::isFTMSEnabled()) {
    json += " + FTMS emulation";
  }
  if (SettingsManager::isVirtualShiftingEnabled()) {
    json += " + virtual shifting";
  }
  if (SettingsManager::isZwiftlessVirtualShiftingEnabled()) {
    json += " + zwiftless shifting";
  }
  json += "\",";

  json += "\"free_heap\": ";
  json += ESP.getFreeHeap();
  json += "}";

  webServer.send(200, "application/json", json);
}

void handleWebServerDebug() {
  String json = "{";
  json += "\"zwift_trainer_mode\": \"";
  switch (DirConManager::getZwiftTrainerMode()) {
    case TrainerMode::SIM_MODE:
      json += "SIM mode";
      break;
    case TrainerMode::SIM_MODE_VIRTUAL_SHIFTING:
      json += "SIM + VS mode";
      break;
    default:
      json += "ERG mode";
      break;
  }
  json += "\",";

  String services_json = "\"ble_services\": {";
  for (Service* service : serviceManager.getServices()) {
    services_json += "\"";
    services_json += service->UUID.to128().toString().c_str();
    services_json += "\": {";
    for (Characteristic* characteristic : service->getCharacteristics()) {
      services_json += "\"";
      services_json += characteristic->UUID.to128().toString().c_str();
      services_json += "\": ";
      services_json += characteristic->getSubscriptions().size();
      services_json += ",";
    }
    if (services_json.endsWith(",")) {
      services_json.remove(services_json.length() - 1);
    }
    services_json += "},";
  }
  if (services_json.endsWith(",")) {
    services_json.remove(services_json.length() - 1);
  }
  services_json += "}";

  json += services_json;

  json += "}";

  webServer.send(200, "application/json", json);
}

void setupWiredShifting() {
  gearUpDebouncer.attach(GEAR_UP_PIN, INPUT_PULLUP);
  gearUpDebouncer.interval(25);
  gearDownDebouncer.attach(GEAR_DOWN_PIN, INPUT_PULLUP);
  gearDownDebouncer.interval(25);
}

void loopWiredShifting() {
  gearUpDebouncer.update();
  gearDownDebouncer.update();
  if (SettingsManager::isZwiftlessVirtualShiftingEnabled()) {
    if (gearUpDebouncer.fell()) {
      DirConManager::shiftGearUp();
    }
    if (gearDownDebouncer.fell()) {
      DirConManager::shiftGearDown();
    }
  }
}

void notifyWebSocketClients() {
  String json = "{";

  json += "\"device_name\": \"";
  json += Utils::getDeviceName().c_str();
  json += "\",";

  json += "\"version\": \"";
  json += VERSION;
  json += "\",";

  json += "\"trainer_mode\": \"";
  switch (DirConManager::getZwiftTrainerMode()) {
    case TrainerMode::SIM_MODE:
      json += "SIM";
      break;
    case TrainerMode::SIM_MODE_VIRTUAL_SHIFTING:
      json += "SIM+VS";
      break;
    default:
      json += "ERG";
      break;
  }
  json += "\",";

  json += "\"zwiftless_virtual_shifting\": ";
  if (SettingsManager::isZwiftlessVirtualShiftingEnabled()) {
    json += "true";
  } else {
    json += "false";
  }
  json += ",";

  json += "\"current_gear\": ";
  json += DirConManager::getCurrentGear();
  json += ",";

  json += "\"current_gear_ratio\": ";
  json += DirConManager::getCurrentGearRatio();
  json += "";
  
  json += "}";
  webSocketServer.broadcastTXT(json);
}

void handleWebSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_DISCONNECTED:
      log_i("Web socket connection %u disconnected", num);
      break;
    case WStype_CONNECTED: 
      {
        IPAddress ip = webSocketServer.remoteIP(num);
        log_i("Web socket connection %u from %d.%d.%d.%d to URL: %s", num, ip[0], ip[1], ip[2], ip[3], payload);
      }
      notifyWebSocketClients();
      break;
    case WStype_TEXT:
      if (length > 0) {
        if (strcmp((char*)payload, "shiftUp") == 0) {
          DirConManager::shiftGearUp();
        }
        if (strcmp((char*)payload, "shiftDown") == 0) {
          DirConManager::shiftGearDown();
        }
      }
      break;
    case WStype_BIN:
    case WStype_ERROR:
    case WStype_FRAGMENT_TEXT_START:
    case WStype_FRAGMENT_BIN_START:
    case WStype_FRAGMENT:
    case WStype_FRAGMENT_FIN:
    default:
      break;
  }
}

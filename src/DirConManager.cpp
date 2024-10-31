#include <Arduino.h>
#include <Config.h>
#include <DirConManager.h>
#include <ESPmDNS.h>
#include <Service.h>
#include <ServiceManagerCallbacks.h>
#include <Utils.h>
#include <BTDeviceManager.h>

ServiceManager *DirConManager::serviceManager{};
Timer<> DirConManager::notificationTimer = timer_create_default();
bool DirConManager::started = false;
AsyncServer *DirConManager::dirConServer = new AsyncServer(DIRCON_TCP_PORT);
AsyncClient *DirConManager::dirConClients[];

class DirConServiceManagerCallbacks : public ServiceManagerCallbacks {
  void onServiceAdded(Service *service) {
    if (service->isAdvertised()) {
      String serviceUUIDs = "";
      for (auto currentService = service->getServiceManager()->getServices()->begin(); currentService != service->getServiceManager()->getServices()->end(); currentService++) {
        if (currentService->isAdvertised()) {
          if (serviceUUIDs != "") {
            serviceUUIDs += ",";
          }
          serviceUUIDs += currentService->UUID.to16().toString().c_str();
        }
      }
      log_i("Updating advertised service UUIDs: %s", serviceUUIDs.c_str());
      MDNS.addServiceTxt(DIRCON_MDNS_SERVICE_NAME, DIRCON_MDNS_SERVICE_PROTOCOL, "ble-service-uuids", serviceUUIDs.c_str());
    };
  };
};

void DirConManager::setServiceManager(ServiceManager *serviceManager) {
  DirConManager::serviceManager = serviceManager;
}

bool DirConManager::start() {
  if (!started) {
    if (serviceManager == nullptr) {
      notificationTimer.every(DIRCON_NOTIFICATION_INTERVAL, DirConManager::doNotifications);
      return false;
    }
    // serviceManager->subscribeOnServiceAdded(DirConManager::handleServiceManagerOnServiceAdded);
    serviceManager->subscribeCallbacks(new DirConServiceManagerCallbacks);
    MDNS.addService(DIRCON_MDNS_SERVICE_NAME, DIRCON_MDNS_SERVICE_PROTOCOL, DIRCON_TCP_PORT);
    MDNS.addServiceTxt(DIRCON_MDNS_SERVICE_NAME, DIRCON_MDNS_SERVICE_PROTOCOL, "mac-address", Utils::getMacAddressString().c_str());
    MDNS.addServiceTxt(DIRCON_MDNS_SERVICE_NAME, DIRCON_MDNS_SERVICE_PROTOCOL, "serial-number", Utils::getSerialNumberString().c_str());
    MDNS.addServiceTxt(DIRCON_MDNS_SERVICE_NAME, DIRCON_MDNS_SERVICE_PROTOCOL, "ble-service-uuids", "");
    dirConServer->begin();
    dirConServer->onClient(&handleNewClient, dirConServer);
    return true;
  }
  return false;
}

void DirConManager::stop() {
  dirConServer->end();
  notificationTimer.cancel();
  MDNS.end();
  MDNS.begin(Utils::getHostName().c_str());
  MDNS.setInstanceName(Utils::getDeviceName().c_str());
}

void DirConManager::update() {
  notificationTimer.tick();
}

bool DirConManager::doNotifications(void *arg) {
  if (started) {
  }
  return true;
}

void DirConManager::handleNewClient(void *arg, AsyncClient *client) {
  bool clientAccepted = false;
  log_i("New DirCon connection from %s", client->remoteIP().toString().c_str());
  for (size_t clientIndex = 0; clientIndex < DIRCON_MAX_CLIENTS; clientIndex++) {
    if ((dirConClients[clientIndex] == nullptr) || ((dirConClients[clientIndex] != nullptr) && (!dirConClients[clientIndex]->connected()))) {
      dirConClients[clientIndex] = client;
      clientAccepted = true;
      break;
    }
  }
  if (clientAccepted) {
    log_i("Free connection slot found, DirCon connection from %s accepted", client->remoteIP().toString().c_str());
    client->onData(&DirConManager::handleDirConData, NULL);
    client->onError(&DirConManager::handleDirConError, NULL);
    client->onDisconnect(&DirConManager::handleDirConDisconnect, NULL);
    client->onTimeout(&DirConManager::handleDirConTimeOut, NULL);
  } else {
    log_e("Maximum number of %d connections reached, DirCon connection from %s rejected", DIRCON_MAX_CLIENTS, client->remoteIP().toString().c_str());
    client->abort();
  }
}

void DirConManager::handleDirConData(void *arg, AsyncClient *client, void *data, size_t len) {
  size_t clientIndex = DirConManager::getDirConClientIndex(client);
  if (clientIndex != (DIRCON_MAX_CLIENTS + 1)) {
    // log_d("DirCon client #%d with IP %s sent data, hex value: %s", clientIndex, client->remoteIP().toString().c_str(), Utils::getHexString((uint8_t *)data, len).c_str());
    DirConMessage currentMessage;
    if (!currentMessage.parse((uint8_t *)data, len, 0)) {
      log_e("Error handling data from DirCon client: Unable to parse DirCon message");
      return;
    }
    if (!DirConManager::processDirConMessage(&currentMessage, client, clientIndex)) {
      log_e("Error handling data from DirCon client: Unable to process DirCon message");
      return;
    }
  } else {
    log_e("Error handling data from DirCon client: Client slot not found");
  }
}

void DirConManager::handleDirConError(void *arg, AsyncClient *client, int8_t error) {
  log_e("DirCon client connection error %s from %s, stopping client...", client->errorToString(error), client->remoteIP().toString().c_str());
  client->stop();
}

void DirConManager::handleDirConDisconnect(void *arg, AsyncClient *client) {
  log_i("DirCon client disconnected");
}

void DirConManager::handleDirConTimeOut(void *arg, AsyncClient *client, uint32_t time) {
  log_e("DirCon client ACK timeout from %s, stopping client...", client->remoteIP().toString().c_str());
  client->stop();
}

void DirConManager::handleServiceManagerOnServiceAdded(Service *service) {
  if (service->isAdvertised()) {
    String serviceUUIDs = "";
    for (auto currentService = serviceManager->getServices()->begin(); currentService != serviceManager->getServices()->end(); currentService++) {
      if (currentService->isAdvertised()) {
        if (serviceUUIDs != "") {
          serviceUUIDs += ",";
        }
        serviceUUIDs += currentService->UUID.to16().toString().c_str();
      }
    }
    log_i("Updating advertised service UUIDs: %s", serviceUUIDs.c_str());
    MDNS.addServiceTxt(DIRCON_MDNS_SERVICE_NAME, DIRCON_MDNS_SERVICE_PROTOCOL, "ble-service-uuids", serviceUUIDs.c_str());
  }
}

size_t DirConManager::getDirConClientIndex(AsyncClient *client) {
  for (size_t clientIndex = 0; clientIndex < DIRCON_MAX_CLIENTS; clientIndex++) {
    if (dirConClients[clientIndex] != nullptr) {
      if (dirConClients[clientIndex] == client) {
        return clientIndex;
        break;
      }
    }
  }
  return DIRCON_MAX_CLIENTS + 1;
}

bool DirConManager::processDirConMessage(DirConMessage *dirConMessage, AsyncClient *client, size_t clientIndex) {
  Service *service = nullptr;
  Characteristic *characteristic = nullptr;
  std::vector<uint8_t> returnData;
  DirConMessage returnMessage;
  returnMessage.Identifier = dirConMessage->Identifier;
  returnMessage.UUID = dirConMessage->UUID;
  switch (dirConMessage->Identifier) {
    case DIRCON_MSGID_DISCOVER_SERVICES:
      log_d("DirCon service discovery request, returning services");
      returnMessage.ResponseCode = DIRCON_RESPCODE_SUCCESS_REQUEST;
      for (auto currentService = serviceManager->getServices()->begin(); currentService != serviceManager->getServices()->end(); currentService++) {
        returnMessage.AdditionalUUIDs.push_back(currentService->UUID);
      }
      break;
    case DIRCON_MSGID_DISCOVER_CHARACTERISTICS:
      log_d("DirCon characteristic discovery request for service UUID %s, returning characteristics", dirConMessage->UUID.to128().toString().c_str());
      returnMessage.ResponseCode = DIRCON_RESPCODE_SUCCESS_REQUEST;
      service = serviceManager->getService(dirConMessage->UUID);
      if (service == nullptr) {
        returnMessage.ResponseCode = DIRCON_RESPCODE_SERVICE_NOT_FOUND;
        break;
      }
      for (auto characteristic = service->getCharacteristics()->begin(); characteristic != service->getCharacteristics()->end(); characteristic++) {
        returnMessage.AdditionalUUIDs.push_back(characteristic->UUID);
        returnMessage.AdditionalData.push_back(getDirConProperties(characteristic->Properties));
      }
      break;
    case DIRCON_MSGID_ENABLE_CHARACTERISTIC_NOTIFICATIONS:
      log_d("DirCon characteristic enable notification request for characteristic UUID %s", dirConMessage->UUID.to128().toString().c_str());
      returnMessage.ResponseCode = DIRCON_RESPCODE_SUCCESS_REQUEST;
      characteristic = serviceManager->getCharacteristic(dirConMessage->UUID);
      if (characteristic == nullptr) {
        returnMessage.ResponseCode = DIRCON_RESPCODE_CHARACTERISTIC_NOT_FOUND;
        break;
      }
      if (dirConMessage->AdditionalData.size() != 1) {
        returnMessage.Identifier = DIRCON_RESPCODE_CHARACTERISTIC_OPERATION_NOT_SUPPORTED;
        break;
      }
      if (dirConMessage->AdditionalData.at(0) == 0) {
        if (characteristic->isSubscribed(clientIndex)) {
          characteristic->removeSubscription(clientIndex);
        }
      } else {
        if (!characteristic->isSubscribed(clientIndex)) {
          characteristic->addSubscription(clientIndex);
        }
      }
      break;
    case DIRCON_MSGID_WRITE_CHARACTERISTIC:
      log_d("DirCon write characteristic request for characteristic UUID %s", dirConMessage->UUID.to128().toString().c_str());
      returnMessage.ResponseCode = DIRCON_RESPCODE_SUCCESS_REQUEST;
      characteristic = serviceManager->getCharacteristic(dirConMessage->UUID);
      if (characteristic == nullptr) {
        returnMessage.ResponseCode = DIRCON_RESPCODE_CHARACTERISTIC_NOT_FOUND;
        break;
      }
      if (dirConMessage->AdditionalData.size() != 1) {
        returnMessage.Identifier = DIRCON_RESPCODE_CHARACTERISTIC_OPERATION_NOT_SUPPORTED;
        break;
      }
      if (characteristic->getService() != nullptr) {
        if (!characteristic->getService()->isInternal()) {
          
          if (!BTDeviceManager::writeBLECharacteristic(characteristic->getService()->UUID, characteristic->UUID, dirConMessage->AdditionalData)) {
            returnMessage.Identifier = DIRCON_RESPCODE_CHARACTERISTIC_OPERATION_NOT_SUPPORTED;
            break;
          }
        } else {
            //TODO!!!
            returnMessage.Identifier = DIRCON_RESPCODE_CHARACTERISTIC_OPERATION_NOT_SUPPORTED;
            break;
        }
      } else {
        returnMessage.ResponseCode = DIRCON_RESPCODE_CHARACTERISTIC_NOT_FOUND;
        break;
      }
      break;
    case DIRCON_MSGID_READ_CHARACTERISTIC:
      log_d("DirCon read characteristic request for characteristic UUID %s", dirConMessage->UUID.to128().toString().c_str());
      returnMessage.ResponseCode = DIRCON_RESPCODE_SUCCESS_REQUEST;
      characteristic = serviceManager->getCharacteristic(dirConMessage->UUID);
      if (characteristic == nullptr) {
        returnMessage.ResponseCode = DIRCON_RESPCODE_CHARACTERISTIC_NOT_FOUND;
        break;
      }
      if (characteristic->getService() != nullptr) {
        if (!characteristic->getService()->isInternal()) {
          returnMessage.AdditionalData = BTDeviceManager::readBLECharacteristic(characteristic->getService()->UUID, characteristic->UUID);
          if (returnMessage.AdditionalData.size() == 0) {
            returnMessage.Identifier = DIRCON_RESPCODE_CHARACTERISTIC_OPERATION_NOT_SUPPORTED;
            break;
          }
        } else {
          // TODO
        }
      } else {
        returnMessage.ResponseCode = DIRCON_RESPCODE_CHARACTERISTIC_NOT_FOUND;
        break;
      }

      break;
    default:
      log_e("Unknown identifier %d in DirCon message, skipping further processing", dirConMessage->Identifier);
      return false;
      break;
  }
  returnData = returnMessage.encode(dirConMessage->SequenceNumber);
  if (returnData.size() > 0) {
    log_d("Sending data to DirCon client #%d with IP %s, hex value: %s", clientIndex, client->remoteIP().toString().c_str(), Utils::getHexString(returnData).c_str());
    client->write((char *)returnData.data(), returnData.size());
  } else {
    log_e("Error encoding DirCon message, aborting");
    return false;
  }

  return true;
}

uint8_t DirConManager::getDirConProperties(uint32_t characteristicProperties) {
  uint8_t returnValue = 0x00;
  if (characteristicProperties & READ == READ) {
    returnValue = returnValue | DIRCON_CHAR_PROP_FLAG_READ;
  }
  if (characteristicProperties & WRITE == WRITE) {
    returnValue = returnValue | DIRCON_CHAR_PROP_FLAG_WRITE;
  }
  if (characteristicProperties & NOTIFY == NOTIFY) {
    returnValue = returnValue | DIRCON_CHAR_PROP_FLAG_NOTIFY;
  }
  return returnValue;
}
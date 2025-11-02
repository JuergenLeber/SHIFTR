#include <BTAdvertisedDeviceCallbacks.h>
#include <BTDeviceManager.h>
#include <NimBLEDevice.h>

void BTAdvertisedDeviceCallbacks::onResult(NimBLEAdvertisedDevice* advertisedDevice) {
  bool addDevice = true;
  for (NimBLEUUID uuid : BTDeviceManager::remoteDeviceFilterUUIDs) {
    if (!advertisedDevice->isAdvertisingService(uuid)) {
      addDevice = false;
      break;
    }
    if (!addDevice) {
      break;
    }
  }
  if (addDevice) {
    BTDeviceManager::addScannedDevice(NimBLEAdvertisedDevice(*advertisedDevice));
    if (BTDeviceManager::getRemoteDevice() != nullptr) {
      BTDeviceManager::stopScan();
    }
  }
}

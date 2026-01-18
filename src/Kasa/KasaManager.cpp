#include "KasaManager.h"

KasaManager::KasaManager() : deviceCount(0) {
}

void KasaManager::begin() {
  loadDevices();
  if (deviceCount > 0) {
    refreshStates();
  }
}

int KasaManager::scanDevices() {
  Serial.println("Scanning for Kasa devices...");

  // Clear existing devices
  deviceCount = 0;

  // Use KASAUtil to scan for devices (timeout in ms)
  int found = kasaUtil.ScanDevices(3000);

  Serial.print("KASAUtil found ");
  Serial.print(found);
  Serial.println(" devices");

  // Copy discovered devices to our list
  for (int i = 0; i < found && deviceCount < MAX_KASA_DEVICES; i++) {
    KASASmartPlug* plug = kasaUtil.GetSmartPlugByIndex(i);
    if (plug != nullptr) {
      strncpy(devices[deviceCount].alias, plug->alias, sizeof(devices[deviceCount].alias) - 1);
      devices[deviceCount].alias[sizeof(devices[deviceCount].alias) - 1] = '\0';
      strncpy(devices[deviceCount].ip, plug->ip_address, sizeof(devices[deviceCount].ip) - 1);
      devices[deviceCount].ip[sizeof(devices[deviceCount].ip) - 1] = '\0';
      devices[deviceCount].state = plug->state == 1;
      devices[deviceCount].online = true;

      Serial.print("Found: ");
      Serial.print(devices[deviceCount].alias);
      Serial.print(" at ");
      Serial.println(devices[deviceCount].ip);

      deviceCount++;
    }
  }

  Serial.print("Scan complete. Saved ");
  Serial.print(deviceCount);
  Serial.println(" devices.");

  // Save discovered devices
  if (deviceCount > 0) {
    saveDevices();
  }

  return deviceCount;
}

bool KasaManager::toggleDevice(int index) {
  if (index < 0 || index >= deviceCount) {
    return false;
  }

  KasaDevice& dev = devices[index];

  // Create a KASASmartPlug instance for this device
  KASASmartPlug plug(dev.alias, dev.ip);

  // Toggle the relay
  bool newState = !dev.state;
  plug.SetRelayState(newState ? 1 : 0);

  // Verify by querying the device
  int result = plug.QueryInfo();
  if (result == 0) {
    dev.state = plug.state == 1;
    dev.online = true;
    return true;
  } else {
    // Assume it worked if we can't verify
    dev.state = newState;
    dev.online = true;
    return true;
  }
}

int KasaManager::getDeviceCount() const {
  return deviceCount;
}

KasaDevice* KasaManager::getDevice(int index) {
  if (index < 0 || index >= deviceCount) {
    return nullptr;
  }
  return &devices[index];
}

String KasaManager::getDevicesJson() {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();

  for (int i = 0; i < deviceCount; i++) {
    JsonObject obj = arr.add<JsonObject>();
    obj["id"] = i;
    obj["alias"] = devices[i].alias;
    obj["ip"] = devices[i].ip;
    obj["state"] = devices[i].state;
    obj["online"] = devices[i].online;
  }

  String result;
  serializeJson(doc, result);
  return result;
}

void KasaManager::refreshStates() {
  Serial.println("Refreshing Kasa device states...");

  for (int i = 0; i < deviceCount; i++) {
    checkDevice(i);
    delay(50);  // Small delay between requests
  }
}

bool KasaManager::checkDevice(int index) {
  if (index < 0 || index >= deviceCount) {
    return false;
  }

  KasaDevice& dev = devices[index];

  // Create a KASASmartPlug instance and query it
  KASASmartPlug plug(dev.alias, dev.ip);
  int result = plug.QueryInfo();

  if (result == 0) {
    dev.online = true;
    dev.state = plug.state == 1;
    return true;
  } else {
    dev.online = false;
    return false;
  }
}

void KasaManager::saveDevices() {
  prefs.begin("kasa", false);  // read-write

  prefs.putInt("count", deviceCount);

  for (int i = 0; i < deviceCount; i++) {
    String key = "d" + String(i);
    prefs.putString((key + "a").c_str(), devices[i].alias);
    prefs.putString((key + "i").c_str(), devices[i].ip);
  }

  prefs.end();
  Serial.println("Kasa devices saved to flash.");
}

void KasaManager::loadDevices() {
  prefs.begin("kasa", true);  // read-only

  deviceCount = prefs.getInt("count", 0);
  if (deviceCount > MAX_KASA_DEVICES) {
    deviceCount = MAX_KASA_DEVICES;
  }

  for (int i = 0; i < deviceCount; i++) {
    String key = "d" + String(i);
    String alias = prefs.getString((key + "a").c_str(), "");
    String ip = prefs.getString((key + "i").c_str(), "");

    strncpy(devices[i].alias, alias.c_str(), sizeof(devices[i].alias) - 1);
    devices[i].alias[sizeof(devices[i].alias) - 1] = '\0';
    strncpy(devices[i].ip, ip.c_str(), sizeof(devices[i].ip) - 1);
    devices[i].ip[sizeof(devices[i].ip) - 1] = '\0';
    devices[i].state = false;
    devices[i].online = false;
  }

  prefs.end();

  if (deviceCount > 0) {
    Serial.print("Loaded ");
    Serial.print(deviceCount);
    Serial.println(" Kasa devices from flash.");
  }
}

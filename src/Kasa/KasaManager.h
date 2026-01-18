#ifndef KASA_MANAGER_H
#define KASA_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <KasaSmartPlug.h>
#include <ArduinoJson.h>

#define MAX_KASA_DEVICES 10

struct KasaDevice {
  char alias[32];
  char ip[16];
  bool state;
  bool online;
};

class KasaManager {
public:
  KasaManager();

  // Initialize and load saved devices
  void begin();

  // Scan network for Kasa devices
  int scanDevices();

  // Toggle device by index
  bool toggleDevice(int index);

  // Get device count
  int getDeviceCount() const;

  // Get device by index
  KasaDevice* getDevice(int index);

  // Get all devices as JSON string
  String getDevicesJson();

  // Refresh state of all devices
  void refreshStates();

  // Save devices to flash
  void saveDevices();

  // Load devices from flash
  void loadDevices();

private:
  KasaDevice devices[MAX_KASA_DEVICES];
  int deviceCount;
  KASAUtil kasaUtil;
  Preferences prefs;

  // Check if device is online and get state
  bool checkDevice(int index);
};

#endif

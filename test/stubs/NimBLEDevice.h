#pragma once
#include <Arduino.h>
#define ESP_PWR_LVL_N12 0
struct NimBLEAddrBase { uint8_t val[6]; };
struct NimBLEAddress {
  const NimBLEAddrBase* getBase() const { static NimBLEAddrBase b{}; return &b; }
  uint8_t getType() const { return 0; }
};
struct NimBLEAdvertisedDevice {
  NimBLEAddress getAddress() const { return NimBLEAddress(); }
  int getRSSI() const { return -50; }
  std::vector<uint8_t> getPayload() const { return std::vector<uint8_t>(); }
};
struct NimBLEScanCallbacks {
  virtual void onResult(const NimBLEAdvertisedDevice*) {}
  virtual ~NimBLEScanCallbacks() {}
};
struct NimBLEScan {
  void setScanCallbacks(NimBLEScanCallbacks*,bool){}
  void setActiveScan(bool){} void setInterval(int){} void setWindow(int){}
  void setDuplicateFilter(bool){} void setMaxResults(int){}
  bool start(int,bool,bool){return true;} bool stop(){return true;}
};
struct NimBLEDeviceStub {
  static void init(const char*){} static void setPower(int){}
  static NimBLEScan* getScan(){ static NimBLEScan s; return &s; }
};
#define NimBLEDevice NimBLEDeviceStub

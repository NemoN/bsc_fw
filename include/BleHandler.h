// Copyright (c) 2022 tobias
//
// This software is released under the MIT License.
// https://opensource.org/licenses/MIT


#ifndef BleHandler_h
#define BleHandler_h

#include "Arduino.h"
#if defined(ENABLE_LEGACY_NEEY_BLE)
#include "NimBLEDevice.h"
#endif
#include "WebSettings.h"
#include "defines.h"
#include "BmsData.h"


enum btDoConnectEnums {btDoConnect, btConnectionSetup, btDoConnectionIdle, btDoConnectionWaitStart};
enum e_btBalancerOnOff {e_BalancerWaitForCmd, e_BalancerChangeToOff, e_BalancerIsOff, e_BalancerChangeToOn, e_BalancerIsOn};

#if defined(ENABLE_LEGACY_NEEY_BLE)
struct bleDevice {
  bool isConnect;
  btDoConnectEnums doConnect;
  String macAdr;
  uint8_t deviceTyp;
  NimBLERemoteCharacteristic* pChr;
  uint8_t sendDataStep;
  e_btBalancerOnOff balancerOn;
};
#endif


class BleHandler {
public:
  BleHandler();
  ~BleHandler();

  void init();
  void run();

  void startScan();
  std::string getBtScanResultAsHtmlTable();

  void sendDataToNeey();
  void readDataFromNeey();

  static void setBalancerState(uint8_t devNr, boolean Alarm);

private:
#if defined(ENABLE_LEGACY_NEEY_BLE)
  uint8_t timer_startScan;
  bool bo_mStartManualScan;

  bool handleConnectionToDevices();
  void handleDisconnectionToDevices();
#endif

};

#endif

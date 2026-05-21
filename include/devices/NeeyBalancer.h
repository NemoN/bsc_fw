// Copyright (c) 2022 tobias
//
// This software is released under the MIT License.
// https://opensource.org/licenses/MIT


#ifndef NEEYBALANCER_H
#define NEEYBALANCER_H

#include "defines.h"
#if defined(ENABLE_LEGACY_NEEY_BLE)
#include "NimBLEDevice.h"
#endif

//
#define NEEYBAL4A_CMD_WRITE                          0x05

#define NEEYBAL4A_FUNC_SETTING_CELLS                 0x01
#define NEEYBAL4A_FUNC_SETTING_START_VOL             0x02
#define NEEYBAL4A_FUNC_SETTING_MAX_BAL_CURRENT       0x03
#define NEEYBAL4A_FUNC_SETTING_SLEEP_VOLTAGE         0x04
#define NEEYBAL4A_FUNC_SETTING_EQUALIZATION_VOLTAGE  0x17
#define NEEYBAL4A_FUNC_SETTING_BAT_CAP               0x16
#define NEEYBAL4A_FUNC_SETTING_BAT_TYPE              0x15
#define NEEYBAL4A_FUNC_SETTING_BUZZER_MODE           0x14
#define NEEYBAL4A_FUNC_SETTING_BALLANCER_ON_OFF      0x0D


//Head
#define OFFSET_NEEYBAL4A_HEAD_STARTFRAME1             0
#define OFFSET_NEEYBAL4A_HEAD_STARTFRAME2             1
#define OFFSET_NEEYBAL4A_HEAD_ADRESS                  2
#define OFFSET_NEEYBAL4A_HEAD_RW                      3
#define OFFSET_NEEYBAL4A_HEAD_CMD1                    4
#define OFFSET_NEEYBAL4A_HEAD_CMD2                    5
#define OFFSET_NEEYBAL4A_HEAD_DATALEN                 6

//Data 0x02
#define OFFSET_NEEYBAL4A_DATA0x2_SERIALNR                 8
#define OFFSET_NEEYBAL4A_DATA0x2_CELVOLTAGE               9
#define OFFSET_NEEYBAL4A_DATA0x2_CELLRESISTANCE         105
#define OFFSET_NEEYBAL4A_DATA0x2_TOTALVOLTAGE           201
#define OFFSET_NEEYBAL4A_DATA0x2_AVERAGEVOLTAGE         205
#define OFFSET_NEEYBAL4A_DATA0x2_DELTACELLVOLTAGE       209
#define OFFSET_NEEYBAL4A_DATA0x2_MAXVOLTCELLNR          213
#define OFFSET_NEEYBAL4A_DATA0x2_MINVOLTCELLNR          214
#define OFFSET_NEEYBAL4A_DATA0x2_SINGLENR               215
#define OFFSET_NEEYBAL4A_DATA0x2_BALANCING              216
#define OFFSET_NEEYBAL4A_DATA0x2_BALANCINGCUR           217
#define OFFSET_NEEYBAL4A_DATA0x2_TEMPERATUR             221
#define OFFSET_NEEYBAL4A_DATA0x2_ERRCELLDETECTION       229
#define OFFSET_NEEYBAL4A_DATA0x2_ERRCELLOV              232
#define OFFSET_NEEYBAL4A_DATA0x2_ERRCELLUV              235
#define OFFSET_NEEYBAL4A_DATA0x2_ERRCELLPOLARITY        238
#define OFFSET_NEEYBAL4A_DATA0x2_ERRHIGHLINERESISTANCE  241
#define OFFSET_NEEYBAL4A_DATA0x2_ERRSYSOVERHEATING      244
#define OFFSET_NEEYBAL4A_DATA0x2_CHARGINGFAULT          245
#define OFFSET_NEEYBAL4A_DATA0x2_DISCHARGEFAULT         246


class NeeyBalancer {
public:
  NeeyBalancer();

#if defined(ENABLE_LEGACY_NEEY_BLE)
  static void neeyBalancerCopyData(uint8_t devNr, uint8_t* pData, size_t length);
  static void neeyBtBuildSendData(uint8_t devTyp, uint8_t* frame, uint8_t byte3, uint8_t cmd, uint8_t func, uint32_t value);
  static void neeyBtBuildSendData(uint8_t devTyp, uint8_t* frame, uint8_t cmd, uint8_t func, uint32_t value);
  static void neeyBtBuildSendData(uint8_t devTyp, uint8_t* frame, uint8_t cmd, uint8_t func, float value);
  static bool neeyWriteData(uint8_t devTyp, uint8_t btDevNr, NimBLERemoteCharacteristic* pChr);
  static void neeyWriteData_GotoStartStep(uint8_t startStep);
  static bool neeyWriteData_GotoNextStep();
  static void neeySetBalancerOnOff(NimBLERemoteCharacteristic* pChr, boolean state);
  static void neeyWriteMsg2(uint8_t devTyp, NimBLERemoteCharacteristic* pChr);
  static void sendNeeyConnectMsg(uint8_t devTyp, NimBLERemoteCharacteristic* pChr);
#endif

  static float    neeyGetReadbackDataFloat(uint8_t devNr, uint8_t dataType);
  static uint32_t neeyGetReadbackDataInt(uint8_t devNr, uint8_t dataType);
  static void     getNeeyReadbackDataAsString(std::string &value);

#if defined(ENABLE_LEGACY_NEEY_BLE)
private:
  static uint8_t neeyBtCrc(uint8_t devTyp, uint8_t* data, uint16_t len);
#endif
};

#endif

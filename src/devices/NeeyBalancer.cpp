// Copyright (c) 2022 tobias
//
// This software is released under the MIT License.
// https://opensource.org/licenses/MIT


#include "devices/NeeyBalancer.h"
#include "BmsData.h"
#include "WebSettings.h"
#include "Utility.h"

static const char* TAG = "NEEY";

#if defined(ENABLE_LEGACY_NEEY_BLE)
static byte NeeyBalancer_cmdBalanceOn[20] PROGMEM =  {0xaa, 0x55, 0x11, 0x00, 0x05, 0x0d, 0x14, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf3, 0xff};
static byte NeeyBalancer_cmdBalanceOff[20] PROGMEM = {0xaa, 0x55, 0x11, 0x00, 0x05, 0x0d, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf2, 0xff};

static uint8_t u8_neeySendStep=0;
static uint8_t  neeyPacketNumber[MUBER_OF_DATA_DEVICES] = {0};
static uint32_t neeyLastRxBytes[MUBER_OF_DATA_DEVICES] = {0};
static uint32_t neeyRxDataType[MUBER_OF_DATA_DEVICES] = {0};


void NeeyBalancer::neeyBalancerCopyData(uint8_t devNr, uint8_t* pData, size_t length)
{
  if(devNr >= MUBER_OF_DATA_DEVICES) return;

  if(length <= 20)
  {
    if((pData[0]==0x55 && pData[1]==0xAA && pData[2]==0x11 && pData[3]==0x01 && pData[4]==0x04 && pData[5]==0x00 && pData[6]==0x64))
    {
      neeyRxDataType[devNr] = 1;
      neeyPacketNumber[devNr] = 0;
      return;
    }
    else if(pData[0]==0x55 && pData[1]==0xAA && pData[2]==0x11 && pData[3]==0x01 && pData[4]==0x02 && pData[5]==0x00)
    {
      neeyRxDataType[devNr] = 2;
      neeyPacketNumber[devNr] = 0;
    }

    if(neeyRxDataType[devNr] == 1)
    {
      if(neeyPacketNumber[devNr] == 0)
      {
        neeyPacketNumber[devNr] = 1;
        bmsDataSemaphoreTake();
        memcpy(getBmsSettingsReadback(devNr), &pData[8], 11);
        bmsDataSemaphoreGive();
      }
      else if(neeyPacketNumber[devNr] == 1)
      {
        neeyPacketNumber[devNr] = 2;
        bmsDataSemaphoreTake();
        memcpy(getBmsSettingsReadback(devNr)+11, &pData[0], 20);
        bmsDataSemaphoreGive();
      }
      else if(neeyPacketNumber[devNr] == 2)
      {
        neeyPacketNumber[devNr] = 3;
        bmsDataSemaphoreTake();
        memcpy(getBmsSettingsReadback(devNr)+31, &pData[0], 1);
        bmsDataSemaphoreGive();
        setBmsLastDataMillis(devNr,millis());
      }
      return;
    }

    if(neeyRxDataType[devNr] == 2)
    {
      if(neeyPacketNumber[devNr] == 0)
      {
        float f_lTmpValue;
        memcpy(&f_lTmpValue, pData+OFFSET_NEEYBAL4A_DATA0x2_CELVOLTAGE, 4);
        setBmsCellVoltage(devNr,0,(uint16_t)(f_lTmpValue*1000));

        memcpy(&f_lTmpValue, pData+OFFSET_NEEYBAL4A_DATA0x2_CELVOLTAGE+4, 4);
        setBmsCellVoltage(devNr,1,(uint16_t)(f_lTmpValue*1000));

        memcpy(((uint8_t*)&neeyLastRxBytes[devNr]), pData+OFFSET_NEEYBAL4A_DATA0x2_CELVOLTAGE+8, 3);
      }
      else if(neeyPacketNumber[devNr] >= 1 && neeyPacketNumber[devNr] <= 4)
      {
        float f_lTmpValue;
        uint8_t ii = (neeyPacketNumber[devNr] - 1) * 5;

        memcpy(((uint8_t*)&f_lTmpValue), ((uint8_t*)&neeyLastRxBytes[devNr]), 3);
        memcpy(((uint8_t*)&f_lTmpValue)+3, pData, 1);
        setBmsCellVoltage(devNr, ii+2, (uint16_t)(f_lTmpValue*1000));

        memcpy(&f_lTmpValue, pData+1, 4);
        setBmsCellVoltage(devNr, ii+3, (uint16_t)(f_lTmpValue*1000));
        memcpy(&f_lTmpValue, pData+5, 4);
        setBmsCellVoltage(devNr, ii+4, (uint16_t)(f_lTmpValue*1000));
        memcpy(&f_lTmpValue, pData+9, 4);
        setBmsCellVoltage(devNr, ii+5, (uint16_t)(f_lTmpValue*1000));
        memcpy(&f_lTmpValue, pData+13, 4);
        setBmsCellVoltage(devNr, ii+6, (uint16_t)(f_lTmpValue*1000));

        memcpy(((uint8_t*)&neeyLastRxBytes[devNr]), pData+17, 3);
      }
      else if(neeyPacketNumber[devNr] == 10)
      {
        float f_lTmpValue;

        memcpy(&f_lTmpValue, pData+1, 4);
        setBmsTotalVoltage(devNr,f_lTmpValue);
        memcpy(&f_lTmpValue, pData+5, 4);
        setBmsAvgVoltage(devNr,(uint16_t)(f_lTmpValue*1000));
        memcpy(&f_lTmpValue, pData+9, 4);
        setBmsMaxCellDifferenceVoltage(devNr,(uint16_t)(f_lTmpValue*1000));

        setBmsMaxVoltageCellNumber(devNr,pData[13]);
        uint16_t tCellVoltage = getBmsCellVoltage(devNr, pData[13]);
        setBmsMaxCellVoltage(devNr, tCellVoltage);

        setBmsMinVoltageCellNumber(devNr,pData[14]);
        tCellVoltage = getBmsCellVoltage(devNr, pData[14]);
        setBmsMinCellVoltage(devNr, tCellVoltage);

        memcpy(((uint8_t*)&neeyLastRxBytes[devNr]), pData+17, 3);
      }
      else if(neeyPacketNumber[devNr] == 11)
      {
        float f_lTmpValue;

        memcpy(((uint8_t*)&f_lTmpValue), ((uint8_t*)&neeyLastRxBytes[devNr]), 3);
        memcpy(((uint8_t*)&f_lTmpValue)+3, pData, 1);
        setBmsBalancingCurrent(devNr,f_lTmpValue);

        memcpy(&f_lTmpValue, pData+1, 4);
        setBmsTempature(devNr,0,f_lTmpValue);
        memcpy(&f_lTmpValue, pData+5, 4);
        setBmsTempature(devNr,1,f_lTmpValue);

        setBmsLastDataMillis(devNr,millis());
      }

      if(neeyPacketNumber[devNr] <= 15) neeyPacketNumber[devNr]++;
      return;
    }
  }
  else
  {
    if((pData[0]==0x55 && pData[1]==0xAA && pData[2]==0x11 && pData[3]==0x01 && pData[4]==0x04 && pData[5]==0x00 && pData[6]==0x64))
    {
      bmsDataSemaphoreTake();
      memcpy(getBmsSettingsReadback(devNr), &pData[8], 32);
      bmsDataSemaphoreGive();
      return;
    }

    if(length == 241)
    {
      if(!(pData[0]==0x55 && pData[1]==0xAA && pData[2]==0x11 && pData[3]==0x01 && pData[4]==0x02 && pData[5]==0x00)) return;
      if((millis()-getBmsLastDataMillis(devNr))<500) return;

      float f_lTmpValue;
      uint32_t u32_lTmpValue;

      for(uint8_t i = 0; i < 24; i++)
      {
        memcpy(&f_lTmpValue, pData+OFFSET_NEEYBAL4A_DATA0x2_CELVOLTAGE+(i*4), 4);
        setBmsCellVoltage(devNr,i,(uint16_t)(f_lTmpValue*1000));
      }

      memcpy(&f_lTmpValue, pData+OFFSET_NEEYBAL4A_DATA0x2_AVERAGEVOLTAGE, 4);
      setBmsAvgVoltage(devNr,(uint16_t)(f_lTmpValue*1000));

      memcpy(&f_lTmpValue, pData+OFFSET_NEEYBAL4A_DATA0x2_DELTACELLVOLTAGE, 4);
      setBmsMaxCellDifferenceVoltage(devNr,(uint16_t)(f_lTmpValue*1000));

      memcpy(&u32_lTmpValue, pData+OFFSET_NEEYBAL4A_DATA0x2_ERRCELLOV, 4);
      setBmsErrors(devNr, u32_lTmpValue);

      bmsData_s *p_lBmsData = getBmsData();
      bmsDataSemaphoreTake();
      memcpy(&p_lBmsData->bmsMaxVoltageCellNumber[devNr], pData+OFFSET_NEEYBAL4A_DATA0x2_MAXVOLTCELLNR, 1);
      memcpy(&p_lBmsData->bmsMinVoltageCellNumber[devNr], pData+OFFSET_NEEYBAL4A_DATA0x2_MINVOLTCELLNR, 1);
      memcpy(&p_lBmsData->bmsIsBalancingActive[devNr], pData+OFFSET_NEEYBAL4A_DATA0x2_BALANCING, 1);
      bmsDataSemaphoreGive();

      memcpy(&f_lTmpValue, pData+OFFSET_NEEYBAL4A_DATA0x2_TOTALVOLTAGE, 4);
      setBmsTotalVoltage(devNr,f_lTmpValue);
      memcpy(&f_lTmpValue, pData+OFFSET_NEEYBAL4A_DATA0x2_BALANCINGCUR, 4);
      setBmsBalancingCurrent(devNr,f_lTmpValue);
      memcpy(&f_lTmpValue, pData+OFFSET_NEEYBAL4A_DATA0x2_TEMPERATUR, 4);
      setBmsTempature(devNr,0,f_lTmpValue);
      memcpy(&f_lTmpValue, pData+OFFSET_NEEYBAL4A_DATA0x2_TEMPERATUR+4, 4);
      setBmsTempature(devNr,1,f_lTmpValue);

      uint16_t f_lMaxZellVoltage = getBmsCellVoltage(devNr,getBmsMaxVoltageCellNumber(devNr));
      uint16_t f_lMinZellVoltage = getBmsCellVoltage(devNr,getBmsMinVoltageCellNumber(devNr));
      setBmsMaxCellVoltage(devNr, f_lMaxZellVoltage);
      setBmsMinCellVoltage(devNr, f_lMinZellVoltage);

      setBmsLastDataMillis(devNr,millis());
    }
  }
}


uint8_t NeeyBalancer::neeyBtCrc(uint8_t devTyp, uint8_t* data, uint16_t len)
{
  uint8_t crc = 0;
  for (uint16_t i = 0; i < len; i++)
  {
    if(devTyp == ID_BT_DEVICE_NEEY_GW_24S4EB) crc = crc ^ data[i];
    else crc = crc + data[i];
  }
  return crc;
}


void NeeyBalancer::neeyBtBuildSendData(uint8_t devTyp, uint8_t* frame, uint8_t byte3, uint8_t cmd, uint8_t func, uint32_t value)
{
  frame[0] = 0xAA;
  frame[1] = 0x55;
  frame[2] = 0x11;
  frame[3] = byte3;
  frame[4] = cmd;
  frame[5] = func;
  frame[6] = 0x14;
  frame[7] = 0x00;
  frame[8] = value >> 0;
  frame[9] = value >> 8;
  frame[10] = value >> 16;
  frame[11] = value >> 24;
  frame[12] = 0x00;
  frame[13] = 0x00;
  frame[14] = 0x00;
  frame[15] = 0x00;
  frame[16] = 0x00;
  frame[17] = 0x00;
  frame[18] = neeyBtCrc(devTyp, frame, 18);
  frame[19] = 0xFF;
}


void NeeyBalancer::neeyBtBuildSendData(uint8_t devTyp, uint8_t* frame, uint8_t cmd, uint8_t func, uint32_t value)
{
  neeyBtBuildSendData(devTyp, frame, 0x0, cmd, func, value);
}


void NeeyBalancer::neeyBtBuildSendData(uint8_t devTyp, uint8_t* frame, uint8_t cmd, uint8_t func, float value)
{
  uint32_t ui32_lValue;
  memcpy(&ui32_lValue, &value, 4);
  neeyBtBuildSendData(devTyp, frame, 0x0, cmd, func, ui32_lValue);
}


void NeeyBalancer::neeyWriteMsg2(uint8_t devTyp, NimBLERemoteCharacteristic* pChr)
{
  uint8_t frame[20];
  neeyBtBuildSendData(devTyp, frame, 0x01, 0x04, 0x0, (uint32_t)0x0);
  pChr->writeValue(frame, 20, true);
}


void NeeyBalancer::sendNeeyConnectMsg(uint8_t devTyp, NimBLERemoteCharacteristic* pChr)
{
  uint8_t frame[20];
  if(devTyp == ID_BT_DEVICE_NEEY_EK_24S4EB)
  {
    neeyBtBuildSendData(devTyp, frame, 0x01, 0x01, 0x0, (uint32_t)0x0);
    frame[18]=0x26;
    pChr->writeValue(frame, 20, true);
    delay(200);

    neeyBtBuildSendData(devTyp, frame, 0x01, 0x04, 0x0, (uint32_t)0x0);
    frame[18]=0x29;
    pChr->writeValue(frame, 20, true);
    delay(200);

    neeyBtBuildSendData(devTyp, frame, 0x01, 0x02, 0x0, (uint32_t)0x0);
    frame[18]=0x27;
    pChr->writeValue(frame, 20, true);
  }
}


void NeeyBalancer::neeySetBalancerOnOff(NimBLERemoteCharacteristic* pChr, boolean state)
{
  if(state) pChr->writeValue(NeeyBalancer_cmdBalanceOn, 20);
  else pChr->writeValue(NeeyBalancer_cmdBalanceOff, 20);
}


bool NeeyBalancer::neeyWriteData(uint8_t devTyp, uint8_t btDevNr, NimBLERemoteCharacteristic* pChr)
{
  bool ret=false;

  uint8_t frame[20];
  uint8_t u8_lValue;
  uint16_t u16_lValue;
  float f_lValue;

  switch(u8_neeySendStep)
  {
    case 1:
      neeyBtBuildSendData(devTyp, frame, 0x04, 0x0, (uint32_t)0x0);
      pChr->writeValue(frame, 20, true);
      break;
    case 2:
      u8_lValue=(uint8_t)WebSettings::getIntFlash(ID_PARAM_NEEY_BALANCER_ON,btDevNr,DT_ID_PARAM_NEEY_BALANCER_ON);
      if(u8_lValue==0) neeySetBalancerOnOff(pChr, false);
      else if(u8_lValue==110) neeySetBalancerOnOff(pChr, true);
      break;
    case 3:
      u8_lValue=(uint8_t)WebSettings::getIntFlash(ID_PARAM_NEEY_BAT_TYPE,btDevNr,DT_ID_PARAM_NEEY_BAT_TYPE);
      neeyBtBuildSendData(devTyp, frame, NEEYBAL4A_CMD_WRITE, NEEYBAL4A_FUNC_SETTING_BAT_TYPE, (uint32_t)u8_lValue);
      pChr->writeValue(frame, 20, true);
      break;
    case 4:
      u8_lValue=(uint8_t)WebSettings::getIntFlash(ID_PARAM_NEEY_CELLS,btDevNr,DT_ID_PARAM_NEEY_CELLS);
      neeyBtBuildSendData(devTyp, frame, NEEYBAL4A_CMD_WRITE, NEEYBAL4A_FUNC_SETTING_CELLS, (uint32_t)u8_lValue);
      pChr->writeValue(frame, 20, true);
      break;
    case 5:
      f_lValue=WebSettings::getFloatFlash(ID_PARAM_NEEY_START_VOLTAGE,btDevNr);
      neeyBtBuildSendData(devTyp, frame, NEEYBAL4A_CMD_WRITE, NEEYBAL4A_FUNC_SETTING_START_VOL, f_lValue);
      pChr->writeValue(frame, 20, true);
      break;
    case 6:
      f_lValue=WebSettings::getFloatFlash(ID_PARAM_NEEY_MAX_BALANCE_CURRENT,btDevNr);
      neeyBtBuildSendData(devTyp, frame, NEEYBAL4A_CMD_WRITE, NEEYBAL4A_FUNC_SETTING_MAX_BAL_CURRENT, f_lValue);
      pChr->writeValue(frame, 20, true);
      break;
    case 7:
      f_lValue=WebSettings::getFloatFlash(ID_PARAM_NEEY_EQUALIZATION_VOLTAGE,btDevNr);
      neeyBtBuildSendData(devTyp, frame, NEEYBAL4A_CMD_WRITE, NEEYBAL4A_FUNC_SETTING_EQUALIZATION_VOLTAGE, f_lValue);
      pChr->writeValue(frame, 20, true);
      break;
    case 8:
      f_lValue=WebSettings::getFloatFlash(ID_PARAM_NEEY_SLEEP_VOLTAGE,btDevNr);
      neeyBtBuildSendData(devTyp, frame, NEEYBAL4A_CMD_WRITE, NEEYBAL4A_FUNC_SETTING_SLEEP_VOLTAGE, f_lValue);
      pChr->writeValue(frame, 20, true);
      break;
    case 9:
      u16_lValue=(uint16_t)WebSettings::getIntFlash(ID_PARAM_NEEY_BAT_CAPACITY,btDevNr,DT_ID_PARAM_NEEY_BAT_CAPACITY);
      neeyBtBuildSendData(devTyp, frame, NEEYBAL4A_CMD_WRITE, NEEYBAL4A_FUNC_SETTING_BAT_CAP, (uint32_t)u16_lValue);
      pChr->writeValue(frame, 20, true);
      break;
    case 10:
      neeyBtBuildSendData(devTyp, frame, 0x01, 0x04, 0x0, (uint32_t)0x0);
      pChr->writeValue(frame, 20, true);
      break;
    case 11:
      neeyBtBuildSendData(devTyp, frame, 0x04, 0x0, (uint32_t)0x0);
      pChr->writeValue(frame, 20, true);
      ret=true;
      break;
  }

  return ret;
}


void NeeyBalancer::neeyWriteData_GotoStartStep(uint8_t startStep)
{
  if(startStep==0) startStep=1;
  u8_neeySendStep=startStep;
}


bool NeeyBalancer::neeyWriteData_GotoNextStep()
{
  if(u8_neeySendStep==11)
  {
    u8_neeySendStep=0;
    return true;
  }

  u8_neeySendStep++;
  return false;
}
#endif


float NeeyBalancer::neeyGetReadbackDataFloat(uint8_t devNr, uint8_t dataType)
{
  float retValue=0;
  bmsDataSemaphoreTake();

  switch(dataType)
  {
    case NEEYBAL4A_FUNC_SETTING_START_VOL:
      memcpy(&retValue, &getBmsSettingsReadback(devNr)[1], 4);
      break;
    case NEEYBAL4A_FUNC_SETTING_MAX_BAL_CURRENT:
      memcpy(&retValue, &getBmsSettingsReadback(devNr)[5], 4);
      break;
    case NEEYBAL4A_FUNC_SETTING_SLEEP_VOLTAGE:
      memcpy(&retValue, &getBmsSettingsReadback(devNr)[9], 4);
      break;
    case NEEYBAL4A_FUNC_SETTING_EQUALIZATION_VOLTAGE:
      memcpy(&retValue, &getBmsSettingsReadback(devNr)[20], 4);
      break;
  }

  bmsDataSemaphoreGive();
  return retValue;
}


uint32_t NeeyBalancer::neeyGetReadbackDataInt(uint8_t devNr, uint8_t dataType)
{
  uint32_t retValue=0;
  bmsDataSemaphoreTake();

  switch(dataType)
  {
    case NEEYBAL4A_FUNC_SETTING_CELLS:
      memcpy(&retValue, &getBmsSettingsReadback(devNr)[0], 1);
      break;
    case NEEYBAL4A_FUNC_SETTING_BAT_TYPE:
      memcpy(&retValue, &getBmsSettingsReadback(devNr)[15], 1);
      break;
    case NEEYBAL4A_FUNC_SETTING_BAT_CAP:
      memcpy(&retValue, &getBmsSettingsReadback(devNr)[16], 4);
      break;
    case NEEYBAL4A_FUNC_SETTING_BUZZER_MODE:
      break;
    case NEEYBAL4A_FUNC_SETTING_BALLANCER_ON_OFF:
      memcpy(&retValue, &getBmsSettingsReadback(devNr)[13], 1);
      break;
  }

  bmsDataSemaphoreGive();
  return retValue;
}

constexpr const char* textNeeySet_displayFlex = "display;flex|";
constexpr const char* textNeeySet_displayNone = "display;none|";
constexpr const char* textNeeySet_btn0 = "btn1;0";
constexpr const char* textNeeySet_btn1 = "btn1;1";

constexpr const char* textNeeySet_NCM = "NCM";
constexpr const char* textNeeySet_LFP = "LFP";
constexpr const char* textNeeySet_LTO = "LTO";
constexpr const char* textNeeySet_PbAc = "PbAc";

constexpr const char* textNeeySet_EIN = "EIN";
constexpr const char* textNeeySet_AUS = "AUS";

void NeeyBalancer::getNeeyReadbackDataAsString(std::string &value)
{
  std::string  str_lText="";
  uint8_t u8_lValue=0;
  uint8_t devTyp;

  //ID_PARAM_NEEY_BUZZER //not use
  //ID_PARAM_NEEY_BALANCER_ON

  /*if(u8_neeySendStep>0) //Write Data, please wait
  {
    value += textNeeySet_displayFlex;  //spinner visible on
    value += textNeeySet_btn0;         //disable
    //Hier kein "|" anhängen, da dies im nächsten Schritte bei den BMS Daten erfolgt
  }
  else
  {
    value += textNeeySet_displayNone;  //spinner visible off
    value += textNeeySet_btn1);        //enable
    //Hier kein "|" anhängen, da dies im nächsten Schritte bei den BMS Daten erfolgt
  }*/

  value += textNeeySet_displayNone;  //spinner visible off
  value += textNeeySet_btn1;         //enable

  for(uint8_t i=0;i<5;i++)
  {
    devTyp = WebSettings::getInt(ID_PARAM_SS_BTDEV,i,DT_ID_PARAM_SS_BTDEV);
    if(devTyp==ID_BT_DEVICE_NEEY_GW_24S4EB || devTyp==ID_BT_DEVICE_NEEY_EK_24S4EB)
    {
      value += "|";

      value += "s" + std::to_string(WebSettings::getParmId(ID_PARAM_NEEY_START_VOLTAGE, i)) + ";";
      value += floatToString(NeeyBalancer::neeyGetReadbackDataFloat(i, NEEYBAL4A_FUNC_SETTING_START_VOL),3) + " V|";

      value += "s" + std::to_string(WebSettings::getParmId(ID_PARAM_NEEY_MAX_BALANCE_CURRENT, i)) + ";";
      value += floatToString(NeeyBalancer::neeyGetReadbackDataFloat(i, NEEYBAL4A_FUNC_SETTING_MAX_BAL_CURRENT),3) + " A|";

      value += "s" + std::to_string(WebSettings::getParmId(ID_PARAM_NEEY_SLEEP_VOLTAGE, i)) + ";";
      value += floatToString(NeeyBalancer::neeyGetReadbackDataFloat(i, NEEYBAL4A_FUNC_SETTING_SLEEP_VOLTAGE),3) + " V|";

      value += "s" + std::to_string(WebSettings::getParmId(ID_PARAM_NEEY_EQUALIZATION_VOLTAGE, i)) + ";";
      value += floatToString(NeeyBalancer::neeyGetReadbackDataFloat(i, NEEYBAL4A_FUNC_SETTING_EQUALIZATION_VOLTAGE),3) + " V|";

      value += "s" + std::to_string(WebSettings::getParmId(ID_PARAM_NEEY_CELLS, i)) + ";";
      value += std::to_string(NeeyBalancer::neeyGetReadbackDataInt(i, NEEYBAL4A_FUNC_SETTING_CELLS)) + "|";

      value += "s" + std::to_string(WebSettings::getParmId(ID_PARAM_NEEY_BAT_TYPE, i)) + ";";
      u8_lValue = NeeyBalancer::neeyGetReadbackDataInt(i, NEEYBAL4A_FUNC_SETTING_BAT_TYPE);
      str_lText="";
      if(u8_lValue==1) str_lText = textNeeySet_NCM;
      else if(u8_lValue==2) str_lText = textNeeySet_LFP;
      else if(u8_lValue==3) str_lText = textNeeySet_LTO;
      else if(u8_lValue==4) str_lText = textNeeySet_PbAc;
      else std::to_string(u8_lValue);
      value += str_lText + "|";

      value += "s" + std::to_string(WebSettings::getParmId(ID_PARAM_NEEY_BAT_CAPACITY, i)) + ";";
      value += std::to_string(NeeyBalancer::neeyGetReadbackDataInt(i, NEEYBAL4A_FUNC_SETTING_BAT_CAP)) + " Ah|";

      value += "s" + std::to_string(WebSettings::getParmId(ID_PARAM_NEEY_BALANCER_ON, i)) + ";";
      u8_lValue = NeeyBalancer::neeyGetReadbackDataInt(i, NEEYBAL4A_FUNC_SETTING_BALLANCER_ON_OFF);
      str_lText="";
      if(u8_lValue==0) str_lText = textNeeySet_EIN;
      else if(u8_lValue==1) str_lText = textNeeySet_AUS;
      value += str_lText;

    }
  }
}
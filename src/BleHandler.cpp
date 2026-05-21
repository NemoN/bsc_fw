// Copyright (c) 2022 tobias
//
// This software is released under the MIT License.
// https://opensource.org/licenses/MIT


#include "BleHandler.h"
#include "log.h"
#include "devices/NeeyBalancer.h"
#include "AlarmRules.h"


static const char *TAG = "BLE_HANDLER";

#if defined(ENABLE_LEGACY_NEEY_BLE)

static bleDevice bleDevices[BT_DEVICES_COUNT];
static NimBLEScan* pBLEScan = nullptr;
static NimBLEAdvertisedDevice* advDevice = nullptr;
static uint8_t u8_mAdvDeviceNumber = 0;
static SemaphoreHandle_t bleMutex = nullptr;

static bool bo_mBleHandlerRunning = false;
static bool bo_mBtScanIsRunning = false;
static bool bo_mBtNotAllDeviceConnectedOrScanRunning = false;
static uint8_t u8_mScanAndNotConnectTimer = 0;
static uint8_t u8_mSendDataToNeey = 0;
static uint32_t u32_mLastNeeyReadRequest[BT_DEVICES_COUNT] = {0};

static byte NeeyBalancer_getInfo[20] PROGMEM =  {0xaa, 0x55, 0x11, 0x01, 0x01, 0x00, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFA, 0xff};
static byte NeeyBalancer_getInfo3[20] PROGMEM = {0xaa, 0x55, 0x11, 0x01, 0x02, 0x00, 0x00, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf9, 0xff};

static NimBLEUUID BtServiceUUID("ffe0");
static NimBLEUUID BtCharUUID("ffe1");

static bool isNeeyDeviceType(uint8_t deviceType)
{
	return (deviceType == ID_BT_DEVICE_NEEY_GW_24S4EB || deviceType == ID_BT_DEVICE_NEEY_EK_24S4EB);
}


static void sendNeeyReadSequence(uint8_t devNr)
{
	if(devNr >= BT_DEVICES_COUNT) return;
	if(bleDevices[devNr].pChr == nullptr) return;

	if(bleDevices[devNr].deviceTyp == ID_BT_DEVICE_NEEY_GW_24S4EB)
	{
		bleDevices[devNr].pChr->writeValue(NeeyBalancer_getInfo, 20);
		delay(200);
		NeeyBalancer::neeyWriteMsg2(ID_BT_DEVICE_NEEY_GW_24S4EB, bleDevices[devNr].pChr);
		delay(200);
		bleDevices[devNr].pChr->writeValue(NeeyBalancer_getInfo3, 20);
	}
	else if(bleDevices[devNr].deviceTyp == ID_BT_DEVICE_NEEY_EK_24S4EB)
	{
		NeeyBalancer::sendNeeyConnectMsg(ID_BT_DEVICE_NEEY_EK_24S4EB, bleDevices[devNr].pChr);
	}

	u32_mLastNeeyReadRequest[devNr] = millis();
}


static bool bleMutexTake(TickType_t waitTicks = portMAX_DELAY)
{
	if(bleMutex == nullptr) return false;
	return xSemaphoreTake(bleMutex, waitTicks) == pdTRUE;
}


static void bleMutexGive()
{
	if(bleMutex != nullptr) xSemaphoreGive(bleMutex);
}


static uint8_t getDataDeviceNrFromBtDevice(uint8_t btDeviceNr)
{
	for(uint8_t n = 0; n < MUBER_OF_DATA_DEVICES; n++)
	{
		uint8_t dataDeviceSchnittstelle = (uint8_t)WebSettings::getInt(ID_PARAM_DEVICE_MAPPING_SCHNITTSTELLE,n,DT_ID_PARAM_DEVICE_MAPPING_SCHNITTSTELLE);
		if(dataDeviceSchnittstelle == btDeviceNr)
		{
			return n;
		}
	}
	return 0xFF;
}


static void updateBmsLastDataMillisForBtDevice(uint8_t btDeviceNr, uint32_t timestamp)
{
	if(btDeviceNr < MUBER_OF_DATA_DEVICES)
	{
		setBmsLastDataMillis(btDeviceNr, timestamp);
	}

	uint8_t dataDevNr = getDataDeviceNrFromBtDevice(btDeviceNr);
	if(dataDevNr != 0xFF && dataDevNr != btDeviceNr)
	{
		setBmsLastDataMillis(dataDevNr, timestamp);
	}
}


class ClientCallbacks : public NimBLEClientCallbacks
{
	void onConnect(NimBLEClient* pClient)
	{
		String devMacAdr = pClient->getPeerAddress().toString().c_str();
		BSC_LOGI(TAG, "onConnect() %s", devMacAdr.c_str());

		for(uint8_t i = 0; i < BT_DEVICES_COUNT; i++)
		{
			if(bleDevices[i].macAdr.equals(devMacAdr))
			{
				pClient->updateConnParams(20,20,0,60);

				bleDevices[i].balancerOn = e_BalancerWaitForCmd;
				bleDevices[i].isConnect = true;
				bleDevices[i].doConnect = btDoConnectionWaitStart;
				bleDevices[i].pChr = nullptr;
				updateBmsLastDataMillisForBtDevice(i, millis());
			}
		}

		u8_mScanAndNotConnectTimer = BT_SCAN_AND_NOT_CONNECT_TIME;
	};

	void onDisconnect(NimBLEClient* pClient)
	{
		String devMacAdr = pClient->getPeerAddress().toString().c_str();

		for(uint8_t i = 0; i < BT_DEVICES_COUNT; i++)
		{
			if(bleDevices[i].macAdr.equals(devMacAdr.c_str()))
			{
				BSC_LOGI(TAG, "onDisconnect() dev=%i, mac=%s", i, devMacAdr.c_str());
				bleDevices[i].isConnect = false;
				bleDevices[i].doConnect = btDoConnectionIdle;
				bleDevices[i].pChr = nullptr;
				bleDevices[i].sendDataStep = 0;
				bleDevices[i].balancerOn = e_BalancerWaitForCmd;
			}
		}
	}

	bool onConnParamsUpdateRequest(NimBLEClient*, const ble_gap_upd_params*)
	{
		return false;
	}
};


class MyAdvertisedDeviceCallbacks: public NimBLEAdvertisedDeviceCallbacks
{
	void onResult(NimBLEAdvertisedDevice* advertisedDevice)
	{
		std::string devMacAdr = advertisedDevice->getAddress().toString();

		for(uint8_t i = 0; i < BT_DEVICES_COUNT; i++)
		{
			String configuredMac = WebSettings::getString(ID_PARAM_SS_BTDEVMAC,i);
			uint8_t configuredType = (uint8_t)WebSettings::getInt(ID_PARAM_SS_BTDEV,i,DT_ID_PARAM_SS_BTDEV);

			if(configuredMac.equals("")) continue;
			if(!isNeeyDeviceType(configuredType)) continue;

			if(configuredMac.equals(devMacAdr.c_str()))
			{
				if(bleMutexTake(pdMS_TO_TICKS(50)))
				{
					if(advDevice != nullptr)
					{
						delete advDevice;
						advDevice = nullptr;
					}

					advDevice = new NimBLEAdvertisedDevice(*advertisedDevice);
					u8_mAdvDeviceNumber = i;
					bleDevices[i].deviceTyp = configuredType;
					bleDevices[i].macAdr = configuredMac;
					bleDevices[i].doConnect = btDoConnect;
					bleMutexGive();
				}

				NimBLEDevice::getScan()->stop();
				return;
			}
		}
	}
};


static void notifyCB_NEEY(NimBLERemoteCharacteristic* pRemoteCharacteristic, uint8_t* pData, size_t length, bool)
{
	std::string notifyMacAdr = pRemoteCharacteristic->getRemoteService()->getClient()->getPeerAddress().toString();

	for(uint8_t i = 0; i < BT_DEVICES_COUNT; i++)
	{
		if(bleDevices[i].macAdr.equals(notifyMacAdr.c_str()))
		{
			NeeyBalancer::neeyBalancerCopyData(i, pData, length);
			updateBmsLastDataMillisForBtDevice(i, millis());

			for(uint8_t n = 0; n < MUBER_OF_DATA_DEVICES; n++)
			{
				uint8_t dataDeviceSchnittstelle = (uint8_t)WebSettings::getInt(ID_PARAM_DEVICE_MAPPING_SCHNITTSTELLE,n,DT_ID_PARAM_DEVICE_MAPPING_SCHNITTSTELLE);
				if(dataDeviceSchnittstelle == i && n != i)
				{
					NeeyBalancer::neeyBalancerCopyData(n, pData, length);
					return;
				}
			}

			return;
		}
	}
}


void scanCompleteCB(NimBLEScanResults)
{
	if(bleMutexTake(pdMS_TO_TICKS(50)))
	{
		bo_mBtScanIsRunning = false;
		bleMutexGive();
	}
}

static ClientCallbacks clientCB;


static bool btDeviceConnect()
{
	if(advDevice == nullptr)
	{
		return false;
	}

	NimBLEClient* pClient = nullptr;
	uint8_t devNr = u8_mAdvDeviceNumber;

	if(!isNeeyDeviceType(bleDevices[devNr].deviceTyp))
	{
		delete advDevice;
		advDevice = nullptr;
		return false;
	}

	if(NimBLEDevice::getClientListSize())
	{
		pClient = NimBLEDevice::getClientByPeerAddress(advDevice->getAddress());
		if(pClient)
		{
			if(!pClient->connect(advDevice, true))
			{
				BSC_LOGW(TAG, "Reconnect failed: mac=%s",advDevice->getAddress().toString().c_str());
				delete advDevice;
				advDevice = nullptr;
				return false;
			}
		}
		else
		{
			pClient = NimBLEDevice::getDisconnectedClient();
		}
	}

	if(!pClient)
	{
		if(NimBLEDevice::getClientListSize() >= NIMBLE_MAX_CONNECTIONS)
		{
			BSC_LOGW(TAG, "Max clients reached - no more connections available");
			delete advDevice;
			advDevice = nullptr;
			return false;
		}

		pClient = NimBLEDevice::createClient();
		pClient->setClientCallbacks(&clientCB, false);
		pClient->setConnectionParams(12,12,0,51);
		pClient->setConnectTimeout(5);

		if (!pClient->connect(advDevice))
		{
			BSC_LOGW(TAG, "Failed to connect, deleted client; dev=%i",devNr);
			NimBLEDevice::deleteClient(pClient);
			delete advDevice;
			advDevice = nullptr;
			return false;
		}
	}

	if(!pClient->isConnected())
	{
		if(!pClient->connect(advDevice))
		{
			BSC_LOGW(TAG, "Failed to connect; dev=%i", devNr);
			delete advDevice;
			advDevice = nullptr;
			return false;
		}
	}

	NimBLERemoteService* pSvc = pClient->getService(BtServiceUUID);
	if(!pSvc)
	{
		pClient->disconnect();
		BSC_LOGW(TAG, "Device not connected; Service not found; dev=%i",devNr);
		delete advDevice;
		advDevice = nullptr;
		return false;
	}

	bleDevices[devNr].pChr = pSvc->getCharacteristic(BtCharUUID);
	if(!bleDevices[devNr].pChr)
	{
		pClient->disconnect();
		BSC_LOGW(TAG, "Device not connected; Characteristic not found; dev=%i",devNr);
		delete advDevice;
		advDevice = nullptr;
		return false;
	}

	if(!bleDevices[devNr].pChr->canNotify())
	{
		pClient->disconnect();
		BSC_LOGW(TAG, "Device not connected; Can not notify; dev=%i",devNr);
		delete advDevice;
		advDevice = nullptr;
		return false;
	}

	if(!bleDevices[devNr].pChr->subscribe(true, notifyCB_NEEY))
	{
		pClient->disconnect();
		BSC_LOGW(TAG, "Device not connected; Can not subscribe; dev=%i",devNr);
		delete advDevice;
		advDevice = nullptr;
		return false;
	}

	delete advDevice;
	advDevice = nullptr;
	BSC_LOGI(TAG, "Device connected; dev=%i",devNr);
	return true;
}


static void btDeviceDisconnectSingle(uint8_t devNr)
{
	if(devNr >= BT_DEVICES_COUNT) return;

	if(NimBLEDevice::getClientListSize())
	{
		std::string adrStr = std::string(bleDevices[devNr].macAdr.c_str());
		NimBLEAddress adr = NimBLEAddress(adrStr);

		NimBLEClient* pClient = NimBLEDevice::getClientByPeerAddress(adr);
		if(pClient)
		{
			pClient->disconnect();
			BSC_LOGI(TAG, "Device disconnected single, dev=%s",bleDevices[devNr].macAdr.c_str());
		}
	}
}


BleHandler::BleHandler()
{
}

BleHandler::~BleHandler()
{
}


void BleHandler::init()
{
	if(bleMutex == nullptr)
	{
		bleMutex = xSemaphoreCreateMutex();
	}

	u8_mScanAndNotConnectTimer = 0;
	timer_startScan = 0;
	bo_mStartManualScan = false;
	bo_mBtScanIsRunning = false;
	bo_mBtNotAllDeviceConnectedOrScanRunning = false;
	u8_mSendDataToNeey = 0;

	for(uint8_t i = 0; i < BT_DEVICES_COUNT; i++)
	{
		bleDevices[i].doConnect = btDoConnectionIdle;
		bleDevices[i].isConnect = false;
		bleDevices[i].macAdr = WebSettings::getString(ID_PARAM_SS_BTDEVMAC,i);
		bleDevices[i].deviceTyp = (uint8_t)WebSettings::getInt(ID_PARAM_SS_BTDEV,i,DT_ID_PARAM_SS_BTDEV);
		bleDevices[i].pChr = nullptr;
		updateBmsLastDataMillisForBtDevice(i, 0);
		bleDevices[i].sendDataStep = 0;
		bleDevices[i].balancerOn = e_BalancerWaitForCmd;
		u32_mLastNeeyReadRequest[i] = 0;
	}

	NimBLEDevice::init("");
	NimBLEDevice::setPower(ESP_PWR_LVL_N0);

	pBLEScan = NimBLEDevice::getScan();
	pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
	pBLEScan->setInterval(45);
	pBLEScan->setWindow(15);
	pBLEScan->setActiveScan(true);
	pBLEScan->setMaxResults(BT_SCAN_RESULTS);
	pBLEScan->clearResults();

	bo_mBleHandlerRunning = true;
}


void BleHandler::startScan()
{
	bo_mStartManualScan = true;
}


void BleHandler::sendDataToNeey()
{
	if(u8_mSendDataToNeey == 0)
	{
		u8_mSendDataToNeey = 1;
	}
}


void BleHandler::readDataFromNeey()
{
	if(u8_mSendDataToNeey == 0)
	{
		BSC_LOGI(TAG,"readDataFromNeey()");
		u8_mSendDataToNeey = 2;
	}
}


void BleHandler::run()
{
	if(!bo_mBleHandlerRunning) return;
	if(pBLEScan != nullptr) bo_mBtScanIsRunning = pBLEScan->isScanning();

	handleDisconnectionToDevices();

	bool bo_lDoStartBtScan = handleConnectionToDevices();

	if(bo_mBtScanIsRunning && u8_mSendDataToNeey > 0 && !bo_lDoStartBtScan)
	{
		BSC_LOGD(TAG, "Send Data to NEEY -> Stoppe Scan!");
		bo_lDoStartBtScan = false;
		pBLEScan->stop();
		bo_mBtNotAllDeviceConnectedOrScanRunning = false;
		bo_mBtScanIsRunning = false;
	}

	if(bo_mStartManualScan && u8_mSendDataToNeey == 0)
	{
		bo_mStartManualScan = false;
		if(!bo_mBtScanIsRunning) bo_lDoStartBtScan = true;
	}

	if(bo_lDoStartBtScan && !bo_mBtScanIsRunning)
	{
		BSC_LOGD(TAG, "Starte BT Scan");
		bo_mBtNotAllDeviceConnectedOrScanRunning = true;
		bo_lDoStartBtScan = false;
		bo_mBtScanIsRunning = true;

		if(!pBLEScan->isScanning())
		{
			u8_mScanAndNotConnectTimer = BT_SCAN_AND_NOT_CONNECT_TIME;
			NimBLEDevice::getScan()->clearResults();
			NimBLEDevice::getScan()->start(0,scanCompleteCB);
		}
	}

	if(u8_mScanAndNotConnectTimer > 0 && bo_mBtScanIsRunning)
	{
		u8_mScanAndNotConnectTimer--;
	}

	if(u8_mScanAndNotConnectTimer == 1)
	{
		u8_mScanAndNotConnectTimer = 0;
		BSC_LOGD(TAG, "Connect Timeout -> Stoppe Scan!");
		pBLEScan->stop();
		bo_mBtNotAllDeviceConnectedOrScanRunning = false;
		bo_mBtScanIsRunning = false;
	}
}


void BleHandler::setBalancerState(uint8_t u8_devNr, boolean bo_state)
{
	if(u8_devNr >= BT_DEVICES_COUNT) return;

	if(bo_state)
	{
		if(bleDevices[u8_devNr].balancerOn != e_BalancerIsOn) bleDevices[u8_devNr].balancerOn = e_BalancerChangeToOn;
	}
	else
	{
		if(bleDevices[u8_devNr].balancerOn != e_BalancerIsOff) bleDevices[u8_devNr].balancerOn = e_BalancerChangeToOff;
	}
}


bool BleHandler::handleConnectionToDevices()
{
	bool bo_lDoStartBtScan = false;

	if(!bo_mBtScanIsRunning)
	{
		for(uint8_t i = 0; i < BT_DEVICES_COUNT; i++)
		{
			uint8_t u8_lBtDevType = (uint8_t)WebSettings::getInt(ID_PARAM_SS_BTDEV,i,DT_ID_PARAM_SS_BTDEV);

			uint8_t u8_devDeativateTriggerNr = (uint8_t)WebSettings::getInt(ID_PARAM_BTDEV_DEACTIVATE,i,DT_ID_PARAM_BTDEV_DEACTIVATE);
			bool bo_devDeactivateTrigger = false;
			if(u8_devDeativateTriggerNr > 0) bo_devDeactivateTrigger = getAlarm(u8_devDeativateTriggerNr - 1);

			if(isNeeyDeviceType(u8_lBtDevType) && !bo_devDeactivateTrigger)
			{
				if(!WebSettings::getString(ID_PARAM_SS_BTDEVMAC,i).equals("") && !bleDevices[i].isConnect && bleDevices[i].doConnect == btDoConnectionIdle)
				{
					bo_lDoStartBtScan = true;
				}
			}

			if(bleDevices[i].doConnect == btDoConnect)
			{
				bleDevices[i].doConnect = btConnectionSetup;
				if(!btDeviceConnect())
				{
					bleDevices[i].doConnect = btDoConnectionIdle;
				}
			}
		}

		bool allConfiguredNeeyConnected = true;
		for(uint8_t i = 0; i < BT_DEVICES_COUNT; i++)
		{
			uint8_t deviceType = (uint8_t)WebSettings::getInt(ID_PARAM_SS_BTDEV,i,DT_ID_PARAM_SS_BTDEV);
			if(isNeeyDeviceType(deviceType))
			{
				if(!bleDevices[i].isConnect)
				{
					allConfiguredNeeyConnected = false;
					break;
				}
			}
		}

		if(allConfiguredNeeyConnected)
		{
			bool bo_lAllDevWrite = false;
			bool bo_lManualReadDone = false;

			for(uint8_t i = 0; i < BT_DEVICES_COUNT; i++)
			{
				if(!isNeeyDeviceType(bleDevices[i].deviceTyp)) continue;

				if(bleDevices[i].doConnect == btDoConnectionWaitStart)
				{
					sendNeeyReadSequence(i);

					bleDevices[i].doConnect = btDoConnectionIdle;
					bleDevices[i].sendDataStep = 0;
					bo_lAllDevWrite = true;
				}
				else if(bleDevices[i].doConnect == btDoConnectionIdle)
				{
					if(bleDevices[i].sendDataStep < 2)
					{
						bleDevices[i].sendDataStep++;
					}
					else
					{
						if(u8_mSendDataToNeey > 0)
						{
							if(u8_mSendDataToNeey == 1)
							{
								NeeyBalancer::neeyWriteData_GotoStartStep(1);
								u8_mSendDataToNeey = 10;
								NeeyBalancer::neeyWriteData(bleDevices[i].deviceTyp, i, bleDevices[i].pChr);
							}
							else if(u8_mSendDataToNeey == 2)
							{
								sendNeeyReadSequence(i);
								bo_lManualReadDone = true;
							}
						}
						else
						{
							if(i < MUBER_OF_DATA_DEVICES)
							{
								uint32_t lastDataAge = millis() - getBmsLastDataMillis(i);
								if(lastDataAge > 3000 && (millis() - u32_mLastNeeyReadRequest[i]) > 3000)
								{
									BSC_LOGD(TAG, "Trigger cyclical NEEY read for dev %i", i);
									sendNeeyReadSequence(i);
								}

								if(lastDataAge > 15000)
								{
									BSC_LOGI(TAG,"No cyclical data from dev %i", i);
									btDeviceDisconnectSingle(i);
								}
							}

							if(bleDevices[i].balancerOn == e_BalancerChangeToOff)
							{
								bleDevices[i].balancerOn = e_BalancerIsOff;
								NeeyBalancer::neeySetBalancerOnOff(bleDevices[i].pChr, false);
							}
							else if(bleDevices[i].balancerOn == e_BalancerChangeToOn)
							{
								bleDevices[i].balancerOn = e_BalancerIsOn;
								NeeyBalancer::neeySetBalancerOnOff(bleDevices[i].pChr, true);
							}
						}
					}
				}
			}

			if(u8_mSendDataToNeey == 10)
			{
				if(NeeyBalancer::neeyWriteData_GotoNextStep()) u8_mSendDataToNeey = 0;
			}
			else if(u8_mSendDataToNeey == 2 && bo_lManualReadDone)
			{
				u8_mSendDataToNeey = 0;
			}

			if(bo_lAllDevWrite) bo_mBtNotAllDeviceConnectedOrScanRunning = false;
		}
	}

	return bo_lDoStartBtScan;
}


void BleHandler::handleDisconnectionToDevices()
{
	for(uint8_t i = 0; i < BT_DEVICES_COUNT; i++)
	{
		if(!bleDevices[i].isConnect) continue;

		uint8_t u8_devDeativateTriggerNr = (uint8_t)WebSettings::getInt(ID_PARAM_BTDEV_DEACTIVATE,i,DT_ID_PARAM_BTDEV_DEACTIVATE);
		bool bo_devDeactivateTrigger = false;
		if(u8_devDeativateTriggerNr > 0) bo_devDeactivateTrigger = getAlarm(u8_devDeativateTriggerNr - 1);

		uint8_t configuredType = (uint8_t)WebSettings::getInt(ID_PARAM_SS_BTDEV,i,DT_ID_PARAM_SS_BTDEV);
		bool hasMac = !WebSettings::getString(ID_PARAM_SS_BTDEVMAC,i).equals("");

		if(!isNeeyDeviceType(configuredType) || !hasMac || bo_devDeactivateTrigger)
		{
			if(NimBLEDevice::getClientListSize())
			{
				NimBLEClient* pClient = NimBLEDevice::getClientByPeerAddress(NimBLEAddress(bleDevices[i].macAdr.c_str(), BLE_ADDR_PUBLIC));
				if(pClient)
				{
					if(!pClient->disconnect())
					{
						BSC_LOGI(TAG, "Device disconnected, dev=%s",bleDevices[i].macAdr.c_str());
					}
				}
			}
		}
	}
}


std::string BleHandler::getBtScanResultAsHtmlTable()
{
	std::string btDevScanResult = "<table>";

	NimBLEScanResults results = NimBLEDevice::getScan()->getResults();

	for (int i = 0; i < results.getCount(); i++)
	{
		NimBLEAdvertisedDevice device = results.getDevice(i);

		btDevScanResult += "<tr>";

		btDevScanResult += "<td>";
		btDevScanResult += device.getAddress().toString();
		btDevScanResult += "</td>";

		btDevScanResult += "<td>";
		btDevScanResult += device.getName();
		btDevScanResult += "</td>";

		btDevScanResult += "<td>";
		btDevScanResult += "<button onclick='copyStringToClipboard(\"";
		btDevScanResult += device.getAddress().toString();
		btDevScanResult += "\")'>Copy</button>";
		btDevScanResult += "</td>";

		btDevScanResult += "</tr>";
	}

	btDevScanResult += "<table>";
	return btDevScanResult;
}

#else

BleHandler::BleHandler()
{
}

BleHandler::~BleHandler()
{
}

void BleHandler::init()
{
}

void BleHandler::run()
{
}

void BleHandler::startScan()
{
}

std::string BleHandler::getBtScanResultAsHtmlTable()
{
	return "";
}

void BleHandler::sendDataToNeey()
{
}

void BleHandler::readDataFromNeey()
{
}

void BleHandler::setBalancerState(uint8_t, boolean)
{
}

#endif
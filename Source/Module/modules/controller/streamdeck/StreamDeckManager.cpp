/*
  ==============================================================================

    StreamDeckManager.cpp
    Created: 23 Nov 2019 2:48:42am
    Author:  bkupe

  ==============================================================================
*/

#include "StreamDeckManager.h"
#include "hidapi.h"

juce_ImplementSingleton(StreamDeckManager)

StreamDeckManager::StreamDeckManager() :
	queuedNotifier(100)
{
	checkDevices();
	startTimerHz(1);
}

StreamDeckManager::~StreamDeckManager()
{

}

void StreamDeckManager::checkDevices()
{
	//if (!shouldCheck) return;

	bool changed = false;
	Array<String> foundSerials;

	//for (int i = 0; i < TYPE_MAX; i++)
	//{
	hid_device_info* deviceInfos = hid_enumerate(vid, pid);
	hid_device_info* dInfo = deviceInfos;


	while (dInfo != nullptr)
	{
		StreamDeck* p = getItemWithSerial(String(dInfo->serial_number));


		if (p != nullptr)
		{
			//already opened, do nothing;
		}
		else
		{
			//open device
			openDevice(dInfo);
			changed = true;
		}

		foundSerials.add(dInfo->serial_number);
		dInfo = dInfo->next;
	}

	hid_free_enumeration(deviceInfos);


	Array<StreamDeck*> devicesToRemove;
	for (auto& d : devices)
	{
		if (!foundSerials.contains(d->serialNumber))
		{
			//if (d->flashState == Device::FLASHING) continue;
			LOG("Stream Deck removed : " << d->serialNumber);
			devicesToRemove.add(d);
		}
	}

	for (auto& d : devicesToRemove)
	{
		devices.removeObject(d);
		changed = true;
	}

	if (changed)
	{
		queuedNotifier.addMessage(new StreamDeckManagerEvent(StreamDeckManagerEvent::DEVICES_CHANGED));
	}
}


StreamDeck* StreamDeckManager::getItemWithSerial(StringRef serial)
{
	for (auto& d : devices) if (d->serialNumber == serial) return d;
	return nullptr;
}

StreamDeck* StreamDeckManager::getItemWithHidDevice(hid_device* device)
{

	for (auto& d : devices) if (d->device == device) return d;
	return nullptr;
}

StreamDeck* StreamDeckManager::openDevice(hid_device_info* deviceInfo)
{
	if (deviceInfo->vendor_id == 0 || deviceInfo->product_id == 0 || deviceInfo->serial_number == 0) return nullptr;

	hid_device* d = hid_open(deviceInfo->vendor_id, deviceInfo->product_id, deviceInfo->serial_number);
	if (d == NULL)
	{
		DBG("Device could not be opened " << deviceInfo->serial_number);
		return nullptr;
	}

	LOG("Stream Deck added : " << deviceInfo->product_string << " (" << deviceInfo->manufacturer_string << ") " << deviceInfo->serial_number << " : " << String::toHexString(deviceInfo->vendor_id) << ", " << String::toHexString(deviceInfo->product_id) << ", " << deviceInfo->product_string);

	StreamDeck* cd = new StreamDeck(d, String(deviceInfo->serial_number));
	devices.add(cd);
	return cd;
}

void StreamDeckManager::timerCallback()
{
	checkDevices();
}
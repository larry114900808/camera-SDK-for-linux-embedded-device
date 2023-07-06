#pragma once
#include <windows.h>
#include <basetyps.h>
#include <winioctl.h>
#include <setupapi.h>
#include <initguid.h>
#include <string.h>
#include <stdio.h>
#include <tchar.h>
//#include "usb200.h"
//#include "usbview/uvcdesc.h"
#include <usbspec.h>
#include "usbioctl.h"
#include "usbiodef.h"

#define MAX_HCD 10

// Common Class Interface Descriptor
//
typedef struct _USB_INTERFACE_DESCRIPTOR2 {
	UCHAR  bLength;             // offset 0, size 1
	UCHAR  bDescriptorType;     // offset 1, size 1
	UCHAR  bInterfaceNumber;    // offset 2, size 1
	UCHAR  bAlternateSetting;   // offset 3, size 1
	UCHAR  bNumEndpoints;       // offset 4, size 1
	UCHAR  bInterfaceClass;     // offset 5, size 1
	UCHAR  bInterfaceSubClass;  // offset 6, size 1
	UCHAR  bInterfaceProtocol;  // offset 7, size 1
	UCHAR  iInterface;          // offset 8, size 1
	USHORT wNumClasses;         // offset 9, size 2
} USB_INTERFACE_DESCRIPTOR2, * PUSB_INTERFACE_DESCRIPTOR2;

//
// USB 3.0: 10.13.2.1 Hub Descriptor, Table 10-3. SuperSpeed Hub Descriptor
//
//typedef struct _USB_30_HUB_DESCRIPTOR {
//	UCHAR   bLength;
//	UCHAR   bDescriptorType;
//	UCHAR   bNumberOfPorts;
//	USHORT  wHubCharacteristics;
//	UCHAR   bPowerOnToPowerGood;
//	UCHAR   bHubControlCurrent;
//	UCHAR   bHubHdrDecLat;
//	USHORT  wHubDelay;
//	USHORT  DeviceRemovable;
//} USB_30_HUB_DESCRIPTOR, * PUSB_30_HUB_DESCRIPTOR;

typedef struct _STRING_DESCRIPTOR_NODE
{
	struct _STRING_DESCRIPTOR_NODE* Next;
	UCHAR                           DescriptorIndex;
	USHORT                          LanguageID;
	USB_STRING_DESCRIPTOR           StringDescriptor[0];
} STRING_DESCRIPTOR_NODE, * PSTRING_DESCRIPTOR_NODE;

class USBView
{
private:
	/******************************************************************/
	PCHAR GetDriverKeyName(HANDLE Hub, ULONG ConnectionIndex);
	PCHAR GetHCDDriverKeyName(HANDLE HCD);
	PCHAR GetRootHubName(HANDLE HostController);
	PCHAR WideStrToMultiStr(PWCHAR WideStr);
	bool GetStringDescriptor(HANDLE hHubDevice, ULONG ConnectionIndex, UCHAR DescriptorIndex, CHAR* outBuff);
	PCHAR GetExternalHubName(HANDLE Hub, ULONG ConnectionIndex);
	bool EnumerateHostControllers();
	void EnumerateHub(PCHAR HubName, PCHAR Msg);
	void EnumerateHubPorts(HANDLE hHubDevice, ULONG NumPorts);
	PUSB_DESCRIPTOR_REQUEST GetConfigDescriptor(HANDLE	hHubDevice, ULONG ConnectionIndex, UCHAR DescriptorIndex);
	BOOL AreThereStringDescriptors(PUSB_DEVICE_DESCRIPTOR DeviceDesc, PUSB_CONFIGURATION_DESCRIPTOR ConfigDesc);
	/******************************************************************/

	uint16_t _vendor_id;
	uint16_t _product_id;
	std::string speedstr;
	USB_DEVICE_SPEED usb_speed;

public:
	USBView(uint16_t vendor_id, uint16_t product_id)
	{
		_vendor_id = vendor_id;
		_product_id = product_id;
		speedstr = "";
	}

	int GetUSBDeviceSpeed(USB_DEVICE_SPEED& speed)
	{
		int ret = 0;

		EnumerateHostControllers();
		if ((ret = !speedstr.empty()) == 0)
			return ret;

		speed = usb_speed;

		return 0;
	}
};
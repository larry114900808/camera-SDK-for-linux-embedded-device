#include "pch.h"
#include <memory>
#include "Vzlog.h"
#include "USBView.h"

extern std::unique_ptr<VzLog> vzlog;

void USBView::EnumerateHubPorts(HANDLE hHubDevice, ULONG NumPorts)
{
	ULONG       index;
	BOOL        success;
	PUSB_NODE_CONNECTION_INFORMATION    connectionInfo;
	PUSB_DESCRIPTOR_REQUEST             configDesc;
	PUSB_NODE_CONNECTION_INFORMATION_EX_V2 connectionInfoExV2;

	//list ports of root hub
	unsigned int port;
	port = NumPorts;
	for (index = 1; index <= port; index++)
	{
		ULONG nBytes;
		nBytes = sizeof(USB_NODE_CONNECTION_INFORMATION) + sizeof(USB_PIPE_INFO) * 30;
		connectionInfo = (PUSB_NODE_CONNECTION_INFORMATION)malloc(nBytes);
		if (connectionInfo == NULL)
			goto end;

		connectionInfoExV2 = (PUSB_NODE_CONNECTION_INFORMATION_EX_V2)malloc(sizeof(USB_NODE_CONNECTION_INFORMATION_EX_V2));

		if (connectionInfoExV2 == NULL)
		{
			free(connectionInfo);
			break;
		}

		connectionInfo->ConnectionIndex = index;
		success = DeviceIoControl(hHubDevice, IOCTL_USB_GET_NODE_CONNECTION_INFORMATION, connectionInfo, nBytes, connectionInfo, nBytes, &nBytes, NULL);
		if (!success)
		{
			free(connectionInfo);
			goto end;
		}

		if (connectionInfo->ConnectionStatus == DeviceConnected)
		{

			configDesc = GetConfigDescriptor(hHubDevice, index, 0);

			if (connectionInfo->DeviceIsHub) {
				PCHAR extHubName;
				extHubName = GetExternalHubName(hHubDevice, index);
				if (extHubName != NULL) {
					EnumerateHub(extHubName, (char*)" - External Hub");
					//continue;
				}
				if (extHubName != NULL)
					delete[] extHubName;
			}

			// printf("configIndex:%d\n",configDesc->SetupPacket.);

			UCHAR nProduct = connectionInfo->DeviceDescriptor.iProduct;
			UCHAR nManuf = connectionInfo->DeviceDescriptor.iManufacturer;
			CHAR OutBuffPro[64] = { 0 };
			CHAR OutBuffMan[64] = { 0 };
			GetStringDescriptor(hHubDevice, connectionInfo->ConnectionIndex, nProduct, OutBuffPro);
			GetStringDescriptor(hHubDevice, connectionInfo->ConnectionIndex, nManuf, OutBuffMan);

			if (connectionInfo->DeviceDescriptor.idVendor == _vendor_id && connectionInfo->DeviceDescriptor.idProduct == _product_id)
			{
				connectionInfoExV2->ConnectionIndex = index;
				connectionInfoExV2->Length = sizeof(USB_NODE_CONNECTION_INFORMATION_EX_V2);
				connectionInfoExV2->SupportedUsbProtocols.Usb300 = 1;

				success = DeviceIoControl(hHubDevice,
					IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX_V2,
					connectionInfoExV2,
					sizeof(USB_NODE_CONNECTION_INFORMATION_EX_V2),
					connectionInfoExV2,
					sizeof(USB_NODE_CONNECTION_INFORMATION_EX_V2),
					&nBytes,
					NULL);

				if (!success || nBytes < sizeof(USB_NODE_CONNECTION_INFORMATION_EX_V2))
				{
					free(connectionInfoExV2);
					connectionInfoExV2 = NULL;
				}

				PUSB_NODE_CONNECTION_INFORMATION_EX    connectionInfoEx = NULL;
				ULONG nBytesEx = sizeof(USB_NODE_CONNECTION_INFORMATION_EX) +
					(sizeof(USB_PIPE_INFO) * 30);

				connectionInfoEx = (PUSB_NODE_CONNECTION_INFORMATION_EX)malloc(nBytesEx);

				connectionInfoEx->ConnectionIndex = index;

				success = DeviceIoControl(hHubDevice,
					IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX,
					connectionInfoEx,
					nBytesEx,
					connectionInfoEx,
					nBytesEx,
					&nBytesEx,
					NULL);

				if (success)
				{
					//
					// Since the USB_NODE_CONNECTION_INFORMATION_EX is used to display
					// the device speed, but the hub driver doesn't support indication
					// of superspeed, we overwrite the value if the super speed
					// data structures are available and indicate the device is operating
					// at SuperSpeed.
					// 

					if (connectionInfoEx->Speed == UsbHighSpeed
						&& connectionInfoExV2 != NULL
						&& (connectionInfoExV2->Flags.DeviceIsOperatingAtSuperSpeedOrHigher ||
							connectionInfoExV2->Flags.DeviceIsOperatingAtSuperSpeedPlusOrHigher))
					{
						connectionInfoEx->Speed = UsbSuperSpeed;
					}

					usb_speed = (USB_DEVICE_SPEED)connectionInfoEx->Speed;

					switch (connectionInfoEx->Speed)
					{
					case USB_DEVICE_SPEED::UsbLowSpeed:
						speedstr = "LowSpeed";
						break;
					case USB_DEVICE_SPEED::UsbFullSpeed:
						speedstr = "FullSpeed";
						break;
					case USB_DEVICE_SPEED::UsbHighSpeed:
						speedstr = "HighSpeed";
						break;
					case USB_DEVICE_SPEED::UsbSuperSpeed:
						speedstr = "SuperSpeed";
						break;
					default:
						speedstr = "NA";
					}
					//printf("[PORT%d]: connectionInfoEx: Speed = 0x%02X %s\n", index, connectionInfoEx->Speed, speedstr.c_str());
					vzlog->Debug("[%s][%d] [PORT%d]: connectionInfoEx: Speed=0x%02X %s", __FUNCTION__, __LINE__, index, connectionInfoEx->Speed, speedstr.c_str());
				}

				if (connectionInfoEx != NULL) free(connectionInfoEx);
			}

			//printf("\n\t[PORT%d]: %04X:%04X\t%04X\t%s - %s", index, connectionInfo->DeviceDescriptor.idVendor, connectionInfo->DeviceDescriptor.idProduct,
			//	connectionInfo->DeviceDescriptor.bcdUSB, OutBuffMan, OutBuffPro);
		}

		if (connectionInfo != NULL)
			free(connectionInfo);
		if (connectionInfoExV2 != NULL)
			free(connectionInfoExV2);

	}

end:
	

	CloseHandle(hHubDevice);
}

void USBView::EnumerateHub(PCHAR rootHubName, PCHAR Msg)
{
	ULONG	index;
	BOOL	success;

	PUSB_NODE_CONNECTION_INFORMATION    connectionInfo;
	HANDLE hHubDevice = NULL;

	PCHAR driverKeyName, deviceDesc;

	ULONG nBytes;

	PUSB_NODE_INFORMATION HubInfo;
	HubInfo = (PUSB_NODE_INFORMATION)malloc(sizeof(USB_NODE_INFORMATION));

	PCHAR deviceName;
	deviceName = (PCHAR)malloc(512 * sizeof(char));
	if (rootHubName != NULL)
	{
		strcpy_s(deviceName, 256, "\\\\.\\");
		strcpy_s(deviceName + sizeof("\\\\.\\") - 1, 256, rootHubName);

		vzlog->Debug("[%s][%d] %s: %s", __FUNCTION__, __LINE__, Msg, deviceName);

		hHubDevice = CreateFileA(deviceName, GENERIC_WRITE, FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
		if (hHubDevice == INVALID_HANDLE_VALUE) {
			vzlog->Debug("[%s][%d] CreateFile Fail", __FUNCTION__, __LINE__);
			exit(1);
		}

		free(deviceName);

		success = DeviceIoControl(hHubDevice, IOCTL_USB_GET_NODE_INFORMATION, HubInfo, sizeof(USB_NODE_INFORMATION), HubInfo, sizeof(USB_NODE_INFORMATION), &nBytes, NULL);
		if (!success) {
			vzlog->Debug("[%s][%d] nDeviceIoControl Fail", __FUNCTION__, __LINE__);
			exit(1);
		}
	}

	// noew emuerate all ports 
	EnumerateHubPorts(hHubDevice, HubInfo->u.HubInformation.HubDescriptor.bNumberOfPorts);

	if (HubInfo != NULL)
		free(HubInfo);
}

PCHAR USBView::GetExternalHubName(HANDLE Hub, ULONG ConnectionIndex)
{
	BOOL                        success;
	ULONG                       nBytes;
	USB_NODE_CONNECTION_NAME    extHubName;
	PUSB_NODE_CONNECTION_NAME   extHubNameW;
	PCHAR                       extHubNameA;

	extHubNameW = NULL;
	extHubNameA = NULL;

	extHubName.ConnectionIndex = ConnectionIndex;
	success = DeviceIoControl(Hub, IOCTL_USB_GET_NODE_CONNECTION_NAME, &extHubName, sizeof(extHubName), &extHubName, sizeof(extHubName), &nBytes, NULL);
	if (!success)
		goto GetExternalHubNameError;


	nBytes = extHubName.ActualLength;
	if (nBytes <= sizeof(extHubName))
		goto GetExternalHubNameError;

	extHubNameW = (PUSB_NODE_CONNECTION_NAME)GlobalAlloc(GPTR, nBytes);
	if (extHubNameW == NULL)
		goto GetExternalHubNameError;

	extHubNameW->ConnectionIndex = ConnectionIndex;
	success = DeviceIoControl(Hub, IOCTL_USB_GET_NODE_CONNECTION_NAME, extHubNameW, nBytes, extHubNameW, nBytes, &nBytes, NULL);
	if (!success)
		goto GetExternalHubNameError;


	extHubNameA = WideStrToMultiStr(extHubNameW->NodeName);

	GlobalFree(extHubNameW);

	return extHubNameA;

GetExternalHubNameError:
	if (extHubNameW != NULL)
	{
		GlobalFree(extHubNameW);
		extHubNameW = NULL;
	}

	return NULL;
}

PCHAR USBView::GetDriverKeyName(HANDLE Hub, ULONG ConnectionIndex)
{
	BOOL                                success;
	ULONG                               nBytes;
	USB_NODE_CONNECTION_DRIVERKEY_NAME  driverKeyName;
	PUSB_NODE_CONNECTION_DRIVERKEY_NAME driverKeyNameW;
	PCHAR                               driverKeyNameA;

	driverKeyNameW = NULL;
	driverKeyNameA = NULL;

	driverKeyName.ConnectionIndex = ConnectionIndex;

	success = DeviceIoControl(Hub, IOCTL_USB_GET_NODE_CONNECTION_DRIVERKEY_NAME, &driverKeyName, sizeof(driverKeyName), &driverKeyName, sizeof(driverKeyName), &nBytes, NULL);
	if (!success)
	{
		goto GetDriverKeyNameError;
	}

	nBytes = driverKeyName.ActualLength;
	if (nBytes <= sizeof(driverKeyName))
	{
		goto GetDriverKeyNameError;
	}

	driverKeyNameW = (PUSB_NODE_CONNECTION_DRIVERKEY_NAME)malloc(nBytes);
	if (driverKeyNameW == NULL)
	{
		goto GetDriverKeyNameError;
	}

	driverKeyNameW->ConnectionIndex = ConnectionIndex;

	success = DeviceIoControl(Hub, IOCTL_USB_GET_NODE_CONNECTION_DRIVERKEY_NAME, driverKeyNameW, nBytes, driverKeyNameW, nBytes, &nBytes, NULL);
	if (!success)
	{
		goto GetDriverKeyNameError;
	}
	driverKeyNameA = WideStrToMultiStr(driverKeyNameW->DriverKeyName);
	free(driverKeyNameW);

	return driverKeyNameA;

GetDriverKeyNameError:
	if (driverKeyNameW != NULL)
	{
		free(driverKeyNameW);
		driverKeyNameW = NULL;
	}

	return NULL;
}

PCHAR USBView::GetRootHubName(HANDLE HostController)
{
	BOOL                success;
	ULONG               nBytes;
	USB_ROOT_HUB_NAME   rootHubName;
	PUSB_ROOT_HUB_NAME  rootHubNameW;
	PCHAR               rootHubNameA;

	rootHubNameW = NULL;
	rootHubNameA = NULL;

	success = DeviceIoControl(HostController, IOCTL_USB_GET_ROOT_HUB_NAME, 0, 0, &rootHubName, sizeof(rootHubName), &nBytes, NULL);
	if (!success)
	{
		goto GetRootHubNameError;
	}

	nBytes = rootHubName.ActualLength;

	rootHubNameW = (PUSB_ROOT_HUB_NAME)malloc(nBytes);
	if (rootHubNameW == NULL)
	{
		goto GetRootHubNameError;
	}

	success = DeviceIoControl(HostController, IOCTL_USB_GET_ROOT_HUB_NAME, NULL, 0, rootHubNameW, nBytes, &nBytes, NULL);
	if (!success)
	{
		goto GetRootHubNameError;
	}

	rootHubNameA = WideStrToMultiStr(rootHubNameW->RootHubName);

	free(rootHubNameW);

	return rootHubNameA;

GetRootHubNameError:
	if (rootHubNameW != NULL)
	{
		free(rootHubNameW);
		rootHubNameW = NULL;
	}

	return NULL;
}

PCHAR USBView::GetHCDDriverKeyName(HANDLE HCD)
{
	BOOL                    success;
	ULONG                   nBytes;
	USB_HCD_DRIVERKEY_NAME driverKeyName;
	PUSB_HCD_DRIVERKEY_NAME driverKeyNameW;
	PCHAR                   driverKeyNameA;

	driverKeyNameW = NULL;
	driverKeyNameA = NULL;

	success = DeviceIoControl(HCD, IOCTL_GET_HCD_DRIVERKEY_NAME, &driverKeyName, sizeof(driverKeyName), &driverKeyName, sizeof(driverKeyName), &nBytes, NULL);
	if (!success)
	{
		goto GetHCDDriverKeyNameError;
	}

	nBytes = driverKeyName.ActualLength;
	if (nBytes <= sizeof(driverKeyName))
	{
		goto GetHCDDriverKeyNameError;
	}

	driverKeyNameW = (PUSB_HCD_DRIVERKEY_NAME)malloc(nBytes);

	if (driverKeyNameW == NULL)
	{
		goto GetHCDDriverKeyNameError;
	}

	success = DeviceIoControl(HCD, IOCTL_GET_HCD_DRIVERKEY_NAME, driverKeyNameW, nBytes, driverKeyNameW, nBytes, &nBytes, NULL);
	if (!success)
	{
		goto GetHCDDriverKeyNameError;
	}

	driverKeyNameA = WideStrToMultiStr(driverKeyNameW->DriverKeyName);
	free(driverKeyNameW);

	return driverKeyNameA;

GetHCDDriverKeyNameError:
	if (driverKeyNameW != NULL)
	{
		free(driverKeyNameW);
		driverKeyNameW = NULL;
	}

	return NULL;
}

PCHAR USBView::WideStrToMultiStr(PWCHAR WideStr)
{
	ULONG nBytes;
	PCHAR MultiStr;
	nBytes = WideCharToMultiByte(CP_ACP, 0, WideStr, -1, NULL, 0, NULL, NULL);
	if (nBytes == 0)
	{
		return NULL;
	}

	//MultiStr = (PCHAR)malloc(nBytes);
	MultiStr = new CHAR[nBytes];
	if (MultiStr == NULL)
	{
		return NULL;
	}

	nBytes = WideCharToMultiByte(CP_ACP, 0, WideStr, -1, MultiStr, nBytes, NULL, NULL);
	if (nBytes == 0)
	{
		//free(MultiStr);
		delete[] MultiStr;
		return NULL;
	}

	return MultiStr;
}

BOOL USBView::AreThereStringDescriptors(PUSB_DEVICE_DESCRIPTOR DeviceDesc, PUSB_CONFIGURATION_DESCRIPTOR ConfigDesc)
{
	PUCHAR                  descEnd;
	PUSB_COMMON_DESCRIPTOR  commonDesc;

	// Check Device Descriptor strings
	if (DeviceDesc->iManufacturer || DeviceDesc->iProduct || DeviceDesc->iSerialNumber)
		return TRUE;

	// Check the Configuration and Interface Descriptor strings
	descEnd = (PUCHAR)ConfigDesc + ConfigDesc->wTotalLength;
	commonDesc = (PUSB_COMMON_DESCRIPTOR)ConfigDesc;

	while ((PUCHAR)commonDesc + sizeof(USB_COMMON_DESCRIPTOR) < descEnd &&
		(PUCHAR)commonDesc + commonDesc->bLength <= descEnd)
	{
		switch (commonDesc->bDescriptorType)
		{
		case USB_CONFIGURATION_DESCRIPTOR_TYPE:
			if (commonDesc->bLength != sizeof(USB_CONFIGURATION_DESCRIPTOR))
				break;

			if (((PUSB_CONFIGURATION_DESCRIPTOR)commonDesc)->iConfiguration)
				return TRUE;
			continue;

		case USB_INTERFACE_DESCRIPTOR_TYPE:
			if (commonDesc->bLength != sizeof(USB_INTERFACE_DESCRIPTOR) &&
				commonDesc->bLength != sizeof(USB_INTERFACE_DESCRIPTOR2))
				break;
			if (((PUSB_INTERFACE_DESCRIPTOR)commonDesc)->iInterface)
				return TRUE;
			continue;

		default:
			continue;
		}
		break;
	}

	return FALSE;
}

PUSB_DESCRIPTOR_REQUEST USBView::GetConfigDescriptor(HANDLE	hHubDevice, ULONG ConnectionIndex, UCHAR DescriptorIndex)
{
	BOOL    success;
	ULONG   nBytes;
	ULONG   nBytesReturned;
	UCHAR   configDescReqBuf[sizeof(USB_DESCRIPTOR_REQUEST) + sizeof(USB_CONFIGURATION_DESCRIPTOR)];

	PUSB_DESCRIPTOR_REQUEST         configDescReq;
	PUSB_CONFIGURATION_DESCRIPTOR   configDesc;

	// Request the Configuration Descriptor the first time using our
	// local buffer, which is just big enough for the Cofiguration
	// Descriptor itself.
	nBytes = sizeof(configDescReqBuf);
	configDescReq = (PUSB_DESCRIPTOR_REQUEST)configDescReqBuf;
	configDesc = (PUSB_CONFIGURATION_DESCRIPTOR)(configDescReq + 1);
	memset(configDescReq, 0, nBytes);

	configDescReq->ConnectionIndex = ConnectionIndex;

	// USBHUB uses URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE to process this
	// IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION request.
	// USBD will automatically initialize these fields:
	//     bmRequest = 0x80
	//     bRequest  = 0x06
	// We must inititialize these fields:
	//     wValue    = Descriptor Type (high) and Descriptor Index (low byte)
	//     wIndex    = Zero (or Language ID for String Descriptors)
	//     wLength   = Length of descriptor buffer
	configDescReq->SetupPacket.wValue = (USB_CONFIGURATION_DESCRIPTOR_TYPE << 8) | DescriptorIndex;
	configDescReq->SetupPacket.wLength = (USHORT)(nBytes - sizeof(USB_DESCRIPTOR_REQUEST));

	success = DeviceIoControl(hHubDevice, IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION, configDescReq, nBytes, configDescReq, nBytes, &nBytesReturned, NULL);
	if (!success)
		return NULL;

	if (nBytes != nBytesReturned)
		return NULL;

	if (configDesc->wTotalLength < sizeof(USB_CONFIGURATION_DESCRIPTOR))
		return NULL;

	nBytes = sizeof(USB_DESCRIPTOR_REQUEST) + configDesc->wTotalLength;
	configDescReq = (PUSB_DESCRIPTOR_REQUEST)GlobalAlloc(GPTR, nBytes);
	if (configDescReq == NULL)
		return NULL;

	configDesc = (PUSB_CONFIGURATION_DESCRIPTOR)(configDescReq + 1);
	// Indicate the port from which the descriptor will be requested
	configDescReq->ConnectionIndex = ConnectionIndex;

	// USBHUB uses URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE to process this
	// IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION request.
	// USBD will automatically initialize these fields:
	//     bmRequest = 0x80
	//     bRequest  = 0x06
	// We must inititialize these fields:
	//     wValue    = Descriptor Type (high) and Descriptor Index (low byte)
	//     wIndex    = Zero (or Language ID for String Descriptors)
	//     wLength   = Length of descriptor buffer
	configDescReq->SetupPacket.wValue = (USB_CONFIGURATION_DESCRIPTOR_TYPE << 8) | DescriptorIndex;

	configDescReq->SetupPacket.wLength = (USHORT)(nBytes - sizeof(USB_DESCRIPTOR_REQUEST));

	// Now issue the get descriptor request.
	success = DeviceIoControl(hHubDevice, IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION, configDescReq, nBytes, configDescReq, nBytes, &nBytesReturned, NULL);
	if (!success)
	{
		GlobalFree(configDescReq);
		return NULL;
	}

	if (nBytes != nBytesReturned)
	{
		GlobalFree(configDescReq);
		return NULL;
	}

	if (configDesc->wTotalLength != (nBytes - sizeof(USB_DESCRIPTOR_REQUEST)))
	{
		GlobalFree(configDescReq);
		return NULL;
	}

	return configDescReq;
}

bool USBView::GetStringDescriptor(HANDLE hHubDevice, ULONG ConnectionIndex, UCHAR DescriptorIndex, CHAR* outBuff)
{
	BOOL    success;
	ULONG   nBytes;
	ULONG   nBytesReturned;

	UCHAR   stringDescReqBuf[sizeof(USB_DESCRIPTOR_REQUEST) + MAXIMUM_USB_STRING_LENGTH];

	PUSB_DESCRIPTOR_REQUEST stringDescReq;
	PUSB_STRING_DESCRIPTOR stringDesc;

	nBytes = sizeof(stringDescReqBuf);

	stringDescReq = (PUSB_DESCRIPTOR_REQUEST)stringDescReqBuf;
	stringDesc = (PUSB_STRING_DESCRIPTOR)(stringDescReq + 1);

	ZeroMemory(stringDescReq, nBytes);
	stringDescReq->ConnectionIndex = ConnectionIndex;
	stringDescReq->SetupPacket.wValue = (USB_STRING_DESCRIPTOR_TYPE << 8) | DescriptorIndex;
	stringDescReq->SetupPacket.wIndex = GetSystemDefaultLangID();
	stringDescReq->SetupPacket.wLength = (USHORT)(nBytes - sizeof(USB_DESCRIPTOR_REQUEST));

	success = DeviceIoControl(hHubDevice, IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION, stringDescReq, nBytes, stringDescReq, nBytes, &nBytesReturned, NULL);
	if (!success || nBytesReturned < 2)
		return false;

	if (stringDesc->bDescriptorType != USB_STRING_DESCRIPTOR_TYPE)
		return false;

	if (stringDesc->bLength != nBytesReturned - sizeof(USB_DESCRIPTOR_REQUEST))
		return false;

	if (stringDesc->bLength % 2 != 0)
		return false;

	WCHAR* wChar = new WCHAR[stringDesc->bLength + 1];

	memcpy(wChar, stringDesc->bString, stringDesc->bLength);

	char* szTemp = WideStrToMultiStr(wChar);

	lstrcpyA(outBuff, szTemp);

	if (szTemp)
		delete[]szTemp;

	if (wChar)
		delete[]wChar;

	return true;
}

bool USBView::EnumerateHostControllers()
{
	WCHAR       HCName[16];
	HANDLE      hHCDev;
	int         HCNum;
	PCHAR       driverKeyName;
	//PCHAR       deviceDesc;
	PCHAR       rootHubName;

	for (HCNum = 0; HCNum < MAX_HCD; HCNum++) {
		wsprintf(HCName, _T("\\\\.\\HCD%d"), HCNum);
		hHCDev = CreateFile(HCName, GENERIC_WRITE, FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
		if (hHCDev == INVALID_HANDLE_VALUE) {
			return false;
		}

		driverKeyName = GetHCDDriverKeyName(hHCDev);
		if (driverKeyName)
		{
			rootHubName = GetRootHubName(hHCDev);
			if (rootHubName != NULL)
			{
				EnumerateHub(rootHubName, (char*)"Root Hub");
			}
			else {
				CloseHandle(hHCDev);
				return false;
			}
			if (rootHubName != NULL) delete[] rootHubName;
		}
		else {
			CloseHandle(hHCDev);
			return false;
		}

		delete[] driverKeyName;

		CloseHandle(hHCDev);
	}

	return true;
}


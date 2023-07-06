#include "pch.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <map>
#include <codecvt>
#include <tlhelp32.h>
#include <mfapi.h>
#include <mfplay.h>
#include <mfreadwrite.h>
#include <ks.h>
#include <ksproxy.h>
#include <vidcap.h>
#include <opencv2/highgui.hpp>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include "USBView.h"
#include "CyAPI.h"
#include "Vzlog.h"
#include "VizionSDK.h"
#include "mtypedebug.h"

#include "json/json.hpp"
using json = nlohmann::json;

#pragma comment(lib, "mf")
#pragma comment(lib, "mfplat")
#pragma comment(lib, "mfreadwrite")
#pragma comment(lib, "Shlwapi")

//Macro to check the HR result
#define CHECK_HR_RESULT(hr, msg, ...) if (hr != S_OK) {printf("info: Function: %s, %s failed, Error code: 0x%.2x \n", __FUNCTION__, msg, hr, __VA_ARGS__); goto done; }

// GUID of the extension unit, {6FF33D97-7A1F-4C0D-B2D2-59FB178964C5}
static const GUID xuGuidUVC =
{ 0x6ff33d97, 0x7a1f, 0x4c0d, { 0xb2, 0xd2, 0x59, 0xfb, 0x17, 0x89, 0x64, 0xc5 } };

std::unique_ptr<VzLog> vzlog;

//#define SYNC_MODE

#define XU_I2C_CONTROL_MAX_TRANS_LENS  512
#define XU_I2C_CONTROL_MAX_DATA_LENS (XU_I2C_CONTROL_MAX_TRANS_LENS - 4)
#define XU_I2C_CONTROL_MAX_REG_ADDR_LENS 2

inline std::wstring s2ws(const std::string & str)
{
	using convert_typeX = std::codecvt_utf8<wchar_t>;
	std::wstring_convert<convert_typeX, wchar_t> converterX;

	return converterX.from_bytes(str);
}

inline std::string ws2s(const std::wstring & wstr)
{
	using convert_typeX = std::codecvt_utf8<wchar_t>;
	std::wstring_convert<convert_typeX, wchar_t> converterX;

	return converterX.to_bytes(wstr);
}

//Templates for the App
template <class T> void SafeRelease(T** ppT)
{
	if (*ppT)
	{
		(*ppT)->Release();
		*ppT = NULL;
	}
}

HRESULT SourceReaderCB::OnReadSample(
	HRESULT hrStatus,
	DWORD /* dwStreamIndex */,
	DWORD dwStreamFlags,
	LONGLONG llTimestamp,
	IMFSample* pSample      // Can be NULL
)
{
	IMFMediaBuffer* buffer;
	uint16_t width = this->sample_format.width;
	uint16_t height = this->sample_format.height;
	UINT32 stride;
	
	DWORD size, curr_size;
	BYTE* data_ptr;

	EnterCriticalSection(&m_critsec);

	if (SUCCEEDED(hrStatus))
	{
		if (pSample)
		{
			// Do something with the sample.
			//wprintf(L"Frame @ %I64d\n", llTimestamp);

			/*if (sample_data == nullptr) {
				hrStatus = -1;
				goto exit;
			}*/

			// Get Sample Buffer
			std::unique_ptr<BYTE> data = std::make_unique<BYTE>(width * height * 2);
			hrStatus = pSample->ConvertToContiguousBuffer(&buffer);
			data_ptr = data.get();

			buffer->GetMaxLength(&curr_size);
			stride = width * 2;
			size = stride * height;
			if (size != curr_size) {
				vzlog->Trace("[%s][%d] Buffer size is not correct! Buffer Size: %d, Expected Size: %d", __FUNCTION__, __LINE__, curr_size, size);
				size = curr_size;
				stride = curr_size / height;
			}
			hrStatus = buffer->Lock(&data_ptr, NULL, &size);
			if (hrStatus != S_OK)
				vzlog->Error("[%s][%d] Buffer Lock FAIL!", __FUNCTION__, __LINE__);
			raw_size = size;

			sample_data = new uint8_t[raw_size];

			if (sample_data != nullptr && data_ptr != nullptr)
				memcpy_s(sample_data, size, data_ptr, size);

			/*switch (this->sample_format.format)
			{
			case VZ_IMAGE_FORMAT::UYVY:
			case VZ_IMAGE_FORMAT::YUY2:
				sample_mat = new cv::Mat(height, width, CV_8UC2, sample_data);
				break;
			case VZ_IMAGE_FORMAT::NV12:
				sample_mat = new cv::Mat(height * 3 / 2, width, CV_8UC1, sample_data);
				break;
			default:
				sample_mat = new cv::Mat(1, size, CV_8UC1, sample_data);
			}*/

		done:
			buffer->Unlock();
			buffer->Release();
			data.release();
		}
		else {
			hrStatus = S_FALSE;
			//wprintf(L"Sample Capture Fail: 0x%X\n", hrStatus);
		}
	}
	else
	{
		// Streaming error.
		NotifyError(hrStatus);
	}

	if (MF_SOURCE_READERF_ENDOFSTREAM & dwStreamFlags)
	{
		// Reached the end of the stream.
		m_bEOS = TRUE;
	}

exit:
	m_hrStatus = hrStatus;

	LeaveCriticalSection(&m_critsec);
	SetEvent(m_hEvent);
	return S_OK;
}


int SourceReaderCB::GetResultStatus() {
	int ret = 0;
	switch (m_hrStatus)
	{
	case MF_E_HW_MFT_FAILED_START_STREAMING:
		ret = -2;
		//std::cout << "MF_E_HW_MFT_FAILED_START_STREAMING: VZ_ERROR=" << ret << std::endl;
		vzlog->Error("[%s][%d] MF_E_HW_MFT_FAILED_START_STREAMING: VZ_ERROR=%d", __FUNCTION__, __LINE__, ret);
		break;
	case MF_E_VIDEO_RECORDING_DEVICE_INVALIDATED:
		ret = -3;
		//std::cout << "MF_E_VIDEO_RECORDING_DEVICE_INVALIDATED: VZ_ERROR=" << ret << std::endl;
		vzlog->Error("[%s][%d] MF_E_VIDEO_RECORDING_DEVICE_INVALIDATED: VZ_ERROR=%d", __FUNCTION__, __LINE__, ret);
		break;
	default:
		//std::cout << "GetResultStatus() m_hrStatus=0x" << std::hex << m_hrStatus << " VZ_ERROR=" << ret << std::endl;
		vzlog->Error("[%s][%d] m_hrStatus=0x%x, VZ_ERROR=%d", __FUNCTION__, __LINE__, m_hrStatus, ret);
		ret = -1;
	}
	return ret;
}

#ifdef VZ_FUNC
//Function to get UVC video devices
HRESULT VzGetVideoDevices(void)
{
	HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
	MFStartup(MF_VERSION);

	// Create an attribute store to specify the enumeration parameters.
	hr = MFCreateAttributes(&pVideoConfig, 1);
	CHECK_HR_RESULT(hr, "Create attribute store");

	// Source type: video capture devices
	hr = pVideoConfig->SetGUID(
		MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
		MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID
	);
	CHECK_HR_RESULT(hr, "Video capture device SetGUID");

	// Enumerate devices.
	hr = MFEnumDeviceSources(pVideoConfig, &ppVideoDevices, &noOfVideoDevices);
	CHECK_HR_RESULT(hr, "Device enumeration");

done:
	return hr;
}

//Function to get device friendly name
HRESULT VzGetVideoDeviceFriendlyNames(int deviceIndex)
{
	// Get the the device friendly name.
	WCHAR* szFriendlyName = NULL;
	uint32_t cchName;

	HRESULT hr = ppVideoDevices[deviceIndex]->GetAllocatedString(
		MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME,
		&szFriendlyName, &cchName);
	CHECK_HR_RESULT(hr, "Get video device friendly name");

	CoTaskMemFree(szFriendlyName);

done:
	return hr;
}

//Function to set/get parameters of UVC extension unit
HRESULT SetGetExtensionUnit(GUID xuGuid, DWORD dwExtensionNode, ULONG xuPropertyId, ULONG flags, void* data, int len, ULONG* readCount)
{
	GUID pNodeType;
	IUnknown* unKnown;
	IKsControl* ks_control = NULL;
	IKsTopologyInfo* pKsTopologyInfo = NULL;
	KSP_NODE kspNode;

	HRESULT hr = pVideoSource->QueryInterface(__uuidof(IKsTopologyInfo), (void**)&pKsTopologyInfo);
	CHECK_HR_RESULT(hr, "IMFMediaSource::QueryInterface(IKsTopologyInfo)");

	hr = pKsTopologyInfo->get_NodeType(dwExtensionNode, &pNodeType);
	CHECK_HR_RESULT(hr, "IKsTopologyInfo->get_NodeType(...)");

	hr = pKsTopologyInfo->CreateNodeInstance(dwExtensionNode, IID_IUnknown, (LPVOID*)&unKnown);
	CHECK_HR_RESULT(hr, "ks_topology_info->CreateNodeInstance(...)");

	hr = unKnown->QueryInterface(__uuidof(IKsControl), (void**)&ks_control);
	CHECK_HR_RESULT(hr, "ks_topology_info->QueryInterface(...)");

	kspNode.Property.Set = xuGuid;              // XU GUID
	kspNode.NodeId = (ULONG)dwExtensionNode;   // XU Node ID
	kspNode.Property.Id = xuPropertyId;         // XU control ID
	kspNode.Property.Flags = flags;             // Set/Get request

	hr = ks_control->KsProperty((PKSPROPERTY)&kspNode, sizeof(kspNode), (PVOID)data, len, readCount);
	CHECK_HR_RESULT(hr, "ks_control->KsProperty(...)");

done:
	SafeRelease(&ks_control);
	return hr;
}

HRESULT VzGotoProgramMode()
{
	HRESULT hr = S_FALSE;
	ULONG readCount;
	BYTE tmpString[1] = { 0 };

	if (noOfVideoDevices > 0)
	{
		tmpString[0] = 1;
		if (SetGetExtensionUnit(xuGuidUVC, 2, 2, KSPROPERTY_TYPE_SET | KSPROPERTY_TYPE_TOPOLOGY, (void*)tmpString, 1, &readCount) == S_OK)
			hr = S_OK;
	}

	return hr;
}

int UVC_AP1302_I2C_Write(uint16_t addrReg, uint16_t data)
{
	ULONG readCount;
	BYTE AP1302_I2CMsg[5];

	AP1302_I2CMsg[0] = 0; // Set Write Mode
	AP1302_I2CMsg[1] = addrReg >> 8;
	AP1302_I2CMsg[2] = addrReg & 0xFF;
	AP1302_I2CMsg[3] = data >> 8;
	AP1302_I2CMsg[4] = data & 0xFF;

	if (!SetGetExtensionUnit(xuGuidUVC, 2, 5, KSPROPERTY_TYPE_SET | KSPROPERTY_TYPE_TOPOLOGY, (void*)AP1302_I2CMsg, 5, &readCount)) {
#ifdef _DEBUG
		printf("Info: UVC_AP1302_I2C_Write: I2C Write SUCCESS! AddrReg=%04x, Data=%04x\n", addrReg, data);
#endif
	}
	else {
#ifdef _DEBUG
		printf("Error: UVC_AP1302_I2C_Write: I2C Write Fail! AddrReg=%04x, Data=%04x\n", addrReg, data);
#endif
		return -1;
	}

	return 0;
}

int UVC_AP1302_I2C_Read(uint16_t addrReg, uint16_t& data)
{
	ULONG readCount;
	BYTE AP1302_I2CMsg[5];

	AP1302_I2CMsg[0] = 1; // Set Read Mode
	AP1302_I2CMsg[1] = addrReg >> 8;
	AP1302_I2CMsg[2] = addrReg & 0xFF;
	AP1302_I2CMsg[3] = 0;
	AP1302_I2CMsg[4] = 0;

	if (SetGetExtensionUnit(xuGuidUVC, 2, 5, KSPROPERTY_TYPE_SET | KSPROPERTY_TYPE_TOPOLOGY, (void*)AP1302_I2CMsg, 5, &readCount)) {
#ifdef _DEBUG
		printf("Error: UVC_AP1302_I2C_Read: I2C Set Address Fail! AddrReg=%04x\n", addrReg);
#endif
		return -1;
	}
	if (!SetGetExtensionUnit(xuGuidUVC, 2, 5, KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_TOPOLOGY, (void*)AP1302_I2CMsg, 5, &readCount))
	{
		data = (AP1302_I2CMsg[3] << 8) + AP1302_I2CMsg[4];
#ifdef _DEBUG
		printf("Info : I2C Read: Addr=0x%04x, Data=%04x\n", addrReg, data);
#endif
	}
	else {
#ifdef _DEBUG
		printf("Error: UVC_AP1302_I2C_Read: I2C Read Fail! AddrReg=%04x\n", addrReg);
#endif
		return -1;
	}

	return 0;
}


/*
	VizionSDK General Public API for User.
	It makes users can implement the camera functions with VizionSDK.



	Copyright Technexion@2022
*/

std::string m_FWDWNLOAD_ERROR_MSG[] =
{
	"SUCCESS",
	"FAILED",
	"INVALID_MEDIA_TYPE",
	"INVALID_FWSIGNATURE",
	"DEVICE_CREATE_FAILED",
	"INCORRECT_IMAGE_LENGTH",
	"INVALID_FILE",
	"SPI_FLASH_ERASE_FAILED",
	"CORRUPT_FIRMWARE_IMAGE_FILE",
	"I2C_EEPROM_UNKNOWN_SIZE"
};

HRESULT VzStartFWDownload(FX3_FW_TARGET tgt, char* tatgetimg)
{
	//HRESULT hr = S_FALSE;
	FX3_FWDWNLOAD_ERROR_CODE dwld_status = FAILED;
	//FX3_FW_TARGET tgt = FW_TARGET_NONE;
	std::string tgt_str;
	//char* filename = NULL;
	//char* filepath = NULL;
	//FILE* fw_img_p = NULL;
	bool flag = false;
	//bool status = false;
	//int count = 0;
	//int fileSz = 0;

	USHORT vendor_id = 0x04B4;
	USHORT bootLoader_product_id = 0x00F3;
	USHORT cx3_bootLoader_product_id = 0x0053;
	USHORT bootProgrammer_product_id = 0x4720;

	/* Start the USB device and acquire an handle for it. */
	CCyFX3Device* m_usbDevice = new CCyFX3Device();
	if (m_usbDevice == NULL)
	{
		fprintf(stderr, "Error: Failed to create USB Device\n");
		return -1;
	}

	for (int i = 0; i < m_usbDevice->DeviceCount(); i++)
	{
		if (m_usbDevice->Open((UCHAR)i))
		{
			if (m_usbDevice->VendorID == vendor_id)
			{
				if ((m_usbDevice->ProductID == bootLoader_product_id) ||
					(m_usbDevice->ProductID == cx3_bootLoader_product_id))
				{
					/* We have to look for a device that supports the firmware download vendor commands. */
					if (m_usbDevice->IsBootLoaderRunning())
					{

						if ((tgt == FX3_FW_TARGET::FW_TARGET_I2C) || (tgt == FX3_FW_TARGET::FW_TARGET_SPI))
						{
							printf("Info : Found FX3 USB BootLoader instead of Flash Programmer\n");
							flag = false;
						}
						else
						{
							printf("Info : Found FX3 USB BootLoader\n");
							flag = true;
						}
						break;
					}
				}

				if ((m_usbDevice->ProductID == bootProgrammer_product_id) &&
					((tgt == FX3_FW_TARGET::FW_TARGET_I2C) || (tgt == FX3_FW_TARGET::FW_TARGET_SPI)))
				{
					flag = true;
					printf("Info : Found %s\n", m_usbDevice->FriendlyName);
					break;
				}
			}

			m_usbDevice->Close();
		}
	}

	if (flag == false)
	{
		switch (tgt) {
		case FX3_FW_TARGET::FW_TARGET_RAM:
			tgt_str = "RAM";
			break;
		case FX3_FW_TARGET::FW_TARGET_SPI:
			tgt_str = "SPI";
			break;
		case FX3_FW_TARGET::FW_TARGET_I2C:
			tgt_str = "I2C";
			break;
		default:
			tgt_str = "NoTarget";
			break;
		}

		if (tgt == FX3_FW_TARGET::FW_TARGET_RAM)
			fprintf(stderr, "Error: FX3 USB BootLoader Device not found \n");
		else
		{
			//fprintf (stderr, "Error: FX3 Flash Programmer Device not found \n");
			fprintf(stderr, "Error: For downloading firmware image to %s ,first download Flash Programmer image into RAM\n", tgt_str.c_str());
			fprintf(stderr, "Info : Flash Programmer device is required for downloading image to %s \n", tgt_str.c_str());
			fprintf(stderr, "Note : Flash Programmer image is available in <SuiteUSB Install Path>\\bin directory \n");
		}

		if (m_usbDevice)
			delete m_usbDevice;
		return -1;
	}

	switch (tgt)
	{
	case FX3_FW_TARGET::FW_TARGET_RAM:
		printf("Info : Downloading firmware image into internal RAM \n");
		dwld_status = m_usbDevice->DownloadFw(tatgetimg, RAM);
		break;
	case FX3_FW_TARGET::FW_TARGET_I2C:
	case FX3_FW_TARGET::FW_TARGET_SPI:
		printf("Info : FX3 firmware programming to %s started. Please wait...\n", tgt_str.c_str());
		dwld_status = m_usbDevice->DownloadFw(tatgetimg, (tgt == FX3_FW_TARGET::FW_TARGET_I2C) ? I2CE2PROM : SPIFLASH);
		break;

	default:
		fprintf(stderr, "Error: Invalid Media type\n");
		return dwld_status;
	}

	if (dwld_status == SUCCESS)
	{
		LONG len;
		UCHAR buf[1];

		Sleep(100);
		m_usbDevice->ControlEndPt->Target = TGT_DEVICE;
		m_usbDevice->ControlEndPt->ReqType = REQ_VENDOR;
		m_usbDevice->ControlEndPt->Direction = DIR_FROM_DEVICE;
		m_usbDevice->ControlEndPt->ReqCode = 0xB1;		// Reset by firmware
		m_usbDevice->ControlEndPt->Value = 0x0000;
		m_usbDevice->ControlEndPt->Index = 0x0000;
		m_usbDevice->ControlEndPt->XferData(buf, len);

		printf("Info : Programming to %s completed\n", tgt_str.c_str());
	}
	else
	{
		std::stringstream sstr;
		sstr.str("");
		sstr << "\nError: Firmware download failed unexpectedly with error code: " << m_FWDWNLOAD_ERROR_MSG[dwld_status];
		std::cout << sstr.str() << std::endl;
		if (tgt == FX3_FW_TARGET::FW_TARGET_SPI)
		{
			printf("Info : Please verify the connection to external SPI device\n");
			printf("Note : CYUSB3KIT-003 EZ-USB FX3 SuperSpeed Explorer Kit does not have onboard SPI FLASH\n");
		}
	}

	if (m_usbDevice)
	{
		m_usbDevice->Close();
		delete m_usbDevice;
	}

	return S_OK;
}

int VzGetAutoExposureMode(AE_MODE_STATUS& ae_mode)
{
	uint16_t aeaddr = 0x5002;
	uint16_t aectrl = 0x2090;

	aectrl |= (uint16_t)ae_mode;
	UVC_AP1302_I2C_Read(aeaddr, aectrl);
	ae_mode = (AE_MODE_STATUS)(aectrl & 0x000F);

	return 0;
}

int VzSetAutoExposureMode(AE_MODE_STATUS ae_mode)
{
	uint16_t aeaddr = 0x5002;
	uint16_t aectrl = 0x2090;

	aectrl |= (uint16_t)ae_mode;
	return UVC_AP1302_I2C_Write(aeaddr, aectrl);
}

int VzGetExposureTime(uint32_t& exptime)
{
	uint16_t exptimeaddr = 0x500C;
	uint16_t exptime_lsb = 0, exptime_msb = 0;

	UVC_AP1302_I2C_Read(exptimeaddr, exptime_msb);
	UVC_AP1302_I2C_Read(exptimeaddr + 2, exptime_lsb);

	exptime = (exptime_msb << 16) + exptime_lsb;
	return 0;
}

int VzSetExposureTime(uint32_t exptime)
{
	uint16_t exptimeaddr = 0x500C;
	uint16_t exptime_lsb, exptime_msb;

	exptime_lsb = exptime & 0xFFFF;
	exptime_msb = (exptime >> 16) & 0xFFFF;
	UVC_AP1302_I2C_Write(exptimeaddr, exptime_msb);
	UVC_AP1302_I2C_Write(exptimeaddr + 2, exptime_lsb);

	return 0;
}

int VzGetAutoWhiteBalanceMode(AWB_MODE_STATUS& awb_mode)
{
	uint16_t awbaddr = 0x5100;
	uint16_t awbctrl = 0x1150;

	awbctrl |= (uint16_t)awb_mode;
	UVC_AP1302_I2C_Read(awbaddr, awbctrl);
	awb_mode = (AWB_MODE_STATUS)(awbctrl & 0x000F);

	return 0;
}

int VzSetAutoWhiteBalanceMode(AWB_MODE_STATUS awb_mode)
{
	uint16_t awbaddr = 0x5100;
	uint16_t awbctrl = 0x1150;

	awbctrl |= (uint16_t)awb_mode;
	return UVC_AP1302_I2C_Write(awbaddr, awbctrl);
}

int VzGetTemperature(uint16_t& temp)
{
	uint16_t temp_addr = 0x510A;

	return UVC_AP1302_I2C_Read(temp_addr, temp);
}

int VzSetTemperature(uint16_t temp)
{
	uint16_t temp_addr = 0x510A;

	return UVC_AP1302_I2C_Write(temp_addr, temp);
}

/*
	Extenstion Funcion for General I2C Control
*/

int VzGenI2CRead(uint8_t slvaddr, uint8_t reglen, uint16_t regaddr, uint16_t datalen, uint8_t* data)
{
	ULONG readCount;
	BYTE GenI2CRead[XU_I2C_CONTROL_MAX_TRANS_LENS];

	GenI2CRead[0] = (slvaddr << 1) | 1;
	GenI2CRead[1] = reglen;

	if ((reglen + datalen) > XU_I2C_CONTROL_MAX_DATA_LENS)
		return -1;

	if (reglen > XU_I2C_CONTROL_MAX_REG_ADDR_LENS)
		return -1;

	if (reglen == 1)
		GenI2CRead[3] = (uint8_t)regaddr;
	else if (reglen == 2) {
		GenI2CRead[2] = regaddr >> 8;
		GenI2CRead[3] = regaddr & 0xFF;
	}
	else {
		return -1;
	}
	GenI2CRead[2 + reglen] = datalen >> 8;
	GenI2CRead[3 + reglen] = datalen & 0xFF;

	SetGetExtensionUnit(xuGuidUVC, 2, 6, KSPROPERTY_TYPE_SET | KSPROPERTY_TYPE_TOPOLOGY, (void*)GenI2CRead, XU_I2C_CONTROL_MAX_TRANS_LENS, &readCount);
	if (!SetGetExtensionUnit(xuGuidUVC, 2, 6, KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_TOPOLOGY, (void*)GenI2CRead, XU_I2C_CONTROL_MAX_TRANS_LENS, &readCount))
	{
		memcpy(data, (uint8_t*)GenI2CRead + 4 + reglen, datalen);
	}

	return 0;
}

int VzGenI2CWrite(uint8_t slvaddr, uint8_t reglen, uint16_t regaddr, uint8_t datalen, uint8_t* data)
{
	ULONG readCount;
	BYTE GenI2CWrite[XU_I2C_CONTROL_MAX_TRANS_LENS];

	GenI2CWrite[0] = slvaddr << 1;
	GenI2CWrite[1] = reglen;

	if ((reglen + datalen) > XU_I2C_CONTROL_MAX_DATA_LENS)
		return -1;

	if (reglen == 1)
		GenI2CWrite[2] = (uint8_t)regaddr;
	else if (reglen == 2) {
		GenI2CWrite[2] = regaddr >> 8;
		GenI2CWrite[3] = regaddr & 0xFF;
	}
	else {
		return -1;
	}
	GenI2CWrite[2 + reglen] = datalen >> 8;
	GenI2CWrite[3 + reglen] = datalen & 0xFF;

	memcpy((uint8_t*)(GenI2CWrite + 4 + reglen), data, datalen);

	SetGetExtensionUnit(xuGuidUVC, 2, 6, KSPROPERTY_TYPE_SET | KSPROPERTY_TYPE_TOPOLOGY, (void*)GenI2CWrite, XU_I2C_CONTROL_MAX_TRANS_LENS, &readCount);

	return 0;
}


int VzClearISPBootdata(void)
{
	uint8_t wt_size = 128;
	uint8_t slvaddr = 0x54;
	uint8_t wtbuf[128];
	uint32_t cur_reg = 0;

	for (int i = 0; i < 128; i++)
		wtbuf[i] = 0xFF;

	//Clear EEPROM (0x54)0x0000-0xFFFF, (0x55)0x0000-0xFFFF
	// Clear Slave ID 0x54
	while (1)
	{
		VzGenI2CWrite(0x54, 2, (uint16_t)cur_reg, 128, (uint8_t*)wtbuf);
		cur_reg += 128;
		if (cur_reg > 0xFFFF)
			break;
	}
	// Clear Slave ID 0x55
	while (1)
	{
		VzGenI2CWrite(0x55, 2, (uint16_t)(cur_reg & 0xFFFF), 128, (uint8_t*)wtbuf);
		cur_reg += 128;
		if (cur_reg > 0x1FFFF)
			break;
	}

	return 0;
}

int VzDownloadBootdata(const char* binfilepath)
{
	std::ifstream input(binfilepath, std::ios::binary);
	// copies all data into buffer
	std::vector<uint8_t> buffer(std::istreambuf_iterator<char>(input), {});
	int count = 0;
	int remain = 0;

	remain = buffer.size() % 16;

#ifdef _DEBUG
	printf("Bootdata  : 00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F\n");
	printf("----------------------------------------------------------\n");
	for (int j = 0; j < buffer.size(); j += 16) {
		count++;
		printf("0x%08x:", j);
		for (int i = 0; i < 16; i++) {
			printf(" %02x", (uint8_t)(buffer.data() + i + j));
		}
		printf("\n");
	}
	if (remain > 0) {
		printf("0x%08x:", count * 16);
		for (int i = 0; i < remain; i++) {
			printf(" %02x", (uint8_t)(buffer.data() + i + count * 16));
		}
		printf("\n");
	}
#endif
	for (int j = 0; j < buffer.size(); j += 128) {
		if (j < 0xFFFF)
			VzGenI2CWrite(0x54, 2, (uint16_t)j, 128, (uint8_t*)(buffer.data() + count * 128));
		else
			VzGenI2CWrite(0x55, 2, (uint16_t)(j & 0xFFFF), 128, (uint8_t*)(buffer.data() + count * 128));
		count++;
	}
	if (remain > 0) {
		VzGenI2CWrite(0x55, 2, (uint16_t)(count * 128 & 0xFFFF), remain, (uint8_t*)(buffer.data() + count * 128));
		printf("\n");
	}

#ifdef _DEBUG
	uint8_t readdata[16];
	printf("Bootdata  : 00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F\n");
	printf("----------------------------------------------------------\n");
	for (int j = 0; j < buffer.size(); j += 16) {
		if (j < 0xFFFF)
			VzGenI2CRead(0x54, 2, (uint16_t)j, 16, (uint8_t*)readdata);
		else
			VzGenI2CRead(0x55, 2, (uint16_t)(j & 0xFFFF), 16, (uint8_t*)readdata);
		printf("0x%08x:", j);
		for (int i = 0; i < 16; i++) {
			printf(" %02x", readdata[i]);
		}
		printf("\n");
		count++;
	}
	if (remain > 0) {
		VzGenI2CRead(0x55, 2, (uint16_t)((count * 16) & 0xFFFF), remain, (uint8_t*)readdata);
		printf("0x%08x:", count * 16);
		for (int i = 0; i < remain; i++) {
			printf(" %02x", readdata[i]);
		}
		printf("\n");
	}
#endif

	return 0;
}

int VzGetBootdataHeader(VzHeader* header)
{
	VzHeader* temp_header;
	int header_size = sizeof(struct VzHeader);

	if (header == nullptr)
		return -1;

	temp_header = new VzHeader();
	VzGenI2CRead(0x54, 2, (uint16_t)0x0000, header_size, (uint8_t*)(temp_header));

	memcpy(header, temp_header, header_size);

	if (temp_header != nullptr) {
		delete temp_header;
		temp_header = nullptr;
	}

	return 0;
}

#endif

std::string m_FWDWNLOAD_ERROR_MSG[] =
{
	"SUCCESS",
	"FAILED",
	"INVALID_MEDIA_TYPE",
	"INVALID_FWSIGNATURE",
	"DEVICE_CREATE_FAILED",
	"INCORRECT_IMAGE_LENGTH",
	"INVALID_FILE",
	"SPI_FLASH_ERASE_FAILED",
	"CORRUPT_FIRMWARE_IMAGE_FILE",
	"I2C_EEPROM_UNKNOWN_SIZE"
};

int VcGetBootdataHeaderFromFile(VzHeader* header, const char* binfile)
{
	FILE* fp;

	if (fopen_s(&fp, binfile, "rb") != 0) {
		vzlog->Error("[%s][%d] Open Bootdata File Fail! binfile path=%s", __FUNCTION__, __LINE__, binfile);
		return -1;
	}

	fread(header, sizeof(VzHeader), 1, fp);

	fclose(fp);

	return 0;
}

int VcStartFWDownload(FX3_FW_TARGET tgt, char* tatgetimg)
{
	FX3_FWDWNLOAD_ERROR_CODE dwld_status = FAILED;
	std::string tgt_str;
	bool flag = false;

	USHORT vendor_id = 0x04B4;
	USHORT bootLoader_product_id = 0x00F3;
	USHORT cx3_bootLoader_product_id = 0x0053;
	USHORT bootProgrammer_product_id = 0x4720;

	/* Start the USB device and acquire an handle for it. */
	CCyFX3Device* m_usbDevice = new CCyFX3Device();
	if (m_usbDevice == NULL)
	{
		fprintf(stderr, "Error: Failed to create USB Device\n");
		return -1;
	}

	for (int i = 0; i < m_usbDevice->DeviceCount(); i++)
	{
		if (m_usbDevice->Open((UCHAR)i))
		{
			if (m_usbDevice->VendorID == vendor_id)
			{
				if ((m_usbDevice->ProductID == bootLoader_product_id) ||
					(m_usbDevice->ProductID == cx3_bootLoader_product_id))
				{
					/* We have to look for a device that supports the firmware download vendor commands. */
					if (m_usbDevice->IsBootLoaderRunning())
					{
						if ((tgt == FX3_FW_TARGET::FW_TARGET_I2C) || (tgt == FX3_FW_TARGET::FW_TARGET_SPI))
						{
							printf("Info : Found FX3 USB BootLoader instead of Flash Programmer\n");
							flag = false;
						}
						else
						{
							printf("Info : Found FX3 USB BootLoader\n");
							flag = true;
						}
						break;
					}
				}

				if ((m_usbDevice->ProductID == bootProgrammer_product_id) &&
					((tgt == FX3_FW_TARGET::FW_TARGET_I2C) || (tgt == FX3_FW_TARGET::FW_TARGET_SPI)))
				{
					flag = true;
					printf("Info : Found %s\n", m_usbDevice->FriendlyName);
					break;
				}
			}

			m_usbDevice->Close();
		}
	}

	if (flag == false)
	{
		switch (tgt) {
		case FX3_FW_TARGET::FW_TARGET_RAM:
			tgt_str = "RAM";
			break;
		case FX3_FW_TARGET::FW_TARGET_SPI:
			tgt_str = "SPI";
			break;
		case FX3_FW_TARGET::FW_TARGET_I2C:
			tgt_str = "I2C";
			break;
		default:
			tgt_str = "NoTarget";
			break;
		}

		if (tgt == FX3_FW_TARGET::FW_TARGET_RAM)
			fprintf(stderr, "Error: FX3 USB BootLoader Device not found \n");
		else
		{
			//fprintf (stderr, "Error: FX3 Flash Programmer Device not found \n");
			fprintf(stderr, "Error: For downloading firmware image to %s ,first download Flash Programmer image into RAM\n", tgt_str.c_str());
			fprintf(stderr, "Info : Flash Programmer device is required for downloading image to %s \n", tgt_str.c_str());
			fprintf(stderr, "Note : Flash Programmer image is available in <SuiteUSB Install Path>\\bin directory \n");
		}

		if (m_usbDevice)
			delete m_usbDevice;
		return -1;
	}

	switch (tgt)
	{
	case FX3_FW_TARGET::FW_TARGET_RAM:
		printf("Info : Downloading firmware image into internal RAM \n");
		dwld_status = m_usbDevice->DownloadFw(tatgetimg, RAM);
		break;
	case FX3_FW_TARGET::FW_TARGET_I2C:
	case FX3_FW_TARGET::FW_TARGET_SPI:
		printf("Info : FX3 firmware programming to %s started. Please wait...\n", tgt_str.c_str());
		dwld_status = m_usbDevice->DownloadFw(tatgetimg, (tgt == FX3_FW_TARGET::FW_TARGET_I2C) ? I2CE2PROM : SPIFLASH);
		break;

	default:
		fprintf(stderr, "Error: Invalid Media type\n");
		return dwld_status;
	}

	if (dwld_status == SUCCESS)
	{
		LONG len;
		UCHAR buf[1];

		Sleep(100);
		m_usbDevice->ControlEndPt->Target = TGT_DEVICE;
		m_usbDevice->ControlEndPt->ReqType = REQ_VENDOR;
		m_usbDevice->ControlEndPt->Direction = DIR_FROM_DEVICE;
		m_usbDevice->ControlEndPt->ReqCode = 0xB1;		// Reset by firmware
		m_usbDevice->ControlEndPt->Value = 0x0000;
		m_usbDevice->ControlEndPt->Index = 0x0000;
		m_usbDevice->ControlEndPt->XferData(buf, len);

		//printf("Info : Programming to %s completed\n", tgt_str.c_str());
	}
	else
	{
		std::stringstream sstr;
		sstr.str("");
		sstr << "\nError: Firmware download failed unexpectedly with error code: " << m_FWDWNLOAD_ERROR_MSG[dwld_status];
		std::cout << sstr.str() << std::endl;
		if (tgt == FX3_FW_TARGET::FW_TARGET_SPI)
		{
			printf("Info : Please verify the connection to external SPI device\n");
			printf("Note : CYUSB3KIT-003 EZ-USB FX3 SuperSpeed Explorer Kit does not have onboard SPI FLASH\n");
		}

		if (m_usbDevice)
		{
			m_usbDevice->Close();
			delete m_usbDevice;
		}

		return dwld_status;
	}

	if (m_usbDevice)
	{
		m_usbDevice->Close();
		delete m_usbDevice;
	}

	return 0;
}

VizionCam* VcCreateVizionCamDevice()
{
	VizionCam* vizion_cam = new VizionCam();
	
	return  vizion_cam;
}

int VcReleaseVizionCamDevice(VizionCam* vizion_cam)
{
	if (vizion_cam != nullptr)
	{
		delete vizion_cam;
		vizion_cam = nullptr;
	}
	return 0;
}

VizionCam::VizionCam()
{
	GetVideoDevices();
	dev_idx = -1;
	is_auto_ae = false;
	is_auto_awb = false;
}

VizionCam::VizionCam(uint8_t dev_idx)
{
	GetVideoDevices();
	this->dev_idx = dev_idx;

	is_auto_ae = false;
	is_auto_awb = false;
}

VizionCam::~VizionCam()
{
	if (pCallback != nullptr) { pCallback->Release(); pCallback = nullptr; }
	if (pVideoSource != nullptr) { SafeRelease(&pVideoSource); pVideoSource = nullptr; }
	if (pVideoConfig != nullptr) { SafeRelease(&pVideoConfig); pVideoConfig = nullptr; }
	if (pVideoReader != nullptr) { SafeRelease(&pVideoReader); pVideoReader = nullptr; }
	if (ppVideoDevices != nullptr) {
		for (int i = 0; i < this->noOfVideoDevices; i++)
			SafeRelease(&ppVideoDevices[i]);
		CoTaskMemFree(ppVideoDevices);
	}
}

HRESULT CreateSourceReaderAsync(
	PCWSTR pszURL,
	IMFSourceReaderCallback* pCallback,
	IMFSourceReader** ppReader)
{
	HRESULT hr = S_OK;
	IMFAttributes* pAttributes = NULL;

	hr = MFCreateAttributes(&pAttributes, 1);
	if (FAILED(hr))
	{
		goto done;
	}

	hr = pAttributes->SetUnknown(MF_SOURCE_READER_ASYNC_CALLBACK, pCallback);
	if (FAILED(hr))
	{
		goto done;
	}

	hr = MFCreateSourceReaderFromURL(pszURL, pAttributes, ppReader);

done:
	SafeRelease(&pAttributes);
	return hr;
}

HRESULT VizionCam::GetVideoDevices(void)
{
	HRESULT hr = S_OK;
	if (pVideoConfig != nullptr) { SafeRelease(&pVideoConfig); }
	if (ppVideoDevices != nullptr) {
		for (int i = 0; i < this->noOfVideoDevices; i++)
			SafeRelease(&ppVideoDevices[i]);
		CoTaskMemFree(ppVideoDevices);
	}

	hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
	MFStartup(MF_VERSION);

	// Create an attribute store to specify the enumeration parameters.
	hr = MFCreateAttributes(&this->pVideoConfig, 1);
	//CHECK_HR_RESULT(hr, "Create attribute store");
	

	// Source type: video capture devices
	hr = this->pVideoConfig->SetGUID(
		MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
		MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID
	);
	//CHECK_HR_RESULT(hr, "Video capture device SetGUID");

	// Enumerate devices.
	hr = MFEnumDeviceSources(this->pVideoConfig, &this->ppVideoDevices, &this->noOfVideoDevices);
	//CHECK_HR_RESULT(hr, "Device enumeration");

	// Get the the device friendly name.
	WCHAR* szFriendlyName = NULL;
	WCHAR* szSymbolicLink = NULL;
	uint32_t cchName;

	std::wstring hardwareid;

	devicename_map.clear();
	hardwareid_map.clear();

	for (uint32_t i = 0; i < this->noOfVideoDevices; i++) {
		ppVideoDevices[i]->GetAllocatedString(
			MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME,
			&szFriendlyName, &cchName);

		ppVideoDevices[i]->GetAllocatedString(
			MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK,
			&szSymbolicLink, &cchName);

		std::wstring symboloutput = std::wstring(szSymbolicLink);
		int start = 0;
		int end = 0;
		end = symboloutput.find(L"usb", start);
		hardwareid = symboloutput.substr(end + 4, 17);

		if (szSymbolicLink != NULL && szFriendlyName != NULL) {
			devicename_map.insert(std::pair<int, std::wstring>(i, std::wstring(szFriendlyName)));
			hardwareid_map.insert(std::pair<int, std::wstring>(i, hardwareid));
		}

		CoTaskMemFree(szFriendlyName);
		CoTaskMemFree(szSymbolicLink);

	}

	return hr;
}

HRESULT VizionCam::InitVideoDevice(void)
{
	HRESULT hr = S_OK;

	if(GetVideoDevices() != S_OK)
		return -1;
	
	HANDLE hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (hEvent == NULL)
	{
		hr = HRESULT_FROM_WIN32(GetLastError());
		return hr;
	}

	if (pCallback != nullptr) {
		pCallback->Release();
		pCallback = nullptr;
	}

	if(pVideoConfig == nullptr)
		hr = MFCreateAttributes(&this->pVideoConfig, 1);

	// Create an instance of the callback object.
	pCallback = new (std::nothrow) SourceReaderCB(hEvent);

	if (pCallback == NULL)
	{
		hr = E_OUTOFMEMORY;
		return hr;
	}

	hr = pVideoConfig->SetUnknown(MF_SOURCE_READER_ASYNC_CALLBACK, pCallback);
	if (FAILED(hr))
	{
		return hr;
	}

	try {
		hr = ppVideoDevices[dev_idx]->ActivateObject(IID_PPV_ARGS(&pVideoSource));
		CHECK_HR_RESULT(hr, "Activating video device");
	}
	catch (...) {
		vzlog->Error("[%s][%d] ActivateObject Error!", __FUNCTION__, __LINE__);
		return -1;
	}

	// Create a source reader.
	hr = MFCreateSourceReaderFromMediaSource(pVideoSource, pVideoConfig, &pVideoReader);
	CHECK_HR_RESULT(hr, "Creating video source reader");

done:
	return hr;
}

HRESULT VizionCam::SetGetExtensionUnit(GUID xuGuid, DWORD dwExtensionNode, ULONG xuPropertyId, ULONG flags, void* data, int len, ULONG* readCount)
{
	GUID pNodeType;
	IUnknown* unKnown;
	IKsControl* ks_control = NULL;
	IKsTopologyInfo* pKsTopologyInfo = NULL;
	KSP_NODE kspNode;

	HRESULT hr = pVideoSource->QueryInterface(__uuidof(IKsTopologyInfo), (void**)&pKsTopologyInfo);
	CHECK_HR_RESULT(hr, "IMFMediaSource::QueryInterface(IKsTopologyInfo)");

	hr = pKsTopologyInfo->get_NodeType(dwExtensionNode, &pNodeType);
	CHECK_HR_RESULT(hr, "IKsTopologyInfo->get_NodeType(...)");

	hr = pKsTopologyInfo->CreateNodeInstance(dwExtensionNode, IID_IUnknown, (LPVOID*)&unKnown);
	CHECK_HR_RESULT(hr, "ks_topology_info->CreateNodeInstance(...)");

	hr = unKnown->QueryInterface(__uuidof(IKsControl), (void**)&ks_control);
	CHECK_HR_RESULT(hr, "ks_topology_info->QueryInterface(...)");

	kspNode.Property.Set = xuGuid;              // XU GUID
	kspNode.NodeId = (ULONG)dwExtensionNode;   // XU Node ID
	kspNode.Property.Id = xuPropertyId;         // XU control ID
	kspNode.Property.Flags = flags;             // Set/Get request

	hr = ks_control->KsProperty((PKSPROPERTY)&kspNode, sizeof(kspNode), (PVOID)data, len, readCount);
	CHECK_HR_RESULT(hr, "ks_control->KsProperty(...)");

done:
	SafeRelease(&ks_control);
	return hr;
}

int VizionCam::GotoProgramMode()
{
	HRESULT hr = S_FALSE;
	ULONG readCount;
	BYTE tmpString[1] = { 0 };

	if (noOfVideoDevices > 0)
	{
		tmpString[0] = 1;
		if (SetGetExtensionUnit(xuGuidUVC, 2, 2, KSPROPERTY_TYPE_SET | KSPROPERTY_TYPE_TOPOLOGY, (void*)tmpString, 1, &readCount) == S_OK)
			hr = S_OK;
	}

	return hr;
}

int VizionCam::GetVizionCamDeviceName(wchar_t* devname)
{
	if (dev_name.size() == 0)
		return -1;

	swprintf_s(devname, dev_name.size() + sizeof(wchar_t), L"%s", dev_name.c_str());
	return 0;
}

int VizionCam::GetVideoDeviceList(std::vector<std::wstring>& devname_list)
{
	if (devicename_map.size() < 0)
		return -1;

	devname_list.clear();

	GetVideoDevices();

	for (int i = 0; i < devicename_map.size(); i++)
		devname_list.push_back(devicename_map[i]);

	return 0;
}

int VizionCam::GetDeviceHardwareID(wchar_t* hardware_id)
{
	if (hardwareid_map.size() < 0)
		return -1;

	swprintf_s(hardware_id, hardwareid_map[dev_idx].size() + sizeof(wchar_t), L"%s", hardwareid_map[dev_idx].c_str());

	return 0;
}

int VizionCam::GetUSBFirmwareVersion(BYTE* fw_ver_data)
{
	ULONG readCount;
	if (!SetGetExtensionUnit(xuGuidUVC, 2, 1, KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_TOPOLOGY, (void*)fw_ver_data, 4, &readCount)) {
		vzlog->Trace("[%s][%d] Get USB Version SUCCESS! version=%02d.%02d.%02d.%02d", __FUNCTION__, __LINE__, 
											fw_ver_data[0], fw_ver_data[1], fw_ver_data[2], fw_ver_data[3]);
	}
	else {
		vzlog->Error("[%s][%d] Get USB Version Fail!", __FUNCTION__, __LINE__);
		return -1;
	}
	return 0;
}

int VizionCam::GetUSBFirmwareVersion(char* fw_ver)
{
	ULONG readCount;
	BYTE fw_ver_data[4];

	if (!SetGetExtensionUnit(xuGuidUVC, 2, 1, KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_TOPOLOGY, (void*)fw_ver_data, 4, &readCount)) {

#ifndef SHOW_BETA
		sprintf_s(fw_ver, 16, "%02d.%02d.%02d.%02d", fw_ver_data[0], fw_ver_data[1], fw_ver_data[2], fw_ver_data[3]);
#else
		if (fw_ver_data[2] == 0)
			sprintf_s(fw_ver, 16, "%02d.%02d\n", fw_ver_data[0], fw_ver_data[1]);
		else
			sprintf_s(fw_ver, 16, "%02d.%02d.beta%d", fw_ver_data[0], fw_ver_data[1], fw_ver_data[3]);
#endif
		vzlog->Trace("[%s][%d] Get USB Version SUCCESS! version=%s", __FUNCTION__, __LINE__, fw_ver);
	}
	else {
		vzlog->Error("[%s][%d] Get USB Version Fail!", __FUNCTION__, __LINE__);
		return -1;
	}

	return 0;
}

void VizionCam::GetDeviceSpeed(USB_DEVICE_SPEED& usb_speed)
{
	usb_speed = this->speed;
}

int VizionCam::CheckHeaderVer()
{
	if (GenI2CRead(0x54, 2, 0x0000, 1, &header_ver) < 0)
		return -1;

	return header_ver;
}

int VizionCam::AdvWriteSensorRegister(uint16_t sen_sid, uint16_t sen_addrReg, uint16_t sen_data)
{
	int count = 0;
	uint16_t temp_data = 0x0000;

	while (count < 20)
	{
		if (++count >= 20) {
			vzlog->Error("[%s][%d] I2C BUS 1 Check Fail!", __FUNCTION__, __LINE__);
			return -1;
		}
		UVC_AP1302_I2C_Read(0x60AC, temp_data);
		if ((temp_data & 0x7) == 0)
			break;
	}

	// Start to set SMIP Parameter
	UVC_AP1302_I2C_Write(0x60A8, 0x0000);
	UVC_AP1302_I2C_Write(0x60AA, 0x0002);

	UVC_AP1302_I2C_Write(0x60A0, 0x0000);
	UVC_AP1302_I2C_Write(0x60A2, sen_data);
	UVC_AP1302_I2C_Write(0x60A4, 0xF300 | sen_sid);
	UVC_AP1302_I2C_Write(0x60A6, sen_addrReg);

	UVC_AP1302_I2C_Write(0x60AC, 0x0301);

	count = 0;

	while (count < 20)
	{
		if (++count >= 20) {
			vzlog->Error("[%s][%d] Check Status Fail!", __FUNCTION__, __LINE__);
			return -1;
		}
		UVC_AP1302_I2C_Read(0x60AC, temp_data);
		if ((temp_data & 0x7) == 0)
			break;
	}

	vzlog->Debug("[%s][%d] AdvWriteSensorRegister Success. Sensor Reg=0x%04x, Data=0x%04x", __FUNCTION__, __LINE__, sen_addrReg, sen_data);
	return 0;
}



int VizionCam::AdvReadSensorRegister(uint16_t sen_sid, uint16_t sen_addrReg, uint16_t& sen_data)
{
	int count = 0;
	uint16_t temp_data = 0x0000;

	//if (UVC_AP1302_I2C_Read(0x0000, temp_data) != 0) {
	//	vzlog->Error("[%s][%d] Check ISP ID Fail!", __FUNCTION__, __LINE__);
	//	return -1;
	//}
	//else
	//	vzlog->Debug("[%s][%d] ISP IS WORKING! ISP_ID:0x%04X", __FUNCTION__, __LINE__, temp_data);
	//
	//count = 0;
	while (count < 20)
	{
		if (++count >= 20) {
			vzlog->Error("[%s][%d] I2C BUS 1 Check Fail!", __FUNCTION__, __LINE__);
			return -1;
		}
		UVC_AP1302_I2C_Read(0x60AC, temp_data);
		if ((temp_data & 0x7) == 0)
			break;
	}

	// Start to set SMIP Parameter
	UVC_AP1302_I2C_Write(0x60A8, 0x0000);
	UVC_AP1302_I2C_Write(0x60AA, 0x0002);

	UVC_AP1302_I2C_Write(0x60A0, 0x0300 | sen_sid);
	UVC_AP1302_I2C_Write(0x60A2, sen_addrReg);
	UVC_AP1302_I2C_Write(0x60A4, 0x0000);
	UVC_AP1302_I2C_Write(0x60A6, 0x60A4);

	UVC_AP1302_I2C_Write(0x60AC, 0x6032);

	count = 0;

	while (count < 20)
	{
		if (++count >= 20) {
			vzlog->Error("[%s][%d] Check Status Fail!", __FUNCTION__, __LINE__);
			return -1;
		}
		UVC_AP1302_I2C_Read(0x60AC, temp_data);
		if ((temp_data & 0x7) == 0)
			break;
	}

	UVC_AP1302_I2C_Read(0x60A4, sen_data);

	return 0;
}

int VizionCam::GetUniqueSensorID(char* sensor_id)
{
	uint16_t data = 0;
	uint16_t count = 0;
	// Check Sensor Normal ID
	std::vector<uint8_t> sid_list = { 0x20, 0x6c, 0x6e };
	uint8_t sid = 0x00;
	uint16_t fuse_id_addr = 0x3800;

	uint16_t fuse_id[8];

	//UVC_AP1302_I2C_Read(0x601A, data);
	//if(data & 0x8000)
	//{
	//	vzlog->Error("[%s][%d] Check ISP SYSTEM START Fail! Status=0x%04X", __FUNCTION__, __LINE__, data);
	//	return -1;
	//}

	//UVC_AP1302_I2C_Write(0xFFFE, 0x0000);
	while (++count < 50) {
		if (count >= 50) {
			vzlog->Error("[%s][%d] Check ISP ID Fail!", __FUNCTION__, __LINE__);
			return -1;
		}
		if (UVC_AP1302_I2C_Read(0x0000, data) == 0) {
			vzlog->Debug("[%s][%d] Check ISP ID Success! ISP ID: 0x%04X", __FUNCTION__, __LINE__, data);
			break;
		}
		Sleep(10);
	}

	BYTE fw_ver[4];
	GetUSBFirmwareVersion(fw_ver);

	UVC_AP1302_I2C_Read(0x601A, data);
	if (((fw_ver[0] >= 23) && (fw_ver[1] >= 3) && (fw_ver[2] >= 3) && (fw_ver[3] >= 8)) &&
		(data & 0x200) == 0x200) {
		// Wakup Sensor
		count = 0;
		data = 0;
		UVC_AP1302_I2C_Write(0x601A, 0x380);

		while (count < 10)
		{
			if (++count >= 10) {
				vzlog->Error("[%s][%d] Wake Sensor Up Fail! retry count = %d, Status=0x%04X", __FUNCTION__, __LINE__, count, data);
				break;
			}
			UVC_AP1302_I2C_Read(0x601A, data);
			if ((data & 0x200) == 0){
				vzlog->Debug("[%s][%d] Wake Sensor Up Success! retry count = %d, Status=0x%04X", __FUNCTION__, __LINE__, count, data);
				break;
			}
			Sleep(5);
		}
	}

	Sleep(500);

	for (int i = 0; i < sid_list.size(); i++) {
		if (AdvReadSensorRegister(sid_list[i], 0x3000, data) == 0) {
			if (data != 0) {
				vzlog->Debug("[%s][%d] AdvReadSensorRegister Success. Sensor ID=0x%04x", __FUNCTION__, __LINE__, data);
				sid = sid_list[i];
				break;
			}
		}
	}

	if (sid == 0) {
		vzlog->Error("[%s][%d] Check Sensor SID Fail!", __FUNCTION__, __LINE__);
		return -1;
	}

	if (data == 0x2557 || data == 0x0F56) // if SID is AR0821(0x2557) or AR0822(0x0F56)
		fuse_id_addr = 0x34C0;

	AdvWriteSensorRegister(sid, 0x304C, 0x0100);
	AdvWriteSensorRegister(sid, 0x304A, 0x0210);

	Sleep(500);

	for (int i = 0; i < 8; i++) {
		if (AdvReadSensorRegister(sid, fuse_id_addr + i * 2, fuse_id[i]) == 0) {
			vzlog->Debug("[%s][%d] AdvReadSensorRegister Success. FuseID[%d]=0x%04x", __FUNCTION__, __LINE__, i, fuse_id[i]);
		}
		else {
			vzlog->Error("[%s][%d] AdvReadSensorRegister Fail! Read FuseID[%d] Fail", __FUNCTION__, __LINE__, i);
			return -1;
		}
	}

	sprintf_s(sensor_id, 64, "%04X%04X%04X%04X%04X%04X%04X%04X"
		, fuse_id[7], fuse_id[6], fuse_id[5], fuse_id[4]
		, fuse_id[3], fuse_id[2], fuse_id[1], fuse_id[0]);


	vzlog->Debug("[%s][%d] GetUniqueSensorID Success! Sensor UID = %s", __FUNCTION__, __LINE__, sensor_id);
	return 0;
}

int VizionCam::GetBootdataHeader(VzHeader* header)
{
	VzHeader* temp_header;
	int header_size = sizeof(struct VzHeader);

	if (header == nullptr)
		return -1;

	temp_header = new VzHeader();
	GenI2CRead(0x54, 2, (uint16_t)0x0000, header_size, (uint8_t*)(temp_header));

	memcpy(header, temp_header, header_size);

	if (temp_header != nullptr) {
		delete temp_header;
		temp_header = nullptr;
	}

	return 0;
}

int VizionCam::GetBootdataHeaderV3(VzHeaderV3* header)
{
	VzHeaderV3* temp_header;
	int header_size = sizeof(struct VzHeaderV3);

	if (header == nullptr)
		return -1;

	temp_header = new VzHeaderV3();
	GenI2CRead(0x54, 2, (uint16_t)0x0000, header_size, (uint8_t*)(temp_header));

	memcpy(header, temp_header, header_size);

	if (temp_header != nullptr) {
		delete temp_header;
		temp_header = nullptr;
	}

	return 0;
}

int VizionCam::GenI2CRead(uint8_t slvaddr, uint8_t reglen, uint16_t regaddr, uint16_t datalen, uint8_t* data)
{
	ULONG readCount;
	BYTE GenI2CRead[XU_I2C_CONTROL_MAX_TRANS_LENS];

	GenI2CRead[0] = (slvaddr << 1) | 1;
	GenI2CRead[1] = reglen;

	if ((reglen + datalen) > XU_I2C_CONTROL_MAX_DATA_LENS)
		return -1;

	if (reglen > XU_I2C_CONTROL_MAX_REG_ADDR_LENS)
		return -1;

	if (reglen == 1)
		GenI2CRead[3] = (uint8_t)regaddr;
	else if (reglen == 2) {
		GenI2CRead[2] = regaddr >> 8;
		GenI2CRead[3] = regaddr & 0xFF;
	}
	else {
		return -1;
	}
	GenI2CRead[2 + reglen] = datalen >> 8;
	GenI2CRead[3 + reglen] = datalen & 0xFF;

	SetGetExtensionUnit(xuGuidUVC, 2, 6, KSPROPERTY_TYPE_SET | KSPROPERTY_TYPE_TOPOLOGY, (void*)GenI2CRead, XU_I2C_CONTROL_MAX_TRANS_LENS, &readCount);
	if (!SetGetExtensionUnit(xuGuidUVC, 2, 6, KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_TOPOLOGY, (void*)GenI2CRead, XU_I2C_CONTROL_MAX_TRANS_LENS, &readCount))
	{
		memcpy(data, (uint8_t*)GenI2CRead + 4 + reglen, datalen);
	}

	return 0;
}

int VizionCam::GenI2CWrite(uint8_t slvaddr, uint8_t reglen, uint16_t regaddr, uint8_t datalen, uint8_t* data)
{
	ULONG readCount;
	BYTE GenI2CWrite[XU_I2C_CONTROL_MAX_TRANS_LENS];

	GenI2CWrite[0] = slvaddr << 1;
	GenI2CWrite[1] = reglen;

	if ((reglen + datalen) > XU_I2C_CONTROL_MAX_DATA_LENS)
		return -1;

	if (reglen == 1)
		GenI2CWrite[2] = (uint8_t)regaddr;
	else if (reglen == 2) {
		GenI2CWrite[2] = regaddr >> 8;
		GenI2CWrite[3] = regaddr & 0xFF;
	}
	else {
		return -1;
	}
	GenI2CWrite[2 + reglen] = datalen >> 8;
	GenI2CWrite[3 + reglen] = datalen & 0xFF;

	memcpy((uint8_t*)(GenI2CWrite + 4 + reglen), data, datalen);

	SetGetExtensionUnit(xuGuidUVC, 2, 6, KSPROPERTY_TYPE_SET | KSPROPERTY_TYPE_TOPOLOGY, (void*)GenI2CWrite, XU_I2C_CONTROL_MAX_TRANS_LENS, &readCount);

	return 0;
}

int PropIdToMFProp(int prop_id)
{
	int prop = 0;
	switch (prop_id)
	{
		//case CAPTURE_BRIGHTNESS:
	default:
		prop = VideoProcAmp_Brightness;
		break;
	case CAPTURE_CONTRAST:
		prop = VideoProcAmp_Contrast;
		break;
	case CAPTURE_HUE:
		prop = VideoProcAmp_Hue;
		break;
	case CAPTURE_SATURATION:
		prop = VideoProcAmp_Saturation;
		break;
	case CAPTURE_SHARPNESS:
		prop = VideoProcAmp_Sharpness;
		break;
	case CAPTURE_GAMMA:
		prop = VideoProcAmp_Gamma;
		break;
	case CAPTURE_COLORENABLE:
		prop = VideoProcAmp_ColorEnable;
		break;
	case CAPTURE_WHITEBALANCE:
		prop = VideoProcAmp_WhiteBalance;
		break;
	case CAPTURE_BACKLIGHTCOMPENSATION:
		prop = VideoProcAmp_BacklightCompensation;
		break;
	case CAPTURE_GAIN:
		prop = VideoProcAmp_Gain;
		break;
	case CAPTURE_PAN:
		prop = CameraControl_Pan;
		break;
	case CAPTURE_TILT:
		prop = CameraControl_Tilt;
		break;
	case CAPTURE_ROLL:
		prop = CameraControl_Roll;
		break;
	case CAPTURE_ZOOM:
		prop = CameraControl_Zoom;
		break;
	case CAPTURE_EXPOSURE:
		prop = CameraControl_Exposure;
		break;
	case CAPTURE_IRIS:
		prop = CameraControl_Iris;
		break;
	case CAPTURE_FOCUS:
		prop = CameraControl_Focus;
		break;
	}
	return prop;
}

int VizionCam::SetPropertyValue(CAPTURE_PROPETIES prop_id, long value, int flag)
{
	HRESULT hr;
	IAMVideoProcAmp* procAmp = NULL;
	IAMCameraControl* control = NULL;

	int prop = PropIdToMFProp(prop_id);

	if (prop_id < CAPTURE_PAN)
	{
		hr = pVideoSource->QueryInterface(IID_PPV_ARGS(&procAmp));
		if (SUCCEEDED(hr))
		{
			long v = 0, f = 0;
			hr = procAmp->Set(prop, value, flag ? VideoProcAmp_Flags_Auto : VideoProcAmp_Flags_Manual);

			procAmp->Release();
			return hr;
		}
	}
	else
	{
		hr = pVideoSource->QueryInterface(IID_PPV_ARGS(&control));
		if (SUCCEEDED(hr))
		{
			hr = control->Set(prop, value, flag ? VideoProcAmp_Flags_Auto : VideoProcAmp_Flags_Manual);

			control->Release();
			return hr;
		}
	}

	return -1;
}

int VizionCam::GetPropertyValue(CAPTURE_PROPETIES prop_id, long& value, int& flag)
{
	HRESULT hr;
	IAMVideoProcAmp* procAmp = NULL;
	IAMCameraControl* control = NULL;

	value = 0;
	flag = -1;

	int prop = PropIdToMFProp(prop_id);

	if (prop_id < CAPTURE_PAN)
	{
		hr = pVideoSource->QueryInterface(IID_PPV_ARGS(&procAmp));
		if (SUCCEEDED(hr))
		{
			long v = 0, f = 0;
			hr = procAmp->Get(prop, &v, &f);
			if (SUCCEEDED(hr))
			{
				value = v;
				flag = !!(f & VideoProcAmp_Flags_Auto);
			}

			procAmp->Release();
			return hr;
		}
	}
	else
	{
		hr = pVideoSource->QueryInterface(IID_PPV_ARGS(&control));
		if (SUCCEEDED(hr))
		{
			long v = 0, f = 0;
			hr = control->Get(prop, &v, &f);
			if (SUCCEEDED(hr))
			{
				value = v;
				flag = !!(f & VideoProcAmp_Flags_Auto);
			}
			
			control->Release();
			return hr;
		}
	}

	return -1;
}

int VizionCam::GetPropertyRange(CAPTURE_PROPETIES prop_id, long& min, long& max, long& step, long& def, long& caps)
{
	HRESULT hr;
	IAMVideoProcAmp* procAmp = NULL;
	IAMCameraControl* control = NULL;

	int prop = PropIdToMFProp(prop_id);

	if (prop_id < CAPTURE_PAN)
	{
		hr = pVideoSource->QueryInterface(IID_PPV_ARGS(&procAmp));
		if (SUCCEEDED(hr))
		{
			hr = procAmp->GetRange(prop, &min, &max, &step, &def, &caps);
			caps = !!(caps & CameraControl_Flags_Auto) ? 1 : 0;
			procAmp->Release();
			return hr;
		}
	}
	else
	{
		hr = pVideoSource->QueryInterface(IID_PPV_ARGS(&control));
		if (SUCCEEDED(hr))
		{
			hr = control->GetRange(prop, &min, &max, &step, &def, &caps);
			caps = !!(caps & CameraControl_Flags_Auto) ? 1 : 0;
			control->Release();
			return hr;
		}
	}

	return -1;
}

int VizionCam::UVC_AP1302_I2C_Write(uint16_t addrReg, uint16_t data)
{
	ULONG readCount;
	BYTE AP1302_I2CMsg[5];

	AP1302_I2CMsg[0] = 0; // Set Write Mode
	AP1302_I2CMsg[1] = addrReg >> 8;
	AP1302_I2CMsg[2] = addrReg & 0xFF;
	AP1302_I2CMsg[3] = data >> 8;
	AP1302_I2CMsg[4] = data & 0xFF;

	if (!SetGetExtensionUnit(xuGuidUVC, 2, 5, KSPROPERTY_TYPE_SET | KSPROPERTY_TYPE_TOPOLOGY, (void*)AP1302_I2CMsg, 5, &readCount)) {
		vzlog->Trace("[%s][%d] I2C Write SUCCESS! AddrReg=%04x, Data=%04x", __FUNCTION__, __LINE__, addrReg, data);
	}
	else {
		vzlog->Error("[%s][%d] I2C Write Fail! AddrReg=%04x, Data=%04x", __FUNCTION__, __LINE__, addrReg, data);
		return -1;
	}

	return 0;
}

int VizionCam::UVC_AP1302_I2C_Read(uint16_t addrReg, uint16_t& data)
{
	ULONG readCount;
	BYTE AP1302_I2CMsg[5];

	AP1302_I2CMsg[0] = 1; // Set Read Mode
	AP1302_I2CMsg[1] = addrReg >> 8;
	AP1302_I2CMsg[2] = addrReg & 0xFF;
	AP1302_I2CMsg[3] = 0;
	AP1302_I2CMsg[4] = 0;

	if (SetGetExtensionUnit(xuGuidUVC, 2, 5, KSPROPERTY_TYPE_SET | KSPROPERTY_TYPE_TOPOLOGY, (void*)AP1302_I2CMsg, 5, &readCount)) {
		vzlog->Error("[%s][%d] I2C Set Address Fail! AddrReg=%04x", __FUNCTION__, __LINE__, addrReg);
		return -1;
	}
	if (!SetGetExtensionUnit(xuGuidUVC, 2, 5, KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_TOPOLOGY, (void*)AP1302_I2CMsg, 5, &readCount))
	{
		data = (AP1302_I2CMsg[3] << 8) + AP1302_I2CMsg[4];
		vzlog->Trace("[%s][%d] I2C Read: Addr=0x%04x, Data=%04x", __FUNCTION__, __LINE__, addrReg, data);
	}
	else {
		vzlog->Error("[%s][%d] I2C Set Address Fail! AddrReg=%04x", __FUNCTION__, __LINE__, addrReg);
		return -1;
	}

	return 0;
}

int VizionCam::GetFlipMode(FLIP_MODE& flip)
{
	int ret = 0;
	uint16_t flipaddr = 0x100C;
	uint16_t flipctrl = 0x0000;

	if (ret = UVC_AP1302_I2C_Read(flipaddr, flipctrl) < 0)
		return ret;
	flip = (FLIP_MODE)(flipctrl & 0x3);

	return 0;
}

int VizionCam::SetFlipMode(FLIP_MODE flip)
{
	uint16_t flipaddr = 0x100C;
	uint16_t flipctrl = 0x0000;

	flipctrl = (uint16_t)flip;

	return UVC_AP1302_I2C_Write(flipaddr, flipctrl);
}

int VizionCam::GetEffectMode(EFFECT_MODE& effect)
{
	int ret = 0;
	uint16_t effectaddr = 0x1016;
	uint16_t effectctrl = 0x0000;

	if (ret = UVC_AP1302_I2C_Read(effectaddr, effectctrl) < 0)
		return ret;
	effect = (EFFECT_MODE)(effectctrl & 0xf);

	return 0;
}

int  VizionCam::SetEffectMode(EFFECT_MODE effect)
{
	uint16_t effectaddr = 0x1016;
	uint16_t effectctrl = 0x0000;

	effectctrl = (uint16_t)effect;

	return UVC_AP1302_I2C_Write(effectaddr, effectctrl);
}


int VizionCam::GetAutoExposureMode(AE_MODE_STATUS& ae_mode)
{
	int ret = 0;
	uint16_t aeaddr = 0x5002;
	uint16_t aectrl = 0x2090;

	aectrl |= (uint16_t)ae_mode;
	if(ret = UVC_AP1302_I2C_Read(aeaddr, aectrl) < 0)
		return ret;
	ae_mode = (AE_MODE_STATUS)(aectrl & 0x000F);

	return 0;
}

int VizionCam::SetAutoExposureMode(AE_MODE_STATUS ae_mode)
{
	uint16_t aeaddr = 0x5002;
	uint16_t aectrl = 0x2090;

	aectrl |= (uint16_t)ae_mode;

	return UVC_AP1302_I2C_Write(aeaddr, aectrl);
}

int VizionCam::GetExposureTime(uint32_t& exptime)
{
	int ret = 0;
	uint16_t exptimeaddr = 0x500C;
	uint16_t exptime_lsb = 0, exptime_msb = 0;

	if (ret = UVC_AP1302_I2C_Read(exptimeaddr, exptime_msb) < 0) return ret;
	if (ret = UVC_AP1302_I2C_Read(exptimeaddr + 2, exptime_lsb) < 0) return ret;

	exptime = (exptime_msb << 16) + exptime_lsb;
	return 0;
}

int VizionCam::SetExposureTime(uint32_t exptime)
{
	uint16_t exptimeaddr = 0x500C;
	uint16_t exptime_lsb, exptime_msb;

	if (exptime > VZCAM_ISP_CONTROL_EXPOSURETIME_MAX || exptime < VZCAM_ISP_CONTROL_EXPOSURETIME_MIN) {
		vzlog->Error("[%s][%d] Exposure Time range: %d ~ %d ms.", __FUNCTION__, __LINE__, VZCAM_ISP_CONTROL_EXPOSURETIME_MIN, VZCAM_ISP_CONTROL_EXPOSURETIME_MAX);
		return -1;
	}

	exptime_lsb = exptime & 0xFFFF;
	exptime_msb = (exptime >> 16) & 0xFFFF;
	UVC_AP1302_I2C_Write(exptimeaddr, exptime_msb);
	UVC_AP1302_I2C_Write(exptimeaddr + 2, exptime_lsb);

	return 0;
}

int VizionCam::GetExposureGain(uint8_t& expgain)
{
	uint16_t expgainaddr = 0x5006;
	uint16_t expgain_val;
	if (UVC_AP1302_I2C_Read(expgainaddr, expgain_val) < 0) return -1;
	expgain = (expgain_val >> 8) & 0xFF;
	return  0;
}

int VizionCam::SetExposureGain(uint8_t expgain)
{
	if (expgain > VZCAM_ISP_CONTROL_GAIN_MAX || expgain < VZCAM_ISP_CONTROL_GAIN_MIN) {
		vzlog->Error("[%s][%d] Exosure Gain Range: %d ~ %dx.", __FUNCTION__, __LINE__, VZCAM_ISP_CONTROL_GAIN_MIN, VZCAM_ISP_CONTROL_GAIN_MAX);
		return -1;
	}

	uint16_t expgainaddr = 0x5006;
	uint16_t expgain_val = (((uint16_t)expgain) << 8) & 0xFF00;
	return UVC_AP1302_I2C_Write(expgainaddr, expgain_val);
}

int VizionCam::GetBacklightCompensation(double& ae_comp)
{
	constexpr uint16_t ae_comp_addr = 0x501A;
	uint16_t ae_comp_val = 0;
	long integer;
	double fractional;

	if (UVC_AP1302_I2C_Read(ae_comp_addr, ae_comp_val) < 0) return -1;
	integer = (ae_comp_val >> 8) > 15 ? (long)(ae_comp_val >> 8) - 0x100 : (long)(ae_comp_val >> 8);
	fractional = (double)(ae_comp_val & 0x00FF) / 256;
	
	if (integer > VZCAM_ISP_CONTROL_BACKLIGHT_COMP_MAX || integer < VZCAM_ISP_CONTROL_BACKLIGHT_COMP_MIN)
		return -1;

	ae_comp = integer + fractional;

	return 0;
}

int VizionCam::SetBacklightCompensation(double ae_comp)
{
	if (ae_comp > VZCAM_ISP_CONTROL_BACKLIGHT_COMP_MAX || ae_comp < VZCAM_ISP_CONTROL_BACKLIGHT_COMP_MIN) {
		vzlog->Error("[%s][%d] Digital Zoom CT Y: %.1f ~ %.1f.",
			__FUNCTION__, __LINE__, VZCAM_ISP_CONTROL_BACKLIGHT_COMP_MIN, VZCAM_ISP_CONTROL_BACKLIGHT_COMP_MAX);
		return -1;
	}

	constexpr uint16_t ae_comp_addr = 0x501A;
	uint16_t ae_comp_val = 0;
	long integer;
	double fractional;

	integer = (ae_comp < 0) ? (long)ae_comp + 256 : (long)ae_comp;
	fractional = (ae_comp < 0) ? (long)(integer - 256) - ae_comp : ae_comp - integer;

	ae_comp_val = ae_comp >= 0 ? (uint16_t)(integer << 8) + (uint16_t)(fractional * 256) : (uint16_t)(integer << 8) - (uint16_t)(fractional * 256);

	return UVC_AP1302_I2C_Write(ae_comp_addr, ae_comp_val);
}

int VizionCam::GetMaxFPS(uint8_t& max_fps)
{
	uint16_t maxfpsaddr = 0x2020;
	uint16_t maxfps_val;
	if (UVC_AP1302_I2C_Read(maxfpsaddr, maxfps_val) < 0) return -1;
	max_fps = (maxfps_val >> 8) & 0xFF;
	return  0;
}

int VizionCam::SetMaxFPS(uint8_t max_fps)
{
	uint16_t maxfpsaddr = 0x2020;
	uint16_t maxfps_val = (((uint16_t)max_fps) << 8) & 0xFF00;
	return  UVC_AP1302_I2C_Write(maxfpsaddr, maxfps_val);
}

int VizionCam::GetAutoWhiteBalanceMode(AWB_MODE_STATUS& awb_mode)
{
	uint16_t awbaddr = 0x5100;
	uint16_t awbctrl = 0x1150;

	awbctrl |= (uint16_t)awb_mode;
	if (UVC_AP1302_I2C_Read(awbaddr, awbctrl) < 0) return -1;
	awb_mode = (AWB_MODE_STATUS)(awbctrl & 0x000F);

	return 0;
}

int VizionCam::SetAutoWhiteBalanceMode(AWB_MODE_STATUS awb_mode)
{
	uint16_t awbaddr = 0x5100;
	uint16_t awbctrl = 0x1150;

	awbctrl |= (uint16_t)awb_mode;
	return UVC_AP1302_I2C_Write(awbaddr, awbctrl);
}

int VizionCam::GetTemperature(uint16_t& temp)
{
	uint16_t temp_addr = 0x510A;

	return UVC_AP1302_I2C_Read(temp_addr, temp);
}

int VizionCam::SetTemperature(uint16_t temp)
{
	uint16_t temp_addr = 0x510A;

	if (temp > VZCAM_ISP_CONTROL_TEMPERAURE_MAX || temp < VZCAM_ISP_CONTROL_TEMPERAURE_MIN) {
		vzlog->Error("[%s][%d] Temperature Range: %d ~ %d K.", __FUNCTION__, __LINE__, VZCAM_ISP_CONTROL_TEMPERAURE_MIN, VZCAM_ISP_CONTROL_TEMPERAURE_MAX);
		return -1;
	}

	return UVC_AP1302_I2C_Write(temp_addr, temp);
}

int VizionCam::GetWhiteBalanceQx(double& wb_qx)
{
	constexpr uint16_t wb_qx_addr = 0x5102;
	uint16_t wb_qx_val = 0;
	long integer;
	double fractional;

	if (UVC_AP1302_I2C_Read(wb_qx_addr, wb_qx_val) < 0) return -1;
	integer = (wb_qx_val >> 12) >= 8 ? (long)(wb_qx_val >> 12) - 16 : (long)(wb_qx_val >> 12);
	fractional = (double)(wb_qx_val & 0x0FFF) / 4096;
	wb_qx = integer + fractional;

	return 0;
}
int VizionCam::SetWhiteBalanceQx(double wb_qx)
{
	if (wb_qx > VZCAM_ISP_CONTROL_WHITEBALANCE_QXQY_MAX || wb_qx < VZCAM_ISP_CONTROL_WHITEBALANCE_QXQY_MIN) {
		vzlog->Error("[%s][%d] WhiteBalanceQx range: %01f ~ %01fx.", __FUNCTION__, __LINE__, VZCAM_ISP_CONTROL_WHITEBALANCE_QXQY_MIN, VZCAM_ISP_CONTROL_WHITEBALANCE_QXQY_MAX);
		return -1;
	}

	uint16_t wb_qx_addr = 0x5102;
	uint16_t wb_qx_val = 0;
	long integer = (wb_qx < 0) ? (long)wb_qx + 16 : (long)wb_qx;
	double fractional = (wb_qx < 0) ? (long)(integer - 16) - wb_qx : wb_qx - integer;
	wb_qx_val = wb_qx >= 0 ? (uint16_t)(integer << 12) + (uint16_t)(fractional * 4096) : (uint16_t)(integer << 12) - (uint16_t)(fractional * 4096);

	return UVC_AP1302_I2C_Write(wb_qx_addr, wb_qx_val);
}
int VizionCam::GetWhiteBalanceQy(double& wb_qy)
{
	constexpr uint16_t wb_qy_addr = 0x5104;
	uint16_t wb_qy_val = 0;
	long integer;
	double fractional;

	if (UVC_AP1302_I2C_Read(wb_qy_addr, wb_qy_val) < 0) return -1;
	integer = (wb_qy_val >> 12) >= 8 ? (long)(wb_qy_val >> 12) - 16 : (long)(wb_qy_val >> 12);
	fractional = (double)(wb_qy_val & 0x0FFF) / 4096;
	wb_qy = integer + fractional;

	return 0;
}
int VizionCam::SetWhiteBalanceQy(double wb_qy)
{
	if (wb_qy > VZCAM_ISP_CONTROL_WHITEBALANCE_QXQY_MAX || wb_qy < VZCAM_ISP_CONTROL_WHITEBALANCE_QXQY_MIN) {
		vzlog->Error("[%s][%d] WhiteBalanceQy range: %01f ~ %01fx.", __FUNCTION__, __LINE__, VZCAM_ISP_CONTROL_WHITEBALANCE_QXQY_MIN, VZCAM_ISP_CONTROL_WHITEBALANCE_QXQY_MAX);
		return -1;
	}

	uint16_t wb_qy_addr = 0x5104;
	uint16_t wb_qy_val = 0;
	long integer = (wb_qy < 0) ? (long)wb_qy + 16 : (long)wb_qy;
	double fractional = (wb_qy < 0) ? (long)(integer - 16) - wb_qy : wb_qy - integer;
	wb_qy_val = wb_qy >= 0 ? (uint16_t)(integer << 12) + (uint16_t)(fractional * 4096) : (uint16_t)(integer << 12) - (uint16_t)(fractional * 4096);

	return UVC_AP1302_I2C_Write(wb_qy_addr, wb_qy_val);
}

int VizionCam::GetGamma(double& gamma)
{
	constexpr uint16_t gamma_addr = 0x700A;
	uint16_t gamma_val = 0;
	long integer;
	double fractional;

	if (UVC_AP1302_I2C_Read(gamma_addr, gamma_val) < 0) return -1;
	integer = (gamma_val >> 12) >= 8 ? (long)(gamma_val >> 12) - 16 : (long)(gamma_val >> 12);
	fractional = (double)(gamma_val & 0x0FFF) / 4096;
	gamma = integer + fractional;

	return 0;
}

int VizionCam::SetGamma(double gamma)
{
	if (gamma > VZCAM_ISP_CONTROL_GAMMA_MAX || gamma < VZCAM_ISP_CONTROL_GAMMA_MIN) {
		vzlog->Error("[%s][%d] Gamma range: %01f ~ %01fx.", __FUNCTION__, __LINE__, VZCAM_ISP_CONTROL_GAMMA_MIN, VZCAM_ISP_CONTROL_GAMMA_MAX);
		return -1;
	}

	uint16_t gamma_addr = 0x700A;
	uint16_t gamma_val = 0;
	long integer = (gamma < 0) ? (long)gamma + 16 : (long)gamma;
	double fractional = (gamma < 0) ? (long)(integer - 16) - gamma : gamma - integer;
	gamma_val = gamma >= 0 ? (uint16_t)(integer << 12) + (uint16_t)(fractional * 4096) : (uint16_t)(integer << 12) - (uint16_t)(fractional * 4096);

	return UVC_AP1302_I2C_Write(gamma_addr, gamma_val);
}

int VizionCam::GetSaturation(double& saturation)
{
	constexpr uint16_t saturation_addr = 0x7006;
	uint16_t saturation_val = 0;
	long integer;
	double fractional;

	if (UVC_AP1302_I2C_Read(saturation_addr, saturation_val) < 0) return -1;
	integer = (saturation_val >> 12) >= 8 ? (long)(saturation_val >> 12) - 16 : (long)(saturation_val >> 12);
	fractional = (double)(saturation_val & 0x0FFF) / 4096;
	saturation = integer + fractional;

	return 0;
}

int VizionCam::SetSaturation(double saturation)
{
	if (saturation > VZCAM_ISP_CONTROL_SATURATION_MAX || saturation < VZCAM_ISP_CONTROL_SATURATION_MIN) {
		vzlog->Error("[%s][%d] Saturation range: %01f ~ %01f.", __FUNCTION__, __LINE__, VZCAM_ISP_CONTROL_SATURATION_MIN, VZCAM_ISP_CONTROL_SATURATION_MAX);
		return -1;
	}

	uint16_t saturation_addr = 0x7006;
	uint16_t saturation_val = 0;
	long integer = (saturation < 0) ? (long)saturation + 16 : (long)saturation;
	double fractional = (saturation < 0) ? (long)(integer - 16) - saturation : saturation - integer;
	saturation_val = saturation >= 0 ? (uint16_t)(integer << 12) + (uint16_t)(fractional * 4096) : (uint16_t)(integer << 12) - (uint16_t)(fractional * 4096);

	return UVC_AP1302_I2C_Write(saturation_addr, saturation_val);
}

int VizionCam::GetContrast(double& contrast)
{
	constexpr uint16_t contrast_addr = 0x7002;
	uint16_t contrast_val = 0;
	long integer;
	double fractional;

	if (UVC_AP1302_I2C_Read(contrast_addr, contrast_val) < 0) return -1;
	integer = (contrast_val >> 12) >= 8 ? (long)(contrast_val >> 12) - 16 : (long)(contrast_val >> 12);
	fractional = (double)(contrast_val & 0x0FFF) / 4096;
	contrast = integer + fractional;

	return 0;
}

int VizionCam::SetContrast(double contrast)
{
	if (contrast > VZCAM_ISP_CONTROL_CONTRAST_MAX || contrast < VZCAM_ISP_CONTROL_CONTRAST_MIN) {
		vzlog->Error("[%s][%d] Contrast range: %01f ~ %01f.", __FUNCTION__, __LINE__, VZCAM_ISP_CONTROL_CONTRAST_MIN, VZCAM_ISP_CONTROL_CONTRAST_MAX);
		return -1;
	}

	uint16_t contrast_addr = 0x7002;
	uint16_t contrast_val = 0;
	long integer = (contrast < 0) ? (long)contrast + 16 : (long)contrast;
	double fractional = (contrast < 0) ? (long)(integer - 16) - contrast : contrast - integer;
	contrast_val = contrast >= 0 ? (uint16_t)(integer << 12) + (uint16_t)(fractional * 4096) : (uint16_t)(integer << 12) - (uint16_t)(fractional * 4096);

	return UVC_AP1302_I2C_Write(contrast_addr, contrast_val);
}

int VizionCam::GetSharpening(double& sharpness)
{
	constexpr uint16_t sharpness_addr = 0x7010;
	uint16_t sharpness_val = 0;
	long integer;
	double fractional;

	if (UVC_AP1302_I2C_Read(sharpness_addr, sharpness_val) < 0) return -1;
	integer = (sharpness_val >> 12) >= 8 ? (long)(sharpness_val >> 12) - 16 : (long)(sharpness_val >> 12);
	fractional = (double)(sharpness_val & 0x0FFF) / 4096;
	sharpness = integer + fractional;

	return 0;
}

int VizionCam::SetSharpening(double sharpness)
{
	if (sharpness > VZCAM_ISP_CONTROL_SHARPENING_MAX || sharpness < VZCAM_ISP_CONTROL_SHARPENING_MIN) {
		vzlog->Error("[%s][%d] Sharpening range: %01f ~ %01f.", __FUNCTION__, __LINE__, VZCAM_ISP_CONTROL_SHARPENING_MIN, VZCAM_ISP_CONTROL_SHARPENING_MAX);
		return -1;
	}

	uint16_t sharpness_addr = 0x7010;
	uint16_t sharpness_val = 0;
	long integer = (sharpness < 0) ? (long)sharpness + 16 : (long)sharpness;
	double fractional = (sharpness < 0) ? (long)(integer - 16) - sharpness : sharpness - integer;
	sharpness_val = sharpness >= 0 ? (uint16_t)(integer << 12) + (uint16_t)(fractional * 4096) : (uint16_t)(integer << 12) - (uint16_t)(fractional * 4096);

	return UVC_AP1302_I2C_Write(sharpness_addr, sharpness_val);
}

int VizionCam::GetDenoise(double& denoise)
{
	constexpr uint16_t denoise_addr = 0x700C;
	uint16_t denoise_val = 0;
	long integer;
	double fractional;

	if (UVC_AP1302_I2C_Read(denoise_addr, denoise_val) < 0) return -1;
	integer = (denoise_val >> 12) >= 8 ? (long)(denoise_val >> 12) - 16 : (long)(denoise_val >> 12);
	fractional = (double)(denoise_val & 0x0FFF) / 4096;
	denoise = integer + fractional;

	return 0;
}

int VizionCam::SetDenoise(double denoise)
{
	if (denoise > VZCAM_ISP_CONTROL_DENOISE_MAX || denoise < VZCAM_ISP_CONTROL_DENOISE_MIN) {
		vzlog->Error("[%s][%d] Denoise range: %01f ~ %01f.", __FUNCTION__, __LINE__, VZCAM_ISP_CONTROL_DENOISE_MIN, VZCAM_ISP_CONTROL_DENOISE_MAX);
		return -1;
	}

	uint16_t denoise_addr = 0x700C;
	uint16_t denoise_val = 0;
	long integer = (denoise < 0) ? (long)denoise + 16 : (long)denoise;
	double fractional = (denoise < 0) ? (long)(integer - 16) - denoise : denoise - integer;
	denoise_val = denoise >= 0 ? (uint16_t)(integer << 12) + (uint16_t)(fractional * 4096) : (uint16_t)(integer << 12) - (uint16_t)(fractional * 4096);

	return UVC_AP1302_I2C_Write(denoise_addr, denoise_val);
}

int VizionCam::GetDigitalZoomType(DZ_MODE_STATUS& zoom_type)
{
	constexpr uint16_t zoomtype_addr = 0x1012;
	uint16_t zoomtype_val = 0;

	if (UVC_AP1302_I2C_Read(zoomtype_addr, zoomtype_val) < 0) return -1;
	
	zoom_type = (DZ_MODE_STATUS)zoomtype_val;

	return 0;
}
int VizionCam::SetDigitalZoomType(DZ_MODE_STATUS zoom_type)
{
	constexpr uint16_t zoomtype_addr = 0x1012;
	uint16_t zoomtype_val = (int)zoom_type;

	return UVC_AP1302_I2C_Write(zoomtype_addr, zoomtype_val);
}

int VizionCam::GetDigitalZoomTarget(double& times)
{
	constexpr uint16_t zoom_addr = 0x1010;
	uint16_t zoom_val = 0;
	long integer;
	double fractional;

	if (UVC_AP1302_I2C_Read(zoom_addr, zoom_val) < 0) return -1;
	integer = (zoom_val >> 8) & 0xFF;
	fractional = (double)(zoom_val & 0xFF) / 256;

	if (integer > 8 || integer < 1)
		return -1;

	times = integer + fractional;

	return 0;
}
int VizionCam::SetDigitalZoomTarget(double times)
{
	if (times > VZCAM_ISP_CONTROL_DIGITALZOOM_MAX || times < VZCAM_ISP_CONTROL_DIGITALZOOM_MIN) {
		vzlog->Error("[%s][%d] Digital Zoom Range: %d ~ %dx.", __FUNCTION__, __LINE__, VZCAM_ISP_CONTROL_DIGITALZOOM_MIN, VZCAM_ISP_CONTROL_DIGITALZOOM_MAX);
		return -1;
	}

	constexpr uint16_t zoom_addr = 0x1010;
	uint16_t zoom_val = 0;
	long integer;
	double fractional;

	integer = (long)times;
	fractional = times - integer;

	zoom_val = (uint16_t)(integer << 8) + (uint16_t)(fractional * 256);

	return UVC_AP1302_I2C_Write(zoom_addr, zoom_val);
}

int VizionCam::GetDigitalZoom_CT_X(double& ct_x)
{
	constexpr uint16_t zoom_ctx_addr = 0x118C;
	uint16_t zoom_ctx_val = 0;
	long integer;
	double fractional;

	if (UVC_AP1302_I2C_Read(zoom_ctx_addr, zoom_ctx_val) < 0) return -1;
	integer = (zoom_ctx_val >> 8) & 0xFF;
	fractional = (double)(zoom_ctx_val & 0xFF) / 256;

	if (integer > 1 || integer < 0)
		return -1;

	ct_x = integer + fractional;

	return 0;
}
int VizionCam::SetDigitalZoom_CT_X(double ct_x)
{
	if (ct_x > VZCAM_ISP_CONTROL_DIGITALZOOM_CT_MAX || ct_x < VZCAM_ISP_CONTROL_DIGITALZOOM_CT_MIN) {
		vzlog->Error("[%s][%d] Digital Zoom CT X: %.1f ~ %.1f.", 
			__FUNCTION__, __LINE__, VZCAM_ISP_CONTROL_DIGITALZOOM_CT_MIN, VZCAM_ISP_CONTROL_DIGITALZOOM_CT_MAX);
		return -1;
	}

	constexpr uint16_t zoom_ctx_addr = 0x118C;
	uint16_t zoom_ctx_val = 0;
	long integer;
	double fractional;

	integer = (long)ct_x;
	fractional = ct_x - integer;

	zoom_ctx_val = (uint16_t)(integer << 8) + (uint16_t)(fractional * 256);

	return UVC_AP1302_I2C_Write(zoom_ctx_addr, zoom_ctx_val);
}

int VizionCam::GetDigitalZoom_CT_Y(double& ct_y) 
{
	constexpr uint16_t zoom_cty_addr = 0x118E;
	uint16_t zoom_cty_val = 0;
	long integer;
	double fractional;

	if (UVC_AP1302_I2C_Read(zoom_cty_addr, zoom_cty_val) < 0) return -1;
	integer = (zoom_cty_val >> 8) & 0xFF;
	fractional = (double)(zoom_cty_val & 0xFF) / 256;

	if (integer > 1 || integer < 0)
		return -1;

	ct_y = integer + fractional;

	return 0;
}
int VizionCam::SetDigitalZoom_CT_Y(double ct_y) 
{
	if (ct_y > VZCAM_ISP_CONTROL_DIGITALZOOM_CT_MAX || ct_y < VZCAM_ISP_CONTROL_DIGITALZOOM_CT_MIN) {
		vzlog->Error("[%s][%d] Digital Zoom CT Y: %.1f ~ %.1f.",
			__FUNCTION__, __LINE__, VZCAM_ISP_CONTROL_DIGITALZOOM_CT_MIN, VZCAM_ISP_CONTROL_DIGITALZOOM_CT_MAX);
		return -1;
	}

	constexpr uint16_t zoom_cty_addr = 0x118E;
	uint16_t zoom_cty_val = 0;
	long integer;
	double fractional;

	integer = (long)ct_y;
	fractional = ct_y - integer;

	zoom_cty_val = (uint16_t)(integer << 8) + (uint16_t)(fractional * 256);

	return UVC_AP1302_I2C_Write(zoom_cty_addr, zoom_cty_val);
}

/*
#define VZCAM_ISP_CONTROL_EXPOSURETIME_DEF 33333
#define VZCAM_ISP_CONTROL_GAIN_DEF 1
#define VZCAM_ISP_CONTROL_TEMPERAURE_DEF 5000
#define VZCAM_ISP_CONTROL_GAMMA_DEF 0.0
#define VZCAM_ISP_CONTROL_SATURATION_DEF 1.0
#define VZCAM_ISP_CONTROL_CONTRAST_DEF 0.0
#define VZCAM_ISP_CONTROL_SHARPENING_DEF 0.0
#define VZCAM_ISP_CONTROL_DENOISE_DEF 0.0
#define VZCAM_ISP_CONTROL_DIGITALZOOM_DEF 1.0
#define VZCAM_ISP_CONTROL_DIGITALZOOM_CT_DEF 0.5
*/

int VizionCam::RecoverDefaultSetting()
{
	if (SetExposureTime(VZCAM_ISP_CONTROL_EXPOSURETIME_DEF)) return -1;
	if (SetExposureGain(VZCAM_ISP_CONTROL_GAIN_DEF)) return -1;
	if (SetBacklightCompensation(VZCAM_ISP_CONTROL_BACKLIGHT_COMP_DEF)) return -1;
	if (SetAutoExposureMode(AE_MODE_STATUS::AUTO_EXP)) return -1;
	if (SetTemperature(VZCAM_ISP_CONTROL_TEMPERAURE_DEF)) return -1;
	if (SetAutoWhiteBalanceMode(AWB_MODE_STATUS::AUTO_WB)) return -1;
	if (SetGamma(VZCAM_ISP_CONTROL_GAMMA_DEF)) return -1;
	if (SetSaturation(VZCAM_ISP_CONTROL_SATURATION_DEF)) return -1;
	if (SetContrast(VZCAM_ISP_CONTROL_CONTRAST_DEF)) return -1;
	if (SetSharpening(VZCAM_ISP_CONTROL_SHARPENING_DEF)) return -1;
	if (SetDenoise(VZCAM_ISP_CONTROL_DENOISE_DEF)) return -1;
	if (SetDigitalZoomTarget(VZCAM_ISP_CONTROL_DIGITALZOOM_DEF)) return -1;
	if (SetDigitalZoom_CT_X(VZCAM_ISP_CONTROL_DIGITALZOOM_CT_DEF)) return -1;
	if (SetDigitalZoom_CT_Y(VZCAM_ISP_CONTROL_DIGITALZOOM_CT_DEF)) return -1;
	if (SetDigitalZoomType(DZ_MODE_STATUS::DZ_IMMEDIATE)) return -1;
	if (SetEffectMode(EFFECT_MODE::NORMAL_MODE)) return -1;
	if (SetFlipMode(def_flip)) return -1;

	return 0;
}

int VizionCam::LoadProfileSetting(const char* profile_str)
{
	json profile = json::parse(profile_str);

	try {
		vzprof.profilename = profile["ProfileName"];
		vzprof.productname = profile["ProductName"];
		vzprof.gendate = profile["GenerateDate"];
		vzprof.serialnumber = profile["SerialNumber"];
		vzprof.controlmode = profile["ControlMode"];

		auto cameraconfig = profile["CameraProfile"];
		vzprof.camprofile.width = cameraconfig["Width"];
		vzprof.camprofile.height = cameraconfig["Height"];
		vzprof.camprofile.framerate = cameraconfig["Framerate"];
		vzprof.camprofile.pixelformat_str = cameraconfig["PixelFormat"];
		if (vzprof.camprofile.pixelformat_str == "UYVY")
			vzprof.camprofile.format = UYVY;
		else if (vzprof.camprofile.pixelformat_str == "YUY2")
			vzprof.camprofile.format = YUY2;
		else if (vzprof.camprofile.pixelformat_str == "MJPG")
			vzprof.camprofile.format = MJPG;
		else if (vzprof.camprofile.pixelformat_str == "NV12")
			vzprof.camprofile.format = NV12;
		else
			vzprof.camprofile.format = NONE;

		auto uvcconfig = profile["UVCProfiles"];
		vzprof.uvcprofile.exp_auto = uvcconfig["Exp_Auto"];
		vzprof.uvcprofile.exposure = uvcconfig["Exposure"];
		vzprof.uvcprofile.gain = uvcconfig["Gain"];
		vzprof.uvcprofile.wb_auto = uvcconfig["WB_Auto"];
		vzprof.uvcprofile.temperature = uvcconfig["Temperature"];
		vzprof.uvcprofile.zoom = uvcconfig["Zoom"];
		vzprof.uvcprofile.pan = uvcconfig["Pan"];
		vzprof.uvcprofile.tilt = uvcconfig["Tilt"];
		vzprof.uvcprofile.brightness = uvcconfig["Brightness"];
		vzprof.uvcprofile.contrast = uvcconfig["Contrast"];
		vzprof.uvcprofile.saturation = uvcconfig["Saturation"];
		vzprof.uvcprofile.sharpness = uvcconfig["Sharpness"];
		vzprof.uvcprofile.gamma = uvcconfig["Gamma"];

		auto ispconfig = profile["ISPProfiles"];
		vzprof.ispprofile.ae_exp_mode = ispconfig["AE_EXP_MODE"];
		vzprof.ispprofile.ae_exp_time = ispconfig["AE_EXP_TIME"];
		vzprof.ispprofile.ae_ex_gain = ispconfig["AE_EXP_GAIN"];
		vzprof.ispprofile.awb_mode = ispconfig["AWB_MODE"];
		vzprof.ispprofile.awb_temp = ispconfig["AWB_TEMP"];
		vzprof.ispprofile.zoom_target = ispconfig["ZoomTarget"];
		vzprof.ispprofile.zoom_ctx = ispconfig["Zoom_CTX"];
		vzprof.ispprofile.zoom_cty = ispconfig["Zoom_CTY"];
		vzprof.ispprofile.brightness = ispconfig["Brightness"];
		vzprof.ispprofile.contrast = ispconfig["Contrast"];
		vzprof.ispprofile.saturation = ispconfig["Saturation"];
		vzprof.ispprofile.sharpmess = ispconfig["Sharpness"];
		vzprof.ispprofile.gamma = ispconfig["Gamma"];
		vzprof.ispprofile.denoise = ispconfig["Denoise"];
		vzprof.ispprofile.flipmode = ispconfig["Flip_Mode"];
		vzprof.ispprofile.effectmode = ispconfig["Effect_Mode"];
	}
	catch (std::exception& e)
	{
		vzlog->Error("[%s][%d] LoadProfileSetting Fail!. %s", __FUNCTION__, __LINE__, e.what());
		return -1;
	}

	return 0;
}


int VizionCam::LoadProfileSettingFromPath(const char* profile_path)
{
	std::ifstream file(profile_path);
	json profile = json::parse(file);

	try {
		vzprof.profilename = profile["ProfileName"];
		vzprof.productname = profile["ProductName"];
		vzprof.gendate = profile["GenerateDate"];
		vzprof.serialnumber = profile["SerialNumber"];
		vzprof.controlmode = profile["ControlMode"];

		auto cameraconfig = profile["CameraProfile"];
		vzprof.camprofile.width = cameraconfig["Width"];
		vzprof.camprofile.height = cameraconfig["Height"];
		vzprof.camprofile.framerate = cameraconfig["Framerate"];
		vzprof.camprofile.pixelformat_str = cameraconfig["PixelFormat"];
		if (vzprof.camprofile.pixelformat_str == "UYVY")
			vzprof.camprofile.format = UYVY;
		else if (vzprof.camprofile.pixelformat_str == "YUY2")
			vzprof.camprofile.format = YUY2;
		else if (vzprof.camprofile.pixelformat_str == "MJPG")
			vzprof.camprofile.format = MJPG;
		else if (vzprof.camprofile.pixelformat_str == "NV12")
			vzprof.camprofile.format = NV12;
		else
			vzprof.camprofile.format = NONE;

		auto uvcconfig = profile["UVCProfiles"];
		vzprof.uvcprofile.exp_auto = uvcconfig["Exp_Auto"];
		vzprof.uvcprofile.exposure = uvcconfig["Exposure"];
		vzprof.uvcprofile.gain = uvcconfig["Gain"];
		vzprof.uvcprofile.wb_auto = uvcconfig["WB_Auto"];
		vzprof.uvcprofile.temperature = uvcconfig["Temperature"];
		vzprof.uvcprofile.zoom = uvcconfig["Zoom"];
		vzprof.uvcprofile.pan = uvcconfig["Pan"];
		vzprof.uvcprofile.tilt = uvcconfig["Tilt"];
		vzprof.uvcprofile.brightness = uvcconfig["Brightness"];
		vzprof.uvcprofile.contrast = uvcconfig["Contrast"];
		vzprof.uvcprofile.saturation = uvcconfig["Saturation"];
		vzprof.uvcprofile.sharpness = uvcconfig["Sharpness"];
		vzprof.uvcprofile.gamma = uvcconfig["Gamma"];

		auto ispconfig = profile["ISPProfiles"];
		vzprof.ispprofile.ae_exp_mode = ispconfig["AE_EXP_MODE"];
		vzprof.ispprofile.ae_exp_time = ispconfig["AE_EXP_TIME"];
		vzprof.ispprofile.ae_ex_gain = ispconfig["AE_EXP_GAIN"];
		vzprof.ispprofile.awb_mode = ispconfig["AWB_MODE"];
		vzprof.ispprofile.awb_temp = ispconfig["AWB_TEMP"];
		vzprof.ispprofile.zoom_target = ispconfig["ZoomTarget"];
		vzprof.ispprofile.zoom_ctx = ispconfig["Zoom_CTX"];
		vzprof.ispprofile.zoom_cty = ispconfig["Zoom_CTY"];
		vzprof.ispprofile.brightness = ispconfig["Brightness"];
		vzprof.ispprofile.contrast = ispconfig["Contrast"];
		vzprof.ispprofile.saturation = ispconfig["Saturation"];
		vzprof.ispprofile.sharpmess = ispconfig["Sharpness"];
		vzprof.ispprofile.gamma = ispconfig["Gamma"];
		vzprof.ispprofile.denoise = ispconfig["Denoise"];
		vzprof.ispprofile.flipmode = ispconfig["Flip_Mode"];
		vzprof.ispprofile.effectmode = ispconfig["Effect_Mode"];
	}
	catch (std::exception& e)
	{
		vzlog->Error("[%s][%d] LoadProfileSettingFromPath Fail!. %s", __FUNCTION__, __LINE__, e.what());
		file.close();
		return -1;
	}

	file.close();

	return 0;
}

int VizionCam::SetProfileStreamingConfig(void)
{
	int ret = 0;
	std::vector<VzFormat> vzformatlist;
	if ((ret = GetCaptureFormatList(vzformatlist)) < 0) return ret;

	if (vzformatlist.size() <= 0) return -1;

	// Matching Format
	for (int i = 0; i < vzformatlist.size(); i++)
	{
		if (vzformatlist[i].width == vzprof.camprofile.width &&
			vzformatlist[i].height == vzprof.camprofile.height &&
			vzformatlist[i].framerate == vzprof.camprofile.framerate &&
			vzformatlist[i].format == vzprof.camprofile.format) {
			vzlog->Trace("[%s][%d] Match the Streaming Profile Setting. [%d]W:%d H:%d FPS:%d Format:%s", __FUNCTION__, __LINE__
				, i, vzformatlist[i].width, vzformatlist[i].height, vzformatlist[i].framerate, vzprof.camprofile.pixelformat_str.c_str());
			SetCaptureFormat(vzformatlist[i]);
			return 0;
		}
	}

	vzlog->Error("[%s][%d] SetProfileStreamingConfig Fail! Cannot Match the Streaming Profile Setting. W:%d H:%d FPS:%d Format:%s", __FUNCTION__, __LINE__
		, vzprof.camprofile.width, vzprof.camprofile.height, vzprof.camprofile.framerate, vzprof.camprofile.pixelformat_str.c_str());
	return -1;
}

int VizionCam::SetProfileControlConfig(void)
{
	if (vzprof.controlmode == "ISP")
	{
		SetAutoExposureMode(vzprof.ispprofile.ae_exp_mode ? AE_MODE_STATUS::AUTO_EXP : AE_MODE_STATUS::MANUAL_EXP);
		SetExposureTime(vzprof.ispprofile.ae_exp_time);
		SetExposureGain(vzprof.ispprofile.ae_ex_gain);
		SetBacklightCompensation(vzprof.ispprofile.brightness);

		SetAutoWhiteBalanceMode(vzprof.ispprofile.awb_mode ? AWB_MODE_STATUS::AUTO_WB : AWB_MODE_STATUS::MANUAL_TEMPERATURE_WB);
		SetTemperature(vzprof.ispprofile.awb_temp);

		SetGamma(vzprof.ispprofile.gamma);
		SetSaturation(vzprof.ispprofile.saturation);
		SetContrast(vzprof.ispprofile.contrast); 
		SetSharpening(vzprof.ispprofile.sharpmess);
		SetDenoise(vzprof.ispprofile.denoise);

		SetFlipMode((FLIP_MODE)vzprof.ispprofile.flipmode);
		SetEffectMode((EFFECT_MODE)vzprof.ispprofile.effectmode);

		SetDigitalZoomTarget(vzprof.ispprofile.zoom_target);
		SetDigitalZoom_CT_X(vzprof.ispprofile.zoom_ctx);
		SetDigitalZoom_CT_Y(vzprof.ispprofile.zoom_cty);
	}
	else if(vzprof.controlmode == "UVC")
	{
		int flag = 0;
		SetPropertyValue(CAPTURE_PROPETIES::CAPTURE_BRIGHTNESS, vzprof.uvcprofile.brightness, flag);
		SetPropertyValue(CAPTURE_PROPETIES::CAPTURE_CONTRAST, vzprof.uvcprofile.contrast, flag);
		SetPropertyValue(CAPTURE_PROPETIES::CAPTURE_EXPOSURE, vzprof.uvcprofile.exposure, vzprof.uvcprofile.exp_auto);
		SetPropertyValue(CAPTURE_PROPETIES::CAPTURE_GAIN, vzprof.uvcprofile.gain, flag);
		SetPropertyValue(CAPTURE_PROPETIES::CAPTURE_GAMMA, vzprof.uvcprofile.gamma, flag);
		SetPropertyValue(CAPTURE_PROPETIES::CAPTURE_PAN, vzprof.uvcprofile.pan, flag);
		SetPropertyValue(CAPTURE_PROPETIES::CAPTURE_TILT, vzprof.uvcprofile.tilt, flag);
		SetPropertyValue(CAPTURE_PROPETIES::CAPTURE_WHITEBALANCE, vzprof.uvcprofile.temperature, vzprof.uvcprofile.wb_auto);
		SetPropertyValue(CAPTURE_PROPETIES::CAPTURE_SATURATION, vzprof.uvcprofile.saturation, flag);
		SetPropertyValue(CAPTURE_PROPETIES::CAPTURE_SHARPNESS, vzprof.uvcprofile.sharpness, flag);
		SetPropertyValue(CAPTURE_PROPETIES::CAPTURE_ZOOM, vzprof.uvcprofile.zoom, flag);
	}
	else 
	{
		vzlog->Error("[%s][%d] SetProfileControlConfig Fail! Control Mode Fail. Mode:%s", __FUNCTION__, __LINE__, vzprof.controlmode.c_str());
		return -1;
	}

	return 0;
}

int VizionCam::ClearISPBootdata(void)
{
	uint8_t wt_size = 128;
	uint8_t slvaddr = 0x54;
	uint8_t wtbuf[128];
	uint32_t cur_reg = 0;

	for (int i = 0; i < 128; i++)
		wtbuf[i] = 0xFF;

	//Clear EEPROM (0x54)0x0000-0xFFFF, (0x55)0x0000-0xFFFF
	// Clear Slave ID 0x54
	while (1)
	{
		GenI2CWrite(0x54, 2, (uint16_t)cur_reg, 128, (uint8_t*)wtbuf);
		cur_reg += 128;
		if (cur_reg > 0xFFFF)
			break;
	}
	// Clear Slave ID 0x55
	while (1)
	{
		GenI2CWrite(0x55, 2, (uint16_t)(cur_reg & 0xFFFF), 128, (uint8_t*)wtbuf);
		cur_reg += 128;
		if (cur_reg > 0x1FFFF)
			break;
	}

	return 0;
}

int VizionCam::DownloadBootdata(const char* binfilepath)
{
	std::ifstream input(binfilepath, std::ios::binary);
	// copies all data into buffer
	std::vector<uint8_t> buffer(std::istreambuf_iterator<char>(input), {});
	int count = 0;
	int remain = 0;

	remain = buffer.size() % 16;

#ifdef _DEBUG
	printf("Bootdata  : 00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F\n");
	printf("----------------------------------------------------------\n");
	for (int j = 0; j < buffer.size(); j += 16) {
		count++;
		printf("0x%08x:", j);
		for (int i = 0; i < 16; i++) {
			printf(" %02x", (uint8_t)(buffer.data() + i + j));
		}
		printf("\n");
	}
	if (remain > 0) {
		printf("0x%08x:", count * 16);
		for (int i = 0; i < remain; i++) {
			printf(" %02x", (uint8_t)(buffer.data() + i + count * 16));
		}
		printf("\n");
	}
#endif
	for (int j = 0; j < buffer.size(); j += 128) {
		if (j < 0xFFFF)
			GenI2CWrite(0x54, 2, (uint16_t)j, 128, (uint8_t*)(buffer.data() + count * 128));
		else
			GenI2CWrite(0x55, 2, (uint16_t)(j & 0xFFFF), 128, (uint8_t*)(buffer.data() + count * 128));
		count++;
	}
	if (remain > 0) {
		GenI2CWrite(0x55, 2, (uint16_t)(count * 128 & 0xFFFF), remain, (uint8_t*)(buffer.data() + count * 128));
		printf("\n");
	}

#ifdef _DEBUG
	uint8_t readdata[16];
	printf("Bootdata  : 00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F\n");
	printf("----------------------------------------------------------\n");
	for (int j = 0; j < buffer.size(); j += 16) {
		if (j < 0xFFFF)
			GenI2CRead(0x54, 2, (uint16_t)j, 16, (uint8_t*)readdata);
		else
			GenI2CRead(0x55, 2, (uint16_t)(j & 0xFFFF), 16, (uint8_t*)readdata);
		printf("0x%08x:", j);
		for (int i = 0; i < 16; i++) {
			printf(" %02x", readdata[i]);
		}
		printf("\n");
		count++;
	}
	if (remain > 0) {
		GenI2CRead(0x55, 2, (uint16_t)((count * 16) & 0xFFFF), remain, (uint8_t*)readdata);
		printf("0x%08x:", count * 16);
		for (int i = 0; i < remain; i++) {
			printf(" %02x", readdata[i]);
		}
		printf("\n");
	}
#endif

	return 0;
}

/*
	VizionSDK for Camera Capture Functions
	int Open();   // Open Camera
	int Close();
*/
int VizionCam::Open(int dev_idx)
{
	std::wstring temp;
	if (dev_idx >= devicename_map.size())
		return -1;

	this->dev_idx = dev_idx;

	if(FAILED(InitVideoDevice()))
		return -1;

	dev_name = devicename_map[dev_idx];
	hardware_id = hardwareid_map[dev_idx];

	temp = hardwareid_map[dev_idx].substr(4, 4);
	vendor_id = strtoul(ws2s(temp).c_str(), NULL, 16);
	temp = hardwareid_map[dev_idx].substr(13, 4);
	product_id = strtoul(ws2s(temp).c_str(), NULL, 16);

	
	USBView usbview = USBView(vendor_id, product_id);
	usbview.GetUSBDeviceSpeed(this->speed);

	GetFlipMode(def_flip);

	sample_fail = false;

	return 0;
}

int VizionCam::Close()
{
	if (pCallback != nullptr) { pCallback->Release(); pCallback = nullptr; }
	if (pVideoSource != nullptr) { SafeRelease(&pVideoSource); pVideoSource = nullptr; }
	//if (pVideoConfig != nullptr) { SafeRelease(&pVideoConfig); pVideoConfig = nullptr; }
	if (pVideoReader != nullptr) { SafeRelease(&pVideoReader); pVideoReader = nullptr; }
	//if (ppVideoDevices != nullptr) {
	//	for (int i = 0; i < this->noOfVideoDevices; i++)
	//		SafeRelease(&ppVideoDevices[i]);
	//	CoTaskMemFree(ppVideoDevices);
	//}
	//if (t != nullptr) { delete t; t == nullptr; };

	return 0;
}

std::wstring GetMediaTypeDescriptions(IMFMediaType* pMediaType, char* infos)
{
	HRESULT hr = S_OK;
	GUID MajorType;
	uint32_t cAttrCount;
	LPCWSTR pszGuidStr;
	std::wstring description;
	WCHAR TempBuf[200];

	if (pMediaType == NULL)
	{
		description = L"<NULL>";
		goto done;
	}

	hr = pMediaType->GetMajorType(&MajorType);
	//CHECKHR_GOTO(hr, done);

	//pszGuidStr = STRING_FROM_GUID(MajorType);
	pszGuidStr = GetGUIDNameConst(MajorType);
	if (pszGuidStr != NULL)
	{
		description += pszGuidStr;
		description += L": ";
	}
	else
	{
		description += L"Other: ";
	}

	hr = pMediaType->GetCount(&cAttrCount);
	//CHECKHR_GOTO(hr, done);

	for (uint32_t i = 0; i < cAttrCount; i++)
	{
		GUID guidId;
		MF_ATTRIBUTE_TYPE attrType;

		hr = pMediaType->GetItemByIndex(i, &guidId, NULL);
		//CHECKHR_GOTO(hr, done);

		hr = pMediaType->GetItemType(guidId, &attrType);
		//CHECKHR_GOTO(hr, done);

		//pszGuidStr = STRING_FROM_GUID(guidId);
		pszGuidStr = GetGUIDNameConst(guidId);
		if (pszGuidStr != NULL)
		{
			description += pszGuidStr;
			//printf("GuidStr:%s\n", ws2s(std::wstring(pszGuidStr)).c_str());
		}
		else
		{
			LPOLESTR guidStr = NULL;
			StringFromCLSID(guidId, &guidStr);
			//CHECKHR_GOTO(StringFromCLSID(guidId, &guidStr), done);
			auto wGuidStr = std::wstring(guidStr);
			//description += std::wstring(wGuidStr.begin(), wGuidStr.end()); // GUID's won't have wide chars.
			description += wGuidStr; // GUID's won't have wide chars.

			CoTaskMemFree(guidStr);
		}

		description += L"=";

		switch (attrType)
		{
		case MF_ATTRIBUTE_UINT32:
		{
			uint32_t Val;
			hr = pMediaType->GetUINT32(guidId, &Val);
			//CHECKHR_GOTO(hr, done);

			description += std::to_wstring(Val);
			break;
		}
		case MF_ATTRIBUTE_UINT64:
		{
			UINT64 Val;
			hr = pMediaType->GetUINT64(guidId, &Val);
			//CHECKHR_GOTO(hr, done);

			if (guidId == MF_MT_FRAME_SIZE)
			{
				description += L"W:" + std::to_wstring(HI32(Val)) + L" H: " + std::to_wstring(LO32(Val));
			}
			else if (guidId == MF_MT_FRAME_RATE)
			{
				// Frame rate is numerator/denominator.
				description += std::to_wstring(HI32(Val)) + L"/" + std::to_wstring(LO32(Val));
			}
			else if (guidId == MF_MT_PIXEL_ASPECT_RATIO)
			{
				description += std::to_wstring(HI32(Val)) + L":" + std::to_wstring(LO32(Val));
			}
			else
			{
				//tempStr.Format("%ld", Val);
				description += std::to_wstring(Val);
			}

			//description += tempStr;

			break;
		}
		case MF_ATTRIBUTE_DOUBLE:
		{
			DOUBLE Val;
			hr = pMediaType->GetDouble(guidId, &Val);
			//CHECKHR_GOTO(hr, done);

			//tempStr.Format("%f", Val);
			description += std::to_wstring(Val);
			break;
		}
		case MF_ATTRIBUTE_GUID:
		{
			GUID Val;
			const WCHAR* pValStr;

			hr = pMediaType->GetGUID(guidId, &Val);
			//CHECKHR_GOTO(hr, done);

			//pValStr = STRING_FROM_GUID(Val);
			pValStr = GetGUIDNameConst(Val);
			if (pValStr != NULL)
			{
				description += pValStr;
			}
			else
			{
				LPOLESTR guidStr = NULL;
				StringFromCLSID(Val, &guidStr);
				//CHECKHR_GOTO(StringFromCLSID(Val, &guidStr), done);
				auto wGuidStr = std::wstring(guidStr);
				description += std::wstring(wGuidStr.begin(), wGuidStr.end()); // GUID's won't have wide chars.

				CoTaskMemFree(guidStr);
			}

			break;
		}
		case MF_ATTRIBUTE_STRING:
		{
			hr = pMediaType->GetString(guidId, TempBuf, sizeof(TempBuf) / sizeof(TempBuf[0]), NULL);
			if (hr == HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER))
			{
				description += L"<Too Long>";
				break;
			}
			//CHECKHR_GOTO(hr, done);
			auto wstr = std::wstring(TempBuf);
			description += std::wstring(wstr.begin(), wstr.end()); // It's unlikely the attribute descriptions will contain multi byte chars.

			break;
		}
		case MF_ATTRIBUTE_BLOB:
		{
			description += L"<BLOB>";
			break;
		}
		case MF_ATTRIBUTE_IUNKNOWN:
		{
			description += L"<UNK>";
			break;
		}
		}

		description += L", ";
	}

done:
	sprintf_s(infos, description.size() + sizeof(char), "%s", ws2s(description).c_str());
	return description;
}

VzFormat ParseVzFormatInfo(std::wstring descript)
{
	VzFormat vzformat;
	std::wstring temp_wstr;
	int start_idx = 0;
	int end_idx = 0;
	int data_length = 0;

	start_idx = descript.find(L"MF_MT_FRAME_SIZE");
	end_idx = descript.find(L",", start_idx);
	temp_wstr = descript.substr(start_idx + sizeof("MF_MT_FRAME_SIZE"), end_idx - start_idx - sizeof("MF_MT_FRAME_SIZE"));
	vzlog->Trace("[%s][%d] MF_MT_FRAME_SIZE: %s", __FUNCTION__, __LINE__, ws2s(temp_wstr).c_str());
	std::wstring width_str = temp_wstr.substr(temp_wstr.find(L"W:") + 2, temp_wstr.find(L" H") - temp_wstr.find(L"W") - 2);
	std::wstring height_str = temp_wstr.substr(temp_wstr.find(L"H:") + 2, temp_wstr.find(L"\n") - temp_wstr.find(L"H") - 2);

	start_idx = descript.find(L"MF_MT_FRAME_RATE");
	end_idx = descript.find(L",", start_idx);
	temp_wstr = descript.substr(start_idx + sizeof("MF_MT_FRAME_RATE"), end_idx - start_idx - sizeof("MF_MT_FRAME_RATE"));
	vzlog->Trace("[%s][%d] MF_MT_FRAME_RATE: %s", __FUNCTION__, __LINE__, ws2s(temp_wstr).c_str());
	std::wstring framerate_str1 = temp_wstr.substr(0, temp_wstr.find(L"/"));
	std::wstring framerate_str2 = temp_wstr.substr(temp_wstr.find(L"/") + 1, temp_wstr.find(L"\n") - temp_wstr.find(L"/"));

	start_idx = descript.find(L"MF_MT_SUBTYPE");
	end_idx = descript.find(L",", start_idx);
	std::wstring format_str = descript.substr(start_idx + sizeof("MF_MT_SUBTYPE"), end_idx - start_idx - sizeof("MF_MT_SUBTYPE"));
	vzlog->Trace("[%s][%d] MF_MT_SUBTYPE: %s", __FUNCTION__, __LINE__, ws2s(temp_wstr).c_str());

	vzformat.width = std::stoi(width_str);
	vzformat.height = std::stoi(height_str);
	vzformat.framerate = std::stoi(framerate_str1) / std::stoi(framerate_str2);

	if (format_str == L"MFVideoFormat_UYVY")
		vzformat.format = UYVY;
	else if (format_str == L"MFVideoFormat_YUY2")
		vzformat.format = YUY2;
	else if (format_str == L"MFVideoFormat_NV12")
		vzformat.format = NV12;
	else if (format_str == L"MFVideoFormat_MJPG")
		vzformat.format = MJPG;
	else
		vzformat.format = NONE; 

	return vzformat;
}

HRESULT EnumerateTypesForStream(IMFSourceReader* pReader, DWORD dwStreamIndex, char* infos)
{
	HRESULT hr = S_OK;
	DWORD dwMediaTypeIndex = 0;
	std::wstring descript;
	VzFormat vzformat;

	while (SUCCEEDED(hr))
	{
		IMFMediaType* pType = NULL;
		hr = pReader->GetNativeMediaType(dwStreamIndex, dwMediaTypeIndex, &pType);
		if (hr == MF_E_NO_MORE_TYPES)
		{
			hr = S_OK;
			break;
		}
		else if (SUCCEEDED(hr))
		{
			// Examine the media type. (Not shown.)
			descript = GetMediaTypeDescriptions(pType, infos);
			vzformat = ParseVzFormatInfo(descript);
			std::cout << ws2s(descript) << std::endl << std::endl;

			pType->Release();
		}
		++dwMediaTypeIndex;
	}
	return hr;
}

//HRESULT EnumerateMediaTypes(IMFSourceReader* pReader, char* infos)
//{
//	HRESULT hr = S_OK;
//	DWORD dwStreamIndex = 0;
//
//	while (SUCCEEDED(hr))
//	{
//		printf("EnumerateTypesForStream: dwStreamIndex=%d\n", dwStreamIndex);
//		hr = EnumerateTypesForStream(pReader, dwStreamIndex, infos);
//		if (hr == MF_E_INVALIDSTREAMNUMBER)
//		{
//			hr = S_OK;
//			break;
//		}
//		++dwStreamIndex;
//	}
//
//	vzlog->Debug("[%s][%d] dwStreamIndex=%d", __FUNCTION__, __LINE__, dwStreamIndex);
//
//	return hr;
//}

std::wstring VizionCam::GetMediaTypeDescriptions(IMFMediaType* pMediaType)
{
	HRESULT hr = S_OK;
	GUID MajorType;
	uint32_t cAttrCount;
	LPCWSTR pszGuidStr;
	std::wstring description;
	WCHAR TempBuf[200];

	if (pMediaType == NULL)
	{
		description = L"<NULL>";
		goto done;
	}

	hr = pMediaType->GetMajorType(&MajorType);
	//CHECKHR_GOTO(hr, done);

	//pszGuidStr = STRING_FROM_GUID(MajorType);
	pszGuidStr = GetGUIDNameConst(MajorType);
	if (pszGuidStr != NULL)
	{
		description += pszGuidStr;
		description += L": ";
	}
	else
	{
		description += L"Other: ";
	}

	hr = pMediaType->GetCount(&cAttrCount);
	//CHECKHR_GOTO(hr, done);

	for (uint32_t i = 0; i < cAttrCount; i++)
	{
		GUID guidId;
		MF_ATTRIBUTE_TYPE attrType;

		hr = pMediaType->GetItemByIndex(i, &guidId, NULL);
		//CHECKHR_GOTO(hr, done);

		hr = pMediaType->GetItemType(guidId, &attrType);
		//CHECKHR_GOTO(hr, done);

		//pszGuidStr = STRING_FROM_GUID(guidId);
		pszGuidStr = GetGUIDNameConst(guidId);
		if (pszGuidStr != NULL)
		{
			description += pszGuidStr;
			//printf("GuidStr:%s\n", ws2s(std::wstring(pszGuidStr)).c_str());
		}
		else
		{
			LPOLESTR guidStr = NULL;
			StringFromCLSID(guidId, &guidStr);
			//CHECKHR_GOTO(StringFromCLSID(guidId, &guidStr), done);
			auto wGuidStr = std::wstring(guidStr);
			//description += std::wstring(wGuidStr.begin(), wGuidStr.end()); // GUID's won't have wide chars.
			description += wGuidStr; // GUID's won't have wide chars.

			CoTaskMemFree(guidStr);
		}

		description += L"=";

		switch (attrType)
		{
		case MF_ATTRIBUTE_UINT32:
		{
			uint32_t Val;
			hr = pMediaType->GetUINT32(guidId, &Val);
			//CHECKHR_GOTO(hr, done);

			description += std::to_wstring(Val);
			break;
		}
		case MF_ATTRIBUTE_UINT64:
		{
			UINT64 Val;
			hr = pMediaType->GetUINT64(guidId, &Val);
			//CHECKHR_GOTO(hr, done);

			if (guidId == MF_MT_FRAME_SIZE)
			{
				description += L"W:" + std::to_wstring(HI32(Val)) + L" H: " + std::to_wstring(LO32(Val));
			}
			else if (guidId == MF_MT_FRAME_RATE)
			{
				// Frame rate is numerator/denominator.
				description += std::to_wstring(HI32(Val)) + L"/" + std::to_wstring(LO32(Val));
			}
			else if (guidId == MF_MT_PIXEL_ASPECT_RATIO)
			{
				description += std::to_wstring(HI32(Val)) + L":" + std::to_wstring(LO32(Val));
			}
			else
			{
				//tempStr.Format("%ld", Val);
				description += std::to_wstring(Val);
			}

			//description += tempStr;

			break;
		}
		case MF_ATTRIBUTE_DOUBLE:
		{
			DOUBLE Val;
			hr = pMediaType->GetDouble(guidId, &Val);
			//CHECKHR_GOTO(hr, done);

			//tempStr.Format("%f", Val);
			description += std::to_wstring(Val);
			break;
		}
		case MF_ATTRIBUTE_GUID:
		{
			GUID Val;
			const WCHAR* pValStr;

			hr = pMediaType->GetGUID(guidId, &Val);
			//CHECKHR_GOTO(hr, done);

			//pValStr = STRING_FROM_GUID(Val);
			pValStr = GetGUIDNameConst(Val);
			if (pValStr != NULL)
			{
				description += pValStr;
			}
			else
			{
				LPOLESTR guidStr = NULL;
				StringFromCLSID(Val, &guidStr);
				//CHECKHR_GOTO(StringFromCLSID(Val, &guidStr), done);
				auto wGuidStr = std::wstring(guidStr);
				description += std::wstring(wGuidStr.begin(), wGuidStr.end()); // GUID's won't have wide chars.

				CoTaskMemFree(guidStr);
			}

			break;
		}
		case MF_ATTRIBUTE_STRING:
		{
			hr = pMediaType->GetString(guidId, TempBuf, sizeof(TempBuf) / sizeof(TempBuf[0]), NULL);
			if (hr == HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER))
			{
				description += L"<Too Long>";
				break;
			}
			//CHECKHR_GOTO(hr, done);
			auto wstr = std::wstring(TempBuf);
			description += std::wstring(wstr.begin(), wstr.end()); // It's unlikely the attribute descriptions will contain multi byte chars.

			break;
		}
		case MF_ATTRIBUTE_BLOB:
		{
			description += L"<BLOB>";
			break;
		}
		case MF_ATTRIBUTE_IUNKNOWN:
		{
			description += L"<UNK>";
			break;
		}
		}

		description += L", ";
	}

done:
	//sprintf_s(infos, 65535, "%s", ws2s(description).c_str());
	return description;
}


HRESULT VizionCam::EnumerateTypesForStream(DWORD dwStreamIndex, std::vector<VzFormat>& capformats)
{
	HRESULT hr = S_OK;
	DWORD dwMediaTypeIndex = 0;
	std::wstring descript;
	VzFormat vzformat;

	capformats.clear();

	while (SUCCEEDED(hr))
	{
		IMFMediaType* pType = NULL;
		hr = this->pVideoReader->GetNativeMediaType(dwStreamIndex, dwMediaTypeIndex, &pType);
		if (hr == MF_E_NO_MORE_TYPES)
		{
			hr = S_OK;
			break;
		}
		else if (SUCCEEDED(hr))
		{
			// Examine the media type. (Not shown.)
			descript = GetMediaTypeDescriptions(pType);
			vzformat = ParseVzFormatInfo(descript);
			vzformat.mediatype_idx = (uint8_t)dwMediaTypeIndex;
			if(vzformat.format != NV12)
				capformats.push_back(vzformat);
			vzlog->Trace("[%s][%d] Descrpition: %s", __FUNCTION__, __LINE__, ws2s(descript).c_str());

			pType->Release();
		}
		++dwMediaTypeIndex;
	}
	return hr;
}


//int VizionCam::GetCameraDescription(char* infos)
//{
//	return EnumerateMediaTypes(this->pVideoReader, infos);
//}

// Helper function to set the frame size on a video media type.
inline HRESULT SetFrameSize(IMFMediaType* pType, uint32_t width, uint32_t height)
{
	return MFSetAttributeSize(pType, MF_MT_FRAME_SIZE, width, height);
}

// Helper function to get the frame size from a video media type.
inline HRESULT GetFrameSize(IMFMediaType* pType, uint32_t* pWidth, uint32_t* pHeight)
{
	return MFGetAttributeSize(pType, MF_MT_FRAME_SIZE, pWidth, pHeight);
}

inline HRESULT SetFrameRate(IMFMediaType* pType, uint32_t numerator, uint32_t denominator)
{
	return MFSetAttributeRatio(pType, MF_MT_FRAME_RATE, numerator, denominator);
}

inline HRESULT GetFrameRate(IMFMediaType* pType, uint32_t* pNumerator, uint32_t* pDenominator)
{
	return MFGetAttributeRatio(pType, MF_MT_FRAME_RATE, pNumerator, pDenominator);
}

HRESULT VizionCam::ConfigureDecoder(IMFSourceReader* pReader, DWORD dwStreamIndex)
{
	IMFMediaType* pNativeType = NULL;
	IMFMediaType* pType = NULL;
	uint32_t width = this->capformat.width;
	uint32_t height = this->capformat.height;
	uint32_t pNumerator = 0;
	uint32_t pDenominator = 0;
	int ex_width = width;
	int ex_height = height;

	// Find the native format of the stream.
	vzlog->Trace("[%s][%d] GetNativeMediaType MediaType: %d", __FUNCTION__, __LINE__, this->capformat.mediatype_idx);

	HRESULT hr = pReader->GetNativeMediaType(dwStreamIndex, this->capformat.mediatype_idx, &pNativeType);
	if (FAILED(hr))
	{
		return hr;
	}

	GUID majorType, subtype;

	// Find the major type.
	hr = pNativeType->GetGUID(MF_MT_MAJOR_TYPE, &majorType);
	if (FAILED(hr))
	{
		goto done;
	}

	// Define the output type.
	hr = MFCreateMediaType(&pType);
	if (FAILED(hr))
	{
		goto done;
	}
	
	hr = pType->SetGUID(MF_MT_MAJOR_TYPE, majorType);
	if (FAILED(hr))
	{
		goto done;
	}

	// Select a subtype.
	hr = pNativeType->GetGUID(MF_MT_SUBTYPE, &subtype);
	if (FAILED(hr))
	{
		goto done;
	}

	if (majorType == MFMediaType_Video)
	{
		switch (this->capformat.format) {
		case VZ_IMAGE_FORMAT::UYVY:
			subtype = MFVideoFormat_UYVY;
			break;
		case VZ_IMAGE_FORMAT::YUY2:
			subtype = MFVideoFormat_YUY2;
			break;
		case VZ_IMAGE_FORMAT::NV12:
			subtype = MFVideoFormat_NV12;
			break;
		case VZ_IMAGE_FORMAT::MJPG:
			subtype = MFVideoFormat_MJPG;
			break;
		default:
			subtype = MFVideoFormat_UYVY;
		}

	}
	else if (majorType == MFMediaType_Audio)
	{
		subtype = MFAudioFormat_PCM;
	}
	else
	{
		// Unrecognized type. Skip.
		goto done;
	}
	hr = pType->SetGUID(MF_MT_SUBTYPE, subtype);
	if (FAILED(hr))
	{
		goto done;
	}
	hr = SetFrameSize(pType, width, height);
	if (FAILED(hr))
	{
		goto done;
	}
	
	hr = GetFrameSize(pNativeType, &width, &height);
	if (FAILED(hr))
	{
		goto done;
	}
	if (ex_width != width || ex_height != height)
	{
		vzlog->Error("[%s][%d] SetFrameSize Fail!", __FUNCTION__, __LINE__);
		hr = -S_FALSE;
		goto done;
	}

	hr = GetFrameRate(pNativeType, &pNumerator, &pDenominator);
	if (FAILED(hr))
	{
		goto done;
	}
	hr = SetFrameRate(pType, pNumerator, pDenominator);
	if (FAILED(hr))
	{
		goto done;
	}

	// Set the uncompressed format.
	hr = pReader->SetCurrentMediaType(dwStreamIndex, NULL, pType);
	if (FAILED(hr))
	{
		goto done;
	}

done:
	SafeRelease(&pNativeType);
	SafeRelease(&pType);
	return hr;
}

//std::mutex g_mutex;

int VizionCam::ProcessSamples(IMFSourceReader* pVideoReader, uint8_t* raw_data, uint16_t timeout = 2500)
{
	HRESULT hr = S_OK;
	IMFSample* pSample = NULL;
	size_t  cSamples = 0;

	DWORD streamIndex, flags;
	LONGLONG llTimeStamp;

#ifndef SYNC_MODE
	this->pCallback->SetImageInfo(this->capformat);
	if (!sample_fail) {
		hr = pVideoReader->ReadSample(MF_SOURCE_READER_ANY_STREAM,
			0, NULL, NULL, NULL, NULL);
	}
	
	if (FAILED(hr))
	{
		std::cout << "ReadSample Fail!" << std::endl;
		vzlog->Error("[%s][%d] ReadSample Fail!", __FUNCTION__, __LINE__);
		return hr;
	}

	try {
		BOOL bEOS;

		hr = pCallback->Wait(timeout, &bEOS);
		if (FAILED(hr) || bEOS)
		{
			vzlog->Trace("[%s][%d] Wait Sample Image Timeout %d ms", __FUNCTION__, __LINE__, timeout);
			sample_fail = true;
			return pCallback->GetResultStatus();
		}
		sample_fail = false;

		if (rawdata != nullptr) {
			rawdata->release();
			delete rawdata;
			rawdata = nullptr;
		}

		hr = pCallback->GetImageSample(raw_data, raw_size);
		if (hr < 0)
			return hr;
#else
	hr = pVideoReader->ReadSample(
		MF_SOURCE_READER_ANY_STREAM,    // Stream index.
		0,                              // Flags.
		&streamIndex,                   // Receives the actual stream index. 
		&flags,                         // Receives status flags.
		&llTimeStamp,                   // Receives the time stamp.
		&pSample                        // Receives the sample or NULL.
	);

	if (FAILED(hr))
	{
		std::cout << "ReadSample Fail!" << std::endl;
		return -1;
	}

	//wprintf(L"Stream %d (%I64d)\n", streamIndex, llTimeStamp);
	if (flags & MF_SOURCE_READERF_ENDOFSTREAM)
	{
		wprintf(L"\tEnd of stream\n");
		//quit = true;
	}
	if (flags & MF_SOURCE_READERF_NEWSTREAM)
	{
		wprintf(L"\tNew stream\n");
	}
	if (flags & MF_SOURCE_READERF_NATIVEMEDIATYPECHANGED)
	{
		wprintf(L"\tNative type changed\n");
	}
	if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED)
	{
		wprintf(L"\tCurrent type changed\n");
	}
	if (flags & MF_SOURCE_READERF_STREAMTICK)
	{
		wprintf(L"\tStream tick\n");
	}

	if (flags & MF_SOURCE_READERF_NATIVEMEDIATYPECHANGED)
	{
		// The format changed. Reconfigure the decoder.
		hr = ConfigureDecoder(pVideoReader, streamIndex);
		if (FAILED(hr))
		{
			//std::cout << "ConfigureDecoder Fail! streamIndex: " << streamIndex << std::endl;
			return -1;
		}
	}

	try {
		if (pSample)
		{
			++cSamples;
			// Get Sample Buffer
			IMFMediaBuffer* buffer;
			uint16_t width = this->capformat.width;
			uint16_t height = this->capformat.height;
			UINT32 stride;
			std::unique_ptr<BYTE> data = std::make_unique<BYTE>(width * height * 2);
			DWORD size, curr_size;
			hr = pSample->ConvertToContiguousBuffer(&buffer);
			BYTE* data_ptr = data.get();

			buffer->GetMaxLength(&curr_size);
			stride = width * 2;
			size = stride * height;
			if (size != curr_size) {
				//std::cout << "Buffer size is not correct! Buffer Size: " << curr_size << " Expected Size: " << size << std::endl;
				size = curr_size;
				stride = curr_size / height;
				//goto done;
			}
			hr = buffer->Lock(&data_ptr, NULL, &size);
			if (hr != S_OK)
				std::cout << "Buffer Lock... FAIL!" << std::endl;
			raw_size = size;

			if (raw_data != nullptr && data_ptr != nullptr)
				memcpy_s(raw_data, size, data_ptr, size);

			switch (this->capformat.format)
			{
			case VZ_IMAGE_FORMAT::UYVY:
			case VZ_IMAGE_FORMAT::YUY2:
				rawdata = new cv::Mat(height, width, CV_8UC2, raw_data);
				break;
			case VZ_IMAGE_FORMAT::NV12:
				rawdata = new cv::Mat(height * 3 / 2, width, CV_8UC1, raw_data);
				break;
			default:
				rawdata = new cv::Mat(1, size, CV_8UC1, raw_data);
			}

		done:
			buffer->Unlock();
			buffer->Release();
			data.release();
			SafeRelease(&pSample);
		}
		else
			return -1;
#endif
	}
	catch (std::runtime_error e) {
		std::cout << "pSample Get Buffer... FAIL\n" << e.what() << std::endl;
		vzlog->Error("[%s][%d] Get Buffer... FAIL.\n%s", __FUNCTION__, __LINE__, e.what());
	}

	if (FAILED(hr))
	{
		vzlog->Error("[%s][%d] ProcessSamples FAILED, hr = 0x%x", __FUNCTION__, __LINE__, hr);
		return hr;
	}
	
#ifdef SYNC_MODE
	SafeRelease(&pSample);
#endif
	return hr;
}

int GetCurrentThreadCount()
{
	// first determine the id of the current process
	DWORD const  id = GetCurrentProcessId();

	// then get a process list snapshot.
	HANDLE const  snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPALL, 0);

	// initialize the process entry structure.
	PROCESSENTRY32 entry = { 0 };
	entry.dwSize = sizeof(entry);

	// get the current process info.
	BOOL  ret = true;
	ret = Process32First(snapshot, &entry);
	while (ret && entry.th32ProcessID != id) {
		ret = Process32Next(snapshot, &entry);
	}
	CloseHandle(snapshot);
	return ret
		? entry.cntThreads
		: -1;
}

int VizionCam::GetRawImageCapture(uint8_t* raw_data, int* size = nullptr, uint16_t timeout = 2500)
{
	int ret = 0;

	if ((ret = ProcessSamples(this->pVideoReader, raw_data, timeout)) != 0)
		return ret;

	vzlog->Trace("[%s][%d] Raw data size: %d", __FUNCTION__, __LINE__, raw_size);
	if (size != nullptr)
		*size = raw_size;

	return 0;
}

int VizionCam::GetCaptureFormatList(std::vector<VzFormat>& capformats)
{
	// Enumerate index 0
	try {
		EnumerateTypesForStream(0, capformats);
	}
	catch (...)
	{
		return -1;
	}
	return 0;
}

int VizionCam::SetCaptureFormat(VzFormat capformat)
{
	//Set to MediaType
	std::string format_str;
	this->capformat = capformat;

	switch (capformat.format)
	{
	case VZ_IMAGE_FORMAT::UYVY:
		format_str = "UYVY";
		break;
	case VZ_IMAGE_FORMAT::YUY2:
		format_str = "YUY2";
		break;
	case VZ_IMAGE_FORMAT::NV12:
		format_str = "NV12";
		break;
	case VZ_IMAGE_FORMAT::MJPG:
		format_str = "MJPG";
		break;
	default:
		format_str = "NONE";
		return -1;
	}

	vzlog->Debug("[%s][%d] ConfigureDecoder: format: %s", __FUNCTION__, __LINE__, format_str.c_str());
	if(ConfigureDecoder(this->pVideoReader, 0) != S_OK)
		return -1;

	return 0;
}

int VizionCam::GetImageCapture(uint8_t* img_data, uint16_t timeout = 2500)
{
	int ret = 0;
	int size = 0;
	int width = capformat.width;
	int height = capformat.height;
	
	if (img_data == nullptr) {
		vzlog->Error("[%s][%d] img_data is null pointer.", __FUNCTION__, __LINE__);
		return -1;
	}

	if ((ret = GetRawImageCapture(img_data, &size, timeout)) != 0) {
		vzlog->Error("[%s][%d] GetRawImageCapture Fail! Timeout = %d ms", __FUNCTION__, __LINE__, timeout);
		return ret;
	}

	switch (this->capformat.format)
	{
	case VZ_IMAGE_FORMAT::UYVY:
	case VZ_IMAGE_FORMAT::YUY2:
		rawdata = new cv::Mat(height, width, CV_8UC2, img_data);
		break;
	case VZ_IMAGE_FORMAT::NV12:
		rawdata = new cv::Mat(height * 3 / 2, width, CV_8UC1, img_data);
		break;
	default:
		rawdata = new cv::Mat(1, size, CV_8UC1, img_data);
	}

	if (rawdata == NULL) {
		vzlog->Error("[%s][%d] rawdata is NULL.", __FUNCTION__, __LINE__);
		return -1;
	}
	if (rawdata->empty()) {
		vzlog->Error("[%s][%d] rawdata is empty.", __FUNCTION__, __LINE__);
		return -1;
	}
	
	if(this->capformat.format == VZ_IMAGE_FORMAT::UYVY)
		cv::cvtColor(*rawdata, img, cv::COLOR_YUV2BGR_UYVY);
	else if (this->capformat.format == VZ_IMAGE_FORMAT::YUY2)
		cv::cvtColor(*rawdata, img, cv::COLOR_YUV2BGR_YUY2);
	else if (this->capformat.format == VZ_IMAGE_FORMAT::NV12)
		cv::cvtColor(*rawdata, img, cv::COLOR_YUV2BGR_NV12);
	else if (this->capformat.format == VZ_IMAGE_FORMAT::MJPG)
		 cv::imdecode(*rawdata, cv::IMREAD_COLOR, &img);
	
	if (img.empty()) {
		vzlog->Error("[%s][%d] img is empty.", __FUNCTION__, __LINE__);
		return -1;
	}

	memcpy_s(img_data, img.size().width * img.size().height * img.channels(),
		img.data, img.size().width * img.size().height * img.channels());
	
	return 0;
}

/*
	VizionCam Class Control API
*/

int VcOpen(VizionCam* vizion_cam, int dev_idx)
{
	return vizion_cam->Open(dev_idx);
}

int VcClose(VizionCam* vizion_cam)
{
	return vizion_cam->Close();
}

int VcGetImageCapture(VizionCam* vizion_cam, uint8_t* img_data, uint16_t timeout = 2500)
{
	return vizion_cam->GetImageCapture(img_data, timeout);
}

int VcGetRawImageCapture(VizionCam* vizion_cam, uint8_t* raw_data, int* data_size = nullptr, uint16_t timeout = 2500)
{
	return vizion_cam->GetRawImageCapture(raw_data, data_size, timeout);
}

//int VcGetCameraDescription(VizionCam* vizion_cam, char* infos)
//{
//	return vizion_cam->GetCameraDescription(infos);
//}

int VcGetVideoDeviceList(VizionCam* vizion_cam, std::vector<std::wstring>& devname_list)
{
	return vizion_cam->GetVideoDeviceList(devname_list);
}

int VcSetCaptureFormat(VizionCam* vizion_cam, VzFormat capformat)
{
	return vizion_cam->SetCaptureFormat(capformat);
}

int VcGetCaptureFormatList(VizionCam* vizion_cam, std::vector<VzFormat>& capformats)
{
	return vizion_cam->GetCaptureFormatList(capformats);
}

int VcClearISPBootdata(VizionCam* vizion_cam)
{
	return vizion_cam->ClearISPBootdata();
}

int VcDownloadBootdata(VizionCam* vizion_cam, const char* binfilepath)
{
	return vizion_cam->DownloadBootdata(binfilepath);
}

int VcGetBootdataHeader(VizionCam* vizion_cam, VzHeader* header)
{
	return vizion_cam->GetBootdataHeader(header);
}

VIZION_API int VcGetBootdataHeaderV3(VizionCam* vizion_cam, VzHeaderV3* header)
{
	return vizion_cam->GetBootdataHeaderV3(header);
}

VIZION_API int VcCheckHeaderVer(VizionCam* vizion_cam)
{
	return vizion_cam->CheckHeaderVer();
}

int VcGetVizionCamDeviceName(VizionCam* vizion_cam, wchar_t* devname)
{
	return vizion_cam->GetVizionCamDeviceName(devname);
}

int VcGetDeviceHardwareID(VizionCam* vizion_cam, wchar_t* hardware_id)
{
	return vizion_cam->GetDeviceHardwareID(hardware_id);
}

int VcGetUSBFirmwareVersion(VizionCam* vizion_cam, char* fw_ver)
{
	return vizion_cam->GetUSBFirmwareVersion(fw_ver);
}

int VcGetAutoExposureMode(VizionCam* vizion_cam, AE_MODE_STATUS& ae_mode)
{
	return vizion_cam->GetAutoExposureMode(ae_mode);
}

int VcSetAutoExposureMode(VizionCam* vizion_cam, AE_MODE_STATUS ae_mode)
{
	return vizion_cam->SetAutoExposureMode(ae_mode);
}

int VcGetExposureTime(VizionCam* vizion_cam, uint32_t& exptime)
{
	return vizion_cam->GetExposureTime(exptime);
}
int VcSetExposureTime(VizionCam* vizion_cam, uint32_t exptime)
{
	return vizion_cam->SetExposureTime(exptime);
}

int VcGetMaxFPS(VizionCam* vizion_cam, uint8_t& max_fps)
{
	return vizion_cam->GetMaxFPS(max_fps);
}

int VcSetMaxFPS(VizionCam* vizion_cam, uint8_t max_fps)
{
	return vizion_cam->SetMaxFPS(max_fps);
}

int VcGetExposureGain(VizionCam* vizion_cam, uint8_t& expgain)
{
	return vizion_cam->GetExposureGain(expgain);
}

int VcSetExposureGain(VizionCam* vizion_cam, uint8_t expgain)
{
	return vizion_cam->SetExposureGain(expgain);
}

int VcGetBacklightCompensation(VizionCam* vizion_cam, double& ae_comp)
{
	return vizion_cam->GetBacklightCompensation(ae_comp);
}

int VcSetBacklightCompensation(VizionCam* vizion_cam, double ae_comp)
{
	return vizion_cam->SetBacklightCompensation(ae_comp);
}

int VcGetAutoWhiteBalanceMode(VizionCam* vizion_cam, AWB_MODE_STATUS& awb_mode)
{
	return vizion_cam->GetAutoWhiteBalanceMode(awb_mode);
}

int VcSetAutoWhiteBalanceMode(VizionCam* vizion_cam, AWB_MODE_STATUS awb_mode)
{
	return vizion_cam->SetAutoWhiteBalanceMode(awb_mode);
}

int VcGetTemperature(VizionCam* vizion_cam, uint16_t& temp)
{
	return vizion_cam->GetTemperature(temp);
}

int VcSetTemperature(VizionCam* vizion_cam, uint16_t temp)
{
	return vizion_cam->SetTemperature(temp);
}

int VcGetGamma(VizionCam* vizion_cam, double& gamma)
{
	return vizion_cam->GetGamma(gamma);
}

int VcSetGamma(VizionCam* vizion_cam, double gamma)
{
	return vizion_cam->SetGamma(gamma);
}

int VcGetSaturation(VizionCam* vizion_cam, double& saturation)
{
	return vizion_cam->GetSaturation(saturation);
}

int VcSetSaturation(VizionCam* vizion_cam, double saturation)
{
	return vizion_cam->SetSaturation(saturation);
}

int VcGetContrast(VizionCam* vizion_cam, double& contrast)
{
	return vizion_cam->GetContrast(contrast);
}

int VcSetContrast(VizionCam* vizion_cam, double contrast)
{
	return vizion_cam->SetContrast(contrast);
}

int VcGetSharpening(VizionCam* vizion_cam, double& sharpness)
{
	return vizion_cam->GetSharpening(sharpness);
}

int VcSetSharpening(VizionCam* vizion_cam, double sharpness)
{
	return vizion_cam->SetSharpening(sharpness);
}

int VcGetDenoise(VizionCam* vizion_cam, double& denoise)
{
	return vizion_cam->GetDenoise(denoise);
}

int VcSetDenoise(VizionCam* vizion_cam, double denoise)
{
	return vizion_cam->SetDenoise(denoise);
}

int VcGetDigitalZoomType(VizionCam* vizion_cam, DZ_MODE_STATUS& zoom_type)
{
	return vizion_cam->GetDigitalZoomType(zoom_type);
}

int VcSetDigitalZoomType(VizionCam* vizion_cam, DZ_MODE_STATUS zoom_type)
{
	return vizion_cam->SetDigitalZoomType(zoom_type);
}

int VcGetDigitalZoomTarget(VizionCam* vizion_cam, double& times)
{
	return vizion_cam->GetDigitalZoomTarget(times);
}

int VcSetDigitalZoomTarget(VizionCam* vizion_cam, double times)
{
	return vizion_cam->SetDigitalZoomTarget(times);
}

int VcGetDigitalZoom_CT_X(VizionCam* vizion_cam, double& ct_x)
{
	return vizion_cam->GetDigitalZoom_CT_X(ct_x);
}

int VcSetDigitalZoom_CT_X(VizionCam* vizion_cam, double ct_x)
{
	return vizion_cam->SetDigitalZoom_CT_X(ct_x);
}

int VcGetDigitalZoom_CT_Y(VizionCam* vizion_cam, double& ct_y)
{
	return vizion_cam->GetDigitalZoom_CT_Y(ct_y);
}

int VcSetDigitalZoom_CT_Y(VizionCam* vizion_cam, double ct_y)
{
	return vizion_cam->SetDigitalZoom_CT_Y(ct_y);
}

int VcGetFlipMode(VizionCam* vizion_cam, FLIP_MODE& flip)
{
	return vizion_cam->GetFlipMode(flip);
}

int VcSetFlipMode(VizionCam* vizion_cam, FLIP_MODE flip)
{
	return vizion_cam->SetFlipMode(flip);
}

int VcGetEffectMode(VizionCam* vizion_cam, EFFECT_MODE& effect)
{
	return vizion_cam->GetEffectMode(effect);
}
int VcSetEffectMode(VizionCam* vizion_cam, EFFECT_MODE effect)
{
	return vizion_cam->SetEffectMode(effect);
}

int VcRecoverDefaultSetting(VizionCam* vizion_cam)
{
	return vizion_cam->RecoverDefaultSetting();
}

int VcLoadProfileSetting(VizionCam* vizion_cam, const char* profile_str)
{
	return vizion_cam->LoadProfileSetting(profile_str);
}

int VcLoadProfileSettingFromPath(VizionCam* vizion_cam, const char* profile_path)
{
	return vizion_cam->LoadProfileSettingFromPath(profile_path);
}

int VcSetProfileStreamingConfig(VizionCam* vizion_cam)
{
	return vizion_cam->SetProfileStreamingConfig();
}

int VcSetProfileControlConfig(VizionCam* vizion_cam)
{
	return vizion_cam->SetProfileControlConfig();
}

int VcUVC_AP1302_I2C_Read(VizionCam* vizion_cam, uint16_t addrReg, uint16_t& data)
{
	return vizion_cam->UVC_AP1302_I2C_Read(addrReg, data);
}

int VcUVC_AP1302_I2C_Write(VizionCam* vizion_cam, uint16_t addrReg, uint16_t data)
{
	return vizion_cam->UVC_AP1302_I2C_Write(addrReg, data);
}

int VcSetPropertyValue(VizionCam* vizion_cam, CAPTURE_PROPETIES prop_id, long value, int flag)
{
	return vizion_cam->SetPropertyValue(prop_id, value, flag);
}

int VcGetPropertyValue(VizionCam* vizion_cam, CAPTURE_PROPETIES prop_id, long& value, int& flag)
{
	return vizion_cam->GetPropertyValue(prop_id, value, flag);
}

int VcGetPropertyRange(VizionCam* vizion_cam, CAPTURE_PROPETIES prop_id, long& min, long& max, long& step, long& def, long& caps)
{
	return vizion_cam->GetPropertyRange(prop_id, min, max, step, def, caps);
}

void VcSetLogLevel(VzLogLevel level)
{
	vzlog->SetLogLevel(level);
}

int VcGotoProgramMode(VizionCam* vizion_cam)
{
	return vizion_cam->GotoProgramMode();
}

void VcGetDeviceSpeed(VizionCam* vizion_cam, USB_DEVICE_SPEED& usb_speed)
{
	vizion_cam->GetDeviceSpeed(usb_speed);
}

int VcGetUniqueSensorID(VizionCam* vizion_cam, char* sensor_id)
{
	return vizion_cam->GetUniqueSensorID(sensor_id);
}
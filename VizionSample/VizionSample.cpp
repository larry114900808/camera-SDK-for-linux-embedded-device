#include <iostream>
#include <vector>
#include <string>
#include <windows.h>
#include <ks.h>
#include <strmif.h>
#include <thread>
#include <mutex>
#include <io.h> 
#include <fcntl.h> 

#include <opencv2/highgui.hpp>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/world.hpp>

using namespace cv;

enum class AE_MODE_STATUS
{
	MANUAL_EXP = 0,
	AUTO_EXP = 0xC,
};

enum class AWB_MODE_STATUS
{
	MANUAL_TEMPERATURE_WB = 0x7,
	AUTO_WB = 0xF,
};

enum class DZ_MODE_STATUS
{
	DZ_IMMEDIATE = 0x8000,
	DZ_SLOW = 0x0040,
	DZ_FAST = 0x0200,
};

enum VZ_IMAGE_FORMAT
{
	NONE = 0,
	YUY2,
	UYVY,
};

enum CAPTURE_PROPETIES
{
	CAPTURE_BRIGHTNESS,
	CAPTURE_CONTRAST,
	CAPTURE_HUE,
	CAPTURE_SATURATION,
	CAPTURE_SHARPNESS,
	CAPTURE_GAMMA,
	CAPTURE_COLORENABLE,
	CAPTURE_WHITEBALANCE,
	CAPTURE_BACKLIGHTCOMPENSATION,
	CAPTURE_GAIN,
	CAPTURE_PAN,
	CAPTURE_TILT,
	CAPTURE_ROLL,
	CAPTURE_ZOOM,
	CAPTURE_EXPOSURE,
	CAPTURE_IRIS,
	CAPTURE_FOCUS,
	CAPTURE_PROP_MAX
};

struct VzFormat {
	uint8_t mediatype_idx;
	uint16_t width;
	uint16_t height;
	uint16_t framerate;
	VZ_IMAGE_FORMAT format;
};

#pragma pack(push, 1)
struct VzHeader {
	uint8_t header_version;
	uint16_t content_offset;
	uint8_t product_name[64];
	uint8_t product_version;
	uint8_t head_info[64];
	uint8_t lens_version;
	uint8_t content_version;
	uint32_t content_checksum;
	uint32_t content_len;
	uint16_t pll_bootdata_len;
};
#pragma pack(pop)

enum class VzLogLevel
{
	VZ_LOG_LEVEL_TRACE,
	VZ_LOG_LEVEL_DEBUG,
	VZ_LOG_LEVEL_INFO,
	VZ_LOG_LEVEL_WARN,
	VZ_LOG_LEVEL_ERROR,
	VZ_LOG_LEVEL_CRITICAL,
	VZ_LOG_LEVEL_OFF,
};

enum class FX3_FW_TARGET {
	FW_TARGET_NONE = 0,	/* Invalid target					*/
	FW_TARGET_RAM,		/* Program firmware (hex) to RAM	*/
	FW_TARGET_I2C,		/* Program firmware (hex) to EEPROM	*/
	FW_TARGET_SPI		/* Program firmware (hex) to SPI	*/
};

typedef enum _USB_DEVICE_SPEED {
	UsbLowSpeed = 0,
	UsbFullSpeed,
	UsbHighSpeed,
	UsbSuperSpeed
} USB_DEVICE_SPEED;

class VizionCam;

typedef void (*fpVcSetLogLevel)(VzLogLevel);
typedef VizionCam* (*fpVcCreateVizionCamDevice)();
typedef int (*fpVcReleaseVizionCamDevice)(VizionCam*);
typedef int (*fpVcOpen)(VizionCam*, int dev_idx);
typedef int (*fpVcClose)(VizionCam*);
typedef int (*fpVcGetImageCapture)(VizionCam*, uint8_t*, uint16_t);
typedef int (*fpVcGetRawImageCapture)(VizionCam*, uint8_t*, int* size, uint16_t);
typedef int (*fpVcGetCaptureFormatList)(VizionCam*, std::vector<VzFormat>&);
typedef int (*fpVcSetCaptureFormat)(VizionCam*, VzFormat);
typedef int (*fpVcGetBootdataHeader)(VizionCam*, VzHeader*);
typedef int (*fpVcClearISPBootdata)(VizionCam*);
typedef int (*fpVcDownloadBootdata)(VizionCam*, const char*);
typedef int (*fpVcGotoProgramMode)(VizionCam*);
typedef int (*fpVcStartFWDownload)(FX3_FW_TARGET tgt, const char* tatgetimg);

typedef int (*fpVcGetAutoExposureMode)(VizionCam*, AE_MODE_STATUS&);
typedef int (*fpVcSetAutoExposureMode)(VizionCam*, AE_MODE_STATUS);
typedef int (*fpVcGetExposureTime)(VizionCam*, uint32_t&);
typedef int (*fpVcSetExposureTime)(VizionCam*, uint32_t);
typedef int (*fpVcGetExposureGain)(VizionCam*, uint8_t&);
typedef int (*fpVcSetExposureGain)(VizionCam*, uint8_t);
typedef int (*fpVcGetMaxFPS)(VizionCam*, uint8_t&);
typedef int (*fpVcSetMaxFPS)(VizionCam*, uint8_t);
typedef int (*fpVcGetAutoWhiteBalanceMode)(VizionCam*, AWB_MODE_STATUS&);
typedef int (*fpVcSetAutoWhiteBalanceMode)(VizionCam*, AWB_MODE_STATUS);
typedef int (*fpVcGetTemperature)(VizionCam*, uint16_t&);
typedef int (*fpVcSetTemperature)(VizionCam*, uint16_t);

typedef int (*fpVcGetGamma)(VizionCam*, double&);
typedef int (*fpVcSetGamma)(VizionCam*, double);
typedef int (*fpVcGetSaturation)(VizionCam*, double&);
typedef int (*fpVcSetSaturation)(VizionCam*, double);
typedef int (*fpVcGetContrast)(VizionCam*, double&);
typedef int (*fpVcSetContrast)(VizionCam*, double);
typedef int (*fpVcGetSharpening)(VizionCam*, double&);
typedef int (*fpVcSetSharpening)(VizionCam*, double);
typedef int (*fpVcGetDenoise)(VizionCam*, double&);
typedef int (*fpVcSetDenoise)(VizionCam*, double);

typedef int (*fpGetDigitalZoomType)(VizionCam*, DZ_MODE_STATUS&);
typedef int (*fpSetDigitalZoomType)(VizionCam*, DZ_MODE_STATUS);
typedef int (*fpGetDigitalZoomTarget)(VizionCam*, double&);
typedef int (*fpSetDigitalZoomTarget)(VizionCam*, double);

typedef int(*fpVcGetVideoDeviceList)(VizionCam*, std::vector<std::wstring>&);
typedef int (*fpVcGetVizionCamDeviceName)(VizionCam*, wchar_t*);
typedef int (*fpVcGetDeviceHardwareID)(VizionCam*, wchar_t*);
typedef int (*fpVcGetDeviceSpeed)(VizionCam*, USB_DEVICE_SPEED&);

typedef int (*fpVcUVC_AP1302_I2C_Write)(VizionCam*, uint16_t, uint16_t);
typedef int (*fpVcUVC_AP1302_I2C_Read)(VizionCam*, uint16_t, uint16_t&);
typedef int (*fpVcSetPropertyValue)(VizionCam*, CAPTURE_PROPETIES, long, int);
typedef int (*fpVcGetPropertyValue)(VizionCam*, CAPTURE_PROPETIES, long&, int&);
typedef int (*fpVcGetPropertyRange)(VizionCam*, CAPTURE_PROPETIES, long&, long&, long&, long&, long&);

HINSTANCE hs_vizionsdk;
fpVcSetLogLevel VcSetLogLevel;
fpVcCreateVizionCamDevice VcCreateVizionCamDevice;
fpVcReleaseVizionCamDevice VcReleaseVizionCamDevice;
fpVcOpen VcOpen;
fpVcClose VcClose;
fpVcGetImageCapture VcGetImageCapture;
fpVcGetRawImageCapture VcGetRawImageCapture;
fpVcGetCaptureFormatList VcGetCaptureFormatList;
fpVcSetCaptureFormat VcSetCaptureFormat;
fpVcGetBootdataHeader VcGetBootdataHeader;
fpVcClearISPBootdata VcClearISPBootdata;
fpVcDownloadBootdata VcDownloadBootdata;
fpVcGotoProgramMode VcGotoProgramMode;
fpVcStartFWDownload VcStartFWDownload;

// AE.AWB
fpVcGetAutoExposureMode VcGetAutoExposureMode;
fpVcSetAutoExposureMode VcSetAutoExposureMode;
fpVcGetExposureTime VcGetExposureTime;
fpVcSetExposureTime VcSetExposureTime;
fpVcGetExposureGain VcGetExposureGain;
fpVcSetExposureGain VcSetExposureGain;
fpVcGetMaxFPS VcGetMaxFPS;
fpVcSetMaxFPS VcSetMaxFPS;
fpVcGetAutoWhiteBalanceMode VcGetAutoWhiteBalanceMode;
fpVcSetAutoWhiteBalanceMode VcSetAutoWhiteBalanceMode;
fpVcGetTemperature VcGetTemperature;
fpVcSetTemperature VcSetTemperature;
// Tunning
fpVcGetGamma		 VcGetGamma;
fpVcSetGamma		 VcSetGamma;
fpVcGetSaturation    VcGetSaturation;
fpVcSetSaturation    VcSetSaturation;
fpVcGetContrast      VcGetContrast;
fpVcSetContrast      VcSetContrast;
fpVcGetSharpening    VcGetSharpening;
fpVcSetSharpening    VcSetSharpening;
fpVcGetDenoise		 VcGetDenoise;
fpVcSetDenoise		 VcSetDenoise;

// Digital Zoom
fpGetDigitalZoomType   VcGetDigitalZoomType;
fpSetDigitalZoomType   VcSetDigitalZoomType;
fpGetDigitalZoomTarget VcGetDigitalZoomTarget;
fpSetDigitalZoomTarget VcSetDigitalZoomTarget;

fpVcGetVideoDeviceList VcGetVideoDeviceList;
fpVcGetVizionCamDeviceName VcGetVizionCamDeviceName;
fpVcGetDeviceHardwareID VcGetDeviceHardwareID;
fpVcGetDeviceSpeed VcGetDeviceSpeed;

fpVcUVC_AP1302_I2C_Write VcUVC_AP1302_I2C_Write;
fpVcUVC_AP1302_I2C_Read  VcUVC_AP1302_I2C_Read;
fpVcSetPropertyValue  VcSetPropertyValue;
fpVcGetPropertyValue  VcGetPropertyValue;
fpVcGetPropertyRange  VcGetPropertyRange;

VizionCam* vzcam;
AE_MODE_STATUS ae_mode;
AWB_MODE_STATUS awb_mode;
uint8_t max_fps = 0;
uint8_t exp_gain = 0;
uint32_t exp_time = 0;
uint16_t wb_temp = 0;

double gamma = 0.0;
double saturation = 0.0;
double contrast = 0.0;
double sharpening = 0.0;
double denoise = 0.0;

std::vector<std::wstring> devlist;

static const GUID xuGuidUVC =
{ 0x6ff33d97, 0x7a1f, 0x4c0d, { 0xb2, 0xd2, 0x59, 0xfb, 0x17, 0x89, 0x64, 0xc5 } };

template <typename FP>
int LoadVcFunc(FP& fp, std::string func_name)
{
	fp = (FP)GetProcAddress(hs_vizionsdk, func_name.c_str());
	if (NULL == fp) { printf("Load %s Fail\n", func_name.c_str()); return -1; }
	return 0;
}

int LoadVizionFunc()
{
	if (LoadVcFunc(VcSetLogLevel, "VcSetLogLevel")) return -1;
	if (LoadVcFunc(VcCreateVizionCamDevice, "VcCreateVizionCamDevice")) return -1;
	if (LoadVcFunc(VcReleaseVizionCamDevice, "VcReleaseVizionCamDevice")) return -1;
	if (LoadVcFunc(VcOpen, "VcOpen")) return -1;
	if (LoadVcFunc(VcClose, "VcClose")) return -1;
	if (LoadVcFunc(VcGetImageCapture, "VcGetImageCapture")) return -1;
	if (LoadVcFunc(VcGetRawImageCapture, "VcGetRawImageCapture")) return -1;
	if (LoadVcFunc(VcGetCaptureFormatList, "VcGetCaptureFormatList")) return -1;
	if (LoadVcFunc(VcSetCaptureFormat, "VcSetCaptureFormat")) return -1;
	if (LoadVcFunc(VcGetBootdataHeader, "VcGetBootdataHeader")) return -1;
	if (LoadVcFunc(VcClearISPBootdata, "VcClearISPBootdata")) return -1;
	if (LoadVcFunc(VcDownloadBootdata, "VcDownloadBootdata")) return -1;
	if (LoadVcFunc(VcGotoProgramMode, "VcGotoProgramMode")) return -1;
	if (LoadVcFunc(VcStartFWDownload, "VcStartFWDownload")) return -1;

	if (LoadVcFunc(VcGetAutoExposureMode, "VcGetAutoExposureMode")) return -1;
	if (LoadVcFunc(VcSetAutoExposureMode, "VcSetAutoExposureMode")) return -1;
	if (LoadVcFunc(VcGetExposureTime, "VcGetExposureTime")) return -1;
	if (LoadVcFunc(VcSetExposureTime, "VcSetExposureTime")) return -1;
	if (LoadVcFunc(VcGetExposureGain, "VcGetExposureGain")) return -1;
	if (LoadVcFunc(VcSetExposureGain, "VcSetExposureGain")) return -1;
	if (LoadVcFunc(VcGetMaxFPS, "VcGetMaxFPS")) return -1;
	if (LoadVcFunc(VcSetMaxFPS, "VcSetMaxFPS")) return -1;
	if (LoadVcFunc(VcGetAutoWhiteBalanceMode, "VcGetAutoWhiteBalanceMode")) return -1;
	if (LoadVcFunc(VcSetAutoWhiteBalanceMode, "VcSetAutoWhiteBalanceMode")) return -1;
	if (LoadVcFunc(VcGetTemperature, "VcGetTemperature")) return -1;
	if (LoadVcFunc(VcSetTemperature, "VcSetTemperature")) return -1;

	if (LoadVcFunc(VcGetGamma, "VcGetGamma")) return -1;
	if (LoadVcFunc(VcSetGamma, "VcSetGamma")) return -1;
	if (LoadVcFunc(VcGetSaturation, "VcGetSaturation")) return -1;
	if (LoadVcFunc(VcSetSaturation, "VcSetSaturation")) return -1;
	if (LoadVcFunc(VcGetContrast, "VcGetContrast")) return -1;
	if (LoadVcFunc(VcSetContrast, "VcSetContrast")) return -1;
	if (LoadVcFunc(VcGetSharpening, "VcGetSharpening")) return -1;
	if (LoadVcFunc(VcSetSharpening, "VcSetSharpening")) return -1;
	if (LoadVcFunc(VcGetDenoise, "VcGetDenoise")) return -1;
	if (LoadVcFunc(VcSetDenoise, "VcSetDenoise")) return -1;

	if (LoadVcFunc(VcGetDigitalZoomType, "VcGetDigitalZoomType")) return -1;
	if (LoadVcFunc(VcSetDigitalZoomType, "VcSetDigitalZoomType")) return -1;
	if (LoadVcFunc(VcGetDigitalZoomTarget, "VcGetDigitalZoomTarget")) return -1;
	if (LoadVcFunc(VcSetDigitalZoomTarget, "VcSetDigitalZoomTarget")) return -1;

	if (LoadVcFunc(VcGetVideoDeviceList, "VcGetVideoDeviceList")) return -1;
	if (LoadVcFunc(VcGetVizionCamDeviceName, "VcGetVizionCamDeviceName")) return -1;
	if (LoadVcFunc(VcGetDeviceHardwareID, "VcGetDeviceHardwareID")) return -1;
	if (LoadVcFunc(VcGetDeviceSpeed, "VcGetDeviceSpeed")) return -1;

	if (LoadVcFunc(VcUVC_AP1302_I2C_Write, "VcUVC_AP1302_I2C_Write")) return -1;
	if (LoadVcFunc(VcUVC_AP1302_I2C_Read, "VcUVC_AP1302_I2C_Read")) return -1;
	if (LoadVcFunc(VcSetPropertyValue, "VcSetPropertyValue")) return -1;
	if (LoadVcFunc(VcGetPropertyValue, "VcGetPropertyValue")) return -1;
	if (LoadVcFunc(VcGetPropertyRange, "VcGetPropertyRange")) return -1;

	return 0;
}


bool preview = true;
cv::VideoCapture cap;
cv::Mat frame;
cv::Mat resize_frame;

int frame_cnt = 30;
double framerate_avg = 0;
UINT g_width = 0, g_height = 0;
uint8_t* img_arr;
uint8_t* raw_arr;

WCHAR wchstr[256];

void showImage(UINT width, UINT height)
{
	int i = 0;
	int ret = 0;
	double time_taken = 0;
	int size = 0;
	int timeout = 3000;

	g_width = width;
	g_height = height;


	while (true)
	{
		if (!preview)
			break;

		img_arr = new uint8_t[g_width * g_height * 3];

		ret = VcGetImageCapture(vzcam, img_arr, timeout);

		if (ret != 0)
		{
			std::cout << "VcGetImageCapture Fail ErrCode: " << ret << std::endl;
			goto done;
		}
		
		frame = cv::Mat(g_height, g_width, CV_8UC3, img_arr);
		// check if we succeeded
		if (frame.empty()) {
			std::cout << "ERROR! blank frame grabbed\n";
			break;
		}
		// show live and wait for a key with timeout long enough to show images
		cv::resize(frame, resize_frame, cv::Size(1280, 720));
		imshow("Live", resize_frame);
		
done:
		if (waitKey(5) >= 0)
			break;

		if (img_arr != NULL)
		{
			delete[] img_arr;
			img_arr = NULL;
		}
		if (raw_arr != NULL)
		{
			delete[] raw_arr;
			raw_arr = NULL;
		}

	}
	cv::destroyWindow("Live");
}

int main()
{
	hs_vizionsdk = LoadLibrary(L"VizionSDK.dll");

	if (NULL == hs_vizionsdk)
	{
		printf("Load VizionSDK.dll Fail\n");
		return -1;
	}

	if (LoadVizionFunc() < 0) return -1;
	
	vzcam = VcCreateVizionCamDevice(); // Create control object for Camera 0

	VcGetVideoDeviceList(vzcam, devlist);

	if (devlist.size() == 0) { std::cout << "Cannot Find any Camera!" << std::endl;  return -1; }

	if (VcOpen(vzcam, 0) != 0) {
		std::cout << "Open Fail" << std::endl;
		return -1;
	}

	_setmode(_fileno(stdout), _O_U16TEXT);

	VcGetVizionCamDeviceName(vzcam, wchstr);
	std::wcout << L"Device Name: " << std::wstring(wchstr) << std::endl;
	VcGetDeviceHardwareID(vzcam, wchstr);
	std::wcout << L"Hardware ID: " << std::wstring(wchstr) << std::endl;

	_setmode(_fileno(stdout), _O_TEXT);

	USB_DEVICE_SPEED speed;
	VcGetDeviceSpeed(vzcam, speed);
	std::cout << "USB_DEVICE_SPEED: " << speed << std::endl;
	switch (speed)
	{
	case USB_DEVICE_SPEED::UsbLowSpeed:
		std::cout << "USB_DEVICE_SPEED: LowSpeed" << std::endl;
		break;
	case USB_DEVICE_SPEED::UsbFullSpeed:
		std::cout << "USB_DEVICE_SPEED: FullSpeed" << std::endl;
		break;
	case USB_DEVICE_SPEED::UsbHighSpeed:
		std::cout << "USB_DEVICE_SPEED: HighSpeed" << std::endl;
		break;
	case USB_DEVICE_SPEED::UsbSuperSpeed:
		std::cout << "USB_DEVICE_SPEED: SuperSpeed" << std::endl;
		break;
	default:
		return -1;
	}

	BYTE* data = new BYTE[8];
	ULONG readCount = 0;
	long value = 0;
	int flag = 0;
	long min, max, step, def, caps;

	if (VcGetPropertyRange(vzcam, CAPTURE_BRIGHTNESS, min, max, step, def, caps) == 0)
		std::cout << "Get CAPTURE_BRIGHTNESS min: " << min << " max: " << max << " step: " << step << " def: " << def << " caps: " << caps << std::endl;
	if (VcGetPropertyValue(vzcam, CAPTURE_BRIGHTNESS, value, flag) == 0)
		std::cout << "Get CAPTURE_BRIGHTNESS value: " << value << " flag: " << flag << std::endl;
	if (VcGetPropertyRange(vzcam, CAPTURE_EXPOSURE, min, max, step, def, caps) == 0)
		std::cout << "Get CAPTURE_EXPOSURE min: " << min << " max: " << max << " step: " << step << " def: " << def << " caps: " << caps << std::endl;
	if (VcGetPropertyValue(vzcam, CAPTURE_EXPOSURE, value, flag) == 0)
		std::cout << "Get CAPTURE_EXPOSURE value: " << value << " flag: " << flag << std::endl;
/*
	value = -6;
	flag = 0;
	if (VcSetPropertyValue(vzcam, CAPTURE_EXPOSURE, value, flag) != 0)
		std::cout << "Set CAPTURE_EXPOSURE value: " << value << " flag: " << flag << std::endl;
*/

/*
	std::cout << "Clear AP1302 ISP Bootdata..." << std::endl;
	VcClearISPBootdata(vzcam);
	std::cout << "Download AP1302 ISP Bootdata..." << std::endl;
	VcDownloadBootdata(vzcam, ".\\bootdata_tevi-ar0234.bin");

	VcGetBootdataHeader(vzcam, &header);
	std::cout << "Header Info: " << header.head_info << std::endl;

	// UVC FW Download
	if (VcGotoProgramMode(vzcam) == 0) {
		Sleep(1000);
		VcStartFWDownload(FX3_FW_TARGET::FW_TARGET_SPI, "D:\\Cx3UvcCamera_20220728.img");
		return 0;
	}
*/

	int sel = 0;
	std::vector<VzFormat> vzformatlist;

	VcGetCaptureFormatList(vzcam, vzformatlist);

	for (int i = 0; i < vzformatlist.size(); i++)
		printf("[%d] Width=%d, Height=%d, Framerate=%d\n", i, vzformatlist[i].width, vzformatlist[i].height, vzformatlist[i].framerate);

	std::cout << "Select a Capture Format..Please enter the index of Format." << std::endl;

	std::cin >> sel;

	if (sel > vzformatlist.size() || sel < 0) {
		std::cout << "Select index " << sel << " Fail" << std::endl;
		return -1;
	}

	VcSetCaptureFormat(vzcam, vzformatlist[sel]);

	std::thread t1(showImage, vzformatlist[sel].width, vzformatlist[sel].height);
	t1.detach();

/*
	VcSetExposureGain(vzcam, 3); // Set exp gain 3x.   Range: 1x ~ 64x
	VcGetExposureGain(vzcam, exp_gain);
	printf("Get Exp Gain: %d\n", exp_gain);

	awb_mode = AWB_MODE_STATUS::MANUAL_TEMPERATURE_WB;
	VcSetAutoWhiteBalanceMode(vzcam, awb_mode);
	VcGetAutoWhiteBalanceMode(vzcam, awb_mode);
	if (awb_mode == AWB_MODE_STATUS::AUTO_WB) printf("Get AWB Mode: AUTO_EXP\n");
	else if (awb_mode == AWB_MODE_STATUS::MANUAL_TEMPERATURE_WB) printf("Get AWB Mode: MANUAL_TEMPERATURE_WB\n");

	VcSetTemperature(vzcam, 6500);  // Set wb temperature 6500.   Range: 2300 ~ 15000K
	VcGetTemperature(vzcam, wb_temp);
	printf("Get WB Temperature: %d\n", wb_temp);

	// Tunning
	VcSetGamma(vzcam, 1.5);  // Set gamma 1.5   Range: 0.0 ~ 2.5
	VcGetGamma(vzcam, gamma);
	printf("Get Gamma: %.4f\n", gamma);

	VcSetSaturation(vzcam, 1.5);  // Set Saturatoin 1.5   Range: 0.0 ~ 2.0
	VcGetSaturation(vzcam, saturation);
	printf("Get Saturation: %.4f\n", saturation);

	VcSetContrast(vzcam, -2.0);  // Set Contrast 1.5   Range: -5.0 ~ 5.0
	VcGetContrast(vzcam, contrast);
	printf("Get Contrast: %.4f\n", contrast);

	VcSetSharpening(vzcam, -1.5);  // Set Sharpening 1.5   Range: -2.0 ~ 2.0
	VcGetSharpening(vzcam, sharpening);
	printf("Get harpening: %.4f\n", sharpening);

	VcSetDenoise(vzcam, -1.5);  // Set Denoise 1.5   Range: -2.0 ~ 2.0
	VcGetDenoise(vzcam, denoise);
	printf("Get Denoise: %.4f\n", denoise);
*/	

	ae_mode = AE_MODE_STATUS::MANUAL_EXP;
	VcSetAutoExposureMode(vzcam, ae_mode);

	VcGetAutoExposureMode(vzcam, ae_mode);
	if (ae_mode == AE_MODE_STATUS::AUTO_EXP) printf("Get AE Mode: AUTO_EXP\n");
	else if (ae_mode == AE_MODE_STATUS::AUTO_EXP) printf("Get AE Mode: MANUAL_EXP\n");

	max_fps = (uint8_t)vzformatlist[sel].framerate;
	VcSetMaxFPS(vzcam, max_fps); // Set max fps
	VcGetMaxFPS(vzcam, max_fps);
	printf("Get Max FPS: %d\n", max_fps);
	
	uint32_t exptime;
	VcSetExposureTime(vzcam, 66667); // Set exp time 66667 us.  Range: 1 ~ 500000 us
	VcGetExposureTime(vzcam, exp_time);
	printf("Get AE Exposure Time: %d us\n", exp_time);

	while (true) {
		std::cout << "Enter Expoture Time: (1 ~ 500,000 us, set 0 to quit the program)" << std::endl;
		std::cin >> exptime;

		if (exptime == 0) { std::cout << "Leave expoture setting program..." << std::endl;  break; }

		if (exptime > 500000 || exptime < 1) { std::cout << "Just support Range: 1 ~ 500,000 us!" << std::endl;  continue; }

		VcSetExposureTime(vzcam, exptime);

		VcGetExposureTime(vzcam, exp_time);
		printf("Current Exposure Time: %d ms\n", exp_time / 1000);
	}

	DZ_MODE_STATUS dz_mode;
	double dz_tgt;

	VcGetDigitalZoomType(vzcam, dz_mode);

	if (dz_mode == DZ_MODE_STATUS::DZ_IMMEDIATE) printf("Get DZ Mode: DZ_IMMEDIATE\n");
	else if (dz_mode == DZ_MODE_STATUS::DZ_SLOW) printf("Get DZ Mode: DZ_SLOW\n");
	else if (dz_mode == DZ_MODE_STATUS::DZ_FAST) printf("Get DZ Mode: DZ_FAST\n");

	dz_mode = DZ_MODE_STATUS::DZ_FAST;
	VcSetDigitalZoomType(vzcam, dz_mode);

	VcGetDigitalZoomTarget(vzcam, dz_tgt);
	printf("Get Digital Zoom Target: %f ms\n", dz_tgt);

	while (true) {
		std::cout << "Enter Digital Zoom Target: (1 ~ 4x, set 0 to quit the program)" << std::endl;
		std::cin >> dz_tgt;

		if (dz_tgt == 0) { std::cout << "Leave zoom setting program..." << std::endl;  break; }

		if (dz_tgt > 4 || dz_tgt < 1) { std::cout << "Just support Range: 1 ~ 4x!" << std::endl;  continue; }

		VcSetDigitalZoomTarget(vzcam, dz_tgt);

		VcGetDigitalZoomTarget(vzcam, dz_tgt);
		printf("Current Digital Zoom Target: %f ms\n", dz_tgt);
	}

	//Test Thoughput Limit
	uint16_t thoughput = 100;

	VcUVC_AP1302_I2C_Read(vzcam, 0x6132, thoughput);
	printf("Current Thoughput Limit: %d MBps, Data:0x%04x\n", thoughput >> 6, thoughput);

	while (true) {
		std::cout << "Enter Thoughput limit: (100 ~ 360 MBps, set 0 to quit the program)" << std::endl;
		std::cin >> thoughput;

		if (thoughput == 0) { std::cout << "Leave thoughput setting program..." << std::endl;  break; }

		if (thoughput > 1000 || thoughput < 100) { std::cout << "Just support Range: 100 ~ 360 MBps!" << std::endl;  continue; }

		VcUVC_AP1302_I2C_Write(vzcam, 0x6132, (thoughput << 6));

		VcUVC_AP1302_I2C_Read(vzcam, 0x6132, thoughput);
		printf("Current Thoughput Limit: %d MBps, Data:0x%04x\n", thoughput >> 6, thoughput);
	}

	preview = false;

	Sleep(10);
	
	VcClose(vzcam);

	VcReleaseVizionCamDevice(vzcam);

	return 0;
}
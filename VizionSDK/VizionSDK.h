#pragma once

#ifdef VIZIONSDK_EXPORTS
#define VIZION_API __declspec(dllexport)
#else
#define VIZION_API __declspec(dllimport)
#endif

// Default ISP Setting
#define VZCAM_ISP_CONTROL_EXPOSURETIME_DEF 33333
#define VZCAM_ISP_CONTROL_GAIN_DEF 1
#define VZCAM_ISP_CONTROL_BACKLIGHT_COMP_DEF 0.0
#define VZCAM_ISP_CONTROL_TEMPERAURE_DEF 5000
#define VZCAM_ISP_CONTROL_GAMMA_DEF 0.0
#define VZCAM_ISP_CONTROL_SATURATION_DEF 1.0
#define VZCAM_ISP_CONTROL_CONTRAST_DEF 0.0
#define VZCAM_ISP_CONTROL_SHARPENING_DEF 0.0
#define VZCAM_ISP_CONTROL_DENOISE_DEF 0.0
#define VZCAM_ISP_CONTROL_DIGITALZOOM_DEF 1.0
#define VZCAM_ISP_CONTROL_DIGITALZOOM_CT_DEF 0.5

#define VZCAM_ISP_CONTROL_EXPOSURETIME_MIN 1
#define VZCAM_ISP_CONTROL_EXPOSURETIME_MAX 500000
#define VZCAM_ISP_CONTROL_GAIN_MIN 1
#define VZCAM_ISP_CONTROL_GAIN_MAX 64
#define VZCAM_ISP_CONTROL_BACKLIGHT_COMP_MIN -15.0
#define VZCAM_ISP_CONTROL_BACKLIGHT_COMP_MAX 15.0
#define VZCAM_ISP_CONTROL_TEMPERAURE_MIN 2300
#define VZCAM_ISP_CONTROL_TEMPERAURE_MAX 15000
#define VZCAM_ISP_CONTROL_WHITEBALANCE_QXQY_MIN -4.0
#define VZCAM_ISP_CONTROL_WHITEBALANCE_QXQY_MAX 4.0
#define VZCAM_ISP_CONTROL_GAMMA_MIN 0.0
#define VZCAM_ISP_CONTROL_GAMMA_MAX 2.5
#define VZCAM_ISP_CONTROL_SATURATION_MIN 0.0
#define VZCAM_ISP_CONTROL_SATURATION_MAX 2.0
#define VZCAM_ISP_CONTROL_CONTRAST_MIN -5.0
#define VZCAM_ISP_CONTROL_CONTRAST_MAX 5.0
#define VZCAM_ISP_CONTROL_SHARPENING_MIN -2.0
#define VZCAM_ISP_CONTROL_SHARPENING_MAX 2.0
#define VZCAM_ISP_CONTROL_DENOISE_MIN -2.0
#define VZCAM_ISP_CONTROL_DENOISE_MAX 2.0
#define VZCAM_ISP_CONTROL_DIGITALZOOM_MIN 1.0
#define VZCAM_ISP_CONTROL_DIGITALZOOM_MAX 8.0
#define VZCAM_ISP_CONTROL_DIGITALZOOM_CT_MIN 0.0
#define VZCAM_ISP_CONTROL_DIGITALZOOM_CT_MAX 1.0


enum VZ_RESULT
{
	VZ_SUCCESS = 0,
	VZ_FAIL    = -1,

	VZ_CAPTURE_FORMAT_ERROR,
};

enum VZ_IMAGE_FORMAT
{
	NONE = 0,
	YUY2,
	UYVY,
	NV12,
	MJPG,
};

enum class VZ_CAMERA_TYPE
{
	VZ_TEVI_OV5640 = 0,

	//AR CAMERA
	VZ_TEVI_AR0144,
	VZ_TEVI_AR0234,
	VZ_TEVI_AR0521,
	VZ_TEVI_AR0522,
	VZ_TEVI_AR0821,
	VZ_TEVI_AR0822,
	VZ_TEVI_AR1335,
};

enum class AE_MODE_STATUS
{
	MANUAL_EXP = 0,
	AUTO_EXP = 0xC,
};

enum class AWB_MODE_STATUS
{
	MANUAL_QXQY_WB = 0,
	MANUAL_TEMPERATURE_WB = 0x7,
	AUTO_WB = 0xF,
};

enum class DZ_MODE_STATUS
{
	DZ_IMMEDIATE = 0x8000,
	DZ_SLOW      = 0x0040,
	DZ_FAST      = 0x0200,
};

enum class DZ_TARGET_STATUS
{
	DZ_TGT_1X         = 1,
	DZ_TGT_2X         = 2,
	DZ_TGT_4X         = 4,
	DZ_TGT_NO_SCALING = 0xFF,
};

enum class FLIP_MODE
{
	FLIP_NORMAL        = 0,
	FLIP_H_MIRROR,
	FLIP_V_MIRROR,
	FLIP_ROTATE_180,
};

enum class EFFECT_MODE
{
	NORMAL_MODE        = 0x00,
	BLACK_WHITE_MODE   = 0x03,
	GRAYSCALE_MODE     = 0x06,
	NEGATIVE_MODE      = 0x07,
	SKETCH_MODE        = 0x0F,
};

/* List of supported programming targets */
enum class FX3_FW_TARGET {
	FW_TARGET_NONE = 0,	/* Invalid target					*/
	FW_TARGET_RAM,		/* Program firmware (hex) to RAM	*/
	FW_TARGET_I2C,		/* Program firmware (hex) to EEPROM	*/
	FW_TARGET_SPI		/* Program firmware (hex) to SPI	*/
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

#define HEADER_VER_3

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

#ifdef HEADER_VER_3
struct VzHeaderV3 {   // Header Version 3
	// Fixed Area
	uint8_t header_version;
	uint16_t content_offset;
	uint16_t sensor_type;
	uint8_t sensor_fuseid[16];
	uint8_t product_name[64];
	uint8_t lens_id[16];
	uint16_t fix_checksum;
	// Dynamic update area
	uint8_t tn_fw_version[2];
	uint16_t vendor_fw_version;
	uint16_t custom_number;   // or revision number
	uint8_t record_year;
	uint8_t record_month;
	uint8_t record_day;
	uint8_t record_hour;
	uint8_t record_minute;
	uint8_t record_second;
	uint16_t mipi_datarate;
	uint32_t content_len;
	uint16_t content_checksum;
	uint16_t total_checksum;
};
#endif
#pragma pack(pop)

struct VzFormat {
	uint8_t mediatype_idx;
	uint16_t width;
	uint16_t height;
	uint16_t framerate;
	VZ_IMAGE_FORMAT format;
};

struct CameraProfile {
	int width;
	int height;
	int framerate;
	std::string pixelformat_str;
	VZ_IMAGE_FORMAT format;
};

struct UVCProfiles {
	int exposure;
	int gain;
	int temperature;
	int zoom;
	int pan;
	int tilt;
	int brightness;
	int contrast;
	int saturation;
	int sharpness;
	int gamma;
	bool exp_auto;
	bool wb_auto;
};

struct ISPProfiles {
	double zoom_target;
	double zoom_ctx;
	double zoom_cty;
	double brightness;
	double contrast;
	double saturation;
	double sharpmess;
	double gamma;
	double denoise;
	int max_fps;
	int ae_exp_time;
	int ae_ex_gain;
	int awb_temp;
	int flipmode;
	int effectmode;
	bool ae_exp_mode;
	bool awb_mode;


};

struct VzProfiles {
	std::string profilename;
	std::string gendate;
	std::string productname;
	std::string serialnumber;
	std::string controlmode;
	CameraProfile camprofile;
	UVCProfiles uvcprofile;
	ISPProfiles ispprofile;
};

#include "SourceReaderCallBack.h"

class VIZION_API VizionCam {
private:
	std::wstring dev_name;
	std::wstring hardware_id;
	uint8_t dev_idx;
	uint8_t header_ver;

	VZ_CAMERA_TYPE cam_type;
	VzFormat capformat;
	VzHeader header_info;
	VzProfiles vzprof;

	std::thread t;
	std::thread::native_handle_type handle;
	
	std::mutex m_mutex;
	std::condition_variable cv;

	std::string description;

	bool is_open = false;
	bool sample_fail = false;

	cv::Mat img;
	cv::Mat* rawdata;

	int raw_size;

	bool is_auto_ae;
	bool is_auto_awb;
	FLIP_MODE def_flip;

	uint16_t vendor_id;
	uint16_t product_id;
	USB_DEVICE_SPEED speed;

	uint32_t noOfVideoDevices;

	std::map<int, std::wstring> devicename_map;
	std::map<int, std::wstring> hardwareid_map;

	//Media foundation and DSHOW specific structures, class and variables
	IMFMediaSource* pVideoSource = NULL;
	IMFAttributes* pVideoConfig = NULL;
	IMFActivate** ppVideoDevices = NULL;
	IMFSourceReader* pVideoReader = NULL;
	IMFMediaType* pMediaType = NULL;
	SourceReaderCB* pCallback = NULL;

	HRESULT GetVideoDevices(void);
	HRESULT InitVideoDevice(void);

	std::wstring GetMediaTypeDescriptions(IMFMediaType* pMediaType);
	HRESULT EnumerateTypesForStream(DWORD dwStreamIndex, std::vector<VzFormat>& capformats);
	HRESULT ConfigureDecoder(IMFSourceReader* pReader, DWORD dwStreamIndex);
	int ProcessSamples(IMFSourceReader* pVideoReader, uint8_t* raw_data, uint16_t timeout);
	HRESULT SetGetExtensionUnit(GUID xuGuid, DWORD dwExtensionNode, ULONG xuPropertyId, ULONG flags, void* data, int len, ULONG* readCount);
	
	int GenI2CRead(uint8_t slvaddr, uint8_t reglen, uint16_t regaddr, uint16_t datalen, uint8_t* data);
	int GenI2CWrite(uint8_t slvaddr, uint8_t reglen, uint16_t regaddr, uint8_t datalen, uint8_t* data);

public:
	VizionCam();
	VizionCam(uint8_t dev_idx);
	~VizionCam();

	int Open(int dev_idx);
	int Close();
	int GetVideoDeviceList(std::vector<std::wstring>& devname_list);
	int GetCaptureFormatList(std::vector<VzFormat>& capformats);
	int GetDeviceHardwareID(wchar_t* hardware_id);
	int GetUSBFirmwareVersion(char* fw_ver);
	int GetUSBFirmwareVersion(BYTE* fw_ver_data);
	int GetUniqueSensorID(char* sensor_id);
	void GetDeviceSpeed(USB_DEVICE_SPEED& usb_speed);
	int SetCaptureFormat(VzFormat capformat);
	int GetImageCapture(uint8_t* img_data, uint16_t timeout);
	int GetRawImageCapture(uint8_t* raw_data, int* size, uint16_t timeout);

	int GetVizionCamDeviceName(wchar_t* devname);
	int GetBootdataHeader(VzHeader* header);
	int GetBootdataHeaderV3(VzHeaderV3* header);
	int CheckHeaderVer();


	int GotoProgramMode();
	int ClearISPBootdata(void);
	int DownloadBootdata(const char* binfilepath);

	int GetMaxFPS(uint8_t& max_fps);
	int SetMaxFPS(uint8_t max_fps);

	int GetAutoExposureMode(AE_MODE_STATUS& ae_mode);
	int SetAutoExposureMode(AE_MODE_STATUS ae_mode);
	int GetExposureTime(uint32_t& exptime);
	int SetExposureTime(uint32_t exptime);
	int GetExposureGain(uint8_t& expgain);
	int SetExposureGain(uint8_t expgain);
	int GetBacklightCompensation(double& ae_comp);
	int SetBacklightCompensation(double ae_comp);

	int GetAutoWhiteBalanceMode(AWB_MODE_STATUS& awb_mode);
	int SetAutoWhiteBalanceMode(AWB_MODE_STATUS awb_mode);
	int GetTemperature(uint16_t& temp);
	int SetTemperature(uint16_t temp);
	int GetWhiteBalanceQx(double& wb_qx);
	int SetWhiteBalanceQx(double wb_qx);
	int GetWhiteBalanceQy(double& wb_qy);
	int SetWhiteBalanceQy(double wb_qy);

	// Tunning Function
	int GetGamma(double& gamma);
	int SetGamma(double gamma);
	int GetSaturation(double& saturation);
	int SetSaturation(double saturation); 
	int GetContrast(double& contrast);
	int SetContrast(double contrast);
	int GetSharpening(double& sharpness);
	int SetSharpening(double sharpness);
	int GetDenoise(double& denoise);
	int SetDenoise(double denoise);

	int GetFlipMode(FLIP_MODE& flip);
	int SetFlipMode(FLIP_MODE flip);
	int GetEffectMode(EFFECT_MODE& effect);
	int SetEffectMode(EFFECT_MODE effect);

	int GetDigitalZoomType(DZ_MODE_STATUS& zoom_type);
	int SetDigitalZoomType(DZ_MODE_STATUS zoom_type);
	int GetDigitalZoomTarget(double& times);
	int SetDigitalZoomTarget(double times);
	int GetDigitalZoom_CT_X(double& ct_x);
	int SetDigitalZoom_CT_X(double ct_x);
	int GetDigitalZoom_CT_Y(double& ct_y);
	int SetDigitalZoom_CT_Y(double ct_y);

	int RecoverDefaultSetting();
	int LoadProfileSettingFromPath(const char* profile_path);
	int LoadProfileSetting(const char* profile_path);
	int SetProfileStreamingConfig();
	int SetProfileControlConfig();

	int UVC_AP1302_I2C_Read(uint16_t addrReg, uint16_t& data);
	int UVC_AP1302_I2C_Write(uint16_t addrReg, uint16_t data);
	int AdvWriteSensorRegister(uint16_t sen_sid, uint16_t sen_addrReg, uint16_t sen_data);
	int AdvReadSensorRegister(uint16_t sen_sid, uint16_t sen_addrReg, uint16_t& sen_data);
	int SetPropertyValue(CAPTURE_PROPETIES prop_id, long value, int flag);
	int GetPropertyValue(CAPTURE_PROPETIES prop_id, long& value, int& flag);
	int GetPropertyRange(CAPTURE_PROPETIES prop_id, long& min, long& max, long& step, long& def, long& caps);
};


extern "C" {
	VIZION_API void VcSetLogLevel(VzLogLevel level);

	// VizionCam Class Control API
	VIZION_API VizionCam* VcCreateVizionCamDevice();
	VIZION_API int VcReleaseVizionCamDevice(VizionCam* vizion_cam);
	VIZION_API int VcGetVizionCamDeviceName(VizionCam* vizion_cam, wchar_t* devname);
	VIZION_API int VcGetDeviceHardwareID(VizionCam* vizion_cam, wchar_t* hardware_id);
	VIZION_API int VcGetUSBFirmwareVersion(VizionCam* vizion_cam, char* fw_ver);
	VIZION_API void VcGetDeviceSpeed(VizionCam* vizion_cam, USB_DEVICE_SPEED& usb_speed);
	VIZION_API int VcGetUniqueSensorID(VizionCam* vizion_cam, char* sensor_id);

	VIZION_API int VcOpen(VizionCam* vizion_cam, int dev_idx);
	VIZION_API int VcClose(VizionCam* vizion_cam);
	VIZION_API int VcGetImageCapture(VizionCam* vizion_cam, uint8_t* img_data, uint16_t timeout);
	VIZION_API int VcGetRawImageCapture(VizionCam* vizion_cam, uint8_t* raw_data, int* data_size, uint16_t timeout);
	VIZION_API int VcGetVideoDeviceList(VizionCam* vizion_cam, std::vector<std::wstring>& devname_list);
	VIZION_API int VcGetCaptureFormatList(VizionCam* vizion_cam, std::vector<VzFormat>& capformats);
	VIZION_API int VcSetCaptureFormat(VizionCam* vizion_cam, VzFormat capformat);

	VIZION_API int VcClearISPBootdata(VizionCam* vizion_cam);
	VIZION_API int VcDownloadBootdata(VizionCam* vizion_cam, const char* binfilepath);
	VIZION_API int VcGetBootdataHeader(VizionCam* vizion_cam, VzHeader* header);
	VIZION_API int VcGetBootdataHeaderV3(VizionCam* vizion_cam, VzHeaderV3* header);
	VIZION_API int VcCheckHeaderVer(VizionCam* vizion_cam);

	VIZION_API int VcGetAutoExposureMode(VizionCam* vizion_cam, AE_MODE_STATUS& ae_mode);
	VIZION_API int VcSetAutoExposureMode(VizionCam* vizion_cam, AE_MODE_STATUS ae_mode);
	VIZION_API int VcGetExposureTime(VizionCam* vizion_cam, uint32_t& exptime);
	VIZION_API int VcSetExposureTime(VizionCam* vizion_cam, uint32_t exptime);
	VIZION_API int VcGetExposureGain(VizionCam* vizion_cam, uint8_t& expgain);
	VIZION_API int VcSetExposureGain(VizionCam* vizion_cam, uint8_t expgain);
	VIZION_API int VcGetBacklightCompensation(VizionCam* vizion_cam, double& ae_comp);
	VIZION_API int VcSetBacklightCompensation(VizionCam* vizion_cam, double ae_comp);
	VIZION_API int VcGetMaxFPS(VizionCam* vizion_cam, uint8_t& max_fps);
	VIZION_API int VcSetMaxFPS(VizionCam* vizion_cam, uint8_t max_fps);

	VIZION_API int VcGetAutoWhiteBalanceMode(VizionCam* vizion_cam, AWB_MODE_STATUS& awb_mode);
	VIZION_API int VcSetAutoWhiteBalanceMode(VizionCam* vizion_cam, AWB_MODE_STATUS awb_mode);
	VIZION_API int VcGetTemperature(VizionCam* vizion_cam, uint16_t& temp);
	VIZION_API int VcSetTemperature(VizionCam* vizion_cam, uint16_t temp);

	VIZION_API int VcGetGamma(VizionCam* vizion_cam, double& gamma);
	VIZION_API int VcSetGamma(VizionCam* vizion_cam, double gamma);
	VIZION_API int VcGetSaturation(VizionCam* vizion_cam, double& saturation);
	VIZION_API int VcSetSaturation(VizionCam* vizion_cam, double saturation);
	VIZION_API int VcGetContrast(VizionCam* vizion_cam, double& contrast);
	VIZION_API int VcSetContrast(VizionCam* vizion_cam, double contrast);
	VIZION_API int VcGetSharpening(VizionCam* vizion_cam, double& sharpness);
	VIZION_API int VcSetSharpening(VizionCam* vizion_cam, double sharpness);
	VIZION_API int VcGetDenoise(VizionCam* vizion_cam, double& denoise);
	VIZION_API int VcSetDenoise(VizionCam* vizion_cam, double denoise);

	VIZION_API int VcGetFlipMode(VizionCam* vizion_cam, FLIP_MODE& flip);
	VIZION_API int VcSetFlipMode(VizionCam* vizion_cam, FLIP_MODE flip);
	VIZION_API int VcGetEffectMode(VizionCam* vizion_cam, EFFECT_MODE& effect);
	VIZION_API int VcSetEffectMode(VizionCam* vizion_cam, EFFECT_MODE effect);

	VIZION_API int VcGetDigitalZoomType(VizionCam* vizion_cam, DZ_MODE_STATUS& zoom_type);
	VIZION_API int VcSetDigitalZoomType(VizionCam* vizion_cam, DZ_MODE_STATUS zoom_type);
	VIZION_API int VcGetDigitalZoomTarget(VizionCam* vizion_cam, double& times);
	VIZION_API int VcSetDigitalZoomTarget(VizionCam* vizion_cam, double times);

	VIZION_API int VcGetDigitalZoom_CT_X(VizionCam* vizion_cam, double& ct_x);
	VIZION_API int VcSetDigitalZoom_CT_X(VizionCam* vizion_cam, double ct_x);
	VIZION_API int VcGetDigitalZoom_CT_Y(VizionCam* vizion_cam, double& ct_y);
	VIZION_API int VcSetDigitalZoom_CT_Y(VizionCam* vizion_cam, double ct_y);

	VIZION_API int VcRecoverDefaultSetting(VizionCam* vizion_cam);
	VIZION_API int VcLoadProfileSettingFromPath(VizionCam* vizion_cam, const char* profile_path);
	VIZION_API int VcLoadProfileSetting(VizionCam* vizion_cam, const char* profile_string);
	VIZION_API int VcSetProfileStreamingConfig(VizionCam* vizion_cam);
	VIZION_API int VcSetProfileControlConfig(VizionCam* vizion_cam);

	VIZION_API int VcUVC_AP1302_I2C_Read(VizionCam* vizion_cam, uint16_t addrReg, uint16_t& data);
	VIZION_API int VcUVC_AP1302_I2C_Write(VizionCam* vizion_cam, uint16_t addrReg, uint16_t data);
	VIZION_API int VcSetPropertyValue(VizionCam* vizion_cam, CAPTURE_PROPETIES prop_id, long value, int flag);
	VIZION_API int VcGetPropertyValue(VizionCam* vizion_cam, CAPTURE_PROPETIES prop_id, long& value, int& flag);
	VIZION_API int VcGetPropertyRange(VizionCam* vizion_cam, CAPTURE_PROPETIES prop_id, long& min, long& max, long& step, long& def, long& caps);

	VIZION_API int VcGotoProgramMode(VizionCam* vizion_cam);
	VIZION_API int VcStartFWDownload(FX3_FW_TARGET tgt, char* tatgetimg);

	VIZION_API int VcGetBootdataHeaderFromFile(VzHeader* header, const char* binfile);
}

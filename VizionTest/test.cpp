#include "pch.h"
#include <mfapi.h>
#include <mfplay.h>
#include <mfreadwrite.h>
#include <thread>
#include <mutex>
#include <shlwapi.h>
#include <opencv2/highgui.hpp>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

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

#include "../VizionSDK/USBView.h"
#include "../VizionSDK/VizionSDK.h"

using namespace ::testing;

class VizionCam_Stub : public VizionCam 
{

};

class VizionSDK : public Test {
public:
	int ret;
	VizionCam* cam;
};
TEST_F(VizionSDK, GetDigitalZoomType) {
	cam = new VizionCam();
	DZ_MODE_STATUS mode = DZ_MODE_STATUS::DZ_FAST;

	cam->Open(0);
	ret = cam->GetDigitalZoomType(mode);

	ASSERT_EQ(mode, DZ_MODE_STATUS::DZ_IMMEDIATE);
}

TEST_F(VizionSDK, SetDigitalZoomType) {
	cam = new VizionCam();
	DZ_MODE_STATUS mode = DZ_MODE_STATUS::DZ_FAST;

	cam->Open(0);
	ret = cam->SetDigitalZoomType(mode);
	ret = cam->GetDigitalZoomType(mode);

	ASSERT_EQ(mode, DZ_MODE_STATUS::DZ_FAST);
}

TEST_F(VizionSDK, GetDigitalZoomTarget) {
	cam = new VizionCam();
	double times = 0.0;

	cam->Open(0);
	ret = cam->GetDigitalZoomTarget(times);

	ASSERT_EQ(times, 1.0);
}

TEST_F(VizionSDK, SetDigitalZoomTarget) {
	cam = new VizionCam();
	double times = 4.0;

	cam->Open(0);
	ret = cam->SetDigitalZoomTarget(times);
	ret = cam->GetDigitalZoomTarget(times);

	ASSERT_EQ(times, 4.0);
}

TEST_F(VizionSDK, GetDeviceHardwareID) {
	cam = new VizionCam();
	
	wchar_t vid_pid[16];
	
	cam->Open(0);
	ret = cam->GetDeviceHardwareID(vid_pid);

	ASSERT_STREQ(vid_pid, L"3407:0821");
}

TEST_F(VizionSDK, OpenCamera) {
	cam = new VizionCam();
	ret = cam->Open(0);
	ASSERT_EQ(ret, 0) << "Open Camera Fail! Ret = " << ret;
}

TEST_F(VizionSDK, SetPropertyValue) {
	long value = -5;
	int flag = 0;
	cam = new VizionCam();
	cam->Open(0);
	ret = cam->SetPropertyValue(CAPTURE_PROPETIES::CAPTURE_EXPOSURE, value, flag);
	ASSERT_EQ(ret, 0) << "Set EXPOSURE Value is " << value << " Flag is " << flag;
}

TEST_F(VizionSDK, GetPropertyValue) {
	long value;
	int flag;
	cam = new VizionCam();
	cam->Open(0);

	ret = cam->GetPropertyValue(CAPTURE_PROPETIES::CAPTURE_EXPOSURE, value, flag);
	ASSERT_EQ(ret, 0) << "Get EXPOSURE Value is " << value << " Flag is " << flag;
	ret = cam->GetPropertyValue(CAPTURE_PROPETIES::CAPTURE_GAIN, value, flag);
	ASSERT_EQ(ret, 0) << "Get GAIN Value is " << value << " Flag is " << flag;
}

TEST_F(VizionSDK, GetPropertyRange_EXPOSURE) {
	long min, max, step, def, caps;
	cam = new VizionCam();
	cam->Open(0);

	ret = cam->GetPropertyRange(CAPTURE_PROPETIES::CAPTURE_EXPOSURE, min, max, step, def, caps);
	ASSERT_EQ(ret, 0);
	ASSERT_EQ(step, 1);
	ASSERT_EQ(caps, 1);
	ASSERT_EQ(def, -3);
	ASSERT_EQ(min, -13);
	ASSERT_EQ(max, 0);
}

TEST_F(VizionSDK, GetPropertyRange_WB) {
	long min, max, step, def, caps;
	cam = new VizionCam();
	cam->Open(0);

	ret = cam->GetPropertyRange(CAPTURE_PROPETIES::CAPTURE_WHITEBALANCE, min, max, step, def, caps);
	ASSERT_EQ(ret, 0);
	ASSERT_EQ(step, 100);
	ASSERT_EQ(caps, 1);
	ASSERT_EQ(def, 5000);
	ASSERT_EQ(min, 2300);
	ASSERT_EQ(max, 15000);
}
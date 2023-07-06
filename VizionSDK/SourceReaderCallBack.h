#pragma once
#include <iostream>
#include <mfreadwrite.h>
#include <shlwapi.h>
#include "VizionSDK.h"

class SourceReaderCB : public IMFSourceReaderCallback
{
public:
	SourceReaderCB(HANDLE hEvent) :
		m_nRefCount(1), m_hEvent(hEvent), m_bEOS(FALSE), m_hrStatus(S_OK)
	{
		InitializeCriticalSection(&m_critsec);
	}

	// IUnknown methods
	STDMETHODIMP QueryInterface(REFIID iid, void** ppv)
	{
		static const QITAB qit[] =
		{
			QITABENT(SourceReaderCB, IMFSourceReaderCallback),
			{ 0 },
		};
		return QISearch(this, qit, iid, ppv);
	}
	STDMETHODIMP_(ULONG) AddRef()
	{
		return InterlockedIncrement(&m_nRefCount);
	}
	STDMETHODIMP_(ULONG) Release()
	{
		ULONG uCount = InterlockedDecrement(&m_nRefCount);
		if (uCount == 0)
		{
			delete this;
		}
		return uCount;
	}

	// IMFSourceReaderCallback methods
	STDMETHODIMP OnReadSample(HRESULT hrStatus, DWORD dwStreamIndex,
		DWORD dwStreamFlags, LONGLONG llTimestamp, IMFSample* pSample);

	STDMETHODIMP OnEvent(DWORD, IMFMediaEvent*)
	{
		return S_OK;
	}

	STDMETHODIMP OnFlush(DWORD)
	{
		return S_OK;
	}

public:
	HRESULT Wait(DWORD dwMilliseconds, BOOL* pbEOS)
	{
		*pbEOS = FALSE;

		DWORD dwResult = WaitForSingleObject(m_hEvent, dwMilliseconds);
		if (dwResult == WAIT_TIMEOUT)
		{
			return E_PENDING;
		}
		else if (dwResult != WAIT_OBJECT_0)
		{
			return HRESULT_FROM_WIN32(GetLastError());
		}

		*pbEOS = m_bEOS;
		return m_hrStatus;
	}

	void SetImageInfo(VzFormat format)
	{
		sample_format = format;
		raw_size = sample_format.width * sample_format.height * 2;
	}

	int GetImageSample(uint8_t* img_data, int& size)
	{
		if (img_data == nullptr || sample_data == nullptr)
			return -1;

		if (raw_size <= 0)
			return -1;

		memcpy_s(img_data, raw_size, sample_data, raw_size);

		size = raw_size;

		if (sample_data != nullptr) {
			delete[] sample_data;
			sample_data = nullptr;
		}

		return 0;
	}

	int GetResultStatus();

private:

	// Destructor is private. Caller should call Release.
	virtual ~SourceReaderCB()
	{
	}

	void NotifyError(HRESULT hr)
	{
		wprintf(L"Source Reader error: 0x%X\n", hr);
	}

private:
	long                m_nRefCount;        // Reference count.
	CRITICAL_SECTION    m_critsec;
	HANDLE              m_hEvent;
	BOOL                m_bEOS;
	HRESULT             m_hrStatus;

	uint8_t* sample_data = nullptr;
	VzFormat sample_format;
	int raw_size = 0;
};

#include "pch.h"
#include "CoreAudioRender.h"
#include <stdio.h>
#include "CoreAudioDef.h"
#include <Functiondiscoverykeys_devpkey.h>

const CLSID CLSID_MMDeviceEnumerator = __uuidof(MMDeviceEnumerator);
const IID IID_IMMDeviceEnumerator = __uuidof(IMMDeviceEnumerator);
const IID IID_IAudioEndpointVolume = __uuidof(IAudioEndpointVolume);
const IID IID_IAudioSessionManager2 = __uuidof(IAudioSessionManager2);
const IID IID_IAudioSessionControl2 = __uuidof(IAudioSessionControl2);
const IID IID_ISimpleAudioVolume = __uuidof(ISimpleAudioVolume);
const IID IID_IAudioSessionControl = __uuidof(IAudioSessionControl);
const IID IID_IAudioSessionManager = __uuidof(IAudioSessionManager);

CoreAudioRender::CoreAudioRender() : _cRef(1)
{
	HRESULT hr_retrun;
	hr_retrun = CoInitializeEx(NULL, COINIT_MULTITHREADED);
	m_stopEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	m_audioDataSemphoe = CreateSemaphore(NULL, 0, 10000, NULL);
}
CoreAudioRender::~CoreAudioRender()
{
	if (pEnumerator != NULL)
	{
		pEnumerator->UnregisterEndpointNotificationCallback(this);
		pEnumerator->Release();
		pEnumerator = NULL;
	}
	CloseHandle(m_stopEvent);
	CloseHandle(m_audioDataSemphoe);
	if (pDevice)
		pDevice->Release();
	if (pAudioClient)
		pAudioClient->Release();
	if (pRenderClient)
		pRenderClient->Release();
	if (pwfx)
		CoTaskMemFree(pwfx);
}
bool CoreAudioRender::Init(int wBitsPerSample,int nChannels)
{
	HRESULT hr_retrun;
	//IMMDeviceEnumerator* pEnumerator = NULL;
	m_wBitsPerSample = wBitsPerSample;
	m_nChannels = nChannels;
	hr_retrun = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&pEnumerator);
	if (FAILED(hr_retrun))
	{
		printf("无法枚举音频设备：: %x\n", hr_retrun);
		return false;
	}

	hr_retrun = pEnumerator->RegisterEndpointNotificationCallback((IMMNotificationClient*)this);
	if (hr_retrun == S_OK)
	{
		printf("MMNotificationClient ok\n");
	}
	else
	{
		printf("MMNotificationClient error\n");
	}

	hr_retrun = pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice);
	if (pEnumerator != NULL)
	{
		/*pEnumerator->Release();
		pEnumerator = NULL;*/
	}
	hr_retrun = pDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&pAudioClient);
	if (FAILED(hr_retrun))
	{
		return false;
	}
	//hr_retrun = pDevice->Activate(IID_IAudioEndpointVolume, CLSCTX_ALL, NULL, (void**)&m_pRenderEndptVol);
	/*if (m_pRenderEndptVol)
		m_pRenderEndptVol->SetVolumeLevelScalar(0.5, &GUID_NULL);*/

		//合成器界面
	//CComPtr<IAudioSessionManager> pSessionManager = NULL;
	//CComPtr<ISimpleAudioVolume> pCurrSimVol;
	//hr_retrun = pDevice->Activate(IID_IAudioSessionManager, CLSCTX_INPROC_SERVER, NULL, (void**)(&pSessionManager));
	//if (FAILED(hr_retrun))
	//{
	//	return false;
	//}
	//hr_retrun = pSessionManager->GetSimpleAudioVolume(NULL, FALSE, &pCurrSimVol);

	//hr_retrun = pCurrSimVol->SetMasterVolume(0.8, NULL);

	/////////控制单个进程音量
	//CComPtr<ISimpleAudioVolume> pSimVol;
	//CComPtr<IAudioSessionManager2> pSessMag;
	//CComPtr<IAudioSessionEnumerator> pSessEnum;
	//hr_retrun = pDevice->Activate(IID_IAudioSessionManager2, CLSCTX_ALL, NULL, (LPVOID*)&pSessMag);
	//hr_retrun = pSessMag->GetSessionEnumerator(&pSessEnum);
	//int nCount;
	//hr_retrun = pSessEnum->GetCount(&nCount);

	//CComPtr<IAudioSessionControl> pSessCtrl;
	//CComPtr<IAudioSessionControl2> pSessCtrl2;
	//DWORD pid;
	//float vol;
	//for (int i = 0; i < nCount; i++)
	//{
	//	pSessCtrl = NULL;
	//	pSessCtrl2 = NULL;
	//	pSimVol = NULL;
	//	if (i == nCount-1)
	//	{
	//		hr_retrun = pSessEnum->GetSession(i, &pSessCtrl);
	//		hr_retrun = pSessCtrl->QueryInterface(IID_IAudioSessionControl2, (LPVOID*)&pSessCtrl2);
	//		hr_retrun = pSessCtrl2->GetProcessId(&pid);
	//		hr_retrun = pSessCtrl2->QueryInterface(IID_ISimpleAudioVolume, (LPVOID*)&pSimVol);
	//		hr_retrun = pSimVol->GetMasterVolume(&vol);
	//		printf("Pid:%d vol:%d%%\n", pid, int((vol + 0.005) * 100));
	//		printf("currid = %d \n", GetCurrentProcessId());
	//		//设置所有程序音量为最大
	//		hr_retrun = pSimVol->SetMasterVolume(0.6, NULL);
	//	}
	//}

	hr_retrun = pAudioClient->GetMixFormat(&pwfx);
	if (FAILED(hr_retrun))
	{
		printf("XXX: %x\n", hr_retrun);
		return false;
	}
	AdjustFormatTo16Bits(pwfx);
	//int hnsRequestedDuration = 10000000;
	hr_retrun = pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM, REFTIMES_PER_SEC, 0, pwfx, NULL);  // pwfx

	UINT32         bufferFrameCount;
	hr_retrun = pAudioClient->GetBufferSize(&bufferFrameCount);
	m_bufferFrameCount = bufferFrameCount;
	//HANDLE hAudioSamplesReadyEvent = CreateEventEx(NULL, NULL, 0, EVENT_MODIFY_STATE | SYNCHRONIZE);
	//if (hAudioSamplesReadyEvent == NULL)
	//{
	//	printf("Unable to create samples ready event: %d.\n", GetLastError());
	//}
	//hr_retrun = pAudioClient->SetEventHandle(hAudioSamplesReadyEvent);
	hr_retrun = pAudioClient->GetService(__uuidof(IAudioRenderClient), (void**)&pRenderClient);

	hnsActualDuration = (double)REFTIMES_PER_SEC * bufferFrameCount / pwfx->nSamplesPerSec;

	return true;
}

BOOL CoreAudioRender::AdjustFormatTo16Bits(WAVEFORMATEX* pwfx)
{
	BOOL bRet(FALSE);
	if (pwfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT)
	{
		//WAVE_FORMAT_IEEE_FLOAT
		if (m_wBitsPerSample == 32)
		{
			pwfx->wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
			pwfx->wBitsPerSample = 32;
			//pwfx->nChannels = 1;
		}
		else
		{
			pwfx->wFormatTag = WAVE_FORMAT_PCM;
			pwfx->wBitsPerSample = 16;
		}
		pwfx->nChannels = m_nChannels;
		pwfx->nSamplesPerSec = 48000;
		pwfx->nBlockAlign = pwfx->nChannels * pwfx->wBitsPerSample / 8;
		pwfx->nAvgBytesPerSec = pwfx->nBlockAlign * pwfx->nSamplesPerSec;
		bRet = TRUE;
	}
	else if (pwfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
	{
		PWAVEFORMATEXTENSIBLE pEx = reinterpret_cast<PWAVEFORMATEXTENSIBLE>(pwfx);
		if (IsEqualGUID(KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, pEx->SubFormat))
		{
			if (m_wBitsPerSample == 32)
			{
			//	pwfx->wFormatTag = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
				pEx->Samples.wValidBitsPerSample  = 32;
				pwfx->wBitsPerSample = 32;
				pwfx->nChannels = 1;
			}
			else
			{
				pEx->SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
				pEx->Samples.wValidBitsPerSample = 16;
				pwfx->wBitsPerSample = 16;
			}
			pwfx->nChannels = m_nChannels;
			pwfx->nSamplesPerSec = 48000;
			pwfx->nBlockAlign = pwfx->nChannels * pwfx->wBitsPerSample / 8;
			pwfx->nAvgBytesPerSec = pwfx->nBlockAlign * pwfx->nSamplesPerSec;
			bRet = TRUE;
		}
	}
	return bRet;
}

bool CoreAudioRender::Start()
{
	HRESULT hr_retrun;
	hr_retrun = pAudioClient->Start();
	if (FAILED(hr_retrun))
	{
		return false;
	}
	m_bStop = false;
	m_renderThread = std::thread(&CoreAudioRender::RenderPcmFun, this);
	return true;
}

void CoreAudioRender::Stop()
{
	m_bStop = true;
	if (m_renderThread.joinable())
		m_renderThread.join();
	
	pAudioClient->Stop();
}

void CoreAudioRender::RenderPcm(unsigned char* pData, int len)
{
	static long long tmpTime = GetTickCount();
	ST_AudioData  audioData;
//	bool bDelete = true;
	audioData.len = len;
	audioData.index = 0;
	//audioData.pData = new unsigned char[len];
	//memcpy_s(audioData.pData, pData, len);
	if (len > 0)
	{
		m_audioMutex.lock();
		
		if (m_audioList.size() > 2 && GetTickCount()-tmpTime > 3000)
		{
			tmpTime = GetTickCount();
			printf("render size = %d, len = %d \n", m_audioList.size(), len);
		}
		if (m_audioList.size() < 10)
		{
			audioData.pData = new unsigned char[len];
			memcpy_s(audioData.pData,len, pData, len);
			m_audioList.push_back(audioData);
			ReleaseSemaphore(m_audioDataSemphoe, 1, NULL);
		}
		
		m_audioMutex.unlock();
	//	if (!bDelete)
		{
	//		ReleaseSemaphore(m_audioDataSemphoe, 1, NULL);
		}
	}
}

void CoreAudioRender::RenderPcmFun()
{
	int numFrame = 0;
	bool bRender = true;
	UINT32 numFramesPadding;
	DWORD flags = 0;
	HRESULT hr_retrun;
	BYTE* pData;
	ST_AudioData  audioData;
	audioData.pData = nullptr;
	int copySize = 0;
	while (!m_bStop)
	{
		//tian 10->20
		if(WaitForSingleObject(m_audioDataSemphoe,10) != WAIT_TIMEOUT)
		{
			if (m_audioList.size() < 1)
				continue;
			hr_retrun = pAudioClient->GetCurrentPadding(&numFramesPadding);
			if (hr_retrun != S_OK)
				break;

			if (m_bufferFrameCount - numFramesPadding < 10) // tianyw m_bufferFrameCount pwfx->nSamplesPerSec
				Sleep(10); // tianyw 1000->10
			hr_retrun = pAudioClient->GetCurrentPadding(&numFramesPadding);
			if (hr_retrun != S_OK)
				break;
			m_audioMutex.lock();
			audioData = m_audioList.front();
			m_audioMutex.unlock();
			numFrame = audioData.len / pwfx->nBlockAlign;
			copySize = min(numFrame, m_bufferFrameCount - numFramesPadding); // tianyw m_bufferFrameCount pwfx->nSamplesPerSec
			
			if (copySize < numFrame)
			{
				hr_retrun = pRenderClient->GetBuffer(copySize, &pData);
				if (hr_retrun != S_OK)
					break;
				if(audioData.pData)
					memcpy_s(pData, m_bufferFrameCount, audioData.pData+ audioData.index, copySize * pwfx->nBlockAlign);
				hr_retrun = pRenderClient->ReleaseBuffer(copySize, flags);
				audioData.len -= copySize * pwfx->nBlockAlign;
				audioData.index += copySize * pwfx->nBlockAlign;
				ReleaseSemaphore(m_audioDataSemphoe, 1, NULL);
			//	Sleep(10);
				
			}
			else if (m_bufferFrameCount - numFramesPadding >= numFrame) // tianyw m_bufferFrameCount pwfx->nSamplesPerSec
			{
				hr_retrun = pRenderClient->GetBuffer(numFrame, &pData);
				if (hr_retrun != S_OK)
					break;
				if (audioData.pData)
					memcpy_s(pData, m_bufferFrameCount, audioData.pData + audioData.index, audioData.len);     // tianyw  audioData.pData
				hr_retrun = pRenderClient->ReleaseBuffer(numFrame, flags);
				m_audioMutex.lock();
				m_audioList.pop_front();
				m_audioMutex.unlock();
				if (audioData.pData)
					delete []audioData.pData;
				audioData.pData = nullptr;
			}
			//Sleep(1);
		}
	}
	m_audioMutex.lock();
	while (!m_audioList.empty())
	{
		ST_AudioData audioData = m_audioList.front();
		m_audioList.pop_front();
		if (audioData.pData)
			delete []audioData.pData;
	}
	m_audioMutex.unlock();
}

BOOL CoreAudioRender::ResetDevice()
{
	HRESULT hr_retrun;
	if (pDevice)
	{
		pDevice->Release();
	}
	hr_retrun = pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice);
	if (FAILED(hr_retrun))
	{
		return FALSE;
	}
	if (pAudioClient)
	{
		pAudioClient->Release();
	}
	hr_retrun = pDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&pAudioClient);
	if (FAILED(hr_retrun))
	{
		return FALSE;
	}

	hr_retrun = pAudioClient->GetMixFormat(&pwfx);
	if (FAILED(hr_retrun))
	{
		printf("XXX: %x\n", hr_retrun);
		return FALSE;
	}
	AdjustFormatTo16Bits(pwfx);
	hr_retrun = pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM, REFTIMES_PER_SEC, 0, pwfx, NULL);  // pwfx

	UINT32         bufferFrameCount;
	hr_retrun = pAudioClient->GetBufferSize(&bufferFrameCount);
	m_bufferFrameCount = bufferFrameCount;
	hr_retrun = pAudioClient->GetService(__uuidof(IAudioRenderClient), (void**)&pRenderClient);
	hnsActualDuration = (double)REFTIMES_PER_SEC * bufferFrameCount / pwfx->nSamplesPerSec;
	return TRUE;
}

HRESULT STDMETHODCALLTYPE CoreAudioRender::OnDefaultDeviceChanged(
	EDataFlow flow, ERole role,
	LPCWSTR pwstrDeviceId)
{
	std::string pszFlow;
	std::string pszRole;

	//_PrintDeviceName(pwstrDeviceId);

	switch (flow)
	{
	case eRender:
		pszFlow = "eRender";
		break;
	case eCapture:
		pszFlow = "eCapture";
		break;
	}

	switch (role)
	{
	case eConsole:
		pszRole = "eConsole";
		break;
	case eMultimedia:
		pszRole = "eMultimedia";
		break;
	case eCommunications:
		pszRole = "eCommunications";
		break;
	}
//	if (m_dwDeviceNewState == DEVICE_STATE_UNPLUGGED)
	{
		if (flow == eRender && role == eConsole)
		{
			Stop();
			if (ResetDevice())
			{
				Start();
			}
		}
	}

	printf("  -->New default device: flow = %s, role = %s\n",
		pszFlow.c_str(), pszRole.c_str());
	return S_OK;
}

HRESULT STDMETHODCALLTYPE CoreAudioRender::OnDeviceAdded(LPCWSTR pwstrDeviceId)
{
	//_PrintDeviceName(pwstrDeviceId);

	printf("  -->Added device\n");
	return S_OK;
};

HRESULT STDMETHODCALLTYPE CoreAudioRender::OnDeviceRemoved(LPCWSTR pwstrDeviceId)
{
	//_PrintDeviceName(pwstrDeviceId);

	printf("  -->Removed device\n");
	return S_OK;
}

HRESULT STDMETHODCALLTYPE CoreAudioRender::OnDeviceStateChanged(
	LPCWSTR pwstrDeviceId,
	DWORD dwNewState)
{
	std::string pszState = "?????";
//	_PrintDeviceName(pwstrDeviceId);
	switch (dwNewState)
	{
	case DEVICE_STATE_ACTIVE:
		pszState = "ACTIVE";
		break;
	case DEVICE_STATE_DISABLED:
		pszState = "DISABLED";
		break;
	case DEVICE_STATE_NOTPRESENT:
		pszState = "NOTPRESENT";
		/*m_bStop = true;
		if (pAudioClient)
		{
			pAudioClient->Stop();
		}*/
		break;
	case DEVICE_STATE_UNPLUGGED:
		
		pszState = "UNPLUGGED";
		break;
	}
	
	m_dwDeviceNewState = dwNewState;
	printf("  -->New device state is DEVICE_STATE_%s (0x%8.8x)\n",
		pszState.c_str(), dwNewState);

	return S_OK;
}

HRESULT STDMETHODCALLTYPE CoreAudioRender::OnPropertyValueChanged(
	LPCWSTR pwstrDeviceId,
	const PROPERTYKEY key)
{
	//_PrintDeviceName(pwstrDeviceId);

	//printf("  -->Changed device property "
	//	"{%8.8x-%4.4x-%4.4x-%2.2x%2.2x-%2.2x%2.2x%2.2x%2.2x%2.2x%2.2x}#%d\n",
	//	key.fmtid.Data1, key.fmtid.Data2, key.fmtid.Data3,
	//	key.fmtid.Data4[0], key.fmtid.Data4[1],
	//	key.fmtid.Data4[2], key.fmtid.Data4[3],
	//	key.fmtid.Data4[4], key.fmtid.Data4[5],
	//	key.fmtid.Data4[6], key.fmtid.Data4[7],
	//	key.pid);
	return S_OK;
}

HRESULT CoreAudioRender::_PrintDeviceName(LPCWSTR pwstrId)
{
	HRESULT hr = S_OK;
	IMMDevice* pDevice = NULL;
	IPropertyStore* pProps = NULL;
	PROPVARIANT varString;

	//CoInitialize(NULL);
	PropVariantInit(&varString);

	//if (_pEnumerator == NULL)
	//{
	//	// Get enumerator for audio endpoint devices.
	//	hr = CoCreateInstance(__uuidof(MMDeviceEnumerator),
	//		NULL, CLSCTX_INPROC_SERVER,
	//		__uuidof(IMMDeviceEnumerator),
	//		(void**)&_pEnumerator);
	//}
	if (pEnumerator)
	{
		hr = pEnumerator->GetDevice(pwstrId, &pDevice);
	}
	if (hr == S_OK)
	{
		hr = pDevice->OpenPropertyStore(STGM_READ, &pProps);
	}
	if (hr == S_OK)
	{
		// Get the endpoint device's friendly-name property.
		hr = pProps->GetValue(PKEY_Device_FriendlyName, &varString);
	}
	printf("----------------------\nDevice name: \"%S\"\n"
		"  Endpoint ID string: \"%S\"\n",
		(hr == S_OK) ? varString.pwszVal : L"null device",
		(pwstrId != NULL) ? pwstrId : L"null ID");

	PropVariantClear(&varString);

	if (pProps)
	{
		pProps->Release();
		pProps = NULL;
	}
	if (pDevice)
	{
		pDevice->Release();
		pDevice = NULL;
	}
	//	CoUninitialize();
	return hr;
}


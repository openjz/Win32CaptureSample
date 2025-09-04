#pragma once
#include <MMDeviceAPI.h>
#include <AudioClient.h>
#include <endpointvolume.h>

#include <atlcomcli.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <audioclient.h>
#include <audiopolicy.h>

#include <atlbase.h>
#include <list>
#include <mutex>
#include <thread>
#define REFTIMES_PER_SEC  10000000
#define REFTIMES_PER_MILLISEC 10000

#pragma comment(lib,"Propsys.lib")
struct ST_AudioData;
class CoreAudioRender : public IMMNotificationClient
{
public:
	CoreAudioRender();
	~CoreAudioRender();
	bool Init(int wBitsPerSample,int nChannels);
	bool Start();
	void Stop();
	void RenderPcm(unsigned char *pData,int len);
private:
    DWORD m_dwDeviceNewState = 0;
	LONG _cRef;
	HRESULT _PrintDeviceName(LPCWSTR  pwstrId);
    ULONG STDMETHODCALLTYPE AddRef()
    {
        return InterlockedIncrement(&_cRef);
    }

    ULONG STDMETHODCALLTYPE Release()
    {
        ULONG ulRef = InterlockedDecrement(&_cRef);
        if (0 == ulRef)
        {
            delete this;
        }
        return ulRef;
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(
        REFIID riid, VOID** ppvInterface)
    {
        if (IID_IUnknown == riid)
        {
            AddRef();
            *ppvInterface = (IUnknown*)this;
        }
        else if (__uuidof(IMMNotificationClient) == riid)
        {
            AddRef();
            *ppvInterface = (IMMNotificationClient*)this;
        }
        else
        {
            *ppvInterface = NULL;
            return E_NOINTERFACE;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(
        EDataFlow flow, ERole role,
        LPCWSTR pwstrDeviceId);
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR pwstrDeviceId);
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR pwstrDeviceId);
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(
        LPCWSTR pwstrDeviceId,
        DWORD dwNewState);
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(
        LPCWSTR pwstrDeviceId,
        const PROPERTYKEY key);
private:
	BOOL AdjustFormatTo16Bits(WAVEFORMATEX* pwfx);
	void RenderPcmFun();
    BOOL ResetDevice();
private:
    IMMDeviceEnumerator* pEnumerator = NULL;
	CComPtr<IAudioEndpointVolume> m_pRenderEndptVol;
	IMMDevice* pDevice = NULL;
	IAudioClient* pAudioClient = NULL;
	WAVEFORMATEX* pwfx = NULL;
	IAudioRenderClient* pRenderClient = NULL;
	REFERENCE_TIME hnsActualDuration;
	UINT32         m_bufferFrameCount = 0;
	std::list< ST_AudioData> m_audioList;
	std::mutex   m_audioMutex;
	std::thread m_renderThread;

	int  m_wBitsPerSample;
	int  m_nChannels;

	bool        m_bStop = false;
	HANDLE      m_stopEvent;
	HANDLE  m_audioDataSemphoe = NULL;
};


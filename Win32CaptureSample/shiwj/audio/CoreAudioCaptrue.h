#pragma once
#include "CoreAudioInterface.h"
#include <thread>
#include <atomic>
#include <list>
#include <mutex>
#include "Audio_opus_encoder.h"
#include "Semaphore.h"
#include "Audio_aac_encoder.h"
//#include "CoreAudioDef.h"
//struct ST_AudioData
//{
//	int len;
//	unsigned char* pData;
//};
struct ST_AudioData;
class CoreAudioCaptrue : public CoreAudioCaptrueInterface
{
public:
	CoreAudioCaptrue();
	~CoreAudioCaptrue();

	virtual bool Init(EM_CAPTRUE capType, std::function<void(unsigned char*, int, bool, void*)> callback, void* pArg, bool isEncoder = true, EM_ENCODE_TYPE encodeType = AUDIOENCODE_OPUS);
	virtual void SetCallbackEx(std::function<void(unsigned char*, int, bool,void*, void*)> callback);
	virtual bool Start();
	virtual void Stop();
private:
	void CaptrueFun();
	void EncoderFun();
	void UninitCaptrue();
	BOOL AdjustFormatTo16Bits(WAVEFORMATEX* pwfx);
private:
	int hnsRequestedDuration = 10000000;
	IAudioCaptureClient* pCaptureClient = nullptr;
	IMMDeviceEnumerator* pEnumerator = nullptr;
	IMMDevice* pDevice = nullptr;
	IAudioClient* pAudioClient = nullptr;
	HANDLE hAudioSamplesReadyEvent = NULL;
	HANDLE hStopEvent = NULL;

	std::thread m_captrueThread;
	std::thread m_encoderThread;
	BYTE *pbyCaptureBuffer=nullptr;
	//int   bufferSize = 0;
	std::function<void(unsigned char*, int, bool, void*)>  m_callBack;
	void* m_pArg = nullptr;
	WAVEFORMATEX  *m_waveFormatex=nullptr;
	//std::atomic<bool>  m_stoped = false;
	Audio_opus_encoder* m_opusEncoder = nullptr;
	ST_AudioFormat      m_audioForat;
	bool                m_isEncoder = false;
	unsigned char       m_encoderData[640];
	std::list<ST_AudioData> m_audioList;
	std::mutex              m_audiolistMutex;
	int                     m_captrueBufLen = 0;
	//MySemaphore            m_audioDataSemaph;
	HANDLE                   m_audioSemaphore = NULL;

	std::function<void(unsigned char*, int, bool,void*, void*)>  m_callBackEx;

private:
	std::shared_ptr< Audio_aac_encoder>  m_aacEndoer = nullptr;
	EM_ENCODE_TYPE m_audioEncodeType = AUDIOENCODE_AAC;
};


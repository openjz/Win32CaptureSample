#pragma once
#ifdef WINCOREAUDIO_EXPORTS
#define WINCOREAUDIO_API __declspec(dllexport)
#else
#define WINCOREAUDIO_API __declspec(dllimport)
#endif
#include <MMDeviceAPI.h>
#include <AudioClient.h>
#include <functional>
#include <memory>
enum EM_ENCODE_TYPE
{
	AUDIOENCODE_OPUS,
	AUDIOENCODE_AAC
};
class CoreAudioCaptrueInterface
{
public:
	enum EM_CAPTRUE
	{
		AUDIOCAPTURE_MIC,
		AUDIOCAPTURE_SYSTEM,
	};
public:
	virtual bool Init(EM_CAPTRUE capType, std::function<void(unsigned char*, int, bool, void*)> callback,void *pArg,bool isEncoder=true, EM_ENCODE_TYPE encodeType= AUDIOENCODE_OPUS) = 0;
	virtual bool Start()=0;
	virtual void Stop() = 0;
	virtual void SetCallbackEx(std::function<void(unsigned char*, int, bool, void*, void*)> callback) = 0;
};

class CoreAudioDecoderInterface
{
public:
	struct ST_OPUS_FORMAT
	{
		WORD        nChannels;
		DWORD       nSamplesPerSec;
		WORD        wBitsPerSample;
	};
public:
	virtual bool Init(ST_OPUS_FORMAT format, bool isRender = true, std::function<void(unsigned char*, int, void*)> callback = nullptr, void* pArg = nullptr)=0;
	virtual bool Decoder(unsigned char* pData, int len)=0;
	virtual bool Start()=0;
	virtual void Stop()=0;
};

WINCOREAUDIO_API std::shared_ptr<CoreAudioCaptrueInterface> CreateAudioCaptrue();
WINCOREAUDIO_API std::shared_ptr<CoreAudioDecoderInterface> CreateAudioDecoder();
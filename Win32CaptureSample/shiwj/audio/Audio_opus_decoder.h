#pragma once
#include <functional>
#include "opus.h"
#include "CoreAudioInterface.h"
#include <list>
#include <mutex>
#include "CoreAudioRender.h"
struct ST_AudioData;
#define opus_frame_size		480
class Audio_opus_decoder : public CoreAudioDecoderInterface
{
public:
	Audio_opus_decoder();
	~Audio_opus_decoder();
	bool Init(ST_OPUS_FORMAT format,bool isRender=true, std::function<void(unsigned char*, int,void*)> callback=nullptr, void* pArg=nullptr);
	bool Decoder(unsigned char *pData, int len);
	bool Start();
	void Stop();
	void DecodeFun();
private:
	std::function<void(unsigned char*, int, void*)> m_callback = nullptr;
	bool m_bIsRender = true;
	void* m_pArg = nullptr;
	OpusDecoder* decoder = NULL;
	int   m_nChannels = 0;
	int   m_nSamplessPerSec = 0;
	int   wBitsPerSample = 0;
	opus_int16  *m_audioPcm=nullptr;  //tianyw 16->32
	float* m_audioFloatPcm = nullptr;
	std::list< ST_AudioData>  m_audioList;
	std::mutex      m_audioMutex;

	HANDLE  m_stopEvent = NULL;
	HANDLE  m_audioDataSemphoe=NULL;
	std::shared_ptr< CoreAudioRender>  m_audioRender = nullptr;

	std::thread decoderThread;
};


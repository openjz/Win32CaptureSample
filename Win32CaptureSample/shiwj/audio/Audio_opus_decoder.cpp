#include "pch.h"
#include "Audio_opus_decoder.h"
#include "CoreAudioDef.h"
Audio_opus_decoder::Audio_opus_decoder()
{
	m_stopEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	m_audioDataSemphoe = CreateSemaphore(NULL, 0, 100000, NULL);
}
Audio_opus_decoder::~Audio_opus_decoder()
{
	if (m_stopEvent)
		CloseHandle(m_stopEvent);
	if (m_audioDataSemphoe)
		CloseHandle(m_audioDataSemphoe);
	if(decoder)
		opus_decoder_destroy(decoder);
	if (m_audioPcm)
		delete[] m_audioPcm;
	if (m_audioFloatPcm)
		delete[] m_audioFloatPcm;
	m_audioRender = nullptr;
}
bool Audio_opus_decoder::Init(ST_OPUS_FORMAT format, bool isRender, std::function<void(unsigned char*, int, void*)> callback, void* pArg)
{
	m_nChannels = format.nChannels;
	m_nSamplessPerSec = format.nSamplesPerSec;
	wBitsPerSample = format.wBitsPerSample;
	m_callback = callback;
	m_bIsRender = isRender;
	m_pArg = pArg;
	int error = 0;
	decoder = opus_decoder_create(m_nSamplessPerSec, m_nChannels, &error);
	if (error > 0) {
		return false;
	}
	if (m_audioPcm)
		delete[] m_audioPcm;            //tianyw 16->32
	m_audioPcm = new opus_int16[m_nChannels * m_nSamplessPerSec / 100 * 2];  //test tian * 2 1121

	if (m_audioFloatPcm)
		delete[] m_audioFloatPcm;            //tianyw 16->32
	m_audioFloatPcm = new float[m_nChannels * m_nSamplessPerSec / 50 * 3];

	if (m_bIsRender)
	{
		m_audioRender = std::make_shared<CoreAudioRender>();
		m_audioRender->Init(wBitsPerSample, m_nChannels);
	}
	return true;
}

bool Audio_opus_decoder::Decoder(unsigned char* pData, int len)
{
	static long long tmpTime = GetTickCount();
	ST_AudioData audioData;
	audioData.pData = nullptr;
	audioData.len = len;
	audioData.pData = new unsigned char[len];
	if (pData)
	{
		memcpy_s(audioData.pData, len, pData, len);
		m_audioMutex.lock();
		m_audioList.push_back(audioData);
		if (len != 248)
		{
			//printf("audio size = %d \n", len);
		}
		if (m_audioList.size() > 2 && GetTickCount() - tmpTime > 3000)
		{
			printf("audio Decoder size = %d \n", m_audioList.size());
			tmpTime = GetTickCount();
		}
		m_audioMutex.unlock();
		ReleaseSemaphore(m_audioDataSemphoe, 1, NULL);
	}
	return true;
}

bool Audio_opus_decoder::Start()
{
	ResetEvent(m_stopEvent);
	if(m_bIsRender)
		m_audioRender->Start();
	decoderThread = std::thread(&Audio_opus_decoder::DecodeFun, this);
	return true;
}

void Audio_opus_decoder::Stop()
{
	SetEvent(m_stopEvent);
	if(decoderThread.joinable())
		decoderThread.join();
	m_audioRender->Stop();
}

void Audio_opus_decoder::DecodeFun()
{
	HANDLE waitArray[2];
	waitArray[0] = m_audioDataSemphoe;
	waitArray[1] = m_stopEvent;
	int output_samples = 0;
	DWORD waitResult;
	bool bDecoder = true;
	while (bDecoder)
	{
		waitResult = WaitForMultipleObjects(2, waitArray, FALSE, INFINITE);
		switch (waitResult)
		{
		case WAIT_OBJECT_0:
		{
			m_audioMutex.lock();
			ST_AudioData audioData = m_audioList.front();
			m_audioList.pop_front();
			m_audioMutex.unlock();

			if (wBitsPerSample == 32)
			{
				output_samples = opus_decode_float(decoder, audioData.pData, audioData.len,m_audioFloatPcm, m_nSamplessPerSec/50, 0);
			}
			else
			{
				output_samples = opus_decode(decoder, audioData.pData, audioData.len, m_audioPcm, m_nSamplessPerSec / 100 * 2, 0);  // tianyw  * 4 1121
			}
			if (output_samples > 0)
			{
				if (m_bIsRender)
				{
					if (wBitsPerSample == 32)
					{
						m_audioRender->RenderPcm((unsigned char*)m_audioFloatPcm, output_samples * 4);
					}
					else
					{
						m_audioRender->RenderPcm((unsigned char*)m_audioPcm, output_samples * 4);
					}
				}
				else if (m_callback)
				{
					if(wBitsPerSample == 32)
						m_callback((unsigned char*)m_audioFloatPcm, output_samples * 4, NULL);
					else
						m_callback((unsigned char*)m_audioPcm, output_samples * 4, NULL);
				}
			}
			delete  []audioData.pData;
		}
		break;
		case WAIT_OBJECT_0+1:
			bDecoder = false;
			break;
		}
	}

	while (!m_audioList.empty())
	{
		ST_AudioData audioData = m_audioList.front();
		
		delete []audioData.pData;
		m_audioList.pop_front();
	}
}
WINCOREAUDIO_API std::shared_ptr<CoreAudioDecoderInterface> CreateAudioDecoder()
{
	return std::make_shared<Audio_opus_decoder>();
}
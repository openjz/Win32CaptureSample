#include "pch.h"
#include <malloc.h>
#include "Audio_aac_encoder.h"
#include <stdio.h>

Audio_aac_encoder::Audio_aac_encoder()
{
	
	m_pipeline.audioCapBuffer = new MFRingBuffer(200);
	m_pipeline.audioEncBuffer = new MFRingBuffer(200);
}

Audio_aac_encoder::~Audio_aac_encoder()
{
	if (m_pipeline.audioCapBuffer)
	{
		delete m_pipeline.audioCapBuffer;
		m_pipeline.audioCapBuffer = nullptr;
	}
	if (m_pipeline.audioEncBuffer)
	{
		delete m_pipeline.audioEncBuffer;
		m_pipeline.audioEncBuffer = nullptr;
	}

	UnInit();
}

bool Audio_aac_encoder::Init(AAC_AudioFormat audioformat)
{
	UnInit();
	VFAudioMediaType audioParam;
	audioParam.Channels = audioformat.nChannels;
	audioParam.SampleRate = audioformat.nSamplesPerSec;
	audioParam.BPS = 16;
	m_aacEncoder = new MFMSAACEncoder(&m_pipeline, audioParam, 128);
	//m_aacEncoder->Init();
	m_aacEncoder->Start();
	/*InitParam initParam;
	initParam.u32AudioSamplerate = audioformat.nSamplesPerSec;
	initParam.ucAudioChannel = audioformat.nChannels;
	initParam.u32PCMBitSize = audioformat.nBitSize;
	initParam.ucAudioCodec = Law_PCM16;
	m_aacEncoder = Easy_AACEncoder_Init(initParam);*/
	return true;
}

bool Audio_aac_encoder::Encoder(unsigned char* pPcmData, int frameSize, unsigned char* pOutData, int& outLen)
{
	if (m_aacEncoder)
	{
		return m_aacEncoder->Encode(pPcmData, frameSize, pOutData, outLen);
	}
	//if (m_aacEncoder)
	//{
	//	
	//	if (Easy_AACEncoder_Encode(m_aacEncoder, pPcmData, frameSize, pOutData, (unsigned int*)&outLen)<=0)
	//	{
	//		//printf("Easy_AACEncoder_Encode failed.\n");
	//		return false;
	//	}
	//	return true;
	//}
	return false;
}

void Audio_aac_encoder::UnInit()
{
	if (m_aacEncoder)
	{
		m_aacEncoder->Stop();
		m_aacEncoder->Join();
		delete m_aacEncoder;
		m_aacEncoder = nullptr;
	}
	/*if (m_aacEncoder)
	{
		Easy_AACEncoder_Release(m_aacEncoder);
		m_aacEncoder = nullptr;
	}*/
}
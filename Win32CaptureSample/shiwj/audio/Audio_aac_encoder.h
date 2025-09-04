#pragma once
#include <stdint.h>
//#include "EasyAACEncoderAPI.h"
#include "MFMSAACEncoder.h"
struct AAC_AudioFormat
{
	uint32_t        nChannels;          
	uint32_t       nSamplesPerSec;
	uint32_t       nBitSize;   
};
class Audio_aac_encoder
{
public:
	Audio_aac_encoder();
	~Audio_aac_encoder();
	bool Init(AAC_AudioFormat audioformat);
	bool Encoder(unsigned char *pPcmData,int frameSize,unsigned char *pOutData,int &outLen);
	void UnInit();
private:
	//Easy_Handle m_aacEncoder = nullptr;
	MFMSAACEncoder* m_aacEncoder = nullptr;
	MFPipeline m_pipeline;
};


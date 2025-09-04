#pragma once
#include "opus.h"
struct ST_AudioFormat
{
	WORD        nChannels;          
	DWORD       nSamplesPerSec;     
	DWORD       nAvgBytesPerSec;  
	WORD        nBlockAlign;        
	WORD        wBitsPerSample;    
};
class Audio_opus_encoder
{
public:
	Audio_opus_encoder();
	~Audio_opus_encoder();
	bool Init(ST_AudioFormat audioformat);
	void Encoder(unsigned char *pPcmData,int frameSize,unsigned char *pOpusData,int &opusLen);
private:
	OpusEncoder* m_pOpusEncoder = NULL;
	short*       m_inbuf = NULL;
	unsigned char* m_opusBuf = NULL;
};


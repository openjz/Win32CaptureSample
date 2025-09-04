#include "pch.h"
#include <malloc.h>
#include "Audio_opus_encoder.h"

#define FRAME_SIZE 480
#define BITRATE 48000
Audio_opus_encoder::Audio_opus_encoder()
{

}
Audio_opus_encoder::~Audio_opus_encoder()
{

}

bool Audio_opus_encoder::Init(ST_AudioFormat audioformat)
{
	int rc, err;
	//audioformat.nChannels
	m_pOpusEncoder = opus_encoder_create(audioformat.nSamplesPerSec, audioformat.nChannels, OPUS_APPLICATION_AUDIO, &err);
	if (err != OPUS_OK || m_pOpusEncoder == NULL)
		return false;
	err = opus_encoder_ctl(m_pOpusEncoder, OPUS_SET_VBR(0));
	err = opus_encoder_ctl(m_pOpusEncoder, OPUS_SET_BITRATE(OPUS_AUTO));
	opus_encoder_ctl(m_pOpusEncoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_MUSIC));
	opus_encoder_ctl(m_pOpusEncoder, OPUS_SET_INBAND_FEC(0));
	if (err < 0)
	{
		//fprintf(stderr, "failed to set bitrate: %s\n", opus_strerror(err));
		return false;
	}
	err = opus_encoder_ctl(m_pOpusEncoder, OPUS_SET_COMPLEXITY(8));
	if (err < 0) {
		//fprintf(stderr, "failed to OPUS_SET_COMPLEXITY: %s\n", opus_strerror(err));
		return false;
	}

	//err = opus_encoder_ctl(m_pOpusEncoder, OPUS_SET_LSB_DEPTH(16));
	//OPUS_SET_LSB_DEPTH()
	
	m_inbuf = (short*)malloc(sizeof(short) * FRAME_SIZE * 2 );  //tianyw 16 short-> __int32 32
	m_opusBuf = (unsigned char*)malloc(FRAME_SIZE);
	return true;
}

void Audio_opus_encoder::Encoder(unsigned char* pPcmData, int frameSize, unsigned char* pOpusData, int& opusLen)
{
	int nBytes = 0;
	memcpy_s((char*)m_inbuf, sizeof(short) * FRAME_SIZE * 2, pPcmData, frameSize);
	nBytes = opus_encode(m_pOpusEncoder,(opus_int16*)pPcmData, FRAME_SIZE, m_opusBuf, FRAME_SIZE);   //
	if (nBytes > 0 && pOpusData)
	{
		memcpy_s(pOpusData, FRAME_SIZE,m_opusBuf, nBytes);
	}
	opusLen = nBytes;
}
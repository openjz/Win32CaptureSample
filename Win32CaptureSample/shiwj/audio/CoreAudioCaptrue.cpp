#include "pch.h"
#include "CoreAudioCaptrue.h"
#include "CoreAudioDef.h"

CoreAudioCaptrue::CoreAudioCaptrue()
{
	//HRESULT hr_retrun;
	//hr_retrun = CoInitializeEx(NULL, COINIT_MULTITHREADED);
	hStopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	hAudioSamplesReadyEvent = CreateEventEx(NULL, NULL, 0, EVENT_MODIFY_STATE | SYNCHRONIZE);
	m_audioSemaphore = CreateSemaphore(NULL, 0, 10000,NULL);
}
CoreAudioCaptrue::~CoreAudioCaptrue()
{
	if (hStopEvent)
		CloseHandle(hStopEvent);
	if (m_audioSemaphore)
		CloseHandle(m_audioSemaphore);
	/*if (hAudioSamplesReadyEvent)
		CloseHandle(hAudioSamplesReadyEvent);*/
}

void CoreAudioCaptrue::UninitCaptrue()
{
	if (pDevice != NULL)
	{
		pDevice->Release();
		pDevice = NULL;
	}
	if (pAudioClient != NULL)
	{
		pAudioClient->Release();
		pAudioClient = NULL;
	}
	if (pCaptureClient != NULL)
	{
		pCaptureClient->Release();
		pCaptureClient = NULL;
	}
	if(m_waveFormatex)
		CoTaskMemFree(m_waveFormatex);
	if (pbyCaptureBuffer)
		delete[] pbyCaptureBuffer;
	pbyCaptureBuffer = nullptr;
}
bool CoreAudioCaptrue::Init(EM_CAPTRUE capType, std::function<void(unsigned char*, int, bool, void*)> callback, void* pArg, bool isEncoder, EM_ENCODE_TYPE encodeType)
{
	bool bRet = false;
	HRESULT hr_retrun;
	hr_retrun = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&pEnumerator);
	if (FAILED(hr_retrun))
	{
		printf("无法枚举音频设备：: %x\n", hr_retrun);
		return bRet;
	}
	if(capType == AUDIOCAPTURE_MIC)
		hr_retrun = pEnumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &pDevice);
	else 
		hr_retrun = pEnumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &pDevice);

	if (pEnumerator != NULL)
	{
		pEnumerator->Release();
		pEnumerator = NULL;
	}

	if (FAILED(hr_retrun))
	{
		return bRet;
	}
	hr_retrun = pDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&pAudioClient);
	if (FAILED(hr_retrun))
	{
		printf("创建一个管理对象: %x\n", hr_retrun);
		return bRet;
	}
	//m_waveFormatex = waveInfo;
	//WAVEFORMATEX* pwfx = NULL;
	hr_retrun = pAudioClient->GetMixFormat(&m_waveFormatex);
	if (FAILED(hr_retrun))
	{
		printf("XXX: %x\n", hr_retrun);
		return bRet;
	}

	AdjustFormatTo16Bits(m_waveFormatex);
	if (capType == AUDIOCAPTURE_MIC)
		hr_retrun = pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED,AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_NOPERSIST,
			hnsRequestedDuration, 0, m_waveFormatex, NULL);
	else
		hr_retrun = pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_LOOPBACK,
			hnsRequestedDuration, 0, m_waveFormatex, NULL);
	if (FAILED(hr_retrun))
	{
		printf("初始化终端设备上的流: %x\n", hr_retrun);
		return bRet;
	}
	m_callBack = callback;
	m_pArg = pArg;
	UINT32         bufferFrameCount;
	hr_retrun = pAudioClient->GetBufferSize(&bufferFrameCount);
	m_captrueBufLen = m_waveFormatex->nBlockAlign * m_waveFormatex->nSamplesPerSec / 50;
	if(pbyCaptureBuffer)
		delete[] pbyCaptureBuffer;
	pbyCaptureBuffer = new BYTE[m_captrueBufLen+100];
	hr_retrun = pAudioClient->SetEventHandle(hAudioSamplesReadyEvent);
	if (FAILED(hr_retrun))
	{
		return bRet;
	}
	// GetService 方法从音频客户端对象访问其他服务
	hr_retrun = pAudioClient->GetService(__uuidof(IAudioCaptureClient), (void**)&pCaptureClient);
	if (FAILED(hr_retrun))
	{
		printf("GetService 方法从音频客户端对象访问其他服务: %x.\n", hr_retrun);
		return bRet;
	}
	m_isEncoder = isEncoder;
	if (isEncoder)
	{
		m_audioEncodeType = encodeType;
		if (AUDIOENCODE_OPUS == encodeType)
		{
			m_audioForat.nAvgBytesPerSec = m_waveFormatex->nAvgBytesPerSec;
			m_audioForat.nBlockAlign = m_waveFormatex->nBlockAlign;
			m_audioForat.nChannels = m_waveFormatex->nChannels;
			m_audioForat.nSamplesPerSec = m_waveFormatex->nSamplesPerSec;
			m_audioForat.wBitsPerSample = m_waveFormatex->wBitsPerSample;
			m_opusEncoder = new Audio_opus_encoder();
			bRet = m_opusEncoder->Init(m_audioForat);
		}
		else
		{
			if (nullptr == m_aacEndoer)
			{
				
				m_aacEndoer = std::make_shared<Audio_aac_encoder>();
			}

			AAC_AudioFormat audioForamat;
			audioForamat.nBitSize = 16;
			audioForamat.nChannels = m_waveFormatex->nChannels;
			audioForamat.nSamplesPerSec = m_waveFormatex->nSamplesPerSec;
			bRet = m_aacEndoer->Init(audioForamat);
		}
		
	}
	return true;
}
void CoreAudioCaptrue::SetCallbackEx(std::function<void(unsigned char*, int, bool, void*, void*)> callback)
{
	m_callBackEx = callback;
}
bool CoreAudioCaptrue::Start()
{
	HRESULT hr_retrun;
	hr_retrun = pAudioClient->Start();  // Start recording.
	if (FAILED(hr_retrun))
	{
		printf("Start recording: %x.\n", hr_retrun);
		return false;
	}
	ResetEvent(hStopEvent);
	m_captrueThread = std::thread(&CoreAudioCaptrue::CaptrueFun, this);
	m_encoderThread = std::thread(&CoreAudioCaptrue::EncoderFun, this);
	printf("CoreAudioCaptrue start ok\n");
	return true;
}
void CoreAudioCaptrue::Stop()
{
	if (pAudioClient)
	{
		pAudioClient->Stop();
	}
	
	SetEvent(hStopEvent);
	if (m_captrueThread.joinable())
	{
		m_captrueThread.join();
	}
	if (m_encoderThread.joinable())
	{
		m_encoderThread.join();
		
	}
	UninitCaptrue();
	//auto expected = false;
	//m_stoped.compare_exchange_strong(expected, true);
}

void CoreAudioCaptrue::CaptrueFun()
{
	HRESULT hr_retrun;
	HANDLE waitArray[2];
	waitArray[0] = hAudioSamplesReadyEvent;
	waitArray[1] = hStopEvent;
	UINT32         packetLength = 0;
	DWORD flags;
	BYTE* pData;
	int   frameSize = 0;
	int   btyBuferIndex = 0;
	bool bCaptrue = true;
	while (bCaptrue)
	{
		DWORD waitResult = WaitForMultipleObjects(2, waitArray, FALSE, INFINITE);
		switch (waitResult)
		{
		case WAIT_OBJECT_0 + 0:  
			//方法检索捕获终结点缓冲区中下一个数据包中的帧数。
			hr_retrun = pCaptureClient->GetNextPacketSize(&packetLength);
			if (FAILED(hr_retrun))
			{
				printf("GetNextPacketSize: %x.\n", hr_retrun);
				return ;
			}
			while (packetLength != 0)
			{
				UINT32 numFramesAvailable;//每次从缓存区域里捞出来的 数据帧个数
				hr_retrun = pCaptureClient->GetBuffer(&pData, &numFramesAvailable, &flags, NULL, NULL);
				if (FAILED(hr_retrun))
				{
					printf("pCaptureClient->GetBuffer: %x.\n", numFramesAvailable);
					
				}
				if (flags & AUDCLNT_BUFFERFLAGS_SILENT)
				{
					printf("AUDCLNT_BUFFERFLAGS_SILENT \n");
				}
				//包中的数据与前一个包的设备位置不相关；这可能是由于流状态转换或时间故障。
				if (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY)//
				{
					printf("AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY numFramesAvailable=%d \n", numFramesAvailable);
				}
				if (numFramesAvailable > 0)
				{
					frameSize = numFramesAvailable * m_waveFormatex->nBlockAlign;
					if (flags & AUDCLNT_BUFFERFLAGS_SILENT)
					{
						SecureZeroMemory(pbyCaptureBuffer + btyBuferIndex, frameSize);
						printf(" audio data zero \n");
					}
					else
					{
						CopyMemory(pbyCaptureBuffer+ btyBuferIndex, pData, frameSize);
					}
					if (m_isEncoder)
					{
						if (AUDIOENCODE_OPUS == m_audioEncodeType)
						{
							ST_AudioData  audioData;
							audioData.len = m_captrueBufLen / 2;
							audioData.pData = new unsigned char[audioData.len];
							memcpy_s(audioData.pData, audioData.len, pbyCaptureBuffer, audioData.len);
							m_audioList.push_back(audioData);
							ReleaseSemaphore(m_audioSemaphore, 1, 0);
							btyBuferIndex += (numFramesAvailable * m_waveFormatex->nBlockAlign - m_captrueBufLen / 2);
							if (btyBuferIndex >= m_captrueBufLen / 2)
							{
								ST_AudioData  audioData;
								audioData.len = m_captrueBufLen / 2;
								audioData.pData = new unsigned char[audioData.len];
								memcpy_s(audioData.pData, audioData.len, pbyCaptureBuffer + audioData.len, audioData.len);
								m_audioList.push_back(audioData);
								ReleaseSemaphore(m_audioSemaphore, 1, 0);
								btyBuferIndex -= (m_captrueBufLen / 2);
								if (btyBuferIndex > 0)
									memcpy_s(pbyCaptureBuffer, btyBuferIndex, pbyCaptureBuffer + m_captrueBufLen, btyBuferIndex);
							}
							else if (btyBuferIndex > 0)
							{
								memcpy_s(pbyCaptureBuffer, btyBuferIndex, pbyCaptureBuffer + m_captrueBufLen / 2, btyBuferIndex);
							}
						}
						else
						{
							if (m_aacEndoer)
							{

								

								ST_AudioData  audioData;
								audioData.len = frameSize;
								audioData.pData = new unsigned char[audioData.len];
								memcpy_s(audioData.pData, frameSize, pbyCaptureBuffer, audioData.len);
								m_audioList.push_back(audioData);
								
							}
							ReleaseSemaphore(m_audioSemaphore, 1, 0);
						}
						
					}
					else 
					{
						if (m_callBack)
						{
							m_callBack(pbyCaptureBuffer, numFramesAvailable * m_waveFormatex->nBlockAlign, false, m_pArg);
						}
						else if (m_callBackEx)
						{
							m_callBackEx(pbyCaptureBuffer, numFramesAvailable * m_waveFormatex->nBlockAlign, false,this, m_pArg);
						}
						
					}
					
				}
				hr_retrun = pCaptureClient->ReleaseBuffer(numFramesAvailable);
				if (FAILED(hr_retrun))
				{
					printf("pCaptureClient->ReleaseBuffer: %x.\n", hr_retrun);
					return ;
				}
				//方法检索捕获终结点缓冲区中下一个数据包中的帧数。
				hr_retrun = pCaptureClient->GetNextPacketSize(&packetLength);
				if (FAILED(hr_retrun))
				{
					printf("pCaptureClient->GetNextPacketSize: %x.\n", hr_retrun);
					return;
				}
				//GetCurrentPadding方法检索终结点缓冲区中填充的帧数。
				UINT32 ui32NumPaddingFrames;
				hr_retrun = pAudioClient->GetCurrentPadding(&ui32NumPaddingFrames);
				if (FAILED(hr_retrun))
				{
					printf("pAudioClient->GetCurrentPadding: %x.\n", hr_retrun);
					return ;
				}
				if (0 != ui32NumPaddingFrames)
				{
					printf("GetCurrentPadding : %6u\n", ui32NumPaddingFrames);
				}
			} // end of 'while (packetLength != 0)'
			break;
		case WAIT_OBJECT_0+1:
			bCaptrue = false;
			break;
		} 
	}
}

void CoreAudioCaptrue::EncoderFun()
{
	static int ii = 0;
	HANDLE waitArray[2];
	waitArray[0] = m_audioSemaphore;
	waitArray[1] = hStopEvent;
	
	bool bEncoder = true;
	int bAACBufferSize = 2 * m_captrueBufLen;//提供足够大的缓冲区
	unsigned char* pbAACBuffer = (unsigned char*)malloc(bAACBufferSize * sizeof(unsigned char));
	int outLen = bAACBufferSize;
	while (bEncoder)
	{
		outLen = bAACBufferSize;
		DWORD waitResult = WaitForMultipleObjects(2, waitArray, FALSE, INFINITE);
		switch (waitResult)
		{
		case WAIT_OBJECT_0 + 0:
		{
			ST_AudioData audioData = m_audioList.front();
			if(ii++ ==0)
				//printf("audio encoder \n");
			if (ii == 100)
				ii = 0;
			if (AUDIOENCODE_OPUS == m_audioEncodeType)
			{
				m_opusEncoder->Encoder(audioData.pData, audioData.len, m_encoderData, outLen);
				if (m_callBack)
				{
					m_callBack(m_encoderData, outLen, true, m_pArg);
				}
			}
			else
			{
				if (m_aacEndoer)
				{
					if (m_aacEndoer->Encoder(audioData.pData, audioData.len, pbAACBuffer, outLen))
					{
						if (m_callBack)
						{
							m_callBack(pbAACBuffer, outLen, true, m_pArg);
						}
						else if (m_callBackEx)
						{
							m_callBackEx(pbAACBuffer, outLen, true,this, m_pArg);
						}
					}
				}
			}
			
			
			m_audioList.pop_front();
			if (audioData.pData)
				delete[] audioData.pData;
		}
			break;
		case WAIT_OBJECT_0 + 1:
			bEncoder = false;
			break;
		}
	}
	while (!m_audioList.empty())
	{
		ST_AudioData audioData = m_audioList.front();
		m_audioList.pop_front();
		if (audioData.pData)
			delete[] audioData.pData;
	}
	free(pbAACBuffer);
}

BOOL CoreAudioCaptrue::AdjustFormatTo16Bits(WAVEFORMATEX* pwfx)
{
	BOOL bRet(FALSE);

	if (pwfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT)
	{
		pwfx->wFormatTag = WAVE_FORMAT_PCM;
		pwfx->wBitsPerSample = 16;    // tianyw 16->32
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
			pEx->SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
			pEx->Samples.wValidBitsPerSample = 16;  // tianyw 16->32
			pwfx->wBitsPerSample = 16;              // tianyw 16->32             
			pwfx->nSamplesPerSec = 48000;
			pwfx->nBlockAlign = pwfx->nChannels * pwfx->wBitsPerSample / 8;
			pwfx->nAvgBytesPerSec = pwfx->nBlockAlign * pwfx->nSamplesPerSec;

			bRet = TRUE;
		}
	}
	return bRet;
}

WINCOREAUDIO_API std::shared_ptr<CoreAudioCaptrueInterface> CreateAudioCaptrue()
{
	return std::make_shared<CoreAudioCaptrue>();
}




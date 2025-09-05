#include <filesystem>

#include <mfapi.h>
#include <atlbase.h>
#include <codecapi.h>

#include "ShiwjEncoder.h"
#include "ShiwjCommon.h"

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")

namespace shiwj
{
	static void sleepE(int interValue) {
		if (interValue < 0) {
			return;
		}
		timeBeginPeriod(1);
		DWORD dwTime = timeGetTime();
		Sleep(interValue);
		timeEndPeriod(1);
	}

	CMFEncoder::~CMFEncoder()
	{
		Close();
	}

	int CMFEncoder::Init(EventCBFunc eventCbFunc, 
		winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice device,
		int width, int height, int fps, const char* filePath, bool inputAudio, bool outputAudio)
	{
		PLOG(plog::info) << L"CMFEncoder::Init, width:" << width << L" height:" << height << L" fps:" << fps << L" filepath:" << filePath;
		m_eventCbFunc = eventCbFunc;
		
		InitializeCriticalSection(&(m_cs));
		m_encoderParam = std::make_unique<EncoderParam>();

		HRESULT hr = MFStartup(MF_VERSION);

		hr = MFCreateDXGIDeviceManager(&m_resetToken, m_deviceManager.put());
		if (FAILED(hr))
		{
			PLOG(plog::error) << L"MFCreateDXGIDeviceManager failed. " << hr;
			return 1;
		}

		m_device = device;
		m_d3dDevice = GetDXGIInterfaceFromObject<ID3D11Device>(device);
		m_d3dDevice->GetImmediateContext(m_d3dContext.put());

		//将d3d设备绑定到 mf dxgi device manager
		auto dxgiDevice = m_d3dDevice.as<IDXGIDevice>();
		hr = m_deviceManager->ResetDevice(dxgiDevice.get(), m_resetToken);

		m_writeMp4Event = CreateEvent(NULL, FALSE, FALSE, NULL);
		PLOG(plog::info) << L"DeskCapEncoder create hr=" << hr;

		//start encode
		int ret = 0;

		m_recordInfo.width = width;
		m_recordInfo.height = height;
		m_recordInfo.fps = fps;
		m_recordInfo.filepath = filePath;
		m_recordInfo.inputAudio = false;
		m_recordInfo.outputAudio = false;

		m_encoderParam->width = width;
		m_encoderParam->height = height;
		m_encoderParam->fps = fps;
		m_encoderParam->gop = fps * 1000;
		m_encoderParam->bitRate = 10000000;
		m_encoderParam->quality = 50;

		ret = CreateEncoder();

		ret = StartEncoder();
		PLOG(plog::info) << L"StartEncoder:" << ret;
#if 0
		if (inputAudio)
		{
			if (m_audioInCapture)
			{
				m_audioInCapture->Stop();
			}
			//fopen_s(&m_audioinFile, "audio.aac", "ab");
			m_audioInCapture = CreateAudioCaptrue();
			bRet = m_audioInCapture->Init(CoreAudioCaptrueInterface::AUDIOCAPTURE_MIC, nullptr, this, true, AUDIOENCODE_AAC);
			if (bRet)
			{
				bRet = m_audioInCapture->Start();
				m_audioInCapture->SetCallbackEx(&AudioDataCallBack);
			}
			else
			{
				errcode = DESK_CAP_OPEN_AUDIO_CAP_ERROR;
			}
		}

		if (outputAudio)
		{
			if (m_audioOutCapture)
			{
				m_audioOutCapture->Stop();
			}
			m_audioOutCapture = CreateAudioCaptrue();
			bRet = m_audioOutCapture->Init(CoreAudioCaptrueInterface::AUDIOCAPTURE_SYSTEM, nullptr, this, true, AUDIOENCODE_AAC);
			if (bRet)
			{
				bRet = m_audioOutCapture->Start();
				m_audioOutCapture->SetCallbackEx(&AudioDataCallBack);
			}
			else
			{
				errcode = DESK_CAP_OPEN_AUDIO_CAP_ERROR;
			}
		}
#endif

		m_recordInfo.mp4Handle = m_mp4Encoder.CreateMP4File(m_recordInfo.filepath.c_str(), m_encoderParam->width, m_encoderParam->height, m_nTimeScale, m_encoderParam->fps);
		if (m_recordInfo.mp4Handle == MP4_INVALID_FILE_HANDLE)
		{
			ret = 1;
		}
		else
		{
#if 0
			if (m_audioInCapture)
			{
				m_recordInfo.mp4AudioInTrack = m_mp4Encoder.WriteAACMetadata(m_recordInfo.mp4Handle, 48000, 2);
			}
			if (m_audioOutCapture)
			{
				m_recordInfo.mp4AudioOutTrack = m_mp4Encoder.WriteAACMetadata(m_recordInfo.mp4Handle, 48000, 2);
			}
#endif
			m_bRecord = true;
			thRecord = std::thread(&CMFEncoder::RecordFileFun, this);
			ret = 0;
		}
		PLOG(plog::info) << L"CMFEncoder::Init end";
		return ret;
	}

	int CMFEncoder::EncodeFrame(winrt::com_ptr<ID3D11Texture2D> frameTexture)
	{
		std::lock_guard<std::mutex> lock(m_d3d11textureLock);
		m_d3d11texture = frameTexture;
		return 0;
	}
	void CMFEncoder::Close() {
		PLOG(plog::info) << L"CMFEncoder::Close";
		int ret = 0;
		m_bRecord = false;
		if (thRecord.joinable())
		{
			thRecord.join();
		}
		m_mp4Encoder.CloseMP4File(m_recordInfo.mp4Handle);

		StopEncoder();

		DeleteCriticalSection(&m_cs);
		StopEncoder();

		HRESULT hr;
		hr = MFShutdown();
		PLOG(plog::info) << L"MFShutdown =" << hr;
		CloseHandle(m_writeMp4Event);

		if (m_deviceManager) {
			m_deviceManager->Release();
			m_deviceManager = nullptr;
		}
		if (m_writeMp4Event) {
			CloseHandle(m_writeMp4Event);
			m_writeMp4Event = NULL;
		}
	}

	int CMFEncoder::CreateEncoder()
	{
		int ret = 0;

		m_colorConv = std::make_unique<RGBToNV12>(m_d3dDevice.get(), m_d3dContext.get());
		HRESULT hr = m_colorConv->Init();

		CComHeapPtr<IMFActivate*> activateRaw;
		UINT32 activateCount = 0;

#ifdef CODE_H265
		MFT_REGISTER_TYPE_INFO info = { MFMediaType_Video,  MFVideoFormat_HEVC }; //  MFVideoFormat_H265 MFVideoFormat_H264
#else
		MFT_REGISTER_TYPE_INFO info = { MFMediaType_Video,  MFVideoFormat_H264 };
#endif
		UINT32 flags = MFT_ENUM_FLAG_HARDWARE | MFT_ENUM_FLAG_SORTANDFILTER;

		hr = MFTEnumEx(
			MFT_CATEGORY_VIDEO_ENCODER,
			flags,
			NULL,
			&info,
			&activateRaw,
			&activateCount
		);
		if (FAILED(hr))
		{
			PLOG(plog::error) << L"MFTEnumEx failed:" << hr;
			return 1;
		}

		if (activateCount == 0)
		{
			PLOG(plog::error) << L"activateCount == 0";
			return 1;
		}
		bool bactive = false;

		for (UINT32 i = 0; i < activateCount; i++)
		{
			if (!bactive)
			{
				//hr = activateRaw[i]->ActivateObject(IID_PPV_ARGS(m_transform.put()));
				hr = activateRaw[i]->ActivateObject(winrt::guid_of<IMFTransform>(), m_transform.put_void());

				if (SUCCEEDED(hr))
				{
					bactive = true;
					*(m_mfActive.put()) = activateRaw[i];
					break;
				}
			}
		}

		for (UINT32 i = 0; i < activateCount; i++) {
			activateRaw[i]->Release();
		}

		PLOG(plog::info) << L"h264 encoder enum count=" << activateCount << L",active=" << bactive;
		if (!bactive) {
			PLOG(plog::error) << L"ActivateObject failed." << hr;
			return 1;
		}

		winrt::com_ptr<IMFAttributes> attributes;
		hr = m_transform->GetAttributes(attributes.put());
		hr = attributes->SetUINT32(MF_TRANSFORM_ASYNC_UNLOCK, TRUE);
		m_eventGen = m_transform.as<IMFMediaEventGenerator>();

		hr = m_transform->GetStreamIDs(1, &m_inputStreamID, 1, &m_outputStreamID);
		if (hr == E_NOTIMPL)
		{
			m_inputStreamID = 0;
			m_outputStreamID = 0;
			hr = S_OK;
		}

		//hr = m_transform->QueryInterface(IID_PPV_ARGS(&mpCodecAPI));
		hr = m_transform->QueryInterface(winrt::guid_of<ICodecAPI>(), mpCodecAPI.put_void());

		hr = m_transform->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER, reinterpret_cast<ULONG_PTR>(m_deviceManager.get()));
		if (FAILED(hr)) {
			return 1;
		}
		PLOG(plog::info) << L"ProcessMessage ok";
		hr = attributes->SetUINT32(MF_LOW_LATENCY, TRUE);
		if (FAILED(hr))
		{
			PLOG(plog::error) << L"Attributes SetUINT32 MF_LOW_LATENCY failed, hr:" << hr;
			return 1;
		}
		ret = SetInputOutPut();
		if (ret != 0) {
			PLOG(plog::error) << L"SetInputOutPut failed, ret:" << ret;
			return ret;
		}

		VARIANT var = { 0 };
		var.vt = VT_UI4;
		var.lVal = m_encoderParam->gop;
		hr = mpCodecAPI->SetValue(&CODECAPI_AVEncMPVGOPSize, &var);
		if (FAILED(hr))
		{
			PLOG(plog::error) << L"mpCodecAPI SetValue CODECAPI_AVEncMPVGOPSize failed. hr:" << hr;
			return 1;
		}

		var.lVal = eAVEncCommonRateControlMode_Quality;  //eAVEncCommonRateControlMode_Quality eAVEncCommonRateControlMode_UnconstrainedVBR
		hr = mpCodecAPI->SetValue(&CODECAPI_AVEncCommonRateControlMode, &var);
		if (FAILED(hr))
		{
			PLOG(plog::error) << L"mpCodecAPI SetValue CODECAPI_AVEncCommonRateControlMode failed. hr:" << hr;
			return 1;
		}

		var.lVal = m_encoderParam->quality;
		hr = mpCodecAPI->SetValue(&CODECAPI_AVEncCommonQuality, &var);
		if (FAILED(hr))
		{
			PLOG(plog::error) << L"mpCodecAPI SetValue CODECAPI_AVEncCommonQuality failed. hr:" << hr;
			return 1;
		}

		//create scale texture
		D3D11_TEXTURE2D_DESC desc = { 0 };
#ifdef CODE_H265 
		desc.Format = m_bIsInvada | m_bIsAmd ? DXGI_FORMAT_NV12 : DXGI_FORMAT_B8G8R8A8_UNORM; //m_bIsAmd ? DXGI_FORMAT_NV12 : DXGI_FORMAT_B8G8R8A8_UNORM; // DXGI_FORMAT_NV12; DXGI_FORMAT_B8G8R8A8_UNORM
#else
		desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
#endif
		desc.Width = m_encoderParam->width;
		desc.Height = m_encoderParam->height;//m_encoderParam->height;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_RENDER_TARGET;
		desc.CPUAccessFlags = 0;
		m_d3dDevice->CreateTexture2D(&desc, NULL, scaleTexture.put());
		scaleTextureSize.Width = m_encoderParam->width;
		scaleTextureSize.Height = m_encoderParam->height;

		PLOG(plog::info) << L"Create scale texture success";


		SetEncodeCallback(std::bind(&CMFEncoder::EncodeCallback, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4, std::placeholders::_5, std::placeholders::_6, std::placeholders::_7), nullptr);

		return 0;
	}

	int CMFEncoder::SetInputOutPut() {
		int ret = 0;
		ret = SetOutputType();
		if (ret != 0) {
			return ret;
		}
		ret = SetInputType();
		return ret;
	}

	int CMFEncoder::SetInputType()
	{
		UINT32 width;
		UINT32 height;
		HRESULT hr;
		winrt::com_ptr<IMFMediaType> inputType;
		hr = m_transform->GetInputAvailableType(m_inputStreamID, 0, inputType.put());
		MFGetAttributeSize(inputType.get(), MF_MT_FRAME_SIZE, &width, &height);
		if (FAILED(hr))
		{
			PLOG(plog::error) << L"MFCreateMediaType failed." << hr;
			return 1;
		}

		hr = inputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
		if (FAILED(hr))
		{
			PLOG(plog::error) << L"SetGUID, MF_MT_MAJOR_TYPE, MFMediaType_Video, failed." << hr;
			return 1;
		}
#ifdef CODE_H265
		hr = inputType->SetGUID(MF_MT_SUBTYPE, m_bIsInvada || m_bIsAmd ? MFVideoFormat_NV12 : MFVideoFormat_ARGB32);  // m_bIsAmd?MFVideoFormat_NV12: MFVideoFormat_ARGB32  m_bIsAmd?MFVideoFormat_NV12: MFVideoFormat_ARGB32  MFVideoFormat_ARGB32  MFVideoFormat_NV12
#else
		hr = inputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_ARGB32);
		if (FAILED(hr))
		{
			PLOG(plog::error) << L"SetGUID, MF_MT_SUBTYPE, MFVideoFormat_ARGB32, failed." << hr;
			return 1;
		}
#endif
		PLOG(plog::info) << L"inputType width= " << width << L", height=" << height;

#ifdef CODE_H265
		//  inputType->SetUINT32(MF_MT_VIDEO_NOMINAL_RANGE, MFNominalRange_Normal);
#endif
		PLOG(plog::info) << L"MFSetAttributeRatio fps:" << m_encoderParam->fps;
		hr = MFSetAttributeRatio(inputType.get(), MF_MT_FRAME_RATE, m_encoderParam->fps, 1);
		if (FAILED(hr))
		{
			PLOG(plog::error) << L"MFSetAttributeRatio,MF_MT_FRAME_RATE, failed." << hr;
			return 1;
		}

		hr = m_transform->SetInputType(m_inputStreamID, inputType.get(), 0);
		if (FAILED(hr))
		{
			PLOG(plog::error) << L"SetInputType failed." << hr;
			return 1;
		}
		return 0;
	}

	int CMFEncoder::SetOutputType()
	{
		HRESULT hr;
		winrt::com_ptr<IMFMediaType> outputType;

		hr = m_transform->GetOutputAvailableType(m_outputStreamID, 0, outputType.put());

		hr = outputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
		if (FAILED(hr))
		{
			PLOG(plog::error) << L"SetGUID, MF_MT_MAJOR_TYPE, MFMediaType_Video failed, hr=" << hr;
			return 1;
		}
		hr = outputType->SetUINT32(MF_MT_VIDEO_PROFILE, eAVEncH264VProfile_Main);
		if (FAILED(hr))
		{
			PLOG(plog::error) << L"SetUINT32, MF_MT_VIDEO_PROFILE, eAVEncH264VProfile_Main failed, hr=" << hr;
			return 1;
		}
		hr = outputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
		if (FAILED(hr))
		{
			PLOG(plog::error) << L"SetGUID, MF_MT_SUBTYPE, MFVideoFormat_H264, failed." << hr;
			return 1;
		}
		hr = outputType->SetUINT32(MF_MT_YUV_MATRIX, MFVideoTransferMatrix_BT601);
		if (FAILED(hr))
		{
			PLOG(plog::error) << L"SetUINT32, MF_MT_YUV_MATRIX, MFVideoTransferMatrix_BT601, failed." << hr;
		}
		hr = outputType->SetUINT32(MF_MT_AVG_BITRATE, m_encoderParam->bitRate);
		if (FAILED(hr))
		{
			PLOG(plog::error) << L"SetUINT32 failed, MF_MT_AVG_BITRATE, bitRate=" << m_encoderParam->bitRate << L", hr=" << hr;
			return 1;
		}
		hr = MFSetAttributeSize(outputType.get(), MF_MT_FRAME_SIZE, m_encoderParam->width, m_encoderParam->height);
		if (FAILED(hr))
		{
			PLOG(plog::error) << L"MFSetAttributeSize MF_MT_FRAME_SIZE failed, width" << m_encoderParam->width
				<< L", height=" << m_encoderParam->height << L" hr=" << hr;
			return 1;
		}
		hr = MFSetAttributeRatio(outputType.get(), MF_MT_FRAME_RATE, m_encoderParam->fps, 1);
		if (FAILED(hr))
		{
			PLOG(plog::error) << L"MFSetAttributeRatio MF_MT_FRAME_RATE failed, fps=" << m_encoderParam->fps << L", hr=" << hr;
			return 1;
		}
		hr = outputType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
		if (FAILED(hr))
		{
			PLOG(plog::error) << L"SetUINT32 MF_MT_INTERLACE_MODE failed, hr=" << hr;
			return 1;
		}
		hr = m_transform->SetOutputType(m_outputStreamID, outputType.get(), 0);
		if (FAILED(hr))
		{
			PLOG(plog::error) << L"SetOutputType failed, hr=" << hr;
			return 1;
		}
		return 0;
	}

	void CMFEncoder::SetEncodeCallback(std::function<void(unsigned char*, int, unsigned long long, bool, bool, int, void*)> callback, void* pArg)
	{
		m_callbackFun = callback;
		m_pArg = pArg;
	}

	void CMFEncoder::EncodeCallback(unsigned char* data, int len, unsigned long long time, bool invalid, bool iskey, int Iinvalid, void* parg)
	{

		st_video videodata;
		videodata.len = len;
		videodata.pdata = new unsigned char[len];
		videodata.isIDR = iskey;
		videodata.uiTime = GetTickCount();
		UINT64 currentTime = videodata.uiTime;

		memcpy_s(videodata.pdata, len, data, len);
		EnterCriticalSection(&(m_cs));
		m_videoList.push_back(videodata);
		st_video stVideo = m_videoList.front();
		bool bDelCacheFile = false;
		if (m_cacheMaxSec < (videodata.uiTime - stVideo.uiTime))
		{
			bDelCacheFile = true;
		}
		LeaveCriticalSection(&(m_cs));

		if (bDelCacheFile)
		{
			//清理掉一个GOP数据
			int frameCount = 0;
			while (true)
			{
				EnterCriticalSection(&(m_cs));
				if (m_videoList.size() == 0)
				{
					LeaveCriticalSection(&(m_cs));
					break;
				}
				st_video stVideo = m_videoList.front();
				frameCount++;
				if (stVideo.isIDR)
				{
					delete[] stVideo.pdata;
					m_videoList.pop_front();
					LeaveCriticalSection(&(m_cs));
					break;
				}
				else
				{
					delete[] stVideo.pdata;
				}
				m_videoList.pop_front();
				LeaveCriticalSection(&(m_cs));
			}
			//清理音频数据
			while (true)
			{
				std::lock_guard<std::mutex> lock(m_audioInLock);

				if (m_audioInList.size() == 0)
				{
					break;
				}
				st_audio stAudio = m_audioInList.front();
				frameCount++;
				if (stAudio.uiTime - currentTime > m_cacheMaxSec)
				{
					delete[] stAudio.pdata;
					m_audioInList.pop_front();
					break;
				}
				else
				{
					break;
				}
				m_audioInList.pop_front();
			}
			//清理音频数据
			while (true)
			{
				std::lock_guard<std::mutex> lock(m_audioOutLock);

				if (m_audioOutList.size() == 0)
				{
					break;
				}
				st_audio stAudio = m_audioOutList.front();
				frameCount++;
				if (stAudio.uiTime - currentTime > m_cacheMaxSec)
				{
					delete[] stAudio.pdata;
					m_audioOutList.pop_front();
					break;
				}
				else
				{
					break;
				}
				m_audioOutList.pop_front();
			}
		}
	}

	int CMFEncoder::StartEncoder()
	{
		int ret = 0;
		HRESULT hr;

		hr = m_transform->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, NULL);
		if (FAILED(hr))
		{
			PLOG(plog::error) << L"MFT_MESSAGE_COMMAND_FLUSH failed. hr=" << hr;
			return 1;
		}
		hr = m_transform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, NULL);
		if (FAILED(hr))
		{
			PLOG(plog::error) << L"MFT_MESSAGE_NOTIFY_BEGIN_STREAMING failed. hr=" << hr;
			return 1;
		}
		hr = m_transform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, NULL);
		if (FAILED(hr))
		{
			PLOG(plog::error) << L"MFT_MESSAGE_NOTIFY_START_OF_STREAM failed. hr=" << hr;
			return 1;
		}
		m_closed = true;
		m_encoderThread = std::thread(&CMFEncoder::EncodeThread, this);
		return 0;
	}

	void CMFEncoder::EncodeThread()
	{
		HRESULT hr;
		bool throttle = false;
		UINT32 interlaceMode;
		PLOG(plog::info) << L"CMFEncoder::EncodeThread start";
		//   m_frameTime = 1000.0 / m_encoderParam->deskEncodeParam->frameRate;
		m_frameTime = 1000.0 / m_encoderParam->fps;
		clock_t  startClock = clock();
		clock_t  startClickClock = clock();
		long     tGap = 0;
		long     currTime = 0;
		int      iSleep = 0;
		unsigned long long mtimesstamp = 0;
		static int icount = 0;
		static int itmp = 0;


		while (!m_closed.load())
		{
			winrt::com_ptr<IMFMediaEvent> event;
			if (m_eventGen == nullptr)
			{
				break;
			}
			hr = m_eventGen->GetEvent(0, event.put());
			if (FAILED(hr))
			{
				PLOG(plog::error) << L"m_eventGen->GetEvent failed. hr=" << hr;
				continue;
			}
			MediaEventType eventType;
			hr = event->GetType(&eventType);
			if (FAILED(hr))
			{
				PLOG(plog::error) << L"event->GetType failed. hr=" << hr;
				continue;
			}
			if (m_closed.load())
			{
				break;
			}
			switch (eventType)
			{
			case METransformNeedInput:
				{
					m_eventCbFunc(EncodeEvent::NeedInput);	//向capture请求一帧
					winrt::com_ptr<IMFMediaBuffer> inputBuffer;
					
					winrt::com_ptr<ID3D11Texture2D> texture2D = nullptr;
					while (!m_closed.load())
					{
						std::lock_guard<std::mutex> lock(m_d3d11textureLock);
						if (m_d3d11texture)
						{
							texture2D = m_d3d11texture;
							break;
						}
					}

					D3D11_TEXTURE2D_DESC desc;
					m_d3d11texture->GetDesc(&desc);

					hr = m_colorConv->Convert(texture2D.get(), scaleTexture.get(), desc.Width, desc.Height, 0, 0);
					if (FAILED(hr))
					{
						PLOG(plog::error) << L"convert exception hr=" << hr;
						break;
					}

					hr = MFCreateDXGISurfaceBuffer(__uuidof(ID3D11Texture2D), scaleTexture.get(), 0, FALSE, inputBuffer.put());
					if (FAILED(hr))
					{
						PLOG(plog::error) << L"MFCreateDXGISurfaceBuffer exception hr=" << hr;
						break;
					}

					winrt::com_ptr<IMFSample> sample;
					hr = MFCreateSample(sample.put());
					hr = sample->AddBuffer(inputBuffer.get());
					if (m_transform)
					{
						hr = m_transform->ProcessInput(m_inputStreamID, sample.get(), 0);
						if (FAILED(hr))
						{
							PLOG(plog::error) << L"ProcessInput failed. hr=" << hr;
						}
					}
				}
				break;

			case METransformHaveOutput:
				{
					DWORD status;
					MFT_OUTPUT_DATA_BUFFER outputBuffer;
					outputBuffer.dwStreamID = m_outputStreamID;
					outputBuffer.pSample = nullptr;
					outputBuffer.dwStatus = 0;
					outputBuffer.pEvents = nullptr;

					winrt::com_ptr<IMFMediaBuffer> pBuffer = NULL;
					BYTE* pData;
					hr = m_transform->ProcessOutput(0, 1, &outputBuffer, &status);
					DWORD maxvalue = 0;
					DWORD curr = 0;
					if (outputBuffer.pSample)
					{
						outputBuffer.pSample->GetBufferByIndex(0, pBuffer.put());
						mtimesstamp = GetTickCount() * 1000000;
						hr = pBuffer->Lock(&pData, &maxvalue, &curr);
						if (SUCCEEDED(hr))
						{
							bool biskey = false;
							biskey = IsIDRSample(pData, curr);
							m_callbackFun(pData, curr, mtimesstamp, true, biskey, 0, m_pArg);
							pBuffer->Unlock();
						}
					}

					if (outputBuffer.pSample)
						outputBuffer.pSample->Release();
					if (outputBuffer.pEvents)
						outputBuffer.pEvents->Release();
					currTime = clock();
					tGap = currTime - startClock;
					if (tGap < m_frameTime)
					{
						iSleep = m_frameTime - tGap;
						sleepE(m_frameTime - tGap);
					}

					icount++;
					if (currTime - startClickClock > 10000)
					{
						PLOG(plog::info) << L"frame = " << icount / 10;
						icount = 0;
						startClickClock = currTime;
					}
					startClock = clock();
				}
				break;
			default:
				break;
			}

		}
		if (m_colorConv)
		{
			m_colorConv->Cleanup();
			m_colorConv = nullptr;
		}
		PLOG(plog::info) << L"CMFEncoder::EncodeThread end";
	}

	//todo: 有更好的方法，SUCCEEDED(outputBuffer.pSample->GetUINT32(MFSampleExtension_VideoEncodePictureType, &picType)))
	bool CMFEncoder::IsIDRSample(unsigned char* data, int len)
	{
		int istart = 0;
		unsigned char* pData = data;
		unsigned char btStartCode[4] = { 0x00, 0x00, 0x00, 0x01 };
		unsigned char btStartCode2[3] = { 0x00, 0x00, 0x01 };
		bool  bIdr = false;
		while (istart++ < len - 4)
		{
			if (memcmp(pData, btStartCode2, sizeof(btStartCode2)) == 0 && ((pData[3] & 0x0F) == 5))
			{
				bIdr = true;
				break;
			}
			else if (memcmp(pData, btStartCode, sizeof(btStartCode)) == 0 && ((pData[4] & 0x0F) == 5))
			{
				bIdr = true;
				break;
			}
			pData++;
		}
		return bIdr;
	}

	void CMFEncoder::RecordFileFun()
	{
		int frameCount = 0;
		while (m_bRecord)
		{
			if (m_videoList.size() == 0 && m_audioInList.size() == 0)
			{
				Sleep(10);
			}
			if (m_videoList.size() > 0)
			{
				EnterCriticalSection(&(m_cs));
				st_video frame = m_videoList.front();
				m_videoList.pop_front();
				LeaveCriticalSection(&(m_cs));
				m_mp4Encoder.WriteH264Data(m_recordInfo.mp4Handle, frame.pdata, frame.len, frame.uiTime);

				frameCount++;
				if (frame.pdata)
				{
					delete[] frame.pdata;
				}

			}

			if (m_audioInList.size() > 0)
			{
				m_audioInLock.lock();
				st_audio frame = m_audioInList.front();
				m_audioInList.pop_front();
				m_audioInLock.unlock();
				m_mp4Encoder.WreiteAACData(m_recordInfo.mp4Handle, m_recordInfo.mp4AudioInTrack, frame.pdata, frame.len);

				if (frame.pdata)
				{
					delete[] frame.pdata;
				}

			}

			if (m_audioOutList.size() > 0)
			{
				m_audioOutLock.lock();
				st_audio frame = m_audioOutList.front();
				m_audioOutList.pop_front();
				m_audioOutLock.unlock();
				m_mp4Encoder.WreiteAACData(m_recordInfo.mp4Handle, m_recordInfo.mp4AudioOutTrack, frame.pdata, frame.len);

				if (frame.pdata)
				{
					delete[] frame.pdata;
				}

			}
		}
		PLOG(plog::info) << L"StopRecord:" << frameCount;
	}

	void CMFEncoder::StopEncoder()
	{
		PLOG(plog::info) << L"CMFEncoder::StopEncoder";
		m_closed = true;
		if (m_encoderThread.joinable())
		{
			m_encoderThread.join();
		}
		HRESULT hr;
		if (m_transform)
		{
			hr = m_transform->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, NULL);
			hr = m_transform->ProcessMessage(MFT_MESSAGE_NOTIFY_END_STREAMING, NULL);
			hr = m_transform->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, NULL);
			hr = m_transform->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER, NULL);
		}
		if (mpCodecAPI)
		{
			mpCodecAPI->Release();
			mpCodecAPI = nullptr;
		}
		if (m_transform)
		{
			m_transform->Release();
			m_transform = nullptr;
		}
		if (m_mfActive)
		{
			m_mfActive->ShutdownObject();
			m_mfActive = nullptr;
		}
#if 0
		StopAudioCapture();
#endif
		while (m_videoList.size() > 0)
		{
			st_video frame = m_videoList.front();
			if (frame.pdata)
			{
				delete[] frame.pdata;
			}
			m_videoList.pop_front();
		}
	}
}
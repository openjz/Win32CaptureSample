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
	CMFEncoder::~CMFEncoder()
	{
		if (m_closed == false)
		{
			Close();
		}
	}

	int CMFEncoder::Start(EventCBFunc eventCbFunc,
		winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice device,
		int width, int height, int fps, const char* filePath, bool inputAudio, bool outputAudio)
	{
		PLOG(plog::info) << L"CMFEncoder::Init, width:" << width << L" height:" << height << L" fps:" << fps << L" filepath:" << filePath;
		
		m_eventCbFunc = eventCbFunc;
		m_device = device;
		m_d3dDevice = GetDXGIInterfaceFromObject<ID3D11Device>(device);
		m_d3dDevice->GetImmediateContext(m_d3dContext.put());
		m_encoderParam = std::make_unique<EncoderParam>();

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

		InitializeCriticalSection(&(m_cs));

		HRESULT hr = MFStartup(MF_VERSION);
		if (FAILED(hr))
		{
			PLOG(plog::error) << "MFStartup failed. hr: 0x" << std::hex << hr << std::dec;
			return 1;
		}

		UINT resetToken;
		hr = MFCreateDXGIDeviceManager(&resetToken, m_deviceManager.put());
		if (FAILED(hr))
		{
			PLOG(plog::error) << L"MFCreateDXGIDeviceManager failed. hr: 0x" << std::hex << hr <<std::dec;
			return 1;
		}

		//将dxgi设备绑定到 mf dxgi device manager
		auto dxgiDevice = m_d3dDevice.as<IDXGIDevice>();
		hr = m_deviceManager->ResetDevice(dxgiDevice.get(), resetToken);
		if(FAILED(hr))
		{
			PLOG(plog::error) << L"IMFDXGIDeviceManager::ResetDevice failed. hr: 0x" << std::hex << hr << std::dec;
			return 1;
		}

		PLOG(plog::info) << L"DeskCapEncoder create hr=" << hr;

		//start encode
		int ret = 0;

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

	int CMFEncoder::EncodeFrame(winrt::com_ptr<ID3D11Texture2D> frameTexture, uint64_t tsMicro)
	{
		std::lock_guard<std::mutex> lock(m_inputLock);
		m_inputTexture = frameTexture;
		m_inputTsMicro = tsMicro;
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

		HRESULT hr;
		hr = MFShutdown();
		PLOG(plog::info) << L"MFShutdown =" << hr;

		if (m_deviceManager) {
			m_deviceManager = nullptr;
		}
	}

	int CMFEncoder::CreateEncoder()
	{
		int ret = 0;
		HRESULT hr = S_OK;
		m_imageConv = std::make_unique<CTextureScale>(m_d3dDevice, m_d3dContext);
		ret = m_imageConv->Init();
		if(ret != 0)
		{
			PLOG(plog::error) << L"CTextureScale Init failed, ret:" << ret;
			return ret;
		}
		//枚举并激活编码器
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
			PLOG(plog::error) << L"MFTEnumEx failed, hr: 0x" << std::hex << hr <<std::dec;
			return 1;
		}

		PLOG(plog::info) << L"MFTEnumEx, enum count=" << activateCount;
		if (activateCount == 0)
		{
			PLOG(plog::error) << L"MFTEnumEx, activate count is 0";
			return 1;
		}
		bool bactive = false;

		for (UINT32 i = 0; i < activateCount; i++)
		{
			PLOG(plog::info) << L"======> Activate object [" << i << L"] attributes:";
			shiwj::PrintMFAttributes(activateRaw[i]);
			if (!bactive)
			{
				hr = activateRaw[i]->ActivateObject(winrt::guid_of<IMFTransform>(), m_transform.put_void());
				if (SUCCEEDED(hr))
				{
					bactive = true;
					m_mfActive.copy_from(activateRaw[i]);
				}
			}
		}

		for (UINT32 i = 0; i < activateCount; i++) {
			activateRaw[i]->Release();
		}

		if (!bactive) {
			PLOG(plog::error) << L"ActivateObject failed." << hr;
			return 1;
		}

		winrt::com_ptr<IMFAttributes> attributes;
		m_transform->GetAttributes(attributes.put());
		PLOG(plog::info) << L"======> m_transform attributes(default)";
		shiwj::PrintMFAttributes(attributes.get());

		//m_transform默认具有MF_TRANSFORM_ASYNC属性
		//必须 和MF_TRANSFORM_ASYNC_UNLOCK相配合使用
		hr = attributes->SetUINT32(MF_TRANSFORM_ASYNC_UNLOCK, TRUE);

		//获取编码器输入输出流ID
		//先设置MF_TRANSFORM_ASYNC_UNLOCK，才能成功获取流ID
		hr = m_transform->GetStreamIDs(1, &m_inputStreamID, 1, &m_outputStreamID);
		if (hr == E_NOTIMPL)
		{
			m_inputStreamID = 0;
			m_outputStreamID = 0;
		}

		//去掉B帧
		hr = attributes->SetUINT32(MF_LOW_LATENCY, TRUE);
		if (FAILED(hr))
		{
			PLOG(plog::error) << L"Attributes SetUINT32 MF_LOW_LATENCY failed, hr:" << hr;
			return 1;
		}

		PLOG(plog::info) << L"======> m_transform attributes(final)";
		shiwj::PrintMFAttributes(attributes.get());

		//https://learn.microsoft.com/zh-cn/windows/win32/api/mftransform/ne-mftransform-mft_message_type
		//必须在 SetInputType 或 SetOutputType 之前调用。
		hr = m_transform->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER, reinterpret_cast<ULONG_PTR>(m_deviceManager.get()));
		if (FAILED(hr)) {
			PLOG(plog::error) << L"ProcessMessage MFT_MESSAGE_SET_D3D_MANAGER failed. hr: 0x" << std::hex << hr <<std::dec;
			return 1;
		}

		//设置输入输出参数
		//必须先设置outputType，再设置inputType
		ret = SetOutputType();
		if (ret != 0) {
			PLOG(plog::error) << L"SetOutputType failed, ret:" << ret;
			return ret;
		}
		ret = SetInputType();
		if (ret != 0) {
			PLOG(plog::error) << L"SetInputOutPut failed, ret:" << ret;
			return ret;
		}

		m_eventGen = m_transform.as<IMFMediaEventGenerator>();

		//设置编码参数
		m_codecAPI = m_transform.as<ICodecAPI>();

		VARIANT var = { 0 };
		var.vt = VT_UI4;
		var.lVal = m_encoderParam->gop;
		hr = m_codecAPI->SetValue(&CODECAPI_AVEncMPVGOPSize, &var);
		if (FAILED(hr))
		{
			PLOG(plog::error) << L"mpCodecAPI SetValue CODECAPI_AVEncMPVGOPSize failed. hr:" << hr;
			return 1;
		}

		var.lVal = eAVEncCommonRateControlMode_Quality;
		hr = m_codecAPI->SetValue(&CODECAPI_AVEncCommonRateControlMode, &var);
		if (FAILED(hr))
		{
			PLOG(plog::error) << L"mpCodecAPI SetValue CODECAPI_AVEncCommonRateControlMode failed. hr:" << hr;
			return 1;
		}

		var.lVal = m_encoderParam->quality;
		hr = m_codecAPI->SetValue(&CODECAPI_AVEncCommonQuality, &var);
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
		desc.Height = m_encoderParam->height;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_RENDER_TARGET;
		desc.CPUAccessFlags = 0;
		m_d3dDevice->CreateTexture2D(&desc, NULL, scaleTexture.put());

		PLOG(plog::info) << L"Create scale texture success";
		return 0;
	}

	int CMFEncoder::SetOutputType()
	{
		HRESULT hr;
		winrt::com_ptr<IMFMediaType> outputType;
		hr = m_transform->GetOutputAvailableType(m_outputStreamID, 0, outputType.put());
		if (FAILED(hr))
		{
			PLOG(plog::error) << L"SetOutputType, GetOutputAvailableType failed hr:" << std::hex << hr << std::dec;
			return 1;
		}
		PLOG(plog::info) << L"======> Output type attributes(default):";
		shiwj::PrintMFAttributes(outputType.get());

		hr = outputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
		if (FAILED(hr))
		{
			PLOG(plog::error) << L"SetGUID, MF_MT_MAJOR_TYPE, MFMediaType_Video failed, hr=" << hr;
			return 1;
		}
		hr = outputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
		if (FAILED(hr))
		{
			PLOG(plog::error) << L"SetGUID, MF_MT_SUBTYPE, MFVideoFormat_H264, failed." << hr;
			return 1;
		}
		hr = outputType->SetUINT32(MF_MT_VIDEO_PROFILE, eAVEncH264VProfile_Main);
		if (FAILED(hr))
		{
			PLOG(plog::error) << L"SetUINT32, MF_MT_VIDEO_PROFILE, eAVEncH264VProfile_Main failed, hr=" << hr;
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
		PLOG(plog::info) << L"======> Output type attributes(final):";
		shiwj::PrintMFAttributes(outputType.get());
		return 0;
	}

	int CMFEncoder::SetInputType()
	{
		HRESULT hr;
		winrt::com_ptr<IMFMediaType> inputType;
		hr = m_transform->GetInputAvailableType(m_inputStreamID, 0, inputType.put());
		if (FAILED(hr))
		{
			PLOG(plog::error) << L"SetInputType, GetInputAvailableType failed." << hr;
			return 1;
		}
		PLOG(plog::info) << L"======> Input type attributes(default):";
		shiwj::PrintMFAttributes(inputType.get());

		MFSetAttributeSize(inputType.get(), MF_MT_FRAME_SIZE, m_encoderParam->width, m_encoderParam->height);
		if (FAILED(hr))
		{
			PLOG(plog::error) << L"SetInputType failed." << hr;
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

#ifdef CODE_H265
		//  inputType->SetUINT32(MF_MT_VIDEO_NOMINAL_RANGE, MFNominalRange_Normal);
#endif
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
		PLOG(plog::info) << L"======> Input type attributes(final):";
		shiwj::PrintMFAttributes(inputType.get());
		return 0;
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
		m_closed = false;
		m_encoderThread = std::thread(&CMFEncoder::EncodeThread, this);
		return 0;
	}

	void CMFEncoder::EncodeThread()
	{
		PLOG(plog::info) << L"CMFEncoder::EncodeThread start";
		HRESULT hr;
		uint64_t frameTime = 1000000.0 / (double)(m_encoderParam->fps);	//每帧相隔多少us
		uint64_t lastTime = shiwj::GetCurrentTimestampMicro();	//unit:us
		uint64_t pts = 0;	//unit:100ns
		while (!m_closed.load())
		{
			if (m_eventGen == nullptr)
			{
				PLOG(plog::error) << L"m_eventGen is null";
				break;
			}

			winrt::com_ptr<IMFMediaEvent> event;
			hr = m_eventGen->GetEvent(0, event.put());
			if (FAILED(hr))
			{
				PLOG(plog::error) << L"GetEvent from m_eventGen failed, hr=" << std::hex << hr <<std::dec;
				continue;
			}
			MediaEventType eventType;
			hr = event->GetType(&eventType);
			if (FAILED(hr))
			{
				PLOG(plog::error) << L"Get event type failed, hr=" << std::hex << hr << std::dec;
				continue;
			}
			if(eventType == METransformNeedInput)
			{
				m_eventCbFunc(EncodeEvent::NeedInput);	//向capture请求一帧

				winrt::com_ptr<IMFMediaBuffer> inputBuffer;
				winrt::com_ptr<ID3D11Texture2D> inputTexture = nullptr;
				{
					std::lock_guard<std::mutex> lock(m_inputLock);
					inputTexture = m_inputTexture;
					if (!inputTexture)
					{
						PLOG(plog::error) << L"No input frame.";
						continue;
					}
					pts = shiwj::GetCurrentTimestampMicro()*10;
				}

				D3D11_TEXTURE2D_DESC desc;
				m_inputTexture->GetDesc(&desc);

				hr = m_imageConv->Convert(inputTexture, scaleTexture);
				if (FAILED(hr))
				{
					PLOG(plog::error) << L"convert failed hr=" << std::hex << hr << std::dec;
					break;
				}

				hr = MFCreateDXGISurfaceBuffer(__uuidof(ID3D11Texture2D), scaleTexture.get(), 0, FALSE, inputBuffer.put());
				if (FAILED(hr))
				{
					PLOG(plog::error) << L"MFCreateDXGISurfaceBuffer failed hr=" << std::hex << hr << std::dec;
					break;
				}

				winrt::com_ptr<IMFSample> sample;
				MFCreateSample(sample.put());
				sample->AddBuffer(inputBuffer.get());

				sample->SetSampleTime(pts);
				//sample->SetSampleDuration();//这里我们并不知道下一帧什么时候到来，因此不设置duration
				
				//查看input sample属性
				/*PLOG(plog::info) << L"======> Input sample attributes:";
				shiwj::PrintMFAttributes(sample.get());*/

				//提交input sample
				//PLOG(plog::debug) << L"Submit input sample";
				hr = m_transform->ProcessInput(m_inputStreamID, sample.get(), 0);
				if (FAILED(hr))
				{
					PLOG(plog::error) << L"ProcessInput failed. hr=" << std::hex << hr << std::dec;
				}

				//sleep
				uint64_t currTime = shiwj::GetCurrentTimestampMicro();
				uint64_t timeRun = currTime - lastTime;
				if (timeRun < frameTime)
				{
					uint64_t timeRest = frameTime - timeRun;
					WaitFor(timeRest);
				}
				else
				{
					PLOG(plog::debug) << L"Continue";
				}
				lastTime = shiwj::GetCurrentTimestampMicro();
			}
			else if(eventType == METransformHaveOutput)
			{
				DWORD status;
				MFT_OUTPUT_DATA_BUFFER outputBuffer;
				outputBuffer.dwStreamID = m_outputStreamID;
				outputBuffer.pSample = nullptr;
				outputBuffer.dwStatus = 0;
				outputBuffer.pEvents = nullptr;

				winrt::com_ptr<IMFMediaBuffer> pBuffer = NULL;
				hr = m_transform->ProcessOutput(0, 1, &outputBuffer, &status);
				if(FAILED(hr))
				{
					if (outputBuffer.pSample) {
						outputBuffer.pSample->Release();
					}
					if (outputBuffer.pEvents) {
						outputBuffer.pEvents->Release();
					}
					PLOG(plog::error) << L"ProcessOutput failed. hr=" << std::hex << hr << std::dec;
					continue;
				}

				//查看outputBuffer.pEvents
				/*DWORD pcElements = 0;
				outputBuffer.pEvents->GetElementCount(&pcElements);
				for (int i = 0; i < pcElements; i++)
				{
					winrt::com_ptr<IUnknown> unknow = nullptr;
					outputBuffer.pEvents->GetElement(i, unknow.put());
					winrt::com_ptr<IMFMediaEvent> mediaEvent = unknow.as< IMFMediaEvent>();
				}*/

				if (outputBuffer.pSample)
				{
					//查看output sample
					//PLOG(plog::info) << L"======> MFT_OUTPUT_DATA_BUFFER::pSample attributes:";
					//shiwj::PrintMFAttributes(outputBuffer.pSample);

					// 获取输出样本的时间戳，这个时间戳是调用ProcessInput前通过sample->SetSampleTime(pts);设置的
					// 以100ns为单位
					LONGLONG outputSampleTime = 0;
					hr = outputBuffer.pSample->GetSampleTime(&outputSampleTime);
					if (FAILED(hr))
					{
						PLOG(plog::error) << L"GetSampleTime failed. hr=" << std::hex << hr << std::dec;
						continue;
					}

					uint64_t outputTsMilli = outputSampleTime / 10 /1000;

					//判断是否为idr帧
					bool isIDR = false;
					uint32_t picType = 0;
					hr = outputBuffer.pSample->GetUINT32(MFSampleExtension_VideoEncodePictureType, &picType);
					if(FAILED(hr))
					{
						PLOG(plog::error) << L"GetUINT32 MFSampleExtension_VideoEncodePictureType failed. hr=" << std::hex << hr << std::dec;
						continue;
					}
					if (picType == eAVEncH264PictureType::eAVEncH264PictureType_IDR)
					{
						isIDR = true;
					}

					//push mux data
					outputBuffer.pSample->GetBufferByIndex(0, pBuffer.put());
					BYTE* pData;
					DWORD maxDataLen = 0;
					DWORD dataLen = 0;
					hr = pBuffer->Lock(&pData, &maxDataLen, &dataLen);
					if (SUCCEEDED(hr))
					{
						//make mux data
						st_video videodata;
						videodata.len = dataLen;
						videodata.pdata = new unsigned char[dataLen];
						videodata.isIDR = isIDR;
						videodata.uiTime = outputTsMilli;
						memcpy_s(videodata.pdata, dataLen, pData, dataLen);
						//push to mux list
						EnterCriticalSection(&(m_cs));
						m_videoList.push_back(videodata);
						LeaveCriticalSection(&(m_cs));
						pBuffer->Unlock();
					}
				}

				if (outputBuffer.pSample) {
					outputBuffer.pSample->Release();
				}
				if (outputBuffer.pEvents) {
					outputBuffer.pEvents->Release();
				}
			}
			else
			{
				PLOG(plog::debug) << L"Default event type: " << eventType;
				break;
			}

		}
		PLOG(plog::info) << L"CMFEncoder::EncodeThread end";
	}

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
		if (m_transform)
		{
			m_transform->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, NULL);
			m_transform->ProcessMessage(MFT_MESSAGE_NOTIFY_END_STREAMING, NULL);
			m_transform->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, NULL);
			m_transform->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER, NULL);
		}
		if (m_imageConv)
		{
			m_imageConv->Cleanup();
			m_imageConv = nullptr;
		}
		if (m_codecAPI)
		{
			m_codecAPI = nullptr;
		}
		if (m_transform)
		{
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
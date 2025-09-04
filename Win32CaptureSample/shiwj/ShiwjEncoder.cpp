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
		Close();
	}

	int CMFEncoder::Init(winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice device,
		int width, int height, int fps, const char* filePath, bool inputAudio, bool outputAudio)
	{
		PLOG(plog::info) << L"CMFEncoder::Init, width:" << width << L" height:" << height << L" fps:" << fps << L" filepath:" << filePath;
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
		HRESULT hr = m_deviceManager->ResetDevice(dxgiDevice.get(), m_resetToken);

		m_writeMp4Event = CreateEvent(NULL, FALSE, FALSE, NULL);
		PLOG(plog::info) << L"DeskCapEncoder create hr=" << hr;

		//start encode
		int errcode = 1;
		bool bRet = false;

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

		if (m_bCaptureInit && !m_bEncoderInit)
		{
			bRet = CreateEncoder();
			//m_capture->SetEncoderTarget(m_encoder);
			m_bEncoderInit = bRet;
			LOG_INFO(L"StartRecord encoder bInit=%d\n", bRet);
		}

		if (bRet && m_encoder)
		{
			bRet = m_encoder->StartEncoder();
			LOG_INFO(L"StartEncoder:%d\n", bRet);
		}


		if (m_encoder)
		{
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


			m_recordInfo.mp4Handle = m_mp4Encoder.CreateMP4File(m_recordInfo.filepath.c_str(), m_encoderParam->width, m_encoderParam->height, m_nTimeScale, m_encoderParam->fps);
			if (m_recordInfo.mp4Handle == MP4_INVALID_FILE_HANDLE)
			{
				errcode = DESK_CAP_CREATE_MP4_FILE_ERROR;
			}
			else
			{
				if (m_audioInCapture)
				{
					m_recordInfo.mp4AudioInTrack = m_mp4Encoder.WriteAACMetadata(m_recordInfo.mp4Handle, 48000, 2);
				}
				if (m_audioOutCapture)
				{
					m_recordInfo.mp4AudioOutTrack = m_mp4Encoder.WriteAACMetadata(m_recordInfo.mp4Handle, 48000, 2);
				}

				if (thRecord == nullptr)
				{
					m_bRecord = true;
					thRecord = std::make_shared<std::thread>(&DeskCapEncoder::RecordFileFun, this);
				}
				errcode = DESK_CAP_RETURN_OK;
			}
		}


		if (m_notify)
		{
			m_notify->OnStartManualRecord(errcode, 0, GetTickCount64());
		}
		LOG_INFO(L"StartManualRecord width:%d,height:%d fps:%d recordPath:%s  isCaptureAudio:%d CaptureOutAudio:%d end",
			width, height, fps, util::desktop::Utf8String2Wstring(filePath).c_str(), inputAudio ? 1 : 0, outputAudio ? 1 : 0);
	}

	void CMFEncoder::Close() {
		InnerStopCapture();
		StopEncoder();
		if (m_captrueParam)
			free(m_captrueParam);
		/* if (m_encoderParam && m_encoderParam->deskEncodeParam)
			 free(m_encoderParam->deskEncodeParam);*/  // 0418
		if (m_encoderParam)
			free(m_encoderParam);
		if (m_hWnd)
		{
			DestroyWindow(m_hWnd);
			// CloseHandle(m_hWnd);
		}
		/*  if (m_d3d11device)
		  {
			  m_d3d11device = nullptr;
		  }*/
		  /*  m_device.Close();
			m_deviceManager.Release();
			m_deviceManager = nullptr;
			m_encoder = nullptr;*/
			//  m_encoder = nullptr;

		HRESULT hr;
		hr = MFShutdown();
		CloseHandle(m_writeMp4Event);
		LOG_INFO(L"MFShutdown =%u \n", hr);

		if (m_deviceManager) {
			m_deviceManager->Release();
			m_deviceManager = nullptr;
		}
		if (m_writeMp4Event) {
			CloseHandle(m_writeMp4Event);
			m_writeMp4Event = NULL;
		}
		HRESULT hr = MFShutdown();
		PLOG(plog::info) << L"MFShutdown =" << hr;
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


		SetEncodeCallback(std::bind(&DeskCapEncoder::EncoderCallback, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4, std::placeholders::_5, std::placeholders::_6, std::placeholders::_7), nullptr);


		return bRet;
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

		st_video  videodata;
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
			// printf("cache Video >maxCacheTime maxTime:%d,current:%u", videodata.uiTime - stVideo.uiTime);
			 //DBG_LogInfo("cache Video >maxCacheTime maxTime:%d,current:%d", videodata.uiTime - stVideo.uiTime);
			bDelCacheFile = true;
		}
		LeaveCriticalSection(&(m_cs));
		//if (pFile2 == nullptr) {
		//    fopen_s(&pFile2, "D:\\1\\test9.h264", "wb");
		//}
		//fwrite(data, len, 1, pFile2);

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
		//printf("len = %d\n",len);
	}

}
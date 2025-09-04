#include <mfapi.h>
#include <atlbase.h>

#include "ShiwjEncoder.h"
#include "ShiwjCommon.h"

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")

namespace shiwj {
	CMFEncoder::~CMFEncoder() {
		Close();
	}

	int CMFEncoder::Init(int width, int height, int fps, const char * filePath, winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice device) {
        PLOG(plog::info) << L"CMFEncoder::Init, width:" << width << L" height:"<<height << L" fps:"<<fps <<L" filepath:" << filePath;
        m_encoderParam = std::make_unique<EncoderParam>();

        HRESULT hr = MFStartup(MF_VERSION);

        hr = MFCreateDXGIDeviceManager(&m_resetToken, m_deviceManager.put());
        if (FAILED(hr))
        {
            PLOG(plog::error) << L"MFCreateDXGIDeviceManager failed. "<< hr;
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
        if (!bactive){
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

        hr = m_transform->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER, reinterpret_cast<ULONG_PTR>(m_deviceManager.p));
        if (FAILED(hr))
            return bRet;
        LOG_INFO(L"ProcessMessage ok");
        hr = attributes->SetUINT32(MF_LOW_LATENCY, TRUE);
        if (SUCCEEDED(hr))
        {
            bRet = SetInputOutPut(2);

            LOG_INFO(L"SetOutputType bRet=%d \n", bRet);
            if (!bRet)
            {
                return bRet;
            }
        }


        LOG_INFO(L"SetValue start bRet=%d \n", bRet);
        VARIANT var = { 0 };
        var.vt = VT_UI4;
        //   var.lVal = m_encoderParam->deskEncodeParam->gop;
        var.lVal = m_encoderParam->gop;
        CHECK_HR(mpCodecAPI->SetValue(&CODECAPI_AVEncMPVGOPSize, &var), "Failed to set GOP size");
        LOG_INFO(L"SetValue success bRet=%d \n", bRet);
        // CODECAPI_AVDecVideoFastDecodeMode
            //var.lVal = m_encoderParam->deskEncodeParam->bitRate;
            //CHECK_HR(mpCodecAPI->SetValue(&CODECAPI_AVEncCommonMeanBitRate, &var), "Failed to set MeanBitRate");

            //var.lVal = m_encoderParam->deskEncodeParam->bitRate*2;
            //CHECK_HR(mpCodecAPI->SetValue(&CODECAPI_AVEncCommonMaxBitRate, &var), "Failed to set MeanBitRate");
        var.lVal = eAVEncCommonRateControlMode_Quality;  //eAVEncCommonRateControlMode_Quality eAVEncCommonRateControlMode_UnconstrainedVBR
        CHECK_HR(mpCodecAPI->SetValue(&CODECAPI_AVEncCommonRateControlMode, &var), "Failed to set MeanBitRate");
        LOG_INFO(L"SetValue success bRet=%d \n", bRet);
        // var.lVal = m_encoderParam->deskEncodeParam->quality; // 100;
        var.lVal = m_encoderParam->quality; // 100;
        CHECK_HR(mpCodecAPI->SetValue(&CODECAPI_AVEncCommonQuality, &var), "Failed to set MeanBitRate");
        LOG_INFO(L"SetValue success bRet=%d \n", bRet);

        // var.vt = VT_I4;
        // var.lVal = 2; // 100;
            //hr = mpCodecAPI->SetValue(&CODECAPI_AVEncNumWorkerThreads, &var);
            //CHECK_HR(mpCodecAPI->SetValue(&CODECAPI_AVEncNumWorkerThreads, &var), "Failed to set MeanBitRate");
            /*var.vt = VT_BOOL;
            var.lVal = VARIANT_TRUE;
            CHECK_HR(mpCodecAPI->SetValue(&CODECAPI_AVEncCommonRealTime, &var), "CODECAPI_AVEncCommonRealTime");*/
        m_width = m_encoderParam->width;
        m_height = m_encoderParam->height;
        //   winrt::Windows::Graphics::SizeInt32 capSize = m_capture->GetCaptrueSize();
        //   if (capSize.Width != m_encoderParam->width || capSize.Height != m_encoderParam->height)
        if (true)
        {
            if (scaleTexture)
            {
                scaleTexture->Release();
                scaleTexture = nullptr;
            }


            D3D11_TEXTURE2D_DESC desc = { 0 };
    #ifdef CODE_H265 
            desc.Format = m_bIsInvada | m_bIsAmd ? DXGI_FORMAT_NV12 : DXGI_FORMAT_B8G8R8A8_UNORM; //m_bIsAmd ? DXGI_FORMAT_NV12 : DXGI_FORMAT_B8G8R8A8_UNORM; // DXGI_FORMAT_NV12; DXGI_FORMAT_B8G8R8A8_UNORM
    #else
            /*  desc.Format = m_bIsAmd ? DXGI_FORMAT_B8G8R8A8_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM;*/  // tianyw 2024

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
            m_device->CreateTexture2D(&desc, NULL, &scaleTexture);
            scaleTextureSize.Width = m_encoderParam->width;
            scaleTextureSize.Height = m_encoderParam->height;
        }

        LOG_INFO(L"success bRet=%d \n", bRet);
        return bRet;
        //======================>
        bRet = m_encoder->InitEncoder(m_encoderParam, m_capture);

        if (bRet)
        {
            m_encoder->SetCoderDataCallback(std::bind(&DeskCapEncoder::EncoderCallback, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4, std::placeholders::_5, std::placeholders::_6, std::placeholders::_7), nullptr);
        }

        return bRet;
    }
}
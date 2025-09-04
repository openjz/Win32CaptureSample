#include "pch.h"
#include "SimpleEncoder.h"
#include "SimpleCapture.h"
#include "DeskCapEncoderInterface.h"
#include <memory.h>
#include <mmsystem.h>
#include <exception> 

//#define CODE_H265
static void sleepE(int interValue) {
    if (interValue < 0) {
        return;
    }
    timeBeginPeriod(1);
    DWORD dwTime = timeGetTime();
    Sleep(interValue);
    timeEndPeriod(1);

}

void SleepSelectUS(SOCKET s, int64_t usec)
{
    struct timeval tv;
    fd_set dummy;
    FD_ZERO(&dummy);
    FD_SET(s, &dummy);
    tv.tv_sec = usec / 1000000L;
    tv.tv_usec = usec % 1000000L;
    select(0, 0, 0, &dummy, &tv);
    DWORD err = GetLastError();
    if (err != 0)
        LOG_ERROR(L"Error : %d", err);
}


SimpleEncoder::SimpleEncoder(winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice const& device, CComPtr<IMFDXGIDeviceManager> deviceManager)
{
    LOG_INFO(L"SimpleEncoder");
    HRESULT hr;
    m_deviceManager = deviceManager;
    m_device = GetDXGIInterfaceFromObject<ID3D11Device>(device);
    m_device->GetImmediateContext(m_d3dContext.put());

    m_encoderParam = new EncoderParam;
    //   m_encoderParam->deskEncodeParam = new ST_DeskEncodeParam;

    m_colorConv = std::make_unique< RGBToNV12>(m_device.get(), m_d3dContext.get());
    hr = m_colorConv->Init();
}
SimpleEncoder::~SimpleEncoder()
{
    LOG_INFO(L"~SimpleEncoder");
    if (m_encoderParam)
    {
        //  delete m_encoderParam->deskEncodeParam;
        delete m_encoderParam;
        m_encoderParam = nullptr;
    }
    if (scaleTexture)
    {
        scaleTexture->Release();
        scaleTexture = nullptr;
    }
    m_deviceManager = nullptr;
    m_d3dContext = nullptr;
    m_device = nullptr;
    UnInitEncoder();
}
void SimpleEncoder::UnInitEncoder()
{
    Stop();
    if (mpCodecAPI)
    {
        mpCodecAPI->Release();
        mpCodecAPI = nullptr;
    }
    if (m_transform)
    {
        m_transform.Release();
        m_transform = nullptr;
    }
    /*  if (m_deviceManager)
          m_deviceManager.Release();
      m_deviceManager = nullptr;*/
      //m_device = nullptr;
      //m_capture = nullptr;
      //m_colorConv = nullptr;
      //if (m_eventGen)
      //{
      //    m_eventGen.Release();
      //    m_eventGen = nullptr;
      //}
    if (m_mfActive)
    {
        m_mfActive->ShutdownObject();
        m_mfActive = nullptr;
    }
    LOG_INFO(L"SimpleEncoder::UnInitEncoder \n");
}
bool SimpleEncoder::InitEncoder(EncoderParam* encoderParam, std::shared_ptr<SimpleCapture>& capture)
{
    UnInitEncoder();
    bool bRet = false;
    HRESULT hr;
    m_encoderParam->width = encoderParam->width;
    m_encoderParam->height = encoderParam->height;
    memcpy_s(m_encoderParam, sizeof(EncoderParam), encoderParam, sizeof(EncoderParam));
    m_frameRate = encoderParam->fps;
    //memcpy_s(m_encoderParam->deskEncodeParam, encoderParam->deskEncodeParam, sizeof(ST_DeskEncodeParam));
    //m_frameRate = encoderParam->deskEncodeParam->frameRate;
    CComHeapPtr<IMFActivate*> activateRaw;
    UINT32 activateCount = 0;

    //IDXGIDevice* dxgiDevice;
    //hr = m_device->QueryInterface(static_cast<IDXGIDevice**>(&dxgiDevice));
    //IDXGIAdapter* adapter;
    //hr = dxgiDevice->GetAdapter(&adapter);
    //DXGI_ADAPTER_DESC adapterDesc;
    //hr = adapter->GetDesc(&adapterDesc);

    //LOG_INFO(L"adapterDesc.VendorId = %d, amd:4098", adapterDesc.VendorId);
    //m_bIsAmd = (adapterDesc.VendorId == 4098); // 4318 invada
    //m_bIsInvada = (adapterDesc.VendorId == 4318);
    //if (adapter)
    //    adapter->Release();
    //if (dxgiDevice)
    //    dxgiDevice->Release();

#ifdef CODE_H265
    MFT_REGISTER_TYPE_INFO info = { MFMediaType_Video,  MFVideoFormat_HEVC }; //  MFVideoFormat_H265 MFVideoFormat_H264
#else
    MFT_REGISTER_TYPE_INFO info = { MFMediaType_Video,  MFVideoFormat_H264 };
#endif
    //
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
        LOG_INFO(L"MFTEnumEx failed:%ld,", hr);
        return bRet;
    }

    if (activateCount == 0)
    {
        LOG_INFO(L"activateCount == 0", hr);
        return false;
    }
    bool bactive = false;

    for (UINT32 i = 0; i < activateCount; i++)
    {
        if (!bactive)
        {
            //activate = activateRaw[i];
            //activate->ShutdownObject();
            hr = activateRaw[i]->ActivateObject(IID_PPV_ARGS(&m_transform));
            if (SUCCEEDED(hr))
            {
                bactive = true;
                m_mfActive = activateRaw[i];
                break;
            }
        }
        //     activateRaw[i]->Release();
    }

    for (UINT32 i = 0; i < activateCount; i++)
        activateRaw[i]->Release();
    LOG_INFO(L"h264 encoder enum count=%d,active=%d bRet:%d 222", activateCount, bactive, bRet);
    if (!bactive)
    {
        LOG_ERROR(L"ActivateObject failed.%x", hr);
        return false;
    }

    /* CComHeapPtr<WCHAR> friendlyName;
     UINT32 friendlyNameLength;
     hr = activate->GetAllocatedString(MFT_FRIENDLY_NAME_Attribute, &friendlyName, &friendlyNameLength);
     CHECK_HR(hr, "Failed to read MFT_FRIENDLY_NAME_Attribute");*/
     //  CComPtr<IMFTransform> transform=nullptr;
     /*  IMFTransform* transforam = nullptr;
       hr = activate->ActivateObject(IID_PPV_ARGS(&transforam));
       if (FAILED(hr))
       {
           LOG_INFO(L"ActivateObject failed.%x",hr);
           return false;
       }
       m_transform = transforam;*/

    m_capture = capture;

    if (bactive)
    {
        CComPtr<IMFAttributes> attributes;
        hr = m_transform->GetAttributes(&attributes);
        hr = attributes->SetUINT32(MF_TRANSFORM_ASYNC_UNLOCK, TRUE);
        m_eventGen = m_transform;

        hr = m_transform->GetStreamIDs(1, &m_inputStreamID, 1, &m_outputStreamID);
        if (hr == E_NOTIMPL)
        {
            m_inputStreamID = 0;
            m_outputStreamID = 0;
            hr = S_OK;
        }

        hr = m_transform->QueryInterface(IID_PPV_ARGS(&mpCodecAPI));
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
    }
    LOG_INFO(L"success bRet=%d \n", bRet);
    return bRet;
}

bool SimpleEncoder::StartEncoder()
{
    bool bRet = false;
    HRESULT hr;
    if (!m_transform)
    {
        return false;
    }

    hr = m_transform->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, NULL);
    CHECK_HR(hr);
    LOG_INFO(L"MFT_MESSAGE_COMMAND_FLUSH£º %d\n", bRet);
    hr = m_transform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, NULL);
    CHECK_HR(hr);
    LOG_INFO(L"MFT_MESSAGE_NOTIFY_BEGIN_STREAMING£º %d\n", bRet);
    hr = m_transform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, NULL);
    CHECK_HR(hr);
    LOG_INFO(L"MFT_MESSAGE_NOTIFY_START_OF_STREAM£º %d\n", bRet);
    auto expected = true;
    m_closed.compare_exchange_strong(expected, false);
    m_encoderThread = std::thread(&SimpleEncoder::DecoderThread, this);
    bRet = true;
    return bRet;
}

bool SimpleEncoder::StopEncoder()
{
    LOG_INFO(L"StopEncoder");
    auto expected = false;
    m_closed.compare_exchange_strong(expected, true);
    m_encoderThread.join();
    Stop();
    UnInitEncoder();
    m_capture = nullptr;
    return true;
}
bool SimpleEncoder::SetInputOutPut(int timeCount)
{
    int initCount = 0;
    bool bRet = false;
    while (initCount++ < timeCount)
    {
        bRet = SetOutputType();
        if (bRet)
        {
            bRet = SetInputType();
            if (bRet)
            {
                break;
            }

        }
        Sleep(500 + initCount * 100);
    }
    return bRet;
}
bool SimpleEncoder::SetInputType()
{
    UINT32 iwidht;
    UINT32 iheight;
    HRESULT hr;
    CComPtr<IMFMediaType> inputType;
    hr = MFCreateMediaType(&inputType);
    inputType = nullptr;
    hr = m_transform->GetInputAvailableType(m_inputStreamID, 0, &inputType);
    MFGetAttributeSize(inputType, MF_MT_FRAME_SIZE, &iwidht, &iheight);
    if (FAILED(hr))
    {
        LOG_INFO(L"GetInputAvailableType failed.%ld", hr);
    }
    CHECK_HR(hr);
    LOG_INFO(L"MF_MT_MAJOR_TYPE.%d", MFMediaType_Video);
    hr = inputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    if (FAILED(hr))
    {
        LOG_INFO(L"SetGUID failed.%ld", hr);
    }
    CHECK_HR(hr);
#ifdef CODE_H265
    hr = inputType->SetGUID(MF_MT_SUBTYPE, m_bIsInvada || m_bIsAmd ? MFVideoFormat_NV12 : MFVideoFormat_ARGB32);  // m_bIsAmd?MFVideoFormat_NV12: MFVideoFormat_ARGB32  m_bIsAmd?MFVideoFormat_NV12: MFVideoFormat_ARGB32  MFVideoFormat_ARGB32  MFVideoFormat_NV12
#else
    LOG_INFO(L"MF_MT_SUBTYPE.%d", MFVideoFormat_ARGB32);
    hr = inputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_ARGB32);
    if (FAILED(hr))
    {
        LOG_INFO(L"SetGUID failed.%ld", hr);
    }
#endif
    CHECK_HR(hr);
    LOG_INFO(L"SetInputType 1 hr = %d , %d, %d \n", hr, iwidht, iheight);

#ifdef CODE_H265
    //  inputType->SetUINT32(MF_MT_VIDEO_NOMINAL_RANGE, MFNominalRange_Normal);
#endif
    /*hr = MFSetAttributeSize(inputType, MF_MT_FRAME_SIZE, m_encoderParam->width, m_encoderParam->height);
    CHECK_HR(hr);
    printf("SetInputType 2 hr = %d\n", hr);*/
    //  hr = MFSetAttributeRatio(inputType, MF_MT_FRAME_RATE, m_encoderParam->deskEncodeParam->frameRate, 1);
    LOG_INFO(L"MFSetAttributeRatio.%d", m_encoderParam->fps);
    hr = MFSetAttributeRatio(inputType, MF_MT_FRAME_RATE, m_encoderParam->fps, 1);
    if (FAILED(hr))
    {
        LOG_INFO(L"MFSetAttributeRatio failed.%ld", hr);
    }
    CHECK_HR(hr);
    hr = m_transform->SetInputType(m_inputStreamID, inputType, 0);
    if (FAILED(hr))
    {
        LOG_INFO(L"SetInputType failed.%ld", hr);
    }
    LOG_INFO(L"SetInputType 3 hr = %d\n", hr);
    CHECK_HR(hr);
    return true;
}
bool SimpleEncoder::SetOutputType()
{
    HRESULT hr;
    CComPtr<IMFMediaType> outputType;
    //  hr = MFCreateMediaType(&outputType);

    hr = m_transform->GetOutputAvailableType(m_outputStreamID, 0, &outputType);
    LOG_INFO(L"MFMediaType_Video.%d", m_encoderParam->fps);
    hr = outputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    if (FAILED(hr))
    {
        LOG_ERROR(L"SetGUID failed.%ld", hr);
    }
    CHECK_HR(hr);

    hr = outputType->SetUINT32(MF_MT_VIDEO_PROFILE, eAVEncH264VProfile_Main);
    hr = outputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    hr = outputType->SetUINT32(MF_MT_YUV_MATRIX, MFVideoTransferMatrix_BT601);

    //  hr = outputType->SetUINT32(MF_MT_AVG_BITRATE, m_encoderParam->deskEncodeParam->bitRate);  // 30000000
    hr = outputType->SetUINT32(MF_MT_AVG_BITRATE, m_encoderParam->bitRate);  // 30000000
    if (FAILED(hr))
    {
        LOG_ERROR(L"SetUINT32 failed.%ld", hr);
    }
    LOG_INFO(L"OutPutType:%d %d %d bitRate:%d", eAVEncH264VProfile_Main, MFVideoFormat_H264, MFVideoTransferMatrix_BT601, m_encoderParam->bitRate);
    CHECK_HR(hr);
    LOG_INFO(L"MF_MT_FRAME_SIZE:width:%d Height:%d ", m_encoderParam->width, m_encoderParam->height);
    //UINT32 defaultWidth=0, defaultHeight=0;
    //MFGetAttributeSize(outputType, MF_MT_FRAME_SIZE, &defaultWidth, &defaultHeight);
    hr = MFSetAttributeSize(outputType, MF_MT_FRAME_SIZE, m_encoderParam->width, m_encoderParam->height);
    // MFGetAttributeSize(outputType, MF_MT_FRAME_SIZE, &defaultWidth, &defaultHeight);
    if (FAILED(hr))
    {
        LOG_ERROR(L"MFSetAttributeSize failed.%u", hr);
    }
    CHECK_HR(hr);
    LOG_INFO(L"MF_MT_FRAME_RATE:%d ", m_encoderParam->fps);
    //   hr = MFSetAttributeRatio(outputType, MF_MT_FRAME_RATE, m_encoderParam->deskEncodeParam->frameRate, 1);  // 60
    hr = MFSetAttributeRatio(outputType, MF_MT_FRAME_RATE, m_encoderParam->fps, 1);  // 60

    CHECK_HR(hr);
    hr = outputType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    LOG_INFO(L"MF_MT_INTERLACE_MODE:");
    DWORD dRet = GetLastError();
    if (FAILED(hr))
    {
        LOG_ERROR(L"SetUINT32 failed.%x error=%u", hr, dRet);
    }
    hr = m_transform->SetOutputType(m_outputStreamID, outputType, 0);
    dRet = GetLastError();
    if (FAILED(hr))
    {
        LOG_ERROR(L"SetUINT32 failed.%x %x", hr, dRet);
    }
    LOG_INFO(L"SetOutputType 4 hr = % d, m_outputStreamID = % d, error = % u \n", hr, m_outputStreamID, dRet);
    CHECK_HR(hr);
    return true;
}

bool SimpleEncoder::SetCoderDataCallback(std::function<void(unsigned char*, int, unsigned long long, bool, bool, int, void*)> callback, void* pArg)
{
    m_callbackFun = callback;
    m_pArg = pArg;
    return true;
}
FILE* pFile = nullptr;
void SimpleEncoder::DecoderThread()
{
    HRESULT hr;
    bool encoding = false;
    bool throttle = false;
    UINT32 interlaceMode;
    //SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    LOG_INFO(L"SimpleEncoder::DecoderThread start \n");
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
     
        CComPtr<IMFMediaEvent> event;
        if (m_eventGen == nullptr)
            break;
        hr = m_eventGen->GetEvent(0, &event);
        if (FAILED(hr))
        {
            LOG_ERROR(L"m_eventGen->GetEvent failed:%u", hr);
            continue;
        }
        MediaEventType eventType;
        hr = event->GetType(&eventType);
        if (FAILED(hr))
        {
            LOG_ERROR(L"event->GetType failed:%u", hr);
            continue;
        }
        if (m_closed.load())
        {
            //m_encoderMutex.unlock();
            break;
        }
        switch (eventType)
        {
        case METransformNeedInput:
            encoding = true;
            {
                CComPtr<IMFMediaBuffer> inputBuffer;
                ID3D11Texture2D* texture2D = nullptr;
                while (!m_closed.load())
                {
                    texture2D = m_capture->CaptureNextEx();
                    if (texture2D)
                    {
                        
                        break;
                    }
                }
                if (!texture2D)
                {
                    LOG_ERROR(L"CaptureNextEx failed.");
                    break;
                }
               
                
                D3D11_TEXTURE2D_DESC desc;
                texture2D->GetDesc(&desc);
               
                if (true)
                {
                    hr = m_colorConv->Convert(texture2D, scaleTexture, m_capture->m_nFrameW, m_capture->m_nFrameH, 0, m_capture->m_nCapH);
                    if (FAILED(hr))
                    {
                        LOG_ERROR(L"convert exception");
                        inputBuffer.Release();
                        break;
                    }

                    hr = MFCreateDXGISurfaceBuffer(__uuidof(ID3D11Texture2D), scaleTexture, 0, FALSE, &inputBuffer);
                    if (FAILED(hr))
                    {
                        LOG_ERROR(L"MFCreateDXGISurfaceBuffer exception");
                        inputBuffer.Release();
                        break;
                    }
                }
                else
                {
                    hr = MFCreateDXGISurfaceBuffer(__uuidof(ID3D11Texture2D), texture2D, 0, FALSE, &inputBuffer);
                }
               

                CComPtr<IMFSample> sample;
                hr = MFCreateSample(&sample);
                hr = sample->AddBuffer(inputBuffer);
                if (m_transform)
                {
                    hr = m_transform->ProcessInput(m_inputStreamID, sample, 0);
                    if (FAILED(hr))
                    {
                        LOG_ERROR(L"ProcessInput failed.");
                    }
                }

                inputBuffer.Release();
                sample.Release();    
            }
            break;

        case METransformHaveOutput:
            encoding = false;
            {
                // auto startTime = std::chrono::system_clock::now();
                DWORD status;
                MFT_OUTPUT_DATA_BUFFER outputBuffer;
                outputBuffer.dwStreamID = m_outputStreamID;
                outputBuffer.pSample = nullptr;
                outputBuffer.dwStatus = 0;
                outputBuffer.pEvents = nullptr;

                IMFMediaBuffer* pBuffer = NULL;
                BYTE* pData;
                hr = m_transform->ProcessOutput(0, 1, &outputBuffer, &status);
                DWORD maxvalue = 0;
                DWORD curr = 0;
                if (outputBuffer.pSample)
                {
                    //outputBuffer.pSample->CopyToBuffer(pBuffer);
                    outputBuffer.pSample->GetBufferByIndex(0, &pBuffer);
                    //   outputBuffer.pSample->GetSampleTime(&mtimesstamp);
                    mtimesstamp = GetTickCount() * 1000000;
                    hr = pBuffer->Lock(&pData, &maxvalue, &curr);
                    if (SUCCEEDED(hr))
                    {
                        bool biskey = false;
                        biskey = IsIDRSample(pData, curr);
                        m_callbackFun(pData, curr, mtimesstamp, true, biskey, CAP_MAINDISPLAY, m_pArg);
                        pBuffer->Unlock();
                    }
                    /* if (pFile == nullptr)
                      {
                          fopen_s(&pFile, "test.h264", "wb");
                      }
                      fwrite(pData, curr, 1, pFile);*/
                }
              
                if (pBuffer)
                    pBuffer->Release();
                if (outputBuffer.pSample)
                    outputBuffer.pSample->Release();
                if (outputBuffer.pEvents)
                    outputBuffer.pEvents->Release();
                currTime = clock();
                tGap = currTime - startClock;
                //   printf("encoder time = %d \n", tGap);
                if (tGap < m_frameTime)
                {
                    iSleep = m_frameTime - tGap;
                    //SleepSelectUS(s, iSleep*1000);
                    sleepE(m_frameTime - tGap);

                }

                icount++;
                if (currTime - startClickClock > 10000)
                {
                    LOG_INFO(L"frame = %d", icount / 10);
                   
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

    // closesocket(s);
    if (m_colorConv)
    {
        m_colorConv->Cleanup();
        m_colorConv = nullptr;
    }
    LOG_INFO(L"SimpleEncoder::DecoderThread end \n");
}

void SimpleEncoder::SendDeskResolution(int nWidth, int nHeight, bool isBPG)
{
    if (m_callbackFun)
    {
        unsigned char pData[4] = { 0 };
        pData[0] = static_cast<unsigned char>(nWidth & 0x000000FF);
        pData[1] = static_cast<unsigned char>((nWidth >> 8) & 0x000000FF);
        pData[2] = static_cast<unsigned char>(nHeight & 0x000000FF);
        pData[3] = static_cast<unsigned char>((nHeight >> 8) & 0x000000FF);
        m_callbackFun(pData, 4, 0, false, false, CAP_MAINDISPLAY, m_pArg);
    }
}

void SimpleEncoder::SetForceKeyFrame()
{
    HRESULT hr;
    if (mpCodecAPI)
    {
        VARIANT var = { 0 };
        var.vt = VT_UI4;
        var.lVal = 1;
        Stop();
        hr = mpCodecAPI->SetValue(&CODECAPI_AVEncVideoForceKeyFrame, &var);
        //  m_encoderMutex.unlock();
        Start();
        printf("set is key =%d \n", hr);
    }
}
void SimpleEncoder::SetCaptrueParam(int width, int height, int frameRate)
{
    if ((width != m_width && width > 0) || (height != m_height && height > 0))
    {
        //  Stop();
        LOG_INFO(L"tian SetCaptrueSize width=%d,height=%d \n", width, height);
        // Sleep(100);
        StopEncoder();
        //  Stop();
        UnInitEncoder();
        m_encoderParam->width = width;
        m_encoderParam->height = height;

        if (m_frameRate != frameRate && frameRate > 0)
        {
            m_frameTime = 1000.0 / frameRate;
            m_frameRate = frameRate;
        }
        InitEncoder(m_encoderParam, m_capture);
        // Start();
        StartEncoder();
    }
    else if (m_frameRate != frameRate && frameRate > 0)
    {
        //m_encoderMutex.lock();
        m_frameTime = 1000.0 / frameRate;
        m_frameRate = frameRate;
        //m_encoderMutex.unlock();
    }
}

void SimpleEncoder::ResetParam()
{
    HRESULT hr;
    CComPtr<IMFMediaType> outputType;
    hr = MFCreateMediaType(&outputType);
    outputType = nullptr;
    hr = m_transform->GetOutputAvailableType(m_inputStreamID, 0, &outputType);
    hr = outputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    hr = outputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    //  hr = outputType->SetUINT32(MF_MT_AVG_BITRATE, m_encoderParam->deskEncodeParam->bitRate);  // 30000000
    hr = MFSetAttributeSize(outputType, MF_MT_FRAME_SIZE, m_width, m_height);
    //  hr = MFSetAttributeRatio(outputType, MF_MT_FRAME_RATE, m_encoderParam->deskEncodeParam->frameRate, 1);
    hr = MFSetAttributeRatio(outputType, MF_MT_FRAME_RATE, m_encoderParam->fps, 1);
    hr = m_transform->SetOutputType(m_outputStreamID, outputType, 0);

    CComPtr<IMFMediaType> inputType;
    hr = MFCreateMediaType(&inputType);
    inputType = nullptr;
    hr = m_transform->GetInputAvailableType(m_inputStreamID, 0, &inputType);
    hr = inputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    hr = inputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_ARGB32);
    // hr = MFSetAttributeRatio(inputType, MF_MT_FRAME_RATE, m_encoderParam->deskEncodeParam->frameRate, 1);
    hr = MFSetAttributeRatio(inputType, MF_MT_FRAME_RATE, m_encoderParam->fps, 1);
    hr = MFSetAttributeSize(inputType, MF_MT_FRAME_SIZE, m_width, m_height);
    hr = m_transform->SetInputType(m_inputStreamID, inputType, 0);
}

void SimpleEncoder::Stop()
{
    HRESULT hr;
    if (m_transform)
    {
        LOG_INFO(L"SimpleEncoder::Stop");
        hr = m_transform->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, NULL);
        hr = m_transform->ProcessMessage(MFT_MESSAGE_NOTIFY_END_STREAMING, NULL);
        hr = m_transform->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, NULL);
        hr = m_transform->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER, NULL);
    }


}
void SimpleEncoder::Start()
{
    HRESULT hr;
    if (m_transform)
    {
        hr = m_transform->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, NULL);
        hr = m_transform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, NULL);
        hr = m_transform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, NULL);
    }

}

bool SimpleEncoder::IsIDRSample(unsigned char* data, int len)
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
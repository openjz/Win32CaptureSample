#include "pch.h"
#include "DeskCapEncoder.h"
#include <atlbase.h>
#include <mfapi.h>
#include <Windows.h>
#include <filesystem>
#include <ShellScalingApi.h>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
using namespace winrt;
using namespace Windows::Foundation;
namespace winrt
{
    using namespace Windows::Foundation::Metadata;
    using namespace Windows::Graphics::Capture;
    using namespace Windows::System;
    using namespace Windows::UI;
    using namespace Windows::UI::Composition;
    using namespace Windows::UI::Composition::Desktop;
    using namespace Windows::Graphics::DirectX;
}
namespace util
{
    using namespace desktop;
    using namespace uwp;
}

static void char_to_wchar(const char* ch, std::wstring& w_str)
{
    wchar_t* wchar;
    int len = MultiByteToWideChar(CP_ACP, 0, ch, strlen(ch), NULL, 0);
    wchar = new wchar_t[len + 1];
    MultiByteToWideChar(CP_ACP, 0, ch, strlen(ch), wchar, len);
    wchar[len] = '\0';
    w_str = wchar;
    delete[]wchar;
}

static SIZE GetScreenResolution(HWND hWnd) {
    SIZE size;
    size.cx = size.cy = 0;
    if (!hWnd)
        return size;

    //MONITOR_DEFAULTTONEAREST 返回值是最接近该点的屏幕句柄
    //MONITOR_DEFAULTTOPRIMARY 返回值是主屏幕的句柄
    //如果其中一个屏幕包含该点，则返回值是该屏幕的HMONITOR句柄。如果没有一个屏幕包含该点，则返回值取决于dwFlags的值
    HMONITOR hMonitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFOEX miex;
    miex.cbSize = sizeof(miex);
    if (!GetMonitorInfo(hMonitor, &miex))
        return size;

    DEVMODE dm;
    dm.dmSize = sizeof(dm);
    dm.dmDriverExtra = 0;

    //ENUM_CURRENT_SETTINGS 检索显示设备的当前设置
    //ENUM_REGISTRY_SETTINGS 检索当前存储在注册表中的显示设备的设置
    if (!EnumDisplaySettings(miex.szDevice, ENUM_CURRENT_SETTINGS, &dm))
        return size;

    size.cx = dm.dmPelsWidth;
    size.cy = dm.dmPelsHeight;
    return size;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT wMsg, WPARAM wParam, LPARAM lParam)
{
    return DefWindowProc(hWnd, wMsg, wParam, lParam);
}

void AudioDataCallBack(unsigned char* pOutData, int outDataLen, bool isEncode, void* pObj, void* pArg)
{
    DeskCapEncoder* pThis = (DeskCapEncoder*)pArg;
    pThis->OnAudioDataCallBack(pOutData, outDataLen, isEncode, pObj);
}
void DeskCapEncoder::OnAudioDataCallBack(unsigned char* pOutData, int outDataLen, bool isEncode, void* pArg)
{

    //if (m_audioinFile)
    //{
    //    fwrite(pOutData, outDataLen, 1, m_audioinFile);
    //}
    st_audio  audioData;
    audioData.len = outDataLen;
    audioData.pdata = new unsigned char[outDataLen];
    audioData.uiTime = GetTickCount();
    memcpy_s(audioData.pdata, outDataLen, pOutData, outDataLen);
    if (m_audioInCapture && m_audioInCapture.get() == (CoreAudioCaptrueInterface*)pArg)
    {
        std::lock_guard<std::mutex> lock(m_audioInLock);
        m_audioInList.push_back(audioData);
    }
    if (m_audioOutCapture && m_audioOutCapture.get() == (CoreAudioCaptrueInterface*)pArg)
    {
        std::lock_guard<std::mutex> lock(m_audioOutLock);
        m_audioOutList.push_back(audioData);
    }
}
void DeskCapEncoder::CreateWndInThread(int nWidth, int nHeight)
{
    WNDCLASSEXA wcex;
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = GetModuleHandle(nullptr);
    wcex.hIcon = 0;
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);// (HBRUSH)GetStockObject(BLACK_BRUSH);  CreateSolidBrush(RGB(255, 0, 0)) ;// (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszMenuName = NULL;
    /*  char cName[MAX_PATH] = { 0 };
      GetModuleFileNameA(wcex.hInstance, cName, sizeof(cName));
      char* szApp = strrchr(cName, '\\') + 1;
      strchr(szApp, '.')[0] = '\0';*/
    wcex.lpszClassName = "HideWindows2";
    wcex.hIconSm = 0;
    RegisterClassExA(&wcex);
    // 10000
    m_hWnd = CreateWindowA("HideWindows2", "", WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_POPUP,
        10000, 0, nWidth, nHeight, nullptr, nullptr, wcex.hInstance, this);

    //  m_hWnd = CreateWindowA(szApp, nullptr, WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, 0, 800, 600, NULL, NULL, wcex.hInstance, 0);
    SetWindowLong(m_hWnd, GWL_EXSTYLE, GetWindowLong(m_hWnd, GWL_EXSTYLE) | WS_EX_TOOLWINDOW);
    ShowWindow(m_hWnd, SW_SHOW);
    UpdateWindow(m_hWnd);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
        //收到WM_QUIT的时候退出循环，SendMessage可以跨线程发送消息
    }
}

void DeskCapEncoder::CreatePreviewWndInThread(int nWidth, int nHeight)
{
    const static char* szClass = "MyName";
    const static char* szTitle = "PreviewWnd";
    HINSTANCE hIns = ::GetModuleHandle(0);
    WNDCLASSEXA wc;
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = hIns;
    wc.hIcon = LoadIcon(0, IDI_APPLICATION);
    wc.hIconSm = 0;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hCursor = LoadCursor(0, IDC_ARROW);
    wc.lpfnWndProc = WndProc;
    wc.lpszMenuName = NULL;
    wc.lpszClassName = szClass;

    if (!RegisterClassExA(&wc)) exit(0);
    DWORD style = WS_OVERLAPPEDWINDOW;
    DWORD styleEx = WS_EX_APPWINDOW | WS_EX_WINDOWEDGE;

    m_previewHwnd = CreateWindowA(szClass, szTitle, style,
        0, 0, nWidth, nHeight, nullptr, nullptr, wc.hInstance, 0);
    if (m_previewHwnd == 0) exit(0);
    ShowWindow(m_previewHwnd, SW_SHOW);
    UpdateWindow(m_previewHwnd);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
        //收到WM_QUIT的时候退出循环，SendMessage可以跨线程发送消息
    }
}

DeskCapEncoder::DeskCapEncoder()
{
    InitializeCriticalSection(&(m_cs));
    HRESULT hr;
    m_captrueParam = new ST_DeskCaptrueParam;
    m_encoderParam = new EncoderParam;
    //  m_encoderParam->deskEncodeParam = new ST_DeskEncodeParam; //  0418

      //init_apartment();
    hr = MFStartup(MF_VERSION);
    // m_d3d11device = util::CreateD3DDevice();
    //auto d3dDevice = util::CreateD3DDevice();
    //auto dxgiDevice = d3dDevice.as<IDXGIDevice>();
    //m_device = CreateDirect3DDevice(dxgiDevice.get());
    //MFCreateDXGIDeviceManager(&resetToken, &m_deviceManager);
    //m_deviceManager->ResetDevice(d3dDevice.get(), resetToken);
    IMFDXGIDeviceManager* pDXGIDeviceManager = nullptr;
    hr = MFCreateDXGIDeviceManager(&resetToken, &pDXGIDeviceManager);
    if (FAILED(hr))
    {
        LOG_INFO(L"MFCreateDXGIDeviceManager failed.%u", hr);
        return;
    }
    m_deviceManager = pDXGIDeviceManager;

    m_writeMp4Event = CreateEvent(NULL, FALSE, FALSE, NULL);
    LOG_INFO(L" DeskCapEncoder create hr=%d \n", hr);
    Sleep(500);
   // CreateD3DDevice();
}
DeskCapEncoder::~DeskCapEncoder()
{
    DeleteCriticalSection(&m_cs);
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
}
static void GetMonitorRealResolution(HMONITOR monitor, int* pixelsWidth, int* pixelsHeight)
{
    MONITORINFOEX info = { sizeof(MONITORINFOEX) };
    winrt::check_bool(GetMonitorInfo(monitor, &info));
    DEVMODE devmode = {};
    devmode.dmSize = sizeof(DEVMODE);
    winrt::check_bool(EnumDisplaySettings(info.szDevice, ENUM_CURRENT_SETTINGS, &devmode));
    *pixelsWidth = devmode.dmPelsWidth;
    *pixelsHeight = devmode.dmPelsHeight;
}

bool DeskCapEncoder::CreateDisplayCapture(bool isBorderRequest,bool isCurser,std::wstring ameExeName)
{
    bool bRet = false;
    try
    {
        if (!m_d3ddevice)
        {
            CreateD3DDevice();
        }
        
        LOG_INFO(L"CreateDisplayCapture start");
       
        m_monitors = std::make_unique<MonitorList>(false);
        auto monitors = m_monitors->GetCurrentMonitors();
        for (size_t i = 0; i < monitors.size(); i++)
        {
            LOG_INFO(L"Monitors:%s", monitors[i].DisplayName.c_str());
        }
        if (monitors.size() > 0)
        {
            if (m_captrueParam->capType == CAP_MAINDISPLAY)
            {
                LOG_INFO(L"CreateDisplayCapture CAP_MAINDISPLAY start");
                auto monitor = monitors[0];
                if (!m_monitorName.empty())
                {
                    std::wstring moinitorName = m_monitorName;
                    auto ptr = std::find_if(monitors.begin(), monitors.end(), [&moinitorName](MonitorInfo& info)
                        {
                            if (moinitorName == info.DisplayName)
                            {
                                return true;
                            }
                            else
                            {
                                return false;
                            }
                        });
                    if (ptr != monitors.end())
                    {
                        monitor = *ptr;
                    }
                }
                int pixX, pixY;
                GetMonitorRealResolution(monitor.MonitorHandle, &pixX, &pixY);
                winrt::Windows::Graphics::Capture::GraphicsCaptureItem item{ nullptr };
                item = util::CreateCaptureItemForMonitor(monitor.MonitorHandle);
                winrt::Windows::Graphics::DirectX::DirectXPixelFormat m_pixelFormat = winrt::Windows::Graphics::DirectX::DirectXPixelFormat::R8G8B8A8UIntNormalized;  // NV12  B8G8R8A8UIntNormalized -> R8G8B8A8UIntNormalized //tianyw0416

                m_capture = std::make_shared<SimpleCapture>(m_device, item, m_pixelFormat, m_notify, m_captrueParam->capHwnd,
                    isBorderRequest, isCurser, ameExeName,pixX,pixY);
                LOG_INFO(L"CreateDisplayCapture CAP_MAINDISPLAY end \n");
                bRet = true;
            }
            else if (m_captrueParam->capType == CAP_WINDOW)
            {

                int pixX = 3840;
                int pixY = 2400;
                MonitorInfo* monitor = m_monitors->GetDisplayMonitor();
                if (monitor)
                {
                    GetMonitorRealResolution(monitors[0].MonitorHandle, &pixX, &pixY);

                }
                

                winrt::Windows::Graphics::Capture::GraphicsCaptureItem item{ nullptr };

                if (!util::CreateCaptureItemForWindow(m_captrueParam->capHwnd, item))
                {
                    LOG_ERROR(L"CreateCaptureItemForWindow failed.");
                    return false;
                }
                winrt::Windows::Graphics::DirectX::DirectXPixelFormat m_pixelFormat = winrt::Windows::Graphics::DirectX::DirectXPixelFormat::R8G8B8A8UIntNormalized;  // NV12  B8G8R8A8UIntNormalized -> R8G8B8A8UIntNormalized //tianyw0416
                m_capture = std::make_shared<SimpleCapture>(m_device, item, m_pixelFormat, m_notify, m_captrueParam->capHwnd,
                    isBorderRequest, isCurser, ameExeName, pixX, pixY);

                bRet = true;
            }
        }
    }
    catch (winrt::hresult_error const& ex)
    {
        winrt::hresult hr = ex.code();
        LOG_INFO(L"winrtexception:%u", ex.code());
        return false;
    }
    

    LOG_INFO(L"CreateDisplayCapture end:%d \n", bRet);
    return bRet;
}

//bool DeskCapEncoder::CreateWindowCapture(HWND hwnd)
//{
//    bool bRet = false;
//    m_monitors = std::make_unique<MonitorList>(false);
//    auto monitors = m_monitors->GetCurrentMonitors();
//    if (m_captrueParam->capType == CAP_WINDOW)
//    {
//        winrt::Windows::Graphics::Capture::GraphicsCaptureItem item{ nullptr };
//      //  item = util::CreateCaptureItemForWindow(hwnd);
//        winrt::Windows::Graphics::DirectX::DirectXPixelFormat m_pixelFormat = winrt::Windows::Graphics::DirectX::DirectXPixelFormat::R8G8B8A8UIntNormalized;  // B8G8R8A8UIntNormalized->R8G8B8A8UIntNormalized
//        m_capture = std::make_shared<SimpleCapture>(m_device, item, m_pixelFormat, m_notify);
//        
//        if (m_captrueParam->videoWidth < 1 || m_captrueParam->videoWidth < 1)
//        {
//            SIZE deskSize = GetScreenResolution(m_captrueParam->capHwnd);
//            if (m_captrueParam->videoWidth < 1)
//            {
//                m_encoderParam->width = deskSize.cx;
//            }
//            if (m_captrueParam->videoHeight < 1)
//            {
//                m_encoderParam->height = deskSize.cy;
//            }
//        }
//        bRet = true;
//    }
//    return bRet;
//}

bool DeskCapEncoder::CreateEncoder()
{

    try
    {
        if (!m_capture)
        {
            return false;
        }
        bool bRet = false;
        SimpleEncoder* tm_encoder = nullptr;
        m_encoder = std::make_shared<SimpleEncoder>(m_device, m_deviceManager);
        bRet = m_encoder->InitEncoder(m_encoderParam, m_capture);

        if (bRet)
        {
            m_encoder->SetCoderDataCallback(std::bind(&DeskCapEncoder::EncoderCallback, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4, std::placeholders::_5, std::placeholders::_6, std::placeholders::_7), nullptr);
        }

        return bRet;
    }
    catch (winrt::hresult_error const& ex)
    {
        winrt::hresult hr = ex.code();
        LOG_INFO(L"winrtexception:%u", ex.code());
        return false;
    }
    
}

void DeskCapEncoder::Init(IDeskCapEncoderNotify* notifier)
{

    m_notify = notifier;
    
    InitializeLoger(L"StreamLeGO.log");

    LOG_INFO(L"Init StreamLoGo");
    //CreateD3DDevice();
    if (m_notify)
    {
        m_notify->OnInit(0, 0);
    }
    
}

void DeskCapEncoder::UnInit()
{
    if (m_notify)
    {
        m_notify->OnUnInit(0, 0);
    }
}

void DeskCapEncoder::CreateD3DDevice()
{
    m_d3ddevice = util::CreateD3DDevice(0);
    auto dxgiDevice = m_d3ddevice.as<IDXGIDevice>();
    
    
    m_device = CreateDirect3DDevice(dxgiDevice.get());
    // m_device = device;
    
    HRESULT hr = m_deviceManager->ResetDevice(dxgiDevice.get(), resetToken);
    if (FAILED(hr))
    {
        LOG_INFO(L"ResetDevice failed.%u", hr);
        return;
    }

}

void DeskCapEncoder::StartCapture(uint64_t handle, const char* tiltle, const char* ameExeName, int gameType, int internal, int captureType, bool isCaptureWithCursor, bool isCaptureWithBorder, int width, int height)
{
    
    bool bRet = false;
    m_captureType = captureType;
    LOG_INFO(L"StartCapture: internal:%d captureType:%d capCurse:%d width:%d height:%d  ameExeName:%s handle:%u title:%s", internal, captureType, isCaptureWithCursor ? 1 : 0, width, height, util::desktop::Utf8String2Wstring(ameExeName).c_str(), handle,
        util::desktop::Utf8String2Wstring(tiltle).c_str());
    /*   m_encoderParam->width = width;
       m_encoderParam->height = height;*/
    m_captrueParam->videoWidth = width;
    m_captrueParam->videoHeight = height;

    int errcode = 1;
    m_captrueParam->capHwnd = nullptr;
    if (m_captureType == 1)  // 屏幕
    {
        m_captrueParam->capType = CAP_MAINDISPLAY;
        m_monitorName = util::desktop::Utf8String2Wstring(tiltle);

    }
    else
    {

        m_captrueParam->capType = CAP_WINDOW;
        m_captrueParam->capHwnd = (HWND)handle;
    }

    m_isCaptureWithBorder = isCaptureWithBorder;
    m_isCurser = isCaptureWithCursor;
    m_strTitle = tiltle;
    m_ninternal = internal;
    m_bCaptureInit = CreateDisplayCapture(isCaptureWithBorder,isCaptureWithCursor, util::desktop::Utf8String2Wstring(tiltle));
    if (m_bCaptureInit)
    {
        m_capture->StartCapture(m_captrueParam->videoWidth, m_captrueParam->videoHeight, internal, true);
        //m_capture->IsCursorEnabled(isCaptureWithCursor);
       
        
    }

    /* if (m_bCaptureInit && !m_bEncoderInit)
     {
         bRet = CreateEncoder();
         m_capture->SetEncoderTarget(m_encoder);
         m_bEncoderInit = bRet;
         printf("CreateDisplay encoder bInit=%d\n", bRet);
     }*/

    if (m_notify)
    {
        if (m_bCaptureInit)
        {
            m_notify->OnStartCapture(0, 0);
        }
        else
        {
            m_notify->OnStartCapture(1, (char*)"StartCapture error\n");
        }
    }
    LOG_INFO(L"StartCapture: internal:%d captureType:%d capCurse:%d width:%d height:%d end", internal, captureType, isCaptureWithCursor ? 1 : 0, width, height);
}

void DeskCapEncoder::StopCapture()
{
    
    LOG_INFO(L"StopCapture");

    StopEncoder();
    InnerStopCapture();
    if (m_notify)
    {
        m_notify->OnStopCapture(0, 0);
    }
}
// 开始缓存视频
void DeskCapEncoder::StartReplayBuffer(int width, int height, int fps, char* recordPath, char* highPrefix, int hlBeforeSec, int hlAfterSec, int hlMaxSec, bool isCaptureWithCursor, bool isCaptureAudio, bool outputAudio)
{
   
    LOG_INFO(L"StartReplayBuffer width:%d,height:%d fps:%d recordPath:%s hlBeforeSec:%d hlAfterSec:%d hlMaxSec:%d isCaptureWithCursor:%d isCaptureAudio:%d CaptureOutAudio:%d start",
        width, height, fps, util::desktop::Utf8String2Wstring(recordPath).c_str(), hlBeforeSec, hlAfterSec, hlMaxSec, isCaptureWithCursor ? 1 : 0, isCaptureAudio ? 1 : 0, outputAudio ? 1 : 0);
    bool bRet = false;
    int errcode = 1;

    try
    {
        m_encoderParam->width = width;
        m_encoderParam->height = height;
        m_encoderParam->fps = fps;
        m_encoderParam->gop = fps;
        m_encoderParam->bitRate = 10000000;
        m_encoderParam->quality = 50;

        m_recordPath = recordPath;
        m_highPrefix = highPrefix;
        m_hlBeforeSec = hlBeforeSec * 1000;  //换算成毫秒
        m_hlAfterSec = hlAfterSec * 1000;
        m_hlMaxSec = hlMaxSec * 1000;
        if (m_hlMaxSec > m_cacheMaxSec)
        {
            m_cacheMaxSec = m_hlMaxSec;
        }
        if (thWrite == nullptr)
        {
            ResetEvent(m_writeMp4Event);
            m_bWrite = true;
            m_mediaList.clear();
            thWrite = std::make_shared<std::thread>(&DeskCapEncoder::WriteFileFun, this);
        }

        if (m_bCaptureInit && !m_bEncoderInit)
        {
            bRet = CreateEncoder();
            if (!bRet)
            {
                LOG_ERROR(L"CreateEncoder failed. 重新创建D3Ddevice");
                StopEncoder();
                InnerStopCapture();
                CreateD3DDevice();
                m_bCaptureInit = CreateDisplayCapture(m_isCaptureWithBorder, m_isCurser, util::desktop::Utf8String2Wstring(m_strTitle));
                if (m_bCaptureInit)
                {
                    m_capture->StartCapture(m_captrueParam->videoWidth, m_captrueParam->videoHeight, m_ninternal, true);
                    //m_capture->IsCursorEnabled(isCaptureWithCursor);


                }
                bRet = CreateEncoder();
            }
           // m_capture->SetEncoderTarget(m_encoder);
            m_bEncoderInit = bRet;
            LOG_INFO(L"StartReplayBuffer encoder bInit=%d\n", bRet);
        }

        if (bRet && m_encoder)
        {
            bRet = m_encoder->StartEncoder();
            LOG_INFO(L"deskcap encoder =%d\n", bRet);
        }

        if (isCaptureAudio)
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
        //重置落盘文件技术为1
        m_fileIndex = 1;
       
    }
    catch (winrt::hresult_error const& ex)
    {
        winrt::hresult hr = ex.code();
        LOG_INFO(L"winrtexception:%u", ex.code());
        bRet = false;
        errcode = 4;
    }

    if (m_notify)
    {
        if (bRet)
        {
            LOG_INFO(L"StartReplayBuffer width:%d,height:%d fps:%d recordPath:%s hlBeforeSec:%d hlAfterSec:%d isCaptureWithCursor:%d isCaptureAudio:%d CaptureOutAudio:%d success",
                width, height, fps, util::desktop::Utf8String2Wstring(recordPath).c_str(), hlBeforeSec, hlAfterSec, isCaptureWithCursor ? 1 : 0, isCaptureAudio ? 1 : 0, outputAudio ? 1 : 0);
            m_notify->OnStartReplayBuffer(0, 0);
        }
        else
        {
            LOG_INFO(L"encoder bInit=%d\n", bRet);
            errcode = 3;
            m_notify->OnStartReplayBuffer(errcode, (char*)"encoder bInit\n");
        }
    }

   
}
// 保存高光时刻到磁盘
void DeskCapEncoder::SaveReplayBuffer(unsigned int contextId, uint64_t* hlFramesTimestamps, int nCount, int index)
{
    
    LOG_INFO(L"SaveReplayBuffer contextId:%d nCount:%d index:%d start", contextId, nCount, index);
    uint64_t  startTime = 0;
    uint64_t  endTime = 0;
    uint64_t  videoTimes = 0;
    
    
    std::list<st_media>::iterator   itSatrt = m_mediaList.end();
    for (int i = 0; i < nCount; i++)
    {
        //高光时刻不合法则打印错误日志
        uint64_t currentTime = GetTickCount64();
        if (hlFramesTimestamps[i]> currentTime || (currentTime - hlFramesTimestamps[i])>m_hlMaxSec)
        {
            LOG_ERROR(L"hightTime:%u currentTime:%u maxSec:%u  illegal", hlFramesTimestamps[i],currentTime, m_hlMaxSec);
            continue;
        }
        startTime = hlFramesTimestamps[i] - m_hlBeforeSec;
        endTime = hlFramesTimestamps[i] + m_hlAfterSec;

        if (m_videoList.size()>0)
        {
            EnterCriticalSection(&(m_cs));
            st_video frame = m_videoList.front();
            if (hlFramesTimestamps[i]< m_videoList.front().uiTime)
            {
                LOG_INFO(L"SaveReplayBuffer hightTime:%u < first cacheTime:%u ", hlFramesTimestamps[i], startTime, m_videoList.front().uiTime);
                LeaveCriticalSection(&(m_cs));
                continue;
            }
            if (startTime < m_videoList.front().uiTime)
            {
                LOG_INFO(L"SaveReplayBuffer hightTime:%u startTime%u < first cacheTime:%u ", hlFramesTimestamps[i], startTime, m_videoList.front().uiTime);
                startTime = m_videoList.front().uiTime;
            }
            LeaveCriticalSection(&(m_cs));
        }
        

        LOG_INFO(L"SaveReplayBuffer hightTime:%u startTime:%u endTime:%u", hlFramesTimestamps[i],startTime, endTime);
        // startTime = hlFramesTimestamps[i];
         //去重
        bool bFind = false;
        for (auto it = m_mediaList.begin(); it != m_mediaList.end(); it++)
        {
            m_medialstMutex.lock();
            for (auto contextPtr = it->saveContexts.begin(); contextPtr != it->saveContexts.end();)
            {
                std::vector<uint64_t>& framesTimes = contextPtr->second.frameTimes;
                for (auto framesTimesPtr = framesTimes.begin(); framesTimesPtr != framesTimes.end();)
                {
                    if (*framesTimesPtr == hlFramesTimestamps[i] && !it->m_bSave)
                    {
                        LOG_INFO(L"contextId:%u hightTime:%u 已经存在 contextId:%u修改 该高光时刻 contextid通知 ", contextId, hlFramesTimestamps[i], contextPtr->first);
                        framesTimesPtr = framesTimes.erase(framesTimesPtr);
                        it->saveContexts[contextId].frameTimes.push_back(hlFramesTimestamps[i]);
                        bFind = true;
                        break;

                    }
                    else
                    {
                        framesTimesPtr++;
                    }
                }
                if (bFind)
                {
                    break;
                }
                if (framesTimes.empty())
                {
                    contextPtr = it->saveContexts.erase(contextPtr);
                }
                else
                {
                    contextPtr++;
                }
            }

            
            m_medialstMutex.unlock();
        }
        if (bFind)
        {
            continue;
        }
        for (auto it = m_mediaList.begin(); it != m_mediaList.end(); it++)
        {
            m_medialstMutex.lock();
            //重合
            if (hlFramesTimestamps[i] >= it->startTime && hlFramesTimestamps[i] <= it->endTime)
            {
                if (!it->m_bSave)
                {
                    itSatrt = it;
                    LOG_INFO(L"SaveReplayBuffer  current contextId:%u high:%u startTime:%u endTime:%u in contextId:%d", contextId, hlFramesTimestamps[i],it->startTime, it->endTime, it->contextid);
                    
                    if (endTime - itSatrt->startTime > m_hlMaxSec) //超最大视频切断
                    {
                        itSatrt->endTime = itSatrt->startTime + m_hlMaxSec;
                        LOG_INFO(L"high:%u duration%u>m_hlMaxSec(%u) file:%s startTime:%u endTime:%u", hlFramesTimestamps[i], endTime - itSatrt->startTime, 
                            m_hlMaxSec, util::desktop::Utf8String2Wstring(itSatrt->filename).c_str(), itSatrt->startTime, itSatrt->endTime);
                        itSatrt->saveContexts[contextId].frameTimes.push_back(hlFramesTimestamps[i]);
                        startTime = itSatrt->endTime;

                    }
                    else
                    {
                        itSatrt->endTime = endTime;
                        itSatrt->saveContexts[contextId].frameTimes.push_back(hlFramesTimestamps[i]);
                    }
                    m_medialstMutex.unlock();
                    break;
                }
                else
                {
                    //itSatrt = it; 已经缓存完成有交叉的；则开始时间只向前找4s
                    if (it->endTime<= hlFramesTimestamps[i])
                    {
                        startTime = it->endTime;
                    }
                    else
                    {
                        startTime = hlFramesTimestamps[i] - 4000;
                    }
                    
                    LOG_INFO(L"SaveReplayBuffer  current contextId:%u hight:%u startTime:%u endTime:%u in contextId:%d startTime:%u endTime%u have save complete", 
                        contextId, hlFramesTimestamps[i], startTime,endTime, it->contextid,it->startTime, it->endTime);
                    m_medialstMutex.unlock();
                    break;
                }
               
            }

            m_medialstMutex.unlock();
        }
        
        if(itSatrt ==m_mediaList.end())
        { 
            st_media media;
            media.contextid = contextId;
            media.m_bSave = false;
            media.startTime = startTime;
            media.endTime = endTime;
            media.writeTime = 0;
            media.filename = m_recordPath + m_highPrefix + std::to_string(m_fileIndex++);// +".mp4";
            media.mp4Handle = MP4_INVALID_FILE_HANDLE;
            LOG_INFO(L"current file:%s startTime:%u endTime:%u  high:%u", util::desktop::Utf8String2Wstring(media.filename).c_str(), media.startTime, media.endTime, hlFramesTimestamps[i]);
            media.saveContexts[contextId].frameTimes.push_back(hlFramesTimestamps[i]);
            //m_medialstMutex.lock();
            m_mediaList.push_back(media);
            //m_medialstMutex.unlock();
        }

    }
    SetEvent(m_writeMp4Event);
   
}

void DeskCapEncoder::StopReplayBuffer()
{
    LOG_INFO(L"StopReplayBuffer start");
    if (thWrite)
    {
        m_bWrite = false;
        SetEvent(m_writeMp4Event);
        thWrite->join();
        thWrite = nullptr;
        m_mediaList.clear();
    }
    LOG_INFO(L"Stop WriteMp4 success");
    StopEncoder();
    LOG_INFO(L"Stop encoder success");
    

    if (m_notify)
    {
        m_notify->OnStopReplayBuffer(0, 0);
    }
   
    LOG_INFO(L"StopReplayBuffer success");
}

void DeskCapEncoder::StartRecord(int weight, int height, int fps, const char* filePath, bool inputAudio, bool outputAudio)
{
    int errcode = 1;
    bool bRet;
    try
    {
        LOG_INFO(L"StartRecord width:%d,height:%d fps:%d recordPath:%s  isCaptureAudio:%d CaptureOutAudio:%d start",
            weight, height, fps, util::desktop::Utf8String2Wstring(filePath).c_str(), inputAudio ? 1 : 0, outputAudio ? 1 : 0);
       
        if (!m_bCaptureInit)
        {
            if (m_notify)
            {
                m_notify->OnStartRecord(errcode, 0, GetTickCount64());
                return;
            }
        }
        //去掉.mp4
        std::string toRemove = ".mp4";
        std::string filePathName = filePath;
        int nPos = filePathName.rfind(toRemove);
        if (nPos  != std::string::npos)
        {
            filePathName.erase(nPos, toRemove.length());
        }
        m_recordInfo.width = weight;
        m_recordInfo.height = height;
        m_recordInfo.fps = fps;
        m_recordInfo.filepath = filePathName;
        m_recordInfo.inputAudio = inputAudio;
        m_recordInfo.outputAudio = outputAudio;



        m_encoderParam->width = weight;
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
            LOG_INFO(L"StartRecord encoder bInit=%d", bRet);
        }

        if (bRet && m_encoder)
        {
            bRet = m_encoder->StartEncoder();
            LOG_INFO(L"deskcap encoder start %d",bRet);
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
                errcode = 2;
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
                errcode = 0;
            }
        }

    }
    catch (winrt::hresult_error const& ex)
    {
        winrt::hresult hr = ex.code();
        LOG_INFO(L"winrtexception:%u", ex.code());
        bRet = false;
        errcode = 4;
    }

    
    if (m_notify)
    {
        m_notify->OnStartRecord(errcode, 0, GetTickCount64());
    }
    LOG_INFO(L"StartRecord width:%d,height:%d fps:%d recordPath:%s  isCaptureAudio:%d CaptureOutAudio:%d end",
        weight, height, fps, util::desktop::Utf8String2Wstring(filePath).c_str(), inputAudio ? 1 : 0, outputAudio ? 1 : 0);
}
void DeskCapEncoder::StopEncoder()
{
    try
    {
        if (m_encoder)
        {
            m_encoder->StopEncoder();
            m_encoder = nullptr;
        }
        m_bEncoderInit = false;
        StopAudioCapture();
    }
    catch (winrt::hresult_error const& ex)
    {
        winrt::hresult hr = ex.code();
        LOG_INFO(L"winrtexception:%u", ex.code());
        
    }

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
void DeskCapEncoder::InnerStopCapture()
{


    try
    {
        if (m_capture)
        {
            m_capture->Close();
            m_capture = nullptr;
        }
        m_bCaptureInit = false;
    }
    catch (winrt::hresult_error const& ex)
    {
        winrt::hresult hr = ex.code();
        LOG_INFO(L"winrtexception:%u", ex.code());

    }
    

}

void DeskCapEncoder::StopAudioCapture()
{

    if (m_audioInCapture)
    {
        m_audioInCapture->Stop();
        m_audioInCapture = nullptr;
    }
    if (m_audioOutCapture)
    {
        m_audioOutCapture->Stop();
        m_audioOutCapture = nullptr;
    }
    while (m_audioInList.size() > 0)
    {
        st_audio frame = m_audioInList.front();
        if (frame.pdata)
        {
            delete[] frame.pdata;
        }
        m_audioInList.pop_front();
        m_audioInList.clear();
    }
    while (m_audioOutList.size() > 0)
    {
        st_audio frame = m_audioOutList.front();
        if (frame.pdata)
        {
            delete[] frame.pdata;
        }
        m_audioOutList.pop_front();
        m_audioOutList.clear();
    }
}

void DeskCapEncoder::StopRecord()
{
    LOG_INFO(L"StopRecord");
    int errcode = 0;
    m_bRecord = false;
    if (thRecord)
    {
        thRecord->join();
        m_mp4Encoder.CloseMP4File(m_recordInfo.mp4Handle);
        errcode = 0;
        thRecord = nullptr;
        try
        {
            std::filesystem::path oldfilePath(util::desktop::Utf8String2Wstring(m_recordInfo.filepath));
            std::filesystem::path newfilePath(util::desktop::Utf8String2Wstring(m_recordInfo.filepath + ".mp4"));
            std::filesystem::rename(oldfilePath, newfilePath);
        }
        catch (const std::exception&)
        {
            LOG_ERROR(L"rename:%s to %s failed.", util::desktop::Utf8String2Wstring(m_recordInfo.filepath).c_str(),
                util::desktop::Utf8String2Wstring(m_recordInfo.filepath + ".mp4").c_str());
        }
    }

    StopEncoder();
    
    LOG_INFO(L"StopRecord end");
    if (m_notify && !m_recordInfo.filepath.empty())
    {
        std::string filePath = m_recordInfo.filepath + ".mp4";
        m_notify->OnStopRecord(errcode, 0, filePath.c_str(), GetTickCount64());
        m_recordInfo.filepath = "";
    }
}

void DeskCapEncoder::StartManualRecord(int width, int height, int fps, uint64_t handle, const char* tiltle, int captureType,
    char* filePath, bool isCaptureWithCursor, bool isCaptureWithBorder, bool inputAudio, bool outputAudio)
{
    LOG_INFO(L"StartManualRecord width:%d,height:%d fps:%d recordPath:%s isCaptureWithCursor:%d isCaptureWithBorder:%d isCaptureAudio:%d CaptureOutAudio:%d  title:%s start",
        width, height, fps, util::desktop::Utf8String2Wstring(filePath).c_str(), 
        isCaptureWithCursor?1:0, isCaptureWithBorder?1:0,
        inputAudio ? 1 : 0, outputAudio ? 1 : 0, util::desktop::Utf8String2Wstring(tiltle).c_str());
    int errcode = 1;
    bool bRet = false;

    try
    {
        if (m_capture)
        {
            LOG_INFO(L"StartManualRecord capture have inited\n");
            if (m_notify)
            {
                m_notify->OnStartManualRecord(errcode, 0, GetTickCount64());
            }

            return;
        }

        //去掉.mp4
        std::string toRemove = ".mp4";
        std::string filePathName = filePath;
        int nPos = filePathName.rfind(toRemove);
        if (nPos != std::string::npos)
        {
            filePathName.erase(nPos, toRemove.length());
        }

        m_recordInfo.width = width;
        m_recordInfo.height = height;
        m_recordInfo.fps = fps;
        m_recordInfo.filepath = filePathName;
        m_recordInfo.inputAudio = inputAudio;
        m_recordInfo.outputAudio = outputAudio;

        m_captureType = captureType;

        m_captrueParam->videoWidth = width;
        m_captrueParam->videoHeight = height;

        if (m_captureType == 1)  // 屏幕
        {
            m_captrueParam->capType = CAP_MAINDISPLAY;
            m_monitorName = util::desktop::Utf8String2Wstring(tiltle).c_str();
        }

        else
        {

            m_captrueParam->capType = CAP_WINDOW;
            m_captrueParam->capHwnd = (HWND)handle;
        }


        m_bCaptureInit = CreateDisplayCapture(isCaptureWithBorder, isCaptureWithCursor, util::desktop::Utf8String2Wstring(tiltle));
        if (m_bCaptureInit)
        {

            m_capture->StartCapture(m_captrueParam->videoWidth, m_captrueParam->videoHeight, INFINITE, false);
            

        }


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
    }
    catch (winrt::hresult_error const& ex)
    {
        winrt::hresult hr = ex.code();
        LOG_INFO(L"winrtexception:%u", ex.code());
        bRet = false;
        errcode = 4;

    }
   

    if (m_notify)
    {
        m_notify->OnStartManualRecord(errcode, 0, GetTickCount64());
    }
    LOG_INFO(L"StartManualRecord width:%d,height:%d fps:%d recordPath:%s  isCaptureAudio:%d CaptureOutAudio:%d end",
        width, height, fps, util::desktop::Utf8String2Wstring(filePath).c_str(), inputAudio ? 1 : 0, outputAudio ? 1 : 0);
}
void DeskCapEncoder::StopManualRecord()
{
    LOG_INFO(L"StopManualRecord");
    int errcode = 0;
    m_bRecord = false;
    if (thRecord)
    {
        thRecord->join();
        m_mp4Encoder.CloseMP4File(m_recordInfo.mp4Handle);
        errcode = 0;
        thRecord = nullptr;

        try
        {
            std::filesystem::path oldfilePath(util::desktop::Utf8String2Wstring(m_recordInfo.filepath));
            std::filesystem::path newfilePath(util::desktop::Utf8String2Wstring(m_recordInfo.filepath + ".mp4"));
            std::filesystem::rename(oldfilePath, newfilePath);
        }
        catch (const std::exception&)
        {
            LOG_ERROR(L"rename:%s to %s failed.", util::desktop::Utf8String2Wstring(m_recordInfo.filepath).c_str(),
                util::desktop::Utf8String2Wstring(m_recordInfo.filepath + ".mp4").c_str());
        }
    }

    StopEncoder();
    InnerStopCapture();
    
    //if (m_audioinFile)
    //{
    //    fclose(m_audioinFile);
    //    m_audioinFile = nullptr;
    //}
   
    LOG_INFO(L"StopManualRecord end");
    if (m_notify && !m_recordInfo.filepath.empty())
    {
        std::string filePath = m_recordInfo.filepath + ".mp4";
        m_notify->OnStopManualRecord(errcode, 0, filePath.c_str(), GetTickCount64());
        m_recordInfo.filepath = "";
    }
}


void DeskCapEncoder::WriteFileFun()
{
    while (m_bWrite)
    {
        if (m_mediaList.size() == 0)
        {
            ::WaitForSingleObject(m_writeMp4Event, INFINITE);
        }
        int nRetryCount = 0;
        for (auto it = m_mediaList.begin(); it != m_mediaList.end(); it++)
        {
            if (!m_bWrite)
            {
                break;
            }
            if (it->m_bSave)
            {
                continue;
            }
            LOG_INFO(L"[%d:%d %s] start save\n", it->startTime, it->endTime, util::desktop::Utf8String2Wstring(it->filename).c_str());
            std::list<st_video>::reverse_iterator  findit = m_videoList.rend();
            if (it->writeTime == 0)  //视频文件第一次开始写 需要查找关键帧
            {
                auto reit = m_videoList.rbegin();
                
                while (reit != m_videoList.rend())
                {
                    if (reit->isIDR)
                        findit = reit;
                    if (reit->uiTime <= it->startTime && reit->isIDR)
                    {
                        findit = reit;
                        break;
                    }

                    reit++;
                }
                if (findit != m_videoList.rend())  //找到满足条件关键帧  
                {
                   
                    it->startTime = findit->uiTime;
                    it->writeTime = it->startTime;
                    nRetryCount = 0;
                }
                else   //
                {
                    if(nRetryCount++ >5)
                    {
                        nRetryCount = 0;
                        it = m_mediaList.erase(it);
                        LOG_ERROR(L"[%d:%d %s] can not find idr 5 times quit\n", it->startTime, it->endTime, util::desktop::Utf8String2Wstring(it->filename).c_str());
                        continue;
                    }
                    LOG_ERROR(L"[%d:%d %s]not find idr\n", it->startTime, it->endTime, util::desktop::Utf8String2Wstring(it->filename).c_str());
                    Sleep(500);
                    continue;
                }
                LOG_INFO(L"[%d:%d %s] find idr frame\n", it->startTime, it->endTime, util::desktop::Utf8String2Wstring(it->filename).c_str());
            }
            m_medialstMutex.lock();
            uint64_t endTime = it->endTime;
            m_medialstMutex.unlock();
            if (it->writeTime < endTime && it->writeTime > 0)
            {
                std::list<st_video>::iterator itWrite = m_videoList.begin();
                std::list<st_video>::iterator itPreWrite = m_videoList.begin();
                while (itWrite != m_videoList.end()) // 查找开始写的位置
                {
                    if (itWrite->uiTime >= it->writeTime)
                        break;
                    itWrite++;
                }
                if (itWrite == m_videoList.end())
                    continue;
                it->mp4Handle = m_mp4Encoder.CreateMP4File(it->filename.c_str(), m_encoderParam->width, m_encoderParam->height, m_nTimeScale, m_encoderParam->fps);
                if (it->mp4Handle == MP4_INVALID_FILE_HANDLE)
                {
                    LOG_INFO(L"ERROR:Open file:%s fialed.\n", util::desktop::Utf8String2Wstring(it->filename).c_str());
                    continue;
                }
                int frameCount = 0;
                while (itWrite->uiTime >= it->startTime && itWrite->uiTime <= endTime)  // itWrite != m_videoList.end()
                {
                    // 写视频
                    m_mp4Encoder.WriteH264Data(it->mp4Handle, itWrite->pdata, itWrite->len,itWrite->uiTime);
                    
                    it->writeTime = itWrite->uiTime;
                    itPreWrite = itWrite;
                    itWrite++;
                    while (itWrite == m_videoList.end())
                    {
                        if (!m_bWrite)
                        {
                            break;
                        }
                        Sleep(500);
                        itWrite = itPreWrite;
                        itWrite++;
                    }
                    if (!m_bWrite)
                    {
                        break;
                    }
                    frameCount++;
                    m_medialstMutex.lock();
                    endTime = it->endTime;
                    if (itWrite->uiTime > endTime)
                    {
                        it->m_bSave = true;
                    }
                    m_medialstMutex.unlock();
                }
               

                if (m_audioInCapture)
                {
                    it->mp4AudioInTrack = m_mp4Encoder.WriteAACMetadata(it->mp4Handle, 48000, 2);
                    for (auto ptr = m_audioInList.begin(); ptr != m_audioInList.end(); ptr++)
                    {
                        if (!m_bWrite)
                        {
                            break;
                        }
                        if (ptr->uiTime< it->startTime || ptr->uiTime > it->endTime)
                        {
                            continue;
                        }
                        //写音频
                        m_mp4Encoder.WreiteAACData(it->mp4Handle, it->mp4AudioInTrack, ptr->pdata, ptr->len);

                    }

                }
                if (m_audioOutCapture)
                {
                    it->mp4AudioOutTrack = m_mp4Encoder.WriteAACMetadata(it->mp4Handle, 48000, 2);
                    for (auto ptr = m_audioOutList.begin(); ptr != m_audioOutList.end(); ptr++)
                    {
                        if (!m_bWrite)
                        {
                            break;
                        }
                        if (ptr->uiTime< it->startTime || ptr->uiTime > it->endTime)
                        {
                            continue;
                        }
                        //写音频
                        m_mp4Encoder.WreiteAACData(it->mp4Handle, it->mp4AudioOutTrack, ptr->pdata, ptr->len);

                    }
                }
                m_mp4Encoder.CloseMP4File(it->mp4Handle);
                try
                {
                    std::filesystem::path oldfilePath(util::desktop::Utf8String2Wstring(it->filename));
                    std::filesystem::path newfilePath(util::desktop::Utf8String2Wstring(it->filename + ".mp4"));
                    std::filesystem::rename(oldfilePath, newfilePath);
                }
                catch (const std::exception&)
                {
                    LOG_ERROR(L"rename:%s to %s failed.", util::desktop::Utf8String2Wstring(it->filename).c_str(),
                        util::desktop::Utf8String2Wstring(it->filename + ".mp4").c_str());
                }

                if (m_notify)
                {
                    //推送的时间为当前视频文件的时间长度
                    //uint64_t duration = itWrite->uiTime - it->writeTime;
                    //计算高光时刻相对于文件开始时间偏移量
                    
                    
                    for (auto ptr = it->saveContexts.begin();ptr != it->saveContexts.end();ptr++)
                    {
                        std::vector<uint64_t> offsetFrameTimes;
                        std::vector<uint64_t>&  framesTimes = ptr->second.frameTimes;
                        //std::vector<uint64_t> offsetFrameTimes;
                        for (size_t ind =0;ind<framesTimes.size();ind++)
                        {
                            uint64_t hightTime = framesTimes[ind];
                            uint64_t offset = framesTimes[ind] - it->startTime;
                            offsetFrameTimes.push_back(offset);
                            LOG_INFO(L"[%d:%d %s] start save success contextId:%u,frame:%d  hight:%u offset:%d\n", it->startTime, it->endTime, util::desktop::Utf8String2Wstring(it->filename).c_str(),
                                ptr->first,frameCount, hightTime, offset);
                        }
                        
                        if (!offsetFrameTimes.empty())
                        {
                            std::string fileName = it->filename+".mp4";
                            m_notify->OnSaveReplayBuffer(0, 0, ptr->first, fileName.c_str(), offsetFrameTimes.data(), offsetFrameTimes.size());
                        }
                        
                        
                    }

                    
                    
                }
                //it->m_bSave = true;
                //m_medialstMutex.lock();
                //不清除；再最后停止缓存的时候清除
                //it =  m_mediaList.erase(it);
                //m_medialstMutex.unlock();
                break;
            }
        }
    }
 End:
    LOG_INFO(L"DeskCapEncoder::WriteFileFun End\n");
}

void DeskCapEncoder::RecordFileFun()
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
    LOG_INFO(L"StopRecord:%d", frameCount);
}
FILE* pFile2 = nullptr;
void DeskCapEncoder::EncoderCallback(unsigned char* data, int len, unsigned long long time, bool invalid, bool iskey, int Iinvalid, void* parg)
{
    
    st_video  videodata;
    videodata.len = len;
    videodata.pdata = new unsigned char[len];
    videodata.isIDR = iskey;
    videodata.uiTime = GetTickCount();
    UINT64 currentTime = videodata.uiTime;

    memcpy_s(videodata.pdata,len, data, len);
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
            if (stAudio.uiTime- currentTime> m_cacheMaxSec)
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


void DeskCapEncoder::OutputVideoFun()
{

}

DESKCAPENCODER_API std::shared_ptr<DeskCapEncoderInterface> CreateDeskCapEncoder()
{
    return std::make_shared<DeskCapEncoder>();
}


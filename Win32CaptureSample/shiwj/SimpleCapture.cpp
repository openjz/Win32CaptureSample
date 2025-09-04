#include "pch.h"
#include "DeskCapEncoderInterface.h"
#include "SimpleCapture.h"
#include "SimpleEncoder.h"
#include <timeapi.h>
#include <dwmapi.h>
#include <regex>
#include <shellscalingapi.h>
#pragma  comment(lib, "dwmapi.lib")
std::vector<std::wstring> g_privateAmeExeName;
std::vector<std::wstring> g_fullScreenAmeExeName;
static int g_titleSkipH = 30;
namespace winrt
{
    using namespace Windows::Foundation;
    using namespace Windows::System;
    using namespace Windows::Graphics;
    using namespace Windows::Graphics::Capture;
    using namespace Windows::Graphics::DirectX;
    using namespace Windows::Graphics::DirectX::Direct3D11;
    using namespace Windows::Foundation::Numerics;
    using namespace Windows::UI;
    using namespace Windows::UI::Composition;
}
namespace util
{
    using namespace uwp;
}
BOOL IsWindowCloaked(HWND hwnd)
{
    BOOL isCloaked = FALSE;
    return (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED,
        &isCloaked, sizeof(isCloaked))) && isCloaked);
}
BOOL IsWindowVisibleOnScreen(HWND hwnd)
{
    return IsWindowVisible(hwnd) &&
        !IsWindowCloaked(hwnd);
}
bool InRect(RECT rect, POINT pos)
{
    return (rect.left - pos.x) * (rect.right - pos.x) <= 0 && (rect.top - pos.y) * (rect.bottom - pos.y) <= 0;
}
void trimString(std::wstring& str) {
    size_t start = str.find_first_not_of(L" ");
    if (start == std::wstring::npos) {
        str.clear();
        return;
    }

    size_t end = str.find_last_not_of(L" ");
    str = str.substr(start, end - start + 1);
}

static SIZE GetScreenResolution(HWND hWnd) {
    SIZE size;
    size.cx = size.cy = 0;
    if (!hWnd)
        return size;

    HMONITOR hMonitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFOEX miex;
    miex.cbSize = sizeof(miex);
    if (!GetMonitorInfo(hMonitor, &miex))
        return size;

    DEVMODE dm;
    dm.dmSize = sizeof(dm);
    dm.dmDriverExtra = 0;

    if (!EnumDisplaySettings(miex.szDevice, ENUM_CURRENT_SETTINGS, &dm))
        return size;

    size.cx = dm.dmPelsWidth;
    size.cy = dm.dmPelsHeight;
    return size;
}
float GetScreenDPI(HWND hWnd) {


    DEVICE_SCALE_FACTOR scale;
    HRESULT result = GetScaleFactorForMonitor(MonitorFromWindow(GetDesktopWindow(), MONITOR_DEFAULTTOPRIMARY), &scale);
    int dpiX = scale; // 获取水平DPI
    float dpi = static_cast<float>(dpiX) / 96.0f; // 返回相对于96DPI的缩放比
    //LOG_INFO(L"dpi:%f", dpi);
    //return dpi;

    if (!hWnd)
    {
        hWnd = GetDesktopWindow();
    }

    // 获取窗口当前显示的监视器
    //HWND hWnd = m_capWnd;//根据需要可以替换成自己程序的句柄 
    HMONITOR hMonitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);

    // 获取监视器逻辑宽度与高度
    MONITORINFOEX miex;
    miex.cbSize = sizeof(miex);
    GetMonitorInfo(hMonitor, &miex);
    int cxLogical = (miex.rcMonitor.right - miex.rcMonitor.left);
    int cyLogical = (miex.rcMonitor.bottom - miex.rcMonitor.top);

    // 获取监视器物理宽度与高度
    DEVMODE dm;
    dm.dmSize = sizeof(dm);
    dm.dmDriverExtra = 0;
    EnumDisplaySettings(miex.szDevice, ENUM_CURRENT_SETTINGS, &dm);
    int cxPhysical = dm.dmPelsWidth;
    int cyPhysical = dm.dmPelsHeight;
    double vertScale = ((double)cyPhysical / (double)cyLogical);
    return vertScale > dpi ? vertScale : dpi;
}

bool IsWindowFullScreen(HWND hwnd) {
    // 获取窗口的长和宽
    RECT rect;
    GetWindowRect(hwnd, &rect);
    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;
    // 获取屏幕的长和宽
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    // 如果窗口尺寸与屏幕尺寸相同，则认为是全屏
    return (width == screenWidth && height == screenHeight);
}
int FindExtraWindowHeight(HWND h)
{
    RECT w, c;
    GetWindowRect(h, &w);
    GetClientRect(h, &c);
    //LOG_INFO(L"DPI:%f",GetScreenDPI());
    
    return ((w.bottom - w.top) - (c.bottom - c.top));

   
}

int FindExtraWindowHeightEx(HWND h)
{
    RECT wrect;
    GetWindowRect(h, &wrect);
    RECT crect;
    GetClientRect(h, &crect);
    POINT lefttop = { crect.left, crect.top }; // Practicaly both are 0
    ClientToScreen(h, &lefttop);
    POINT rightbottom = { crect.right, crect.bottom };
    ClientToScreen(h, &rightbottom);

    int left_border = lefttop.x - wrect.left; // Windows 10: includes transparent part
    int right_border = wrect.right - rightbottom.x; // As above
    int bottom_border = wrect.bottom - rightbottom.y; // As above
    int top_border_with_title_bar = lefttop.y - wrect.top; // There is no transparent part
    return top_border_with_title_bar;
}
static void sleepE(int interValue) {
    if (interValue < 0) {
        return;
    }
    timeBeginPeriod(1);
    DWORD dwTime = timeGetTime();
    Sleep(interValue);
    timeEndPeriod(1);

}
SimpleCapture::SimpleCapture(winrt::IDirect3DDevice const& device, winrt::GraphicsCaptureItem const& item, winrt::DirectXPixelFormat pixelFormat,
    IDeskCapEncoderNotify* notifier, HWND hWnd,
    bool isBorderRequest, bool isCurser, std::wstring ameExeName,
    int maxWidth, int maxHeight)
{
    LOG_INFO(L"SimpleCapture");
    //InitializeCriticalSection(&(m_cs));
    m_capWnd = hWnd;
    m_item = item;
    m_device = device;
    m_pixelFormat = pixelFormat;
    m_notify = notifier;

    auto d3dDevice = GetDXGIInterfaceFromObject<ID3D11Device>(m_device);
    d3dDevice->GetImmediateContext(m_d3dContext.put());
   
    m_ameExeName = ameExeName;
    trimString(m_ameExeName);
    g_privateAmeExeName.clear();
    g_fullScreenAmeExeName.clear();
    g_privateAmeExeName.push_back(L"VALORANT");
    g_fullScreenAmeExeName.push_back(L"Counter-Strike 2");
    g_fullScreenAmeExeName.push_back(L"反恐精英：全球攻势");
    g_fullScreenAmeExeName.push_back(L"Dota 2");


    LOG_INFO(L"ameExeName:%s privateExeName:%s", ameExeName.c_str(), g_privateAmeExeName[0].c_str());
    if (m_item)
    {
        //不依赖主线程消息分发
        auto itemSize = m_item.Size();

        //默认取屏幕分辨率
        int screenWdith = maxWidth;// (((float)GetSystemMetrics(SM_CXSCREEN)) * GetScreenDPI(m_capWnd));
        int screenHeight = maxHeight;// (((float)GetSystemMetrics(SM_CYSCREEN)) * GetScreenDPI(m_capWnd));
        LOG_INFO(L"SimpleCapture itemSize:[%d:%d] screenSize:[%d:%d]", itemSize.Width, itemSize.Height,
            screenWdith, screenHeight);
        itemSize.Width = itemSize.Width > screenWdith ? itemSize.Width : screenWdith;
        itemSize.Height = itemSize.Height > screenHeight ? itemSize.Height : screenHeight;
        itemSize.Width += 100;
        itemSize.Height += 100;
        m_framePool = winrt::Direct3D11CaptureFramePool::CreateFreeThreaded(m_device, m_pixelFormat, 2, itemSize);

        LOG_INFO(L"SimpleCapture::SimpleCapture1 \n");
        m_session = m_framePool.CreateCaptureSession(m_item);
        m_session.IsBorderRequired(isBorderRequest);
        m_session.IsCursorCaptureEnabled(isCurser);

        m_lastSize = m_item.Size();
        capture_framepool_trigger_ = m_framePool.FrameArrived(
            winrt::auto_revoke, { this, &SimpleCapture::OnFrameArrived });
            //m_framePool.FrameArrived({ this, &SimpleCapture::OnFrameArrived });

        m_captrueWidth = m_item.Size().Width;
        m_captrueHeight = m_item.Size().Height;

        LOG_INFO(L"SimpleCapture::SimpleCapture \n");
        if (m_session != nullptr)
        {
            LOG_INFO(L"SimpleCapture::SimpleCapture m_session success \n");
        }
        else
        {
            LOG_INFO(L"SimpleCapture::SimpleCapture null \n");
        }
        // WINRT_ASSERT(m_session != nullptr);
    }

    m_colorConv = std::make_unique< RGBToNV12>(d3dDevice.get(), m_d3dContext.get());
    m_colorConv->Init();

    LOG_INFO(L"SimpleCapture::SimpleCapture end \n");
}
void SimpleCapture::IsBorderRequest(bool value)
{
    m_session.IsBorderRequired(value);
}
void SimpleCapture::StartCapture(int outWidth, int outHeight, int internal, bool isCallImage)
{
    LOG_INFO(L"StartCapture \n");
    auto expected = true;
    m_closed.compare_exchange_strong(expected, false);
    m_semaphoreFree = CreateSemaphore(NULL, 1, 1, NULL);
    m_semaphoreFull = CreateSemaphore(NULL, 0, 1, NULL);

    m_captrueEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    m_outputCaptrueEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

    m_outWidth = outWidth;
    m_outHeight = outHeight;
    m_internal = internal;

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;

    desc.Width = m_outWidth;
    desc.Height = m_outHeight;//m_encoderParam->height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    desc.CPUAccessFlags = 0;

    auto d3dDevice = GetDXGIInterfaceFromObject<ID3D11Device>(m_device);
    winrt::check_hresult(d3dDevice->CreateTexture2D(&desc, nullptr, m_scaleTexture.put()));

    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    winrt::check_hresult(d3dDevice->CreateTexture2D(&desc, nullptr, m_currCpuTexture.put()));

    if (isCallImage)
    {
        m_outputThread = std::make_shared<std::thread>(&SimpleCapture::OutputSampleFun, this);
    }

    CheckClosed();
    m_session.StartCapture();
    LOG_INFO(L"StartCapture end\n");
    m_captureState.store((int)CaptureState::Init);

    //  CreateWrite();
}
winrt::ICompositionSurface SimpleCapture::CreateSurface(winrt::Compositor const& compositor)
{
    CheckClosed();
    return util::CreateCompositionSurfaceForSwapChain(compositor, m_swapChain.get());
}
void SimpleCapture::Close()
{
    auto expected = false;
    if (m_closed.compare_exchange_strong(expected, true))
    {
        if (capture_framepool_trigger_)
        {
            capture_framepool_trigger_.revoke();
        }
        if (m_framePool)
            m_framePool.Close();
        try {
            if (m_session)
                m_session.Close();
        }
        catch (...)
        {
            LOG_INFO(L"m_session close exception");
        }
        
        m_swapChain = nullptr;
        m_framePool = nullptr;
        m_session = nullptr;
        m_item = nullptr;
        
    }
    while (m_captureState.load() != (int)CaptureState::Init)
    {
        m_captureState.store((int)CaptureState::Stop);
        Sleep(100);
    }
    SetEvent(m_outputCaptrueEvent);
    if (m_semaphoreFree)
    {
        CloseHandle(m_semaphoreFree);
        m_semaphoreFree = NULL;
    }
    if (m_semaphoreFull)
    {
        CloseHandle(m_semaphoreFull);
        m_semaphoreFull = NULL;
    }
    if (m_captrueEvent)
    {
        CloseHandle(m_captrueEvent);
        m_captrueEvent = NULL;
    }

    if (m_outputCaptrueEvent)
    {
        CloseHandle(m_outputCaptrueEvent);
        m_outputCaptrueEvent = NULL;
    }

  /*  if (m_encoder)
    {
        m_encoder = nullptr;
    }*/

    if (m_outputThread)
    {
        m_outputThread->join();
        m_outputThread = nullptr;
    }

    if (m_colorConv)
    {
        m_colorConv->Cleanup();
        m_colorConv = nullptr;
    }
   
    m_currCpuTexture = nullptr;
    m_scaleTexture = nullptr;
    m_outImageTexture = nullptr;
    m_CapTexture = nullptr;
    m_swapChain = nullptr;
    m_d3dContext = nullptr;
    
    if (m_rgbBuf)
    {
        delete[] m_rgbBuf;
    }
    if (pMouseData)
    {
        free(pMouseData);
        pMouseData = nullptr;
    }

   // m_encoder = nullptr;
}

void SimpleCapture::ResizeSwapChain()
{
    if (m_swapChain)
    {
        winrt::check_hresult(m_swapChain->ResizeBuffers(2, static_cast<uint32_t>(m_lastSize.Width), static_cast<uint32_t>(m_lastSize.Height),
            static_cast<DXGI_FORMAT>(m_pixelFormat), 0));
    }
}

bool SimpleCapture::TryResizeSwapChain(winrt::Direct3D11CaptureFrame const& frame)
{
    auto const contentSize = frame.ContentSize();
    if ((contentSize.Width != m_lastSize.Width) ||
        (contentSize.Height != m_lastSize.Height))
    {
        // The thing we have been capturing has changed size, resize the swap chain to match.
        m_lastSize = contentSize;
        ResizeSwapChain();
        return true;
    }
    return false;
}

bool SimpleCapture::TryUpdatePixelFormat()
{
    auto lock = m_lock.lock_exclusive();
    if (m_pixelFormatUpdate.has_value())
    {
        auto pixelFormat = m_pixelFormatUpdate.value();
        m_pixelFormatUpdate = std::nullopt;
        if (pixelFormat != m_pixelFormat)
        {
            m_pixelFormat = pixelFormat;
            ResizeSwapChain();
            return true;
        }
    }
    return false;
}

bool SimpleCapture::CaptureWindow()
{
    bool bRet = false;
    if (m_capWnd)
    {
        HBITMAP hBitmap;
        if (!LoopRenderWnd(hBitmap))
            return bRet;

        if (!m_currTexture)
        {
            D3D11_TEXTURE2D_DESC desc = {};
            desc.Width = m_captrueWidth;
            desc.Height = m_captrueHeight;
            desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            desc.ArraySize = 1;
            desc.BindFlags = 0;
            desc.MiscFlags = 0;
            desc.SampleDesc.Count = 1;
            desc.SampleDesc.Quality = 0;
            desc.MipLevels = 1;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            desc.Usage = D3D11_USAGE_STAGING;

            auto d3dDevice = GetDXGIInterfaceFromObject<ID3D11Device>(m_device);
            winrt::check_hresult(d3dDevice->CreateTexture2D(&desc, nullptr, m_currTexture.put()));

        }

        D3D11_MAPPED_SUBRESOURCE resource;
        UINT subresource = D3D11CalcSubresource(0, 0, 0);
        m_d3dContext->Map(m_currTexture.get(), subresource, D3D11_MAP_WRITE, 0, &resource);

        BITMAP bmp;
        GetObject(hBitmap, sizeof(BITMAP), &bmp);
        BITMAPINFO pbi;
        pbi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        pbi.bmiHeader.biWidth = bmp.bmWidth;
        pbi.bmiHeader.biHeight = -bmp.bmHeight;
        pbi.bmiHeader.biPlanes = bmp.bmPlanes;
        pbi.bmiHeader.biBitCount = 32;
        pbi.bmiHeader.biCompression = BI_RGB;
        if (m_rgbBuf == nullptr)
        {
            m_rgbBuf = new char[bmp.bmWidth * bmp.bmHeight * 4];
        }

        HDC hDC = GetDC(nullptr);
        GetDIBits(hDC, hBitmap, 0, bmp.bmHeight, m_rgbBuf, &pbi, DIB_RGB_COLORS);
        bool bCapCousor = CheckMouse(hBitmap);
        if (bCapCousor)
        {
            overlyMouser(m_rgbBuf);
        }
        BYTE* destptr = reinterpret_cast<BYTE*>(resource.pData);
        memcpy_s(destptr, resource.RowPitch * bmp.bmHeight, m_rgbBuf, bmp.bmWidth * bmp.bmHeight * 4);
        DeleteObject(hBitmap);
        //   m_semaphoreFree->Wait();
    }
}
void SimpleCapture::RenderTexture(winrt::com_ptr<ID3D11Texture2D>  texture)
{
    if (m_previewHwnd)
    {
        winrt::com_ptr<ID3D11Texture2D> backBuffer;
        winrt::check_hresult(m_swapChain->GetBuffer(0, winrt::guid_of<ID3D11Texture2D>(), backBuffer.put_void()));
        m_d3dContext->CopyResource(backBuffer.get(), texture.get());
        DXGI_PRESENT_PARAMETERS presentParameters{};
        m_swapChain->Present1(1, 0, &presentParameters);
    }
}
HRESULT SaveBitmapToFile(unsigned char* bmBits, int bmWidth, int bmHeight, const wchar_t* filePath)
{
    HRESULT result = S_OK;

    // 创建位图文件头
    BITMAPFILEHEADER fileHeader;
    fileHeader.bfType = 0x4D42;  // "BM"
    fileHeader.bfSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + bmWidth * bmHeight;
    fileHeader.bfReserved1 = 0;
    fileHeader.bfReserved2 = 0;
    fileHeader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);

    // 创建位图信息头
    BITMAPINFOHEADER infoHeader;
    infoHeader.biSize = sizeof(BITMAPINFOHEADER);
    infoHeader.biWidth = bmWidth;
    infoHeader.biHeight = -bmHeight;
    infoHeader.biPlanes = 1;
    infoHeader.biBitCount = 32;
    infoHeader.biCompression = BI_RGB;
    infoHeader.biSizeImage = 0;
    infoHeader.biXPelsPerMeter = 0;
    infoHeader.biYPelsPerMeter = 0;
    infoHeader.biClrUsed = 0;
    infoHeader.biClrImportant = 0;

    // 创建文件并写入位图数据
    HANDLE fileHandle = CreateFileW(filePath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (fileHandle != INVALID_HANDLE_VALUE)
    {
        DWORD bytesWritten = 0;
        WriteFile(fileHandle, &fileHeader, sizeof(BITMAPFILEHEADER), &bytesWritten, NULL);
        WriteFile(fileHandle, &infoHeader, sizeof(BITMAPINFOHEADER), &bytesWritten, NULL);
        WriteFile(fileHandle, bmBits, bmWidth * bmHeight * 4, &bytesWritten, NULL);
        CloseHandle(fileHandle);
    }
    else
    {
        result = E_FAIL;
    }

    return result;
}

void SimpleCapture::SaveBitMap(winrt::com_ptr<ID3D11Texture2D>  texture)
{
    HRESULT hr;
    D3D11_TEXTURE2D_DESC desc = {};
    D3D11_MAPPED_SUBRESOURCE mappedResource;
    hr = m_d3dContext->Map(texture.get(), 0, D3D11_MAP_READ, 0, &mappedResource);
    if (FAILED(hr)) {
        return;
    }
    texture->GetDesc(&desc);
    size_t rowPitch = mappedResource.RowPitch;
    size_t depthPitch = mappedResource.DepthPitch;
    std::vector<uint8_t> cpuData(depthPitch);
    uint8_t* pSrcRow = (uint8_t*)mappedResource.pData;

    for (UINT row = 0; row < desc.Height; row++) {
        memcpy_s(cpuData.data() + row * desc.Width * 4, depthPitch, pSrcRow, desc.Width * 4);
        pSrcRow += rowPitch;
    }
    unsigned char* pbmBits = static_cast<unsigned char*>(mappedResource.pData);
    SaveBitmapToFile(pbmBits, m_outWidth, m_outHeight, L"test.bmp");

}
void SimpleCapture::OutputSampleFun()
{
    HRESULT hr;
    D3D11_TEXTURE2D_DESC desc = {};
    //SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    while (!m_closed.load())
    {
        WaitForSingleObject(m_outputCaptrueEvent, INFINITE);
        m_nCapH = GetCapHight();
        //EnterCriticalSection(&(m_cs));
        winrt::com_ptr<ID3D11Texture2D> cpuTexture = m_outImageTexture;
       // LeaveCriticalSection(&m_cs);
        if (!cpuTexture)
        {
            continue;
        }
        if (!m_colorConv || m_colorConv->Convert(cpuTexture.get(), m_scaleTexture.get(), m_nFrameW, m_nFrameH, 0, m_nCapH) != S_OK)
        {
            LOG_ERROR(L"convert exception");
            continue;
        }

        m_d3dContext->CopyResource(m_currCpuTexture.get(), m_scaleTexture.get());
        D3D11_MAPPED_SUBRESOURCE mappedResource;
        hr = m_d3dContext->Map(m_currCpuTexture.get(), 0, D3D11_MAP_READ, 0, &mappedResource);
        if (FAILED(hr)) {
            continue;
        }
        m_currCpuTexture->GetDesc(&desc);
        size_t rowPitch = mappedResource.RowPitch;
        size_t depthPitch = mappedResource.DepthPitch;
        std::vector<uint8_t> cpuData(depthPitch);
        uint8_t* pSrcRow = (uint8_t*)mappedResource.pData;
        
        for (UINT row = 0; row < desc.Height; row++) {
            memcpy_s(cpuData.data() + row * desc.Width * 4, depthPitch, pSrcRow, desc.Width * 4);
            pSrcRow += rowPitch;
        }
        m_d3dContext->Unmap(m_currCpuTexture.get(), 0);
      // unsigned char* pbmBits = static_cast<unsigned char*>(mappedResource.pData);
      // wchar_t fileName[256];
      // wsprintf(fileName, L"C:\\Windows\\temp\\test\\%u.bmp", GetTickCount());
      // SaveBitmapToFile(pbmBits, m_outWidth, m_outHeight, fileName);
        if (m_notify)
        {


            // DBG_LogInfo("OnImageNotify");
            m_notify->OnImageDataRecv(cpuData.data(), cpuData.size(), 0, GetTickCount64());

            // DBG_LogInfo("OnImageNotify");
        }
    }
}

int SimpleCapture::GetCapHight()
{
    int captionHeight = 0;
    if (m_capWnd)
    {
        bool isFullScreen = IsWindowFullScreen(m_capWnd);
        auto ptr = std::find_if(g_privateAmeExeName.begin(), g_privateAmeExeName.end(), [this](std::wstring& ameExeName)
            {return this->m_ameExeName == ameExeName; });


        if (ptr != g_privateAmeExeName.end())
        {
            // LOG_INFO(L"ameExeName:%s is not standard window", m_ameExeName.c_str());
            captionHeight = (((float)FindExtraWindowHeight(m_capWnd)) * GetScreenDPI(m_capWnd));// +g_titleSkipH;
            // LOG_INFO(L"ameExeName:%s 窗口标题栏高度：%d", m_ameExeName.c_str(), captionHeight);
        }
        else
        {
            ptr = std::find_if(g_fullScreenAmeExeName.begin(), g_fullScreenAmeExeName.end(), [this](std::wstring& ameExeName)
                {return this->m_ameExeName == ameExeName; });
            if (ptr != g_fullScreenAmeExeName.end())
            {
                if (isFullScreen)
                {
                    return captionHeight;
                }
            }
            LONG style = GetWindowLongPtr(m_capWnd, GWL_STYLE);
            if (style & WS_CAPTION)
            {

                //LOG_INFO(L"ameExeName:%s 窗口标题栏未隐藏", m_ameExeName.c_str());
                //captionHeight =((float)FindExtraWindowHeight(m_capWnd)) * GetScreenDPI(m_capWnd);//FindExtraWindowHeight(m_capWnd) * GetScreenDPI(m_capWnd);
                captionHeight = ((float)FindExtraWindowHeightEx(m_capWnd));
                ////captionHeight = GetSystemMetrics(SM_CYCAPTION)* GetScreenDPI(m_capWnd);//FindExtraWindowHeight(m_capWnd) * 1.25;//GetScreenDPI(m_capWnd);
            }

        }


    }
    return captionHeight;
}

void SimpleCapture::OnFrameArrived(winrt::Direct3D11CaptureFramePool const& sender, winrt::IInspectable const&)
{
    //最大100帧
    static int m_frameTime = 16;
    static clock_t  startClock = 0;

    long     tGap = 0;
    clock_t currTime = clock();
   

    if (m_closed.load())
    {
        LOG_INFO(L"OnFrameArrived closed");
        m_captureState.store((int)CaptureState::Init);
        SetEvent(m_outputCaptrueEvent);
        return;
    }


    static  int startTime = GetTickCount();
    auto frame = sender.TryGetNextFrame();
    auto const contentSize = frame.ContentSize();
    auto surfaceTexture = GetDXGIInterfaceFromObject<ID3D11Texture2D>(frame.Surface());
    m_nFrameW = contentSize.Width;
    m_nFrameH = contentSize.Height;
    
    if (!m_currTexture && m_device)
    {
        D3D11_TEXTURE2D_DESC desc = {};
        surfaceTexture->GetDesc(&desc);
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags = 0;
        desc.MiscFlags = 0;
        auto d3dDevice = GetDXGIInterfaceFromObject<ID3D11Device>(m_device);
        winrt::check_hresult(d3dDevice->CreateTexture2D(&desc, nullptr, m_currTexture.put()));
    }
    if (!m_outImageTexture && m_device)
    {

        auto d3dDevice = GetDXGIInterfaceFromObject<ID3D11Device>(m_device);
        winrt::com_ptr<ID3D11Texture2D> currentTexture = m_currTexture;
        D3D11_TEXTURE2D_DESC desc = {};
        currentTexture->GetDesc(&desc);
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET;
        desc.CPUAccessFlags = 0;

        winrt::check_hresult(d3dDevice->CreateTexture2D(&desc, nullptr, m_outImageTexture.put()));
    }
    if (startClock != 0)
    {
        tGap = currTime - startClock;
        if (tGap > m_frameTime)
        {
            m_d3dContext->CopyResource(m_currTexture.get(), surfaceTexture.get());  //m_currTexture
            startClock = clock();
        }
    }
    else
    {
        m_d3dContext->CopyResource(m_currTexture.get(), surfaceTexture.get());  //m_currTexture
        startClock = clock();
    }

   

    if (GetTickCount() - startTime > m_internal)
    {
        
        m_d3dContext->CopyResource(m_outImageTexture.get(), surfaceTexture.get());  //m_currTexture
        SetEvent(m_outputCaptrueEvent);
        startTime = GetTickCount();

    }
    if (m_previewHwnd)  // 
    {
        //       ii = 0;
        winrt::com_ptr<ID3D11Texture2D> backBuffer;
        winrt::check_hresult(m_swapChain->GetBuffer(0, winrt::guid_of<ID3D11Texture2D>(), backBuffer.put_void()));
        m_d3dContext->CopyResource(backBuffer.get(), surfaceTexture.get());
        DXGI_PRESENT_PARAMETERS presentParameters{};
        m_swapChain->Present1(1, 0, &presentParameters);
    }
    SetEvent(m_captrueEvent);
    /*if (m_lastSize.Width != contentSize.Width ||
        m_lastSize.Height != contentSize.Height)
    {
        m_framePool.Recreate(m_device, m_pixelFormat, 2, contentSize);
    }*/

}
ID3D11Texture2D* SimpleCapture::CaptureNextEx()
{

    if (WaitForSingleObject(m_captrueEvent, 1000) != WAIT_OBJECT_0)
    {
        return nullptr;
    }

    if (m_closed.load() || !m_currTexture)
    {
        return nullptr;
    }
   
    //EnterCriticalSection(&(m_cs));
    winrt::com_ptr<ID3D11Texture2D> currentTexture = m_currTexture;
    //LeaveCriticalSection(&m_cs);
    if (!currentTexture)
    {
        return nullptr;
    }
    if (!m_CapTexture)
    {
        auto d3dDevice = GetDXGIInterfaceFromObject<ID3D11Device>(m_device);
        winrt::com_ptr<ID3D11Texture2D> outTexture;
        D3D11_TEXTURE2D_DESC desc = {};
        currentTexture->GetDesc(&desc);
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET;
        desc.CPUAccessFlags = 0;
        desc.MiscFlags = 0;

        winrt::check_hresult(d3dDevice->CreateTexture2D(&desc, nullptr, m_CapTexture.put()));
    }
    
    m_d3dContext->CopyResource(m_CapTexture.get(), currentTexture.get());


    return m_CapTexture.get();
}
winrt::com_ptr<ID3D11Texture2D> SimpleCapture::CaptureNext()
{
    // auto lock = m_lockSurface.lock_exclusive();
    
    if (m_closed.load())
    {
        return nullptr;
    }
    if (!m_currTexture)
    {
        WaitForSingleObject(m_captrueEvent, INFINITE);
    }
    //EnterCriticalSection(&(m_cs));
    winrt::com_ptr<ID3D11Texture2D> currentTexture = m_currTexture;
    //LeaveCriticalSection(&m_cs);
    if (!currentTexture)
    {
        return nullptr;
    }
    m_nCapH = GetCapHight();
    auto d3dDevice = GetDXGIInterfaceFromObject<ID3D11Device>(m_device);
    winrt::com_ptr<ID3D11Texture2D> outTexture;
    D3D11_TEXTURE2D_DESC desc = {};
    currentTexture->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    desc.CPUAccessFlags = 0;
    desc.MiscFlags = 0;

    winrt::check_hresult(d3dDevice->CreateTexture2D(&desc, nullptr, outTexture.put()));
    m_d3dContext->CopyResource(outTexture.get(), currentTexture.get());
    

    return outTexture;
}

void SimpleCapture::SetCapWnd(HWND mhwnd, HWND viewWnd)
{
    m_capWnd = mhwnd;
    m_viewWnd = viewWnd;
    /* GetWindowThreadProcessId(mhwnd, &m_dwPid);

     SIZE deskSize = GetScreenResolution(mhwnd);*/

     /* HDC hScreenDC = GetWindowDC(GetDesktopWindow());
      m_captrueWidth = GetDeviceCaps(hScreenDC, HORZRES);
      m_captrueHeight = GetDeviceCaps(hScreenDC, VERTRES);*/

      // m_captrueWidth = deskSize.cx;// deskSize.cx;
     //  m_captrueHeight = deskSize.cy; // deskSize.cy;
       //GetWindowRect(mhwnd, &m_capRect);
}
void SimpleCapture::RenderWnd(HWND hWnd)
{
    HwndHithumb hThumb;
    hThumb.m_pHitHumbNail = NULL;
    m_mpThumb.insert(std::make_pair(hWnd, hThumb));
    HwndHithumb& rhThumb = m_mpThumb.find(hWnd)->second;
    DwmRegisterThumbnail(m_viewWnd, hWnd, &(rhThumb.m_pHitHumbNail));

    RECT rcSource, rcDest;
    GetWindowRect(hWnd, &rcSource);
    rcDest.left = rcSource.left * 1; rcDest.right = rcSource.right * 1;
    rcDest.top = rcSource.top * 1; rcDest.bottom = rcSource.bottom * 1;
    m_CapWindowVec.push_back(rcSource);


    DWM_THUMBNAIL_PROPERTIES* pDwnProperty = &(rhThumb.m_pPorperty);
    pDwnProperty->dwFlags = DWM_TNP_RECTDESTINATION | DWM_TNP_VISIBLE | DWM_TNP_OPACITY | DWM_TNP_SOURCECLIENTAREAONLY;
    pDwnProperty->fVisible = TRUE;
    pDwnProperty->opacity = 255;
    pDwnProperty->rcDestination = rcDest;
    pDwnProperty->rcSource = rcSource;
    pDwnProperty->fSourceClientAreaOnly = FALSE;
    DwmUpdateThumbnailProperties(rhThumb.m_pHitHumbNail, pDwnProperty);
}
bool SimpleCapture::LoopRenderWnd(HBITMAP& hBitmap)
{
    for (const auto& thumb : m_mpThumb)
    {
        DwmUnregisterThumbnail(thumb.second.m_pHitHumbNail);
    }
    m_mpThumb.clear();

    HWND hDeskWnd = GetDesktopWindow(); // GetDesktopWindow();
    HWND hFirstWnd = GetWindow(hDeskWnd, GW_CHILD);
    HWND hNextWnd = GetWindow(hFirstWnd, GW_HWNDLAST);
    m_vEnumWnds.clear();
    DWORD m_findPid;
    do {
        GetWindowThreadProcessId(hNextWnd, &m_findPid);
        if (m_dwPid == m_findPid && IsWindowVisibleOnScreen(hNextWnd))
            m_vEnumWnds.push_back(hNextWnd);
        hFirstWnd = hNextWnd;
    } while (hNextWnd = GetNextWindow(hFirstWnd, GW_HWNDPREV));

    m_CapWindowVec.clear();
    // m_vEnumWnds_New.clear();
    for (auto iter = m_vEnumWnds.cbegin(); iter != m_vEnumWnds.cend(); ++iter)
    {
        if (*iter == NULL)
        {
            m_vEnumWnds.clear();
            m_vEnumWnds_New.clear();
            return false;
        }
        /* if (m_mpThumb.find(*iter) == m_mpThumb.end())
         {
             m_vEnumWnds_New.push_back(*iter);
         }*/
        RenderWnd(*iter);
    }
    // drawBlack();
    HDC hdc = GetWindowDC(m_viewWnd);
    HDC hMemDC = CreateCompatibleDC(hdc);
    RECT rct;
    GetWindowRect(m_viewWnd, &rct);
    hBitmap = CreateCompatibleBitmap(hdc, rct.right - rct.left, rct.bottom - rct.top);
    SelectObject(hMemDC, hBitmap);

    PrintWindow(m_viewWnd, hMemDC, PW_RENDERFULLCONTENT);
    /* char path[255];
     sprintf_s(path, 255, "tmp%d.bmp", ii);
     SaveBmp(hBitmap, path);*/
     // DeleteObject(hBitmap);
    DeleteDC(hMemDC);
    DeleteDC(hdc);

    m_vEnumWnds.clear();
    Sleep(30);
    return true;
}

bool SimpleCapture::CheckMouse(HBITMAP& hBitmap)
{
    ci.cbSize = sizeof(CURSORINFO);
    GetCursorInfo(&ci);
    bool bInRect = false;
    for (int i = 0; i < m_CapWindowVec.size(); i++)
    {
        if (InRect(m_CapWindowVec[i], ci.ptScreenPos))
        {
            bInRect = true;
            break;
        }
    }
    if (bInRect)
    {
        ICONINFO IconInfo;
        if (GetIconInfo(ci.hCursor, &IconInfo))
        {
            BITMAP bmp = { 0 };
            GetObject(IconInfo.hbmColor, sizeof(bmp), &bmp);
            if (pMouseData && bmp.bmWidth * bmp.bmHeight * 4 != iMouseLeng)
            {
                free(pMouseData);
                pMouseData = nullptr;
            }
            if (pMouseData == nullptr)
                pMouseData = (char*)malloc(bmp.bmWidth * bmp.bmHeight * 4);
            iMouseLeng = bmp.bmWidth * bmp.bmHeight * 4;
            GetBitmapBits(IconInfo.hbmColor, bmp.bmWidth * bmp.bmHeight * 4, pMouseData);
            mouse_width = bmp.bmWidth;
            mouse_height = bmp.bmHeight;
            ci.ptScreenPos.x -= IconInfo.xHotspot;
            ci.ptScreenPos.y -= IconInfo.yHotspot;

            if (IconInfo.hbmMask != NULL)
            {
                DeleteObject(IconInfo.hbmMask);
            }
            if (IconInfo.hbmColor != NULL)
            {
                DeleteObject(IconInfo.hbmColor);
            }
        }
    }
    return bInRect;
}

void SimpleCapture::overlyMouser(char* pData)
{
    int x = ci.ptScreenPos.x;
    int y = ci.ptScreenPos.y;
    char* pDeskData = pData; // (uchar*)vBufferRgb;
    if (x >= 0 && x <= m_captrueWidth && y >= 0 && y <= m_captrueHeight)
    {
        for (int i = 0; i < mouse_height; i++)
            for (int j = 0; j < mouse_width; j++)
            {
                if (pMouseData[i * mouse_width * 4 + j * 4 + 3])
                {
                    if (y + i < m_captrueHeight && x + j < m_captrueWidth)
                    {
                        pDeskData[((y + i) * m_captrueWidth + x) * 4 + 4 * j] = pMouseData[i * mouse_width * 4 + j * 4];
                        pDeskData[((y + i) * m_captrueWidth + x) * 4 + 4 * j + 1] = pMouseData[i * mouse_width * 4 + j * 4 + 1];
                        pDeskData[((y + i) * m_captrueWidth + x) * 4 + 4 * j + 2] = pMouseData[i * mouse_width * 4 + j * 4 + 2];
                        pDeskData[((y + i) * m_captrueWidth + x) * 4 + 4 * j + 3] = pMouseData[i * mouse_width * 4 + j * 4 + 3];
                    }
                }
            }
    }
    SecureZeroMemory(pMouseData, iMouseLeng);
}

void SimpleCapture::drawBlack()
{
    RECT rc;
    HDC hdc = GetWindowDC(m_viewWnd);
    HBRUSH hbrBlack = (HBRUSH)GetStockObject(BLACK_BRUSH); // WHITE_BRUSH  BLACK_BRUSH
    GetClientRect(m_viewWnd, &rc);
    //  SetMapMode(hdc, MM_ANISOTROPIC);
    //  SetWindowExtEx(hdc, 100, 100, NULL);
    //  SetViewportExtEx(hdc, rc.right, rc.bottom, NULL);
    FillRect(hdc, &rc, hbrBlack);
    DeleteDC(hdc);
}

void SimpleCapture::SetPreviewWnd(HWND vhwnd)
{
    m_previewHwnd = vhwnd;
    auto d3dDevice = GetDXGIInterfaceFromObject<ID3D11Device>(m_device);
    m_swapChain = util::desktop::CreateDXGISwapChainForWindow(d3dDevice, m_item.Size().Width, m_item.Size().Height, static_cast<DXGI_FORMAT>(m_pixelFormat), 2, vhwnd);
}

winrt::Windows::Graphics::SizeInt32 SimpleCapture::GetCaptrueSize()
{
    winrt::Windows::Graphics::SizeInt32  capSize;
    capSize.Width = m_captrueWidth;
    capSize.Height = m_captrueHeight;
    return capSize;
}



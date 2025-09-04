#pragma once
#include "Semaphore.h"
#include <set>
#include <vector>

#include "Preproc.h"
enum class CaptureState
{
    Init,
    Running,
    Stop
};
struct HwndHithumb
{
    HTHUMBNAIL					m_pHitHumbNail;
    DWM_THUMBNAIL_PROPERTIES	m_pPorperty;
};
class IDeskCapEncoderNotify;
class SimpleEncoder;
class SimpleCapture
{
public:
    SimpleCapture(
        winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice const& device,
        winrt::Windows::Graphics::Capture::GraphicsCaptureItem const& item,
        winrt::Windows::Graphics::DirectX::DirectXPixelFormat pixelFormat, IDeskCapEncoderNotify* notifier,HWND hWnd,
        bool isBorderRequest,bool isCurser,std::wstring ameExeName,
        int maxWidth,int maxHeight);
    ~SimpleCapture() { 
        Close(); 
        //DeleteCriticalSection(&m_cs);
        LOG_INFO(L"~SimpleCapture");
    }

    void StartCapture(int outWidth,int outHeight,int internal,bool isCallImage);
    winrt::Windows::UI::Composition::ICompositionSurface CreateSurface(
        winrt::Windows::UI::Composition::Compositor const& compositor);

    bool IsCursorEnabled() { CheckClosed(); return m_session.IsCursorCaptureEnabled(); }
	void IsCursorEnabled(bool value) { CheckClosed(); m_session.IsCursorCaptureEnabled(value); }
    winrt::Windows::Graphics::Capture::GraphicsCaptureItem CaptureItem() { return m_item; }
    void IsBorderRequest(bool value);
    winrt::com_ptr<ID3D11Texture2D> CaptureNext();
    ID3D11Texture2D* CaptureNextEx();

    void SetPixelFormat(winrt::Windows::Graphics::DirectX::DirectXPixelFormat pixelFormat)
    {
        CheckClosed();
        auto lock = m_lock.lock_exclusive();
        m_pixelFormatUpdate = pixelFormat;
    }
    void Close();
    void SetCapWnd(HWND mhwnd,HWND viewWnd);
    void SetPreviewWnd(HWND vhwnd);
    winrt::Windows::Graphics::SizeInt32 GetCaptrueSize();
    //void SetEncoderTarget(std::shared_ptr<SimpleEncoder> encoder) { m_encoder = encoder; }
    void SetCaptrueGapTime(int gapTime);
private:
    void OnFrameArrived(
        winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool const& sender,
        winrt::Windows::Foundation::IInspectable const& args);

    inline void CheckClosed()
    {
        if (m_closed.load() == true)
        {
            throw winrt::hresult_error(RO_E_CLOSED);
        }
    }

    void ResizeSwapChain();
    bool TryResizeSwapChain(winrt::Windows::Graphics::Capture::Direct3D11CaptureFrame const& frame);
    bool TryUpdatePixelFormat();
    void RenderWnd(HWND hWnd);
    bool LoopRenderWnd(HBITMAP& hBitmap);
    bool CaptureWindow();
    bool CheckMouse(HBITMAP& hBitmap);
    void overlyMouser(char* pData);
    void drawBlack();
    void RenderTexture(winrt::com_ptr<ID3D11Texture2D>  texture);
    void OutputSampleFun();
    void SaveBitMap(winrt::com_ptr<ID3D11Texture2D>  texture);
    int GetCapHight();
    //void CreateWrite();
private:
    winrt::Windows::Graphics::Capture::GraphicsCaptureItem m_item{ nullptr };
    winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool m_framePool{ nullptr };
    winrt::Windows::Graphics::Capture::GraphicsCaptureSession m_session{ nullptr };
    winrt::Windows::Graphics::SizeInt32 m_lastSize;

    winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice m_device{ nullptr };
    winrt::com_ptr<IDXGISwapChain1> m_swapChain{ nullptr };
    winrt::com_ptr<ID3D11DeviceContext> m_d3dContext{ nullptr };
    winrt::Windows::Graphics::DirectX::DirectXPixelFormat m_pixelFormat;

    wil::srwlock m_lockSurface;
    winrt::com_ptr<ID3D11Texture2D>  m_currTexture=nullptr;
    winrt::com_ptr<ID3D11Texture2D>  m_currCpuTexture=nullptr;
    winrt::com_ptr<ID3D11Texture2D>  m_scaleTexture = nullptr;
    winrt::com_ptr<ID3D11Texture2D>  m_outImageTexture = nullptr;
    winrt::com_ptr<ID3D11Texture2D>  m_CapTexture = nullptr;

    wil::srwlock m_lock;
    std::optional<winrt::Windows::Graphics::DirectX::DirectXPixelFormat> m_pixelFormatUpdate = std::nullopt;

    std::atomic<bool> m_closed = false;
    std::atomic<bool> m_captureNextImage = false;
   // std::mutex  m_captrueMutex;
    std::atomic<int> m_captureState;

   // std::shared_ptr<MySemaphore> m_semaphoreFree;
   // std::unique_ptr<MySemaphore> m_semaphoreFull;

    HANDLE        m_semaphoreFree = NULL;
    HANDLE        m_semaphoreFull = NULL;
    HANDLE        m_captrueEvent = NULL;
    HANDLE        m_outputCaptrueEvent = NULL;

    std::set<HWND>   m_disabelWndSet;
    std::set<HWND>   m_enableWndSet;
    
    std::vector<RECT>  m_CapWindowVec;
    int              m_captype = 2; //CAP_WINDOW;
   // std::shared_ptr<SimpleEncoder> m_encoder = nullptr;
public:
    HWND             m_capWnd = nullptr;
    HWND             m_viewWnd = nullptr;
    HWND             m_previewHwnd = nullptr;
  //  RECT             m_capRect;
    DWORD            m_dwPid = 0;

    std::map<HWND, HwndHithumb>		m_mpThumb;
    std::vector<HWND>				m_vEnumWnds;
    std::vector<HWND>				m_vEnumWnds_New;
    char                           *m_rgbBuf = nullptr;
    char* pMouseData = nullptr;
    int iMouseLeng = 0;
    int mouse_width;
    int mouse_height;
    CURSORINFO ci;
    int m_captrueWidth = 0;
    int m_captrueHeight = 0;

    int m_outWidth = 0;
    int m_outHeight = 0;
    int m_internal = 0;// Ê±¼ä¼ä¸ô

    std::unique_ptr<RGBToNV12>   m_colorConv = nullptr;
    std::shared_ptr<std::thread>  m_outputThread = nullptr;
    IDeskCapEncoderNotify* m_notify = nullptr;

    /*IMFSinkWriter* pSinkWriter = nullptr;
    DWORD           m_streamIndex;*/
    int m_wdith;
    int m_height;

    LONGLONG rtStart = 0;
    //CRITICAL_SECTION m_cs;
    int m_nFrameW;
    int m_nFrameH;
    int m_nCapH = 0;
    std::wstring m_ameExeName;
    winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool::
        FrameArrived_revoker capture_framepool_trigger_;
    
};

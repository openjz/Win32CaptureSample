#pragma once
#include <winrt/Windows.Graphics.Capture.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.capture.h>
#include "Log.hpp"
namespace util
{
    inline bool CreateCaptureItemForWindow(HWND hwnd, winrt::Windows::Graphics::Capture::GraphicsCaptureItem& item)
    {
        

       
        auto interop_factory = winrt::get_activation_factory<winrt::Windows::Graphics::Capture::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
        //winrt::Windows::Graphics::Capture::GraphicsCaptureItem item = { nullptr };
        try
        {
            
            HRESULT hr = interop_factory->CreateForWindow(hwnd, winrt::guid_of<ABI::Windows::Graphics::Capture::IGraphicsCaptureItem>(), winrt::put_abi(item));
            winrt::check_hresult(hr);
        }
        catch (winrt::hresult_error const& ex)
        {
            winrt::hresult hr = ex.code(); // HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND).
            LOG_ERROR(L"CreateCaptureItemForWindow failed.%u ",hr);
            return false;
        }
        catch (...)
        {
            LOG_ERROR(L"CreateCaptureItemForWindow failed known exception");
            return false;
        }
       
        
        return true;
    }

    inline auto CreateCaptureItemForMonitor(HMONITOR hmon)
    {
        auto interop_factory = winrt::get_activation_factory<winrt::Windows::Graphics::Capture::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
        winrt::Windows::Graphics::Capture::GraphicsCaptureItem item = { nullptr };
        winrt::check_hresult(interop_factory->CreateForMonitor(hmon, winrt::guid_of<ABI::Windows::Graphics::Capture::IGraphicsCaptureItem>(), winrt::put_abi(item)));
        
        return item;
    }
}

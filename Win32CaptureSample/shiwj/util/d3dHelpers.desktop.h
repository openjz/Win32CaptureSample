#pragma once
#include <d3d11.h>
#include <dxgi1_2.h>

namespace util::desktop
{
    inline auto CreateDXGISwapChainForWindow(winrt::com_ptr<ID3D11Device> const& device, const DXGI_SWAP_CHAIN_DESC1* desc, HWND window)
    {
        auto dxgiDevice = device.as<IDXGIDevice2>();
        winrt::com_ptr<IDXGIAdapter> adapter;
        winrt::check_hresult(dxgiDevice->GetParent(winrt::guid_of<IDXGIAdapter>(), adapter.put_void()));
        winrt::com_ptr<IDXGIFactory2> factory;
        winrt::check_hresult(adapter->GetParent(winrt::guid_of<IDXGIFactory2>(), factory.put_void()));

        winrt::com_ptr<IDXGISwapChain1> swapchain;
        winrt::check_hresult(factory->CreateSwapChainForHwnd(device.get(), window, desc, nullptr, nullptr, swapchain.put()));
        return swapchain;
    }

    inline auto CreateDXGISwapChainForWindow(winrt::com_ptr<ID3D11Device> const& device,
        uint32_t width, uint32_t height, DXGI_FORMAT format, uint32_t bufferCount, HWND window)
    {
        DXGI_SWAP_CHAIN_DESC1 desc = {};
        desc.Width = width;
        desc.Height = height;
        desc.Format = format;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;
        desc.BufferCount = bufferCount;
        desc.Scaling = DXGI_SCALING_STRETCH;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

        return CreateDXGISwapChainForWindow(device, &desc, window);
    }

    inline std::string Wstring2Utf8String(const std::wstring& wstr)
    {
        std::string strRet = "";
        CHAR* pszBuf = NULL;
        int cchChar = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, NULL, 0, NULL, NULL);
        if (cchChar > 0)
        {
            pszBuf = (CHAR*)malloc(cchChar * sizeof(CHAR));
            if (pszBuf)
            {
                SecureZeroMemory(pszBuf, cchChar * sizeof(CHAR));
                WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, pszBuf, cchChar, NULL, NULL);
                strRet = std::string(pszBuf);
            }
            free(pszBuf);
            pszBuf = NULL;
        }
        return strRet;
    }

    inline std::wstring Utf8String2Wstring(const std::string& str)
    {
        std::wstring wstrRet = L"";
        WCHAR* pwszBuf = NULL;
        int cchWideChar = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, NULL, 0);
        if (cchWideChar > 0)
        {
            pwszBuf = (WCHAR*)malloc(cchWideChar * sizeof(WCHAR));
            if (pwszBuf)
            {
                SecureZeroMemory(pwszBuf, cchWideChar * sizeof(WCHAR));
                MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, pwszBuf, cchWideChar);
                wstrRet = std::wstring(pwszBuf);
            }
            free(pwszBuf);
            pwszBuf = NULL;
        }
        return wstrRet;
    }
}

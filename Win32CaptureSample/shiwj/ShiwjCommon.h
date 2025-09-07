#pragma once
#include "../pch.h"
#include <mfapi.h>
#include <mftransform.h>
#include "thirdparty/plog/Log.h"
#include "thirdparty/plog/Initializers/RollingFileInitializer.h"

namespace shiwj{
	void Init();

	int CreateFoldersRecursive(std::wstring const& fullPath);

	std::wstring GetCurrentBasePath();

	std::wstring GetLogPath();

	std::wstring GetDataPath();

	std::wstring GetVideoPath();

	int GetOrCreateFile(const wchar_t* filePath, winrt::Windows::Storage::StorageFile& outfile);

	std::string Wstring2Utf8String(const std::wstring& wstr);

	std::wstring Utf8String2Wstring(const std::string& str);

	uint64_t GetCurrentTimestampMicro();

	uint64_t GetCurrentTimestampMilli();

	//media foundation
	void PrintMFAttributes(IMFAttributes* pAttr);

	std::wstring GetMFAttributeName(const winrt::guid& guid);

	void WaitFor(uint64_t intervalUs);

    class CTextureScale
    {
    public:

        CTextureScale(winrt::com_ptr<ID3D11Device> pDev, winrt::com_ptr<ID3D11DeviceContext> pCtx);
        ~CTextureScale();
        int Init();

        int Convert(winrt::com_ptr<ID3D11Texture2D> input, winrt::com_ptr<ID3D11Texture2D> output);

        void Cleanup();

    private:
        winrt::com_ptr<ID3D11Device> m_d3dDevice = nullptr;
        winrt::com_ptr<ID3D11DeviceContext> m_d3dContext = nullptr;

        winrt::com_ptr<ID3D11VideoDevice> m_videoDevice = nullptr;
        winrt::com_ptr<ID3D11VideoContext> m_videoContext = nullptr;
        winrt::com_ptr<ID3D11VideoProcessor> m_videoProcessor = nullptr;
        winrt::com_ptr<ID3D11VideoProcessorEnumerator> m_videoProcessEnum = nullptr;

        D3D11_TEXTURE2D_DESC m_inDesc = { 0 };
        D3D11_TEXTURE2D_DESC m_outDesc = { 0 };
    };
}

int WriteBinaryToFile(winrt::Windows::Storage::StorageFile& file, winrt::array_view<uint8_t> data);


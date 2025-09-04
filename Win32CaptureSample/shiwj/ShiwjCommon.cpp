#include <filesystem>
#include <chrono>
#include <mutex>

#include <windows.h>
#include <wincodec.h> // WIC 头文件
#include <d3d11.h>
#include <pathcch.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Storage.Streams.h>

#include "../pch.h"
#include "ShiwjCommon.h"

#pragma comment(lib, "Pathcch.lib")

namespace winrt
{
    using namespace Windows::Foundation;
    using namespace Windows::Foundation::Numerics;
    using namespace Windows::Graphics;
    using namespace Windows::Graphics::Capture;
    using namespace Windows::Graphics::DirectX;
    using namespace Windows::Graphics::DirectX::Direct3D11;
    using namespace Windows::System;
    using namespace Windows::UI;
    using namespace Windows::UI::Composition;
    using namespace Windows::Storage;
    using namespace Windows::Storage::Streams;
    using namespace Windows::Foundation;
    using namespace Windows::Foundation::Metadata;
    using namespace Windows::Graphics::Capture;
    using namespace Windows::Graphics::DirectX;
    using namespace Windows::Graphics::Imaging;
    using namespace Windows::Storage;
    using namespace Windows::Storage::Pickers;
    using namespace Windows::System;
    using namespace Windows::UI;
    using namespace winrt;
    using namespace Windows::Graphics::Imaging;
    using namespace Windows::Storage::Streams;
}

namespace util
{
    using namespace robmikh::common::uwp;
}

namespace shiwj {

    constexpr wchar_t* g_videoPath = L"Win32CaptureSample\\video\\";
    constexpr wchar_t* g_logPath = L"Win32CaptureSample\\log\\";
    constexpr wchar_t* g_dataPath = L"Win32CaptureSample\\data\\";

    void Init()
    {
        std::wstring wsProcessPath = GetCurrentBasePath();
        std::wstring wsLogPath = GetLogPath();
        std::wstring wsVideoPath = GetVideoPath();
        std::wstring wsDataPath = GetDataPath();
        //create log path
        CreateFoldersRecursive(wsLogPath);
        CreateFoldersRecursive(wsVideoPath);
        CreateFoldersRecursive(wsDataPath);

        //init log
        plog::init(plog::debug, (wsLogPath + L"\\shiwj_capture.log").c_str());
        PLOG(plog::debug) << L"Hello";
    }

    int CreateFoldersRecursive(std::wstring const& fullPath)
    {
        std::filesystem::path path(fullPath);

        PLOG(plog::debug) << L"Try to create folder :" << fullPath;
        // 获取根目录
        winrt::StorageFolder currentFolder = winrt::StorageFolder::GetFolderFromPathAsync(
            path.root_path().wstring()).get();

        // 逐级创建目录
        for (auto const& part : path.relative_path())
        {
            try
            {
                std::wstring partName = part.wstring();
                currentFolder = currentFolder.GetFolderAsync(partName).get();
            }
            catch (winrt::hresult_error const& ex)
            {
                // 判断是否为目录未找到错误
                if (ex.code() == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND))
                {
                    currentFolder = currentFolder.CreateFolderAsync(part.wstring()).get();
                }
                else
                {
                    PLOG(plog::debug) << L"Create folder failed:" << currentFolder.Path();
                    return 1;
                }
            }
        }
        return 0;
    }

    std::wstring GetCurrentBasePath()
    {
        HANDLE hdl = GetCurrentProcess();
        wchar_t name[512];
        SecureZeroMemory(name, sizeof(name));
        DWORD nameLen = sizeof(name) / sizeof(wchar_t);
        BOOL ret = QueryFullProcessImageNameW(hdl, 0, name, &nameLen);
        if (!ret)
        {
            return L"";
        }
        wchar_t buffer[MAX_PATH] = { 0 };
        errno_t err = wcscpy_s(buffer, name);
        if (err != 0)
        {
            return L"";
        }
        HRESULT res = PathCchRemoveFileSpec(buffer, sizeof(buffer));
        if (res != S_OK)
        {
            return L"";
        }
        return buffer;
    }

    std::wstring GetLogPath() {
        static std::mutex mu;
        static std::wstring logPath = L"";
        std::lock_guard<std::mutex> lock(mu);
        if (logPath == L"") {
            std::wstring wsProcessPath = GetCurrentBasePath();
            logPath = wsProcessPath + L"\\" + g_logPath;
        }
        return logPath;
    }

    std::wstring GetDataPath() {
        static std::mutex mu;
        static std::wstring dataPath = L"";
        std::lock_guard<std::mutex> lock(mu);
        if (dataPath == L"") {
            std::wstring wsProcessPath = GetCurrentBasePath();
            dataPath = wsProcessPath + L"\\" + g_dataPath;
        }
        return dataPath;
    }

    std::wstring GetVideoPath() {
        static std::mutex mu;
        static std::wstring videoPath = L"";
        std::lock_guard<std::mutex> lock(mu);
        if (videoPath == L"") {
            std::wstring wsProcessPath = GetCurrentBasePath();
            videoPath = wsProcessPath + L"\\" + g_videoPath;
        }
        return videoPath;
    }

    int GetOrCreateFile(const wchar_t* filePath, winrt::StorageFile& outfile)
    {
        try
        {
            // 尝试直接获取文件
            winrt::StorageFile file = winrt::StorageFile::GetFileFromPathAsync(filePath).get();
            outfile = file;
            return 0;
        }
        catch (winrt::hresult_error const& ex)
        {
            if (ex.code() == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND))
            {

                // 解析路径
                std::filesystem::path path(filePath);
                std::wstring directory = path.parent_path().wstring();
                std::wstring fileName = path.filename().wstring();

                if (directory.empty() || fileName.empty())
                {
                    PLOG(plog::debug) << L"Invalid file path:" << filePath;
                    return 1;
                }
                winrt::StorageFolder folder = winrt::StorageFolder::GetFolderFromPathAsync(GetDataPath()).get();
                // 创建文件
                outfile = folder.CreateFileAsync(fileName, winrt::CreationCollisionOption::ReplaceExisting).get();
                return 0;
            }
            else
            {
                PLOG(plog::debug) << L"Open file failed, path:" << filePath << " msg: " << ex.message();
                return 1;
            }
        }
    }

    std::string Wstring2Utf8String(const std::wstring& wstr)
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

    std::wstring Utf8String2Wstring(const std::string& str)
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

uint64_t GetCurrentTimestampMicro()
{
    std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
    uint64_t timestampMicro = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
    return timestampMicro;
}

int WriteBinaryToFile(
    winrt::StorageFile& file,
    winrt::array_view<uint8_t> data)
{
    try
    {
        // 打开文件流
        winrt::IRandomAccessStream stream = file.OpenAsync(winrt::FileAccessMode::ReadWrite).get();
        stream.Size(0); // 清空原有内容

        // 写入二进制数据
        winrt::DataWriter writer(stream);
        writer.WriteBytes(data);
        writer.StoreAsync().get();
        writer.FlushAsync().get();

        return 0;
    }
    catch (winrt::hresult_error const& ex)
    {
        PLOG(plog::debug) << L"Write file failed:" << file.Path() << " " << ex.message().c_str();
        return 1;
    }
}

uint64_t GetCurrentTimestampMilli()
{
    std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
    uint64_t timestampMilli = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    return timestampMilli;
}

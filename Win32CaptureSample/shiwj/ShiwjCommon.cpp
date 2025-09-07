#include <filesystem>
#include <chrono>
#include <mutex>

#include <windows.h>
#include <wincodec.h> // WIC 头文件
#include <d3d11.h>
#include <pathcch.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Storage.Streams.h>

#include "ShiwjCommon.h"

#pragma comment(lib, "Pathcch.lib")
#pragma comment(lib, "Crypt32.lib")

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
		PLOG(plog::info) << L"==================== Hello ====================";
	}

	int CreateFoldersRecursive(std::wstring const& fullPath)
	{
		if (fullPath.empty())
		{
			return 1;
		}
		std::error_code ec;
		if (!std::filesystem::exists(fullPath))
		{
			// create_directories: 递归创建
			bool ret = std::filesystem::create_directories(fullPath, ec);
			if (!ret)
			{
				PLOG(plog::debug) << L"[CreateFoldersRecursive] failed: "
					<< fullPath << L" ec=" << ec.value() << L" msg="
					<< shiwj::Utf8String2Wstring(std::system_category().message(ec.value()));
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

	uint64_t GetCurrentTimestampMicro()
	{
		std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
		uint64_t timestampMicro = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
		return timestampMicro;
	}

	uint64_t GetCurrentTimestampMilli()
	{
		std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
		uint64_t timestampMilli = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
		return timestampMilli;
	}

	int ConvertHexStrToBytes(const char* hexStr, std::vector<uint8_t>& dataVec) {

		if (!hexStr || strlen(hexStr) == 0) {
			dataVec.resize(0);
			return 0;
		}

		DWORD binarySize = 0;

		// 获取所需的缓冲区大小
		if (!CryptStringToBinaryA(hexStr, static_cast<DWORD>(strlen(hexStr)),
			CRYPT_STRING_HEX, nullptr, &binarySize, nullptr, nullptr)) {
			return 1;
		}

		// 分配缓冲区并转换
		dataVec.resize(binarySize);
		if (!CryptStringToBinaryA(hexStr, static_cast<DWORD>(strlen(hexStr)),
			CRYPT_STRING_HEX, dataVec.data(), &binarySize, nullptr, nullptr)) {
			return 1;
		}

		return 0;
	}

	std::string ConvertBytesToHexStr(const unsigned char* bytes, uint64_t bsize)
	{
		DWORD hexSize = 0;

		// 获取所需的缓冲区大小
		if (!CryptBinaryToStringA(bytes, bsize, CRYPT_STRING_HEX | CRYPT_STRING_NOCRLF,
			nullptr, &hexSize)) {
			return "";
		}

		// 分配缓冲区并转换
		std::shared_ptr<char[]> hexBuffer(new char[hexSize]);

		if (!CryptBinaryToStringA(bytes, bsize, CRYPT_STRING_HEX | CRYPT_STRING_NOCRLF,
			hexBuffer.get(), &hexSize)) {
			return "";
		}

		return hexBuffer.get();
	}

	void PrintMFAttributes(IMFAttributes* pAttr)
	{
		if (!pAttr)
		{
			PLOG(plog::error) << L"IMFAttributes pointer is null.";
			return;
		}

		// 获取属性数量
		UINT32 count = 0;
		HRESULT hr = pAttr->GetCount(&count);
		if (FAILED(hr)) {
			PLOG(plog::error) << L"Failed to get attribute count, hr: 0x" << std::hex << hr << std::dec;
			return;
		}
		std::wstringstream output;
		output << L"IMFAttributes has " << count << L" attributes:\n";

		// 枚举所有属性
		for (UINT32 i = 0; i < count; i++)
		{
			GUID guid;
			PROPVARIANT var;
			PropVariantInit(&var);

			// 获取索引为i的属性
			hr = pAttr->GetItemByIndex(i, &guid, &var);
			if (FAILED(hr)) {
				PLOG(plog::error) << L"Failed to get attribute at index " << i << L", hr: 0x" << std::hex << hr << std::dec;
				continue;
			}

			// 打印属性name
			output << L"- Attr(" << i << L")" << GetMFAttributeName(guid);

			MF_ATTRIBUTE_TYPE type;;
			pAttr->GetItemType(guid, &type);
			switch (type)
			{
			case MF_ATTRIBUTE_UINT32:
			{
				uint32_t u32;
				pAttr->GetUINT32(guid, &u32);
				output << L"(uint32): " << u32;
				break;
			}
			case MF_ATTRIBUTE_UINT64:
			{
				uint64_t u64;
				pAttr->GetUINT64(guid, &u64);
				output << L"(uint64): " << u64;
				break;
			}
			case MF_ATTRIBUTE_DOUBLE:
			{
				double d;
				pAttr->GetDouble(guid, &d);
				output << L"(double): " << d;
				break;
			}
			case MF_ATTRIBUTE_GUID:
			{
				GUID g;
				pAttr->GetGUID(guid, &g);
				wchar_t gString[256] = { 0 };
				StringFromGUID2(g, gString, sizeof(gString) / sizeof(wchar_t));
				output << L"(guid): " << gString;
				break;
			}
			case MF_ATTRIBUTE_STRING:
			{
				uint32_t strLen = 0;
				pAttr->GetStringLength(guid, &strLen);
				wchar_t* str = new wchar_t[strLen + 1];
				pAttr->GetString(guid, str, strLen + 1, NULL);
				output << L"(string): " << str;
				delete[] str;
				break;
			}
			case MF_ATTRIBUTE_BLOB:
			{
				uint32_t blobSize = 0;
				pAttr->GetBlobSize(guid, &blobSize);
				std::vector<uint8_t> blobVec(blobSize);
				pAttr->GetBlob(guid, blobVec.data(), blobSize, NULL);
				std::wstringstream ss;
				for (UINT32 j = 0; j < blobSize; j++) {
					ss << std::hex << blobVec[j];
				}
				output << L"(blob): 0x" << ss.str();
				break;
			}
			case MF_ATTRIBUTE_IUNKNOWN:
			{
				PLOG(plog::info) << L"(iunknow)";
				break;
			}
			default:
			{
				PLOG(plog::info) << L"(unknown type): " << type;
				break;
			}
			}

			PropVariantClear(&var);

			if (IsEqualGUID(guid, MF_MT_FRAME_SIZE))
			{
				uint32_t width = 0, height = 0;
				hr = MFGetAttributeSize(pAttr, MF_MT_FRAME_SIZE, &width, &height);
				if (FAILED(hr))
				{
					PLOG(plog::error) << L"MFGetAttributeSize MF_MT_FRAME_SIZE failed, hr=" << std::hex << hr << std::dec;
				}
				else
				{
					output << L" (" << width << L"x" << height <<L")";
				}
			}
			else if (IsEqualGUID(guid, MF_MT_FRAME_RATE))
			{
				uint32_t num = 0, den = 0;
				hr = MFGetAttributeRatio(pAttr, MF_MT_FRAME_RATE, &num, &den);
				if (FAILED(hr))
				{
					PLOG(plog::error) << L"MFSetAttributeRatio MF_MT_FRAME_RATE failed, hr=" << std::hex << hr << std::dec;
				}
				else
				{
					output << L" (" << num << L"/" << den << L"=" << (den == 0 ? L"invalid" : std::to_wstring((double)num / (double)den)) << L")";
				}
			}
			output << L"\n";
		}
		PLOG(plog::info) << output.str();
		return;
	}

	std::wstring GetMFAttributeName(const winrt::guid& guid)
	{
		static const std::unordered_map<GUID, std::wstring, std::hash<winrt::guid>> attributeNameMap = {
			//mfapi.h
			{MF_MT_MAJOR_TYPE, L"MF_MT_MAJOR_TYPE"},
			{MF_MT_SUBTYPE, L"MF_MT_SUBTYPE"},
			{MF_MT_FRAME_SIZE, L"MF_MT_FRAME_SIZE"},
			{MF_MT_FRAME_RATE, L"MF_MT_FRAME_RATE"},
			{MF_MT_AVG_BITRATE, L"MF_MT_AVG_BITRATE"},
			{MF_MT_PIXEL_ASPECT_RATIO, L"MF_MT_PIXEL_ASPECT_RATIO"},
			{MF_MT_COMPRESSED, L"MF_MT_COMPRESSED"},
			{MF_MT_SAMPLE_SIZE, L"MF_MT_SAMPLE_SIZE"},
			{MF_MT_INTERLACE_MODE, L"MF_MT_INTERLACE_MODE"},
			{MFT_GFX_DRIVER_VERSION_ID_Attribute, L"MFT_GFX_DRIVER_VERSION_ID_Attribute"},
			{MFT_SUPPORT_DYNAMIC_FORMAT_CHANGE, L"MFT_SUPPORT_DYNAMIC_FORMAT_CHANGE"},
			{MF_VIDEO_MAX_MB_PER_SEC, L"MF_VIDEO_MAX_MB_PER_SEC"},
			{MF_MT_VIDEO_LEVEL,L"MF_MT_VIDEO_LEVEL"},
			{MF_MT_VIDEO_PROFILE,L"MF_MT_VIDEO_PROFILE"},
			{MF_MT_FIXED_SIZE_SAMPLES,L"MF_MT_FIXED_SIZE_SAMPLES"},
			{MF_MT_ALL_SAMPLES_INDEPENDENT,L"MF_MT_ALL_SAMPLES_INDEPENDENT"},
			{MF_MT_YUV_MATRIX, L"MF_MT_YUV_MATRIX"},
			{MF_LOW_LATENCY, L"MF_LOW_LATENCY"},
			{MFSampleExtension_DecodeTimestamp, L"MFSampleExtension_DecodeTimestamp"},
			{MFSampleExtension_LongTermReferenceFrameInfo, L"MFSampleExtension_LongTermReferenceFrameInfo"},
			{MFSampleExtension_VideoEncodePictureType, L"MFSampleExtension_VideoEncodePictureType"},
			{MFSampleExtension_CleanPoint, L"MFSampleExtension_CleanPoint"},
			{MFSampleExtension_Discontinuity, L"MFSampleExtension_Discontinuity"},

			//mftransform.h
			{MF_SA_D3D11_AWARE, L"MF_SA_D3D11_AWARE"},
			{MFT_ENCODER_SUPPORTS_CONFIG_EVENT, L"MFT_ENCODER_SUPPORTS_CONFIG_EVENT"},
			{MF_TRANSFORM_CATEGORY_Attribute, L"MF_TRANSFORM_CATEGORY_Attribute"},
			{MF_TRANSFORM_FLAGS_Attribute, L"MF_TRANSFORM_FLAGS_Attribute"},
			{MF_TRANSFORM_ASYNC, L"MF_TRANSFORM_ASYNC"},
			{MF_TRANSFORM_ASYNC_UNLOCK, L"MF_TRANSFORM_ASYNC_UNLOCK"},
			{MFT_ENUM_HARDWARE_URL_Attribute, L"MFT_ENUM_HARDWARE_URL_Attribute"},
			{MFT_FRIENDLY_NAME_Attribute, L"MFT_FRIENDLY_NAME_Attribute"},
			{MFT_ENUM_HARDWARE_VENDOR_ID_Attribute, L"MFT_ENUM_HARDWARE_VENDOR_ID_Attribute"},
			{MFT_INPUT_TYPES_Attributes, L"MFT_INPUT_TYPES_Attributes"},
			{MFT_OUTPUT_TYPES_Attributes, L"MFT_OUTPUT_TYPES_Attributes"},
			{MFT_TRANSFORM_CLSID_Attribute, L"MFT_TRANSFORM_CLSID_Attribute"},
			{MFT_CODEC_MERIT_Attribute, L"MFT_CODEC_MERIT_Attribute"},

		};
		auto it = attributeNameMap.find(guid);
		if (it == attributeNameMap.end())
		{
			wchar_t guidString[256] = { 0 };
			StringFromGUID2(guid, guidString, sizeof(guidString) / sizeof(wchar_t));
			return guidString;
		}
		return it->second;
	}

	void WaitFor(uint64_t intervalUs)
	{
		if (intervalUs < 0)
		{
			return;
		}

		// 使用高精度定时器失败，回退到原方法
		HANDLE hTimer = CreateWaitableTimerExW(nullptr, nullptr,
			CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);

		if (hTimer != nullptr)
		{
			LARGE_INTEGER dueTime;
			dueTime.QuadPart = -static_cast<LONGLONG>(intervalUs) * 10LL; // 转换为100纳秒单位，负值表示相对时间

			if (SetWaitableTimer(hTimer, &dueTime, 0, nullptr, nullptr, FALSE))
			{
				WaitForSingleObject(hTimer, INFINITE);
			}
			else
			{
				PLOG(plog::debug) << L"SetWaitableTimer failed, use Sleep instead.";
				timeBeginPeriod(1);
				Sleep(intervalUs / 1000);
				timeEndPeriod(1);
			}
			CloseHandle(hTimer);
		}
		else
		{
			PLOG(plog::debug) << L"CreateWaitableTimerExW failed, use Sleep instead.";
			timeBeginPeriod(1);
			Sleep(intervalUs / 1000);
			timeEndPeriod(1);
		}
	}
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


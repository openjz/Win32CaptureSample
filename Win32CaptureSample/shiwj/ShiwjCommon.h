#pragma once
#include "../pch.h"
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
}

int WriteBinaryToFile(winrt::Windows::Storage::StorageFile& file, winrt::array_view<uint8_t> data);


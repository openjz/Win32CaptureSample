#pragma once
/*!
* @file Log.hpp
* @brief Log related interfaces
*
* @author fanmj2@lenovo.com
* @version 1.0
* @date 2021-3-5
*/

#include <string>
#include <shlobj.h>
#include <atlstr.h>
#include <tchar.h>
#include <sddl.h>

#define LENOVO_LOG_LEVEL_ERROR		1
#define LENOVO_LOG_LEVEL_WARNING	2
#define LENOVO_LOG_LEVEL_INFO		3
#define LENOVO_LOG_LEVEL_TRACE		4
#define LENOVO_LOG_LEVEL_DEBUG		5

#define LOG_WIDEN2(x) L ## x
#define LOG_WIDEN(x) LOG_WIDEN2(x)
#define __LOG__WFILE__ LOG_WIDEN(__FILE__)

//compatible with the interface of the old version log library
#define  __WriteLog__				CGlobalLogImplement::GetInstance()->WriteLog
#define __SetEnableLogPrintf__		CGlobalLogImplement::GetInstance()->SetEnableLogPrintf
#define __Set_Global_LogLevel__		CGlobalLogImplement::GetInstance()->SetLogLevel
#define InitializeLoger				CGlobalLogImplement::GetInstance()->Initialize
#define InitializeLogerWithApp		CGlobalLogImplement::GetInstance()->InitializeWithApp

/// @brief Record error level logs
#define LOG_ERROR(MSG,...)		__WriteLog__(LENOVO_LOG_LEVEL_ERROR, MSG, __LOG__WFILE__, __LINE__, __VA_ARGS__)
/// @brief Record warning level logs
#define LOG_WARNING(MSG,...)	__WriteLog__(LENOVO_LOG_LEVEL_WARNING, MSG, __LOG__WFILE__, __LINE__, __VA_ARGS__)
/// @brief Record info level logs
#define LOG_INFO(MSG,...)		__WriteLog__(LENOVO_LOG_LEVEL_INFO, MSG, __LOG__WFILE__, __LINE__, __VA_ARGS__)
/// @brief Record trace level logs
#define LOG_TRACE(MSG,...)		__WriteLog__(LENOVO_LOG_LEVEL_TRACE, MSG, __LOG__WFILE__, __LINE__, __VA_ARGS__)
/// @brief Record debug level logs
#define LOG_DEBUG(MSG,...)		__WriteLog__(LENOVO_LOG_LEVEL_DEBUG, MSG, __LOG__WFILE__, __LINE__, __VA_ARGS__)

#define LOG_ERROR_RETURN_VOID(MSG, ...) { __WriteLog__(LENOVO_LOG_LEVEL_ERROR, MSG, __LOG__WFILE__, __LINE__, __VA_ARGS__); return; }
#define LOG_ERROR_RETURN_FALSE(MSG, ...) { __WriteLog__(LENOVO_LOG_LEVEL_ERROR, MSG, __LOG__WFILE__, __LINE__, __VA_ARGS__); return FALSE; }
#define WRITE_LOG	LOG_INFO

class CGlobalLogImplement
{
public:
	BOOL				g_lenovo_log_work = FALSE;
	BOOL				g_lenovo_log_printf = FALSE;
	int					g_lenovo_global_log_level = LENOVO_LOG_LEVEL_DEBUG;
	INT64				g_lenovo_global_max_size;
	TCHAR				g_lenovo_obfuscation_log_Path[MAX_PATH] = { 0 };
	bool				g_lenovo_log_autochecksize;
	HANDLE				g_lenovo_global_log_Mutex = NULL;
	BOOL				g_lenovo_log_obfuscation = FALSE;

	void Initialize(LPCTSTR file = NULL, INT64 maxSize = 1024 * 1024, bool obfuscation = true)
	{
		InitializeLogerWithApp(L"SmartEngine", file, maxSize, obfuscation);
	}

	BOOL GetRegistryValue(HKEY hKey, const CString& szRegKeyPath, const CString& szRegKeyName, DWORD& dwRegValue, BOOL bUseWOW)
	{
		DWORD mdwDataType;
		DWORD mdwSize = sizeof(DWORD);
		HKEY  mhCtrlNwKey;
		bool bStaus = FALSE;
		mdwDataType = REG_DWORD;

		DWORD dwOption = KEY_READ | KEY_QUERY_VALUE;
		if (bUseWOW)
		{
			dwOption = KEY_READ | KEY_QUERY_VALUE | KEY_WOW64_64KEY;
		}

		if (RegOpenKeyEx(hKey, szRegKeyPath, 0, dwOption, &mhCtrlNwKey) == ERROR_SUCCESS)
		{
			if (RegQueryValueEx(mhCtrlNwKey, szRegKeyName, NULL, &mdwDataType, (LPBYTE)&dwRegValue, &mdwSize) == ERROR_SUCCESS)
			{
				bStaus = TRUE;
			}

			RegCloseKey(mhCtrlNwKey);
		}
		return bStaus;
	}
	void InitializeWithApp(LPCTSTR appName = NULL, LPCTSTR file = NULL, INT64 maxSize = 1024 * 1024, bool obfuscation = true, bool autoCheckSize = true)
	{
		g_lenovo_log_autochecksize = autoCheckSize;
		g_lenovo_global_max_size = maxSize;
		g_lenovo_log_obfuscation = obfuscation;
		DWORD dwUseLog = 0;
		
		// check if the log is enabled
		BOOL bret=GetRegistryValue(HKEY_LOCAL_MACHINE, _T("SOFTWARE\\Lenovo\\SmartEngine"), _T("log"), dwUseLog, TRUE);
		if (bret)
		{
			g_lenovo_log_work = dwUseLog;
		}
		

#ifdef DEBUG
		//g_lenovo_log_obfuscation = false;//DEBUG is null
		g_lenovo_global_log_level = LENOVO_LOG_LEVEL_DEBUG;	//DEBUG gen all log
#else
		g_lenovo_global_log_level = LENOVO_LOG_LEVEL_INFO;	//Release default log is INFO
#endif
		//SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32);
		
		//
		ATL::CString initPath;
		::SHGetSpecialFolderPath(NULL, initPath.GetBuffer(MAX_PATH), CSIDL_COMMON_APPDATA, FALSE);
		initPath.ReleaseBuffer();
		initPath.Append(_T("\\Lenovo\\SmartEngine\\config\\config.ini"));
		
		if (::PathFileExists(initPath))
		{
			g_lenovo_global_log_level = ::GetPrivateProfileInt(_T("config"), L"g_lenovo_global_log_level", g_lenovo_global_log_level, initPath);
			g_lenovo_global_max_size = ::GetPrivateProfileInt(_T("config"), L"g_lenovo_global_max_size", (int)g_lenovo_global_max_size, initPath);
			g_lenovo_log_obfuscation = ::GetPrivateProfileInt(_T("config"), L"g_lenovo_log_obfuscation", g_lenovo_log_obfuscation, initPath);
			g_lenovo_log_printf = ::GetPrivateProfileInt(_T("config"), L"g_lenovo_log_printf", g_lenovo_log_printf, initPath);
		}

		//If File is not passed in initialization, the process name is used as the log file by default
		ATL::CString logFile;
		if (file == NULL || wcsnlen_s(file, MAX_PATH) == 0)
		{
			ATL::CString strPath;
			wchar_t exe_path[MAX_PATH];
			DWORD dwSize = MAX_PATH;
			if (!QueryFullProcessImageName(GetCurrentProcess(), 0, strPath.GetBuffer(MAX_PATH), &dwSize))
			{
				exe_path[0] = '\0';
			}
			//::GetModuleFileName(NULL, strPath.GetBuffer(MAX_PATH), MAX_PATH);
			strPath.ReleaseBuffer();
			logFile = ::PathFindFileName(strPath);
			logFile.Append(L".log");
		}
		else
		{
			logFile = file;
		}

		ATL::CString logFolder;
		//If the file path is passed in, it is used directly, otherwise the log path of the housekeeper is used by default
		int backslashPos = logFile.ReverseFind(_T('\\'));
		if (backslashPos > 0)
		{
			logFolder = logFile.Mid(0, backslashPos + 1);
		}
		else
		{
			if (IsUserAdmin())
			{
				::SHGetSpecialFolderPath(NULL, logFolder.GetBuffer(MAX_PATH), CSIDL_COMMON_APPDATA, FALSE);
				logFolder.ReleaseBuffer();
			}
			else
			{
				::SHGetSpecialFolderPath(NULL, logFolder.GetBuffer(MAX_PATH), CSIDL_LOCAL_APPDATA, FALSE);
				logFolder.ReleaseBuffer();
			}

			if (appName == NULL || wcsnlen_s(appName, MAX_PATH) == 0)
			{
				logFolder.Append(_T("\\Lenovo\\SmartEngine\\logs\\"));
			}
			else
			{
				// Use AppName as the directory name under ProgramData. When the software is uninstalled, the software itself needs to be responsible for deleting it.
				logFolder.Append(_T("\\Lenovo\\"));
				logFolder.Append(appName);
				logFolder.Append(L"\\logs\\");
			}
			logFile = logFolder + logFile;
		}
		if (!::PathIsDirectory(logFolder))
		{
			::SHCreateDirectoryEx(NULL, logFolder, 0);
		}

		if (GetFileAttributes(logFolder) & FILE_ATTRIBUTE_REPARSE_POINT)
		{
			return;
		}

		if (g_lenovo_log_obfuscation)
		{
			//Compatible with unobfuscated logs of previous versions of the housekeeper, delete old log files
			int index = logFile.ReverseFind(_T('.'));
			ATL::CString oldFile = logFile.Mid(0, index);
			oldFile.Append(_T(".normal"));
			oldFile.Append(logFile.Right(logFile.GetLength() - index));
			if (::PathFileExists(oldFile))
			{
				::DeleteFile(oldFile);
			}

			logFile = logFile + L".logdat";
		}
		_tcscpy_s(g_lenovo_obfuscation_log_Path, logFile.GetBuffer());

		LogCheckMaxSize();

		if (g_lenovo_global_log_Mutex == NULL)
		{
			SECURITY_ATTRIBUTES sa;
			SECURITY_DESCRIPTOR sd;
			::InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION);
			::SetSecurityDescriptorDacl(&sd, TRUE, NULL, FALSE);
			const wchar_t* sddl = L"D:(A;;FA;;;AU)(A;;FA;;;SY)(A;;FA;;;BA)(D;;FA;;;NU)";
			PSECURITY_DESCRIPTOR pSD = NULL;

			if (!ConvertStringSecurityDescriptorToSecurityDescriptor(sddl, SDDL_REVISION_1, &pSD, NULL))
			{
				pSD = &sd;
				LOG_WARNING(L"[server] acl init failed with err=%d ", GetLastError());
			}

			sa.nLength = sizeof(SECURITY_ATTRIBUTES);
			sa.bInheritHandle = TRUE;
			sa.lpSecurityDescriptor = pSD;
			g_lenovo_global_log_Mutex = ::CreateMutex(&sa, FALSE, ATL::CString(L"Global\\SELOG_") + ::PathFindFileName(logFile));
		}
	}



	void WriteLog(int level, LPCTSTR format, LPCTSTR sourceFile, int sourceline, ...)
	{
		if (level > g_lenovo_global_log_level)
		{
			return;
		}
		if (!g_lenovo_log_work)
		{
			return;
		}
		//If the caller has not initialized it, it will be initialized with default parameters.
		if (wcsnlen_s(g_lenovo_obfuscation_log_Path, MAX_PATH) == 0)
		{
			InitializeLogerWithApp();
		}
		if (wcsnlen_s(g_lenovo_obfuscation_log_Path, MAX_PATH) == 0)
		{
			return;
		}

		if (g_lenovo_global_log_Mutex != NULL)
		{
			::WaitForSingleObject(g_lenovo_global_log_Mutex, INFINITE);
		}
		DWORD NumberOfBytesWritten = 0;
		try
		{
			if (g_lenovo_log_autochecksize)
			{
				// Automatically detect the size of the log
				static int logCount = 0;
				if (++logCount % 100 == 0)
				{
					LogCheckMaxSize();
				}
			}
			ATL::CString logFormat;
			ATL::CString logContent;
			SYSTEMTIME currentTime = { 0 };
			GetLocalTime(&currentTime);

			logFormat.Format(_T("%.2d-%.2d %.2d:%.2d:%.2d:%.3d : %s[%.5d] %s(%d) %s\n"),
				currentTime.wMonth, currentTime.wDay, currentTime.wHour, currentTime.wMinute, currentTime.wSecond, currentTime.wMilliseconds,
				GetLogLevelString(level),
				GetCurrentThreadId(),
				PathFindFileName(sourceFile), sourceline,  format);

			va_list vaPtr;
			va_start(vaPtr, sourceline);
			logContent.FormatV(logFormat.GetBuffer(), vaPtr);
			va_end(vaPtr);

			if (g_lenovo_log_printf)
			{
				log_printf_console(level, logContent);
			}

			HANDLE fileHandle = INVALID_HANDLE_VALUE;
			do
			{
				fileHandle = CreateFile(g_lenovo_obfuscation_log_Path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, 0, NULL);
				if (fileHandle == INVALID_HANDLE_VALUE)
				{
					break;
				}

				if (GetFileAttributes(g_lenovo_obfuscation_log_Path) & FILE_ATTRIBUTE_REPARSE_POINT)
				{
					break;
				}
				SetFilePointer(fileHandle, 0, NULL, FILE_END);

				std::string utf8Content = ATL::CT2A(logContent, CP_UTF8).m_psz;
				if (g_lenovo_log_obfuscation)
				{
					int length = (int)utf8Content.length();
					LogStringBitCode((char*)utf8Content.c_str(), length);
					WriteFile(fileHandle, &length, 4, &NumberOfBytesWritten, NULL);
					WriteFile(fileHandle, utf8Content.c_str(), (DWORD)utf8Content.length(), &NumberOfBytesWritten, NULL);
				}
				else
				{
					WriteFile(fileHandle, utf8Content.c_str(), (DWORD)utf8Content.length(), &NumberOfBytesWritten, NULL);
				}

			} while (false);

			if (fileHandle != INVALID_HANDLE_VALUE)
			{
				CloseHandle(fileHandle);
			}

		}
		catch (...)
		{

		}
		::ReleaseMutex(g_lenovo_global_log_Mutex);
	}

	void SetEnableLogPrintf()
	{
		g_lenovo_log_printf = true;
	}

	void SetLogLevel(int level)
	{
		g_lenovo_global_log_level = level;
	}

	static CGlobalLogImplement* GetInstance()
	{
		static CGlobalLogImplement instance;
		return &instance;
	}


private:

	void LogCheckMaxSize()
	{
		HANDLE hFile = ::CreateFile(g_lenovo_obfuscation_log_Path, GENERIC_READ, FILE_SHARE_WRITE | FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		if (hFile != INVALID_HANDLE_VALUE)
		{
			LARGE_INTEGER filesize;
			::GetFileSizeEx(hFile, &filesize);
			::CloseHandle(hFile);
			if (filesize.QuadPart >= g_lenovo_global_max_size)
			{
				::DeleteFile(g_lenovo_obfuscation_log_Path);
			}
		}
	}

	void log_printf_console(int level, ATL::CString& logContent)
	{
		if (level == LENOVO_LOG_LEVEL_ERROR)
		{
			::SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_RED);
		}
		else if (level == LENOVO_LOG_LEVEL_WARNING)
		{
			::SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_INTENSITY | FOREGROUND_BLUE);
		}
		else if (level == LENOVO_LOG_LEVEL_INFO)
		{
			::SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_INTENSITY | FOREGROUND_GREEN);
		}
		else if (level == LENOVO_LOG_LEVEL_TRACE)
		{
			::SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_INTENSITY | FOREGROUND_RED | FOREGROUND_GREEN);
		}
		else
		{
			::SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
		}
		wprintf(L"%s",logContent.GetBuffer());
	}

	void LogStringBitCode(char* p, int length)
	{
		for (int i = 0; i < length; i++)
		{
			p[i] = ~p[i];
		}
	}

	ATL::CString GetLogLevelString(int level)
	{
		switch (level)
		{
		case LENOVO_LOG_LEVEL_ERROR:
			return L"[ERR]";
		case LENOVO_LOG_LEVEL_WARNING:
			return L"[WAR]";
		case LENOVO_LOG_LEVEL_INFO:
			return L"[INF]";
		case LENOVO_LOG_LEVEL_TRACE:
			return L"[TRA]";
		case LENOVO_LOG_LEVEL_DEBUG:
			return L"[DBG]";
		default:
			return L"[NONE]";;
		}
	}
	bool IsUserAdmin()
	{
		BOOL bIsAdmin = FALSE;
		SID_IDENTIFIER_AUTHORITY NtAuthority = SECURITY_NT_AUTHORITY;
		PSID AdministratorsGroup;

		if (AllocateAndInitializeSid(&NtAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS,
			0, 0, 0, 0, 0, 0, &AdministratorsGroup))
		{
			if (!CheckTokenMembership(NULL, AdministratorsGroup, &bIsAdmin))
			{
				bIsAdmin = FALSE;
			}
			FreeSid(AdministratorsGroup);
		}
		return bIsAdmin == TRUE;
	}
};

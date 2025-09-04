#pragma once
#include <string>
#include <source_location>
#include <fstream>
#include <mutex>
#include <queue>
#define DBG_LogInfo(...) Debug_Write_log( __FILE__, __func__, __LINE__, \
	LogLevel::LOG_INFO, __VA_ARGS__)

enum class LogLevel
{
	LOG_INFO,
	LOG_WARN,
	LOG_ERROR
};

void Debug_Log_init(const std::string& fileName);
void Debug_LOG_UnInit();
void Debug_Write_log(const char* file, const char* func,
	uint32_t line, const LogLevel level,
	const char* format, ...);
 
class CLogger
{
public:
	CLogger();
	~CLogger();
	bool Init(const std::string& fileName);
	void WriteLog(const std::string& msg);
	bool UnInit();
public:
	static CLogger* Instance();
	void WirteLogThread();
private:
	std::string m_strLogFileName;
	FILE* m_pFile;
	static CLogger* g_logger;
	std::shared_ptr<std::thread>  m_writeLogThread = nullptr;
	bool m_bInit = false;
	std::mutex m_mutex;
	std::deque<std::string> m_logMsgs;
};
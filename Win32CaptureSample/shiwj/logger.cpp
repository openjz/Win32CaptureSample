#include "pch.h"
#include "logger.h"
#include <iostream>
#include <chrono>
#include <iomanip>
#define BUF_SIZE_256    (256)
#define BUF_SIZE_128    (128)
std::map<LogLevel, std::string> g_logLevlevelMaps;
CLogger* CLogger::g_logger = nullptr;
CLogger::CLogger():m_pFile(nullptr)
{
    g_logLevlevelMaps[LogLevel::LOG_INFO] = "INFO";
    g_logLevlevelMaps[LogLevel::LOG_WARN] = "WARN";
    g_logLevlevelMaps[LogLevel::LOG_ERROR] = "ERROR";
}

CLogger::~CLogger()
{

}

CLogger* CLogger::Instance()
{
    if (nullptr == g_logger)
    {
        g_logger = new CLogger();
    }
    return g_logger;
}
void CLogger::WirteLogThread()
{
    while (m_bInit)
    {
        m_mutex.lock();
        if (m_logMsgs.size()==0)
        {
           
            m_mutex.unlock();
            Sleep(40);
            continue;
        }
        std::string msg = m_logMsgs.front();
        m_logMsgs.pop_front();
        m_mutex.unlock();
        if (m_pFile)
        {
            fwrite(msg.c_str(), msg.length(), 1, m_pFile);
            fflush(m_pFile);
        }
       
    }
}
bool CLogger::Init(const std::string& fileName)
{
    UnInit();
	m_strLogFileName = fileName;
    errno_t rt = fopen_s(&m_pFile,fileName.c_str(), "ab+");
    if (rt!=0 ||m_pFile == nullptr)
    {
        return false;
    }
    m_bInit = true;
    m_writeLogThread = std::make_shared<std::thread>(&CLogger::WirteLogThread, this);
	return true;
}

 void CLogger::WriteLog( const std::string& msg)
 {
     m_mutex.lock();
     //if (m_pFile)
     //{
     //    fwrite(msg.c_str(), msg.length(), 1, m_pFile);
     //    fflush(m_pFile);
     //}
     m_logMsgs.push_back(msg); 
     m_mutex.unlock();
     
}

 bool CLogger::UnInit()
 {
     if (m_bInit)
     {
         m_bInit = false;
         m_writeLogThread->join();
         m_writeLogThread = nullptr;
     }
     if (m_pFile)
     {
         fclose(m_pFile);
         m_pFile = nullptr;
     }
     return true;
 }

 void Debug_Log_init(const std::string& fileName)
 {
     CLogger::Instance()->Init(fileName);
 }

 void Debug_LOG_UnInit()
 {
     CLogger::Instance()->UnInit();
 }
 void Debug_Write_log(const char* file, const char* func,
     uint32_t line, const LogLevel level,
     const char* format, ...)
 {
     char lineLog[BUF_SIZE_256];
     char fmt[BUF_SIZE_128];
     char fct[BUF_SIZE_128];

     strncpy_s(fmt, format, sizeof(fmt) - 1);
     strncpy_s(fct, func, sizeof(fct) - 1);

     int32_t size = 0;
     int32_t headSize = 0;
     int32_t writeLen = 0;
     auto now = std::chrono::system_clock::now();
     auto in_time_t = std::chrono::system_clock::to_time_t(now);
     tm tm_time;
     localtime_s(&tm_time,&in_time_t);

     headSize = _snprintf_s(lineLog, BUF_SIZE_256, BUF_SIZE_256 - 1, "[%4u-%02u-%02u %02u:%02u:%02u][%02u][%s][%s][%s]",
         tm_time.tm_year, tm_time.tm_mon, tm_time.tm_mday, tm_time.tm_hour, tm_time.tm_min, tm_time.tm_sec, line,g_logLevlevelMaps[level].c_str(), file, fct);

     va_list va = NULL;
     va_start(va, format);
     size = _vsnprintf_s(lineLog + headSize, BUF_SIZE_256, BUF_SIZE_256- headSize, fmt, va);
     va_end(va);

     size += headSize;

     //如果原始字符串已经添加\n则不主动添加
     if (lineLog[size - 1] != '\n')
     {
         lineLog[size] = '\n';
         ++size;
     }
     CLogger::Instance()->WriteLog(lineLog);
 }
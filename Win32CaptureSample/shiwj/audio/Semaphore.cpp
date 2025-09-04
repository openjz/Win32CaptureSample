#include "pch.h"
#include "Semaphore.h"
MySemaphore::MySemaphore(int iCount)
{
    m_iCount = iCount;
}
MySemaphore::~MySemaphore()
{
}
void MySemaphore::Signal() {
    std::unique_lock<std::mutex> lock(m_mLock);
    if (++m_iCount >= 0) {
        m_cConditionVariable.notify_one();
    }
}
bool MySemaphore::Wait(int outTime) {
    std::unique_lock<std::mutex> lock(m_mLock);
    --m_iCount;
   // m_cConditionVariable.wait(lock, [this] { return m_iCount >= 0; });
    return m_cConditionVariable.wait_for(lock, std::chrono::seconds(outTime),[this] { return m_iCount >= 0; });
}
int MySemaphore::GetValue() {
    std::unique_lock<std::mutex> lock(m_mLock);
    return m_iCount;
}
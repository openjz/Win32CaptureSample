#pragma once
#include <mutex>
#include <condition_variable>

class MySemaphore{

public:
    MySemaphore(int iCount = 0);
    ~MySemaphore();

    void Signal();
    bool Wait(int outTime);
    int GetValue();

    MySemaphore(const MySemaphore& rhs) = delete;
    MySemaphore(MySemaphore&& rhs) = delete;
    MySemaphore& operator=(const MySemaphore& rhs) = delete;
    MySemaphore& operator=(MySemaphore&& rhs) = delete;

private:
    std::mutex m_mLock;
    std::condition_variable m_cConditionVariable;
    int m_iCount;
};
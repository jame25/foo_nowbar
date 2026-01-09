#pragma once

namespace mmh {
class Thread {
public:
    void create_thread()
    {
        if (!m_thread)
            m_thread = reinterpret_cast<HANDLE>(
                _beginthreadex(nullptr, NULL, &g_on_thread, static_cast<LPVOID>(this), NULL, nullptr));
    }
    void set_priority(int priority)
    {
        m_priority = priority;
        if (m_thread != nullptr)
            SetThreadPriority(m_thread, m_priority);
    }
    bool wait_for_thread(DWORD timeout = pfc_infinite)
    {
        if (m_thread) {
            return WAIT_TIMEOUT != WaitForSingleObject(m_thread, timeout);
        }
        return true;
    }
    void wait_for_and_release_thread(DWORD timeout = pfc_infinite)
    {
        wait_for_thread(timeout);
        release_thread();
    }
    bool is_thread_open() { return m_thread != nullptr; }
    HANDLE get_thread() { return m_thread; }
    virtual DWORD on_thread() = 0;
    Thread() : m_thread(nullptr), m_priority(THREAD_PRIORITY_NORMAL) {}

    virtual ~Thread() = default;
    ;

    void release_thread()
    {
        if (m_thread) {
            CloseHandle(m_thread);
            m_thread = nullptr;
        }
    }

private:
    static unsigned CALLBACK g_on_thread(LPVOID lpThreadParameter)
    {
        Thread* p_this = reinterpret_cast<Thread*>(lpThreadParameter);
        if (p_this->m_priority)
            SetThreadPriority(GetCurrentThread(), p_this->m_priority);
        return p_this->on_thread();
    }
    HANDLE m_thread;
    int m_priority;
};

HRESULT set_thread_description(HANDLE thread, PCWSTR thread_description);

} // namespace mmh

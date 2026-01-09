#include "stdafx.h"

namespace fbh {

class LowLevelMouseHookManager::HookThread final : public pfc::thread {
public:
    HookThread();
    ~HookThread();

private:
    static constexpr unsigned MSG_QUIT = WM_USER + 3;
    static LRESULT CALLBACK s_on_event(int code, WPARAM wp, LPARAM lp) noexcept;

    void threadProc() override;
    HHOOK m_hook;
    DWORD m_thread_id;
};

LowLevelMouseHookManager::HookThread::HookThread() : m_hook(nullptr), m_thread_id(0)
{
    winStart(GetThreadPriority(GetCurrentThread()), &m_thread_id);
}

LowLevelMouseHookManager::HookThread::~HookThread()
{
    if (!PostThreadMessage(m_thread_id, MSG_QUIT, 0, 0))
        uBugCheck();
    waitTillDone();
}

LRESULT LowLevelMouseHookManager::HookThread::s_on_event(int code, WPARAM wp, LPARAM lp) noexcept
{
    if (code >= 0 && core_api::are_services_available()) {
        fb2k::inMainThread([wp, msllhs{*reinterpret_cast<LPMSLLHOOKSTRUCT>(lp)}] {
            LowLevelMouseHookManager::s_get_instance().on_event(wp, msllhs);
        });
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

void LowLevelMouseHookManager::HookThread::threadProc()
{
    TRACK_CALL_TEXT("LowLevelMouseHookManager::HookThread::threadProc");
    MSG msg;
    BOOL res;

    m_hook = SetWindowsHookEx(WH_MOUSE_LL, &s_on_event, wil::GetModuleInstanceHandle(), NULL);

    while ((res = GetMessage(&msg, nullptr, 0, 0)) != 0) {
        if (res == -1) {
            PFC_ASSERT(!"GetMessage failure");
            uBugCheck();
        } else if (msg.hwnd == nullptr && msg.message == MSG_QUIT) {
            PostQuitMessage(0);
        } else {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    UnhookWindowsHookEx(m_hook);
    m_hook = nullptr;
}

LowLevelMouseHookManager& LowLevelMouseHookManager::s_get_instance()
{
    static LowLevelMouseHookManager instance;
    return instance;
}

void LowLevelMouseHookManager::register_hook(HookCallback* callback)
{
    m_callbacks.push_back(callback);

    if (m_callbacks.size() == 1) {
        m_hook_thread = std::make_unique<HookThread>();
    }
}

void LowLevelMouseHookManager::deregister_hook(HookCallback* callback)
{
    std::erase(m_callbacks, callback);

    if (m_callbacks.empty()) {
        m_hook_thread.reset();
    }
}

LowLevelMouseHookManager::LowLevelMouseHookManager() : m_hook(nullptr) {}

LowLevelMouseHookManager::~LowLevelMouseHookManager()
{
    PFC_ASSERT(m_callbacks.empty());
}

void LowLevelMouseHookManager::on_event(WPARAM msg, const MSLLHOOKSTRUCT& mllhs) const
{
    for (auto& callback : m_callbacks)
        callback->on_hooked_message(msg, mllhs);
}

} // namespace fbh

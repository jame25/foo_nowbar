#pragma once

namespace fbh {
class LowLevelMouseHookManager {
public:
    class HookCallback {
    public:
        virtual void on_hooked_message(WPARAM msg, const MSLLHOOKSTRUCT& mllhs) = 0;
    };

    static LowLevelMouseHookManager& s_get_instance();

    void register_hook(HookCallback* callback);
    void deregister_hook(HookCallback* callback);

private:
    class HookThread;

    LowLevelMouseHookManager();
    ~LowLevelMouseHookManager();
    LowLevelMouseHookManager(const LowLevelMouseHookManager&) = delete;
    LowLevelMouseHookManager(LowLevelMouseHookManager&&) = delete;

    void on_event(WPARAM msg, const MSLLHOOKSTRUCT& mllhs) const;

    std::unique_ptr<HookThread> m_hook_thread;
    std::vector<HookCallback*> m_callbacks;
    HHOOK m_hook;

    friend class MainThreadCallback;
};
} // namespace fbh

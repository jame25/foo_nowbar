#include "stdafx.h"

namespace uih {
class hook_list {
public:
    pfc::ptr_list_t<MessageHook> m_hooks;
    HHOOK m_hook{nullptr};
};

hook_list g_hooks[static_cast<size_t>(MessageHookType::hook_type_count)];

LRESULT CALLBACK g_on_hooked_message(uih::MessageHookType p_type, int code, WPARAM wp, LPARAM lp)
{
    const auto type_index = static_cast<size_t>(p_type);
    bool b_call_next = true;

    if (code >= 0) {
        size_t n;
        size_t count = g_hooks[type_index].m_hooks.get_count();

        for (n = 0; n < count; n++) {
            if ((p_type != MessageHookType::type_get_message || wp == PM_REMOVE)
                && g_hooks[type_index].m_hooks[n]->on_hooked_message(p_type, code, wp, lp)) {
                // if (p_type != type_get_message)
                //{
                b_call_next = false;
                break;
                //}
            }
        }
    }
    return b_call_next ? CallNextHookEx(nullptr, code, wp, lp) : p_type != MessageHookType::type_get_message ? TRUE : 0;
}

LRESULT CALLBACK g_keyboard_proc(int code, WPARAM wp, LPARAM lp) noexcept
{
    return g_on_hooked_message(MessageHookType::type_keyboard, code, wp, lp);
}

LRESULT CALLBACK g_getmsg_proc(int code, WPARAM wp, LPARAM lp) noexcept
{
    return g_on_hooked_message(MessageHookType::type_get_message, code, wp, lp);
}

LRESULT CALLBACK g_message_proc(int code, WPARAM wp, LPARAM lp) noexcept
{
    return g_on_hooked_message(MessageHookType::type_message_filter, code, wp, lp);
}

LRESULT CALLBACK g_mouse_proc(int code, WPARAM wp, LPARAM lp) noexcept
{
    return g_on_hooked_message(MessageHookType::type_mouse, code, wp, lp);
}

LRESULT CALLBACK g_mouse_low_level_proc(int code, WPARAM wp, LPARAM lp) noexcept
{
    return g_on_hooked_message(MessageHookType::type_mouse_low_level, code, wp, lp);
}

const HOOKPROC g_hook_procs[] = {g_keyboard_proc, g_getmsg_proc, g_message_proc, g_mouse_proc, g_mouse_low_level_proc};
const int g_hook_ids[] = {WH_KEYBOARD, WH_GETMESSAGE, WH_MSGFILTER, WH_MOUSE, WH_MOUSE_LL};

void register_message_hook(MessageHookType p_type, MessageHook* p_hook)
{
    const auto type_index = static_cast<size_t>(p_type);
    if (g_hooks[type_index].m_hooks.add_item(p_hook) == 0) {
        g_hooks[type_index].m_hook = SetWindowsHookEx(g_hook_ids[type_index], g_hook_procs[type_index],
            p_type != MessageHookType::type_mouse_low_level ? nullptr : wil::GetModuleInstanceHandle(),
            p_type != MessageHookType::type_mouse_low_level ? GetCurrentThreadId() : NULL);
    }
}
void deregister_message_hook(MessageHookType p_type, MessageHook* p_hook)
{
    const auto type_index = static_cast<size_t>(p_type);
    g_hooks[type_index].m_hooks.remove_item(p_hook);
    if (g_hooks[type_index].m_hooks.get_count() == 0) {
        UnhookWindowsHookEx(g_hooks[type_index].m_hook);
        g_hooks[type_index].m_hook = nullptr;
    }
}

class CallbackMessageHook : public MessageHook {
public:
    explicit CallbackMessageHook(MessageHookCallback callback) : m_callback(std::move(callback)) {}

private:
    bool on_hooked_message(MessageHookType type, int code, WPARAM wp, LPARAM lp) override
    {
        return m_callback(code, wp, lp);
    }

    MessageHookCallback m_callback;
};

struct MessageHookToken : EventToken {
    MessageHookToken(MessageHookType type, MessageHookCallback callback)
        : m_type(type)
        , m_callback_wrapper(std::move(callback))
    {
        register_message_hook(m_type, &m_callback_wrapper);
    }
    ~MessageHookToken() override { deregister_message_hook(m_type, &m_callback_wrapper); }

private:
    MessageHookType m_type;
    CallbackMessageHook m_callback_wrapper;
};

std::unique_ptr<EventToken> register_message_hook(MessageHookType type, MessageHookCallback callback)
{
    return std::make_unique<MessageHookToken>(type, std::move(callback));
}

} // namespace uih

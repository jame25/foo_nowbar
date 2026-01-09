#pragma once

namespace uih {
enum class MessageHookType {
    type_keyboard = 0, // return true in on_hooked_message to prevent the message from being passed to other hooks /
                       // actual win proc
    type_get_message, // returning true in on_hooked_message stops further hooks being called only. (not recommended!)
    type_message_filter, // WH_MSGFILTER
    type_mouse, // WH_MOUSE
    type_mouse_low_level, // WH_MOUSE_LL
    hook_type_count,
};

// derive from this
class NOVTABLE MessageHook {
public:
    virtual bool on_hooked_message(MessageHookType p_type, int code, WPARAM wp, LPARAM lp) = 0;
    // NB: returning true will not call CallNextHookEx! Check MSDN doc for hook type whether this is acceptable or not.
    // If you return true, actual return value will depend on type of hook.
    // code >= 0
};

void register_message_hook(MessageHookType p_type, MessageHook* p_hook);
void deregister_message_hook(MessageHookType p_type, MessageHook* p_hook);

using MessageHookCallback = std::function<bool(int code, WPARAM wp, LPARAM lp)>;
std::unique_ptr<EventToken> register_message_hook(MessageHookType type, MessageHookCallback callback);

} // namespace uih

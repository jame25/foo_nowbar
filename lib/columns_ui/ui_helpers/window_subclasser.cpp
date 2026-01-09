#include "stdafx.h"

namespace uih {

namespace {

struct WindowState {
    WNDPROC wnd_proc{};
    std::function<std::optional<LRESULT>(WNDPROC window_proc, HWND wnd, UINT msg, WPARAM wp, LPARAM lp)>
        message_handler;
};

std::unordered_map<HWND, WindowState> state_map;

LRESULT __stdcall handle_subclassed_window_message(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) noexcept
{
    const auto& state = state_map.at(wnd);
    const auto wnd_proc = state.wnd_proc;

    if (const auto result = state.message_handler(wnd_proc, wnd, msg, wp, lp))
        return *result;

    if (msg == WM_NCDESTROY)
        state_map.erase(wnd);

    return CallWindowProcW(wnd_proc, wnd, msg, wp, lp);
}

} // namespace

void subclass_window(HWND wnd,
    std::function<std::optional<LRESULT>(WNDPROC wnd_proc, HWND wnd, UINT msg, WPARAM wp, LPARAM lp)> message_handler)
{
    const auto wnd_proc = SubclassWindow(wnd, handle_subclassed_window_message);
    state_map[wnd] = WindowState{wnd_proc, std::move(message_handler)};
}

} // namespace uih

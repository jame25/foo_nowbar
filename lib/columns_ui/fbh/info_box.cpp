#include "stdafx.h"

namespace fbh {

void show_info_box_modeless(HWND wnd_parent, const char* title, const char* message, uih::InfoBoxType type,
    bool no_wrap, std::function<std::optional<INT_PTR>(HWND, UINT, WPARAM, LPARAM)> on_before_message)
{
    auto handle_before_message = [on_before_message{std::move(on_before_message)}](
                                     HWND wnd, UINT msg, WPARAM wp, LPARAM lp) -> std::optional<INT_PTR> {
        if (msg == WM_INITDIALOG)
            modeless_dialog_manager::g_add(wnd);
        else if (msg == WM_NCDESTROY)
            modeless_dialog_manager::g_remove(wnd);

        return on_before_message ? on_before_message(wnd, msg, wp, lp) : std::nullopt;
    };

    uih::InfoBox::s_open_modeless(wnd_parent, title, message, type, std::move(handle_before_message), no_wrap);
}

INT_PTR show_info_box_modal(HWND wnd_parent, const char* title, const char* message, uih::InfoBoxType type,
    uih::InfoBoxModalType modal_type, bool no_wrap,
    std::function<std::optional<INT_PTR>(HWND, UINT, WPARAM, LPARAM)> on_before_message)
{
    modal_dialog_scope scope;
    auto handle_before_message = [&scope, on_before_message{std::move(on_before_message)}](
                                     HWND wnd, UINT msg, WPARAM wp, LPARAM lp) mutable -> std::optional<INT_PTR> {
        if (msg == WM_INITDIALOG)
            scope.initialize(wnd);
        else if (msg == WM_NCDESTROY)
            scope.deinitialize();

        return on_before_message ? on_before_message(wnd, msg, wp, lp) : std::nullopt;
    };

    return uih::InfoBox::s_open_modal(
        wnd_parent, title, message, type, modal_type, std::move(handle_before_message), no_wrap);
}

} // namespace fbh

#include "stdafx.h"

namespace fbh {

INT_PTR modal_dialog_box(UINT resource_id, HWND parent_window,
    std::function<INT_PTR(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)> on_message, bool poke)
{
    if (poke && !ModalDialogPrologue())
        return FALSE;

    modal_dialog_scope scope;

    return uih::modal_dialog_box(resource_id, parent_window,
        [&scope, on_message{std::move(on_message)}](HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
            if (msg == WM_INITDIALOG)
                scope.initialize(wnd);
            else if (msg == WM_NCDESTROY)
                scope.deinitialize();

            return on_message(wnd, msg, wp, lp);
        });
}

HWND modeless_dialog_box(
    UINT resource_id, HWND parent_window, std::function<INT_PTR(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)> on_message)
{
    return uih::modeless_dialog_box(
        resource_id, parent_window, [on_message{std::move(on_message)}](HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
            if (msg == WM_INITDIALOG)
                modeless_dialog_manager::g_add(wnd);
            else if (msg == WM_NCDESTROY)
                modeless_dialog_manager::g_remove(wnd);

            return on_message(wnd, msg, wp, lp);
        });
}

} // namespace fbh

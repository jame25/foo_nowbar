#pragma once

namespace fbh {

INT_PTR modal_dialog_box(UINT resource_id, HWND parent_window,
    std::function<INT_PTR(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)> on_message, bool poke = true);

HWND modeless_dialog_box(
    UINT resource_id, HWND parent_window, std::function<INT_PTR(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)> on_message);

} // namespace fbh

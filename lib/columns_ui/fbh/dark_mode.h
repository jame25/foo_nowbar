#pragma once

namespace fbh {

fb2k::coreDarkModeObj::ptr create_dark_mode_obj();

INT_PTR auto_dark_modal_dialog_box(
    UINT resource_id, HWND parent_window, std::function<INT_PTR(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)> on_message);

std::tuple<HWND, bool> auto_dark_modeless_dialog_box(
    UINT resource_id, HWND parent_window, std::function<INT_PTR(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)> on_message);

} // namespace fbh

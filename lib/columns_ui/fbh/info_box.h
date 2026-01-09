#pragma once

namespace fbh {

void show_info_box_modeless(HWND wnd_parent, const char* title, const char* message, uih::InfoBoxType type,
    bool no_wrap = false,
    std::function<std::optional<INT_PTR>(HWND, UINT, WPARAM, LPARAM)> on_before_message = nullptr);

INT_PTR show_info_box_modal(HWND wnd_parent, const char* title, const char* message, uih::InfoBoxType type,
    uih::InfoBoxModalType modal_type, bool no_wrap = false,
    std::function<std::optional<INT_PTR>(HWND, UINT, WPARAM, LPARAM)> on_before_message = nullptr);

} // namespace fbh

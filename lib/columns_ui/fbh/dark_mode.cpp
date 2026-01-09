#include "stdafx.h"

namespace fbh {

fb2k::coreDarkModeObj::ptr create_dark_mode_obj()
{
    fb2k::coreDarkMode::ptr api;
    fb2k::coreDarkMode::tryGet(api);

    if (api.is_valid()) {
        return api->createAuto();
    }

    return {};
}

INT_PTR auto_dark_modal_dialog_box(
    UINT resource_id, HWND parent_window, std::function<INT_PTR(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)> on_message)
{
    const auto dark_mode_obj = create_dark_mode_obj();

    return uih::modal_dialog_box(resource_id, parent_window,
        [dark_mode_obj{std::move(dark_mode_obj)}, on_message{std::move(on_message)}](
            HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
            const auto result = on_message(wnd, msg, wp, lp);

            if (dark_mode_obj.is_valid() && msg == WM_INITDIALOG) {
                dark_mode_obj->addDialog(wnd);
                dark_mode_obj->addControls(wnd);
            }

            return result;
        });
}

std::tuple<HWND, bool> auto_dark_modeless_dialog_box(
    UINT resource_id, HWND parent_window, std::function<INT_PTR(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)> on_message)
{
    const auto dark_mode_obj = create_dark_mode_obj();
    bool has_dark_mode = dark_mode_obj.is_valid();

    const auto wnd = uih::modeless_dialog_box(resource_id, parent_window,
        [dark_mode_obj{std::move(dark_mode_obj)}, on_message{std::move(on_message)}](
            HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
            const auto result = on_message(wnd, msg, wp, lp);

            if (dark_mode_obj.is_valid() && msg == WM_INITDIALOG) {
                dark_mode_obj->addDialog(wnd);
                dark_mode_obj->addControls(wnd);
            }

            return result;
        });

    return std::make_tuple(wnd, has_dark_mode);
}

} // namespace fbh

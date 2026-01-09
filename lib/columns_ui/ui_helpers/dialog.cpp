#include "stdafx.h"

namespace uih {

namespace {
struct DialogBoxData {
    std::function<INT_PTR(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)> on_message;
    std::shared_ptr<DialogBoxData> self;
};

INT_PTR WINAPI on_dialog_box_message(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) noexcept
{
    DialogBoxData* data{};
    if (msg == WM_INITDIALOG) {
        data = reinterpret_cast<DialogBoxData*>(lp);
        SetWindowLongPtr(wnd, DWLP_USER, lp);
    } else {
        data = reinterpret_cast<DialogBoxData*>(GetWindowLongPtr(wnd, DWLP_USER));
    }

    const auto result = data ? data->on_message(wnd, msg, wp, lp) : FALSE;

    if (msg == WM_NCDESTROY) {
        assert(data);

        if (data)
            data->self.reset();

        SetWindowLongPtr(wnd, DWLP_USER, NULL);
    }

    return result;
}

} // namespace

INT_PTR modal_dialog_box(
    UINT resource_id, HWND wnd, std::function<INT_PTR(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)> on_message)
{
    const auto data = std ::make_unique<DialogBoxData>(DialogBoxData{std::move(on_message), {}});

    return DialogBoxParam(wil::GetModuleInstanceHandle(), MAKEINTRESOURCE(resource_id), wnd, on_dialog_box_message,
        reinterpret_cast<LPARAM>(data.get()));
}

INT_PTR modal_dialog_box(
    LPDLGTEMPLATE dlg_template, HWND wnd, std::function<INT_PTR(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)> on_message)
{
    const auto data = std ::make_unique<DialogBoxData>(DialogBoxData{std::move(on_message), {}});

    return DialogBoxIndirectParam(
        wil::GetModuleInstanceHandle(), dlg_template, wnd, on_dialog_box_message, reinterpret_cast<LPARAM>(data.get()));
}

HWND modeless_dialog_box(
    UINT resource_id, HWND wnd, std::function<INT_PTR(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)> on_message)
{
    std::shared_ptr<DialogBoxData> data = std ::make_shared<DialogBoxData>(DialogBoxData{std::move(on_message), {}});
    data->self = data;

    return CreateDialogParam(wil::GetModuleInstanceHandle(), MAKEINTRESOURCE(resource_id), wnd, on_dialog_box_message,
        reinterpret_cast<LPARAM>(data.get()));
}

HWND modeless_dialog_box(
    LPDLGTEMPLATE dlg_template, HWND wnd, std::function<INT_PTR(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)> on_message)
{
    std::shared_ptr<DialogBoxData> data = std ::make_shared<DialogBoxData>(DialogBoxData{std::move(on_message), {}});
    data->self = data;

    return CreateDialogIndirectParam(
        wil::GetModuleInstanceHandle(), dlg_template, wnd, on_dialog_box_message, reinterpret_cast<LPARAM>(data.get()));
}

} // namespace uih

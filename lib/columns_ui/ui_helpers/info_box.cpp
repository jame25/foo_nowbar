#include "stdafx.h"

using namespace std::string_literals;
using namespace uih::literals::spx;

namespace uih {

const std::unordered_map<InfoBoxType, int> icon_map{{InfoBoxType::Neutral, 0},
    {InfoBoxType::Information, OIC_INFORMATION}, {InfoBoxType::Warning, OIC_WARNING}, {InfoBoxType::Error, OIC_ERROR}};

const std::unordered_map<InfoBoxType, INT_PTR> sound_map{{InfoBoxType::Neutral, 0},
    {InfoBoxType::Information, SND_ALIAS_SYSTEMASTERISK}, {InfoBoxType::Warning, SND_ALIAS_SYSTEMEXCLAMATION},
    {InfoBoxType::Error, SND_ALIAS_SYSTEMHAND}};

void InfoBox::s_open_modeless(HWND wnd_parent, const char* title, const char* text, InfoBoxType type,
    std::function<std::optional<INT_PTR>(HWND, UINT, WPARAM, LPARAM)> on_before_message, bool no_wrap)
{
    const auto message_window = std::make_shared<InfoBox>(std::move(on_before_message), no_wrap);
    message_window->create(wnd_parent, title, text, type);
}

INT_PTR InfoBox::s_open_modal(HWND wnd_parent, const char* title, const char* text, InfoBoxType type,
    InfoBoxModalType modal_type, std::function<std::optional<INT_PTR>(HWND, UINT, WPARAM, LPARAM)> on_before_message,
    bool no_wrap)
{
    const auto message_window = std::make_shared<InfoBox>(std::move(on_before_message), no_wrap);
    return message_window->create(wnd_parent, title, text, type, modal_type);
}

int InfoBox::calc_height() const
{
    RECT button_rect{};
    GetWindowRect(m_wnd_cancel_button, &button_rect);

    RECT window_rect{};
    GetWindowRect(m_wnd, &window_rect);

    RECT client_rect{};
    GetClientRect(m_wnd, &client_rect);

    return get_large_padding() * 6 + scale_dpi_value(1) + wil::rect_height(button_rect)
        + (wil::rect_height(window_rect) - wil::rect_height(client_rect))
        + std::max(get_text_height(), get_icon_height());
}

int InfoBox::calc_width() const
{
    if (!m_no_wrap)
        return scale_dpi_value(default_width_dip);

    RECT client_rect{};
    GetClientRect(m_wnd, &client_rect);

    RECT window_rect{};
    GetWindowRect(m_wnd, &window_rect);

    RECT icon_rect{};

    if (m_wnd_static)
        GetWindowRect(m_wnd_static, &icon_rect);

    SCROLLINFO edit_si{};
    edit_si.cbSize = sizeof(edit_si);
    edit_si.fMask = SIF_RANGE;
    GetScrollInfo(m_wnd_edit, SB_HORZ, &edit_si);

    const auto edit_margins = SendMessage(m_wnd_edit, EM_GETMARGINS, 0, 0);

    const auto edit_width = edit_si.nMax - edit_si.nMin + 1 + LOWORD(edit_margins) + HIWORD(edit_margins);
    const auto padding_and_icon_width = get_large_padding() * (m_wnd_static ? 4 : 2)
        + (m_wnd_static ? get_small_padding() + wil::rect_width(icon_rect) : 0);
    const int border_width = wil::rect_width(window_rect) - wil::rect_width(client_rect);

    return edit_width + padding_and_icon_width + border_width;
}

INT_PTR InfoBox::create(
    HWND wnd_parent, const char* title, const char* text, InfoBoxType type, std::optional<InfoBoxModalType> modal_type)
{
    std::vector<uint8_t> dlg_template(sizeof(DLGTEMPLATE) + sizeof(WORD) * 2, 0);

    const auto lpdt = reinterpret_cast<LPDLGTEMPLATE>(dlg_template.data());
    lpdt->style = WS_SYSMENU | WS_POPUP | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_CAPTION | DS_MODALFRAME;

    const auto title_utf16 = mmh::to_utf16(title);
    const std::span title_data(
        reinterpret_cast<const uint8_t*>(title_utf16.data()), sizeof(wchar_t) * (title_utf16.size() + 1));
    ranges::insert(dlg_template, dlg_template.end(), title_data);

    const auto desktop_window = GetDesktopWindow();

    if (wnd_parent && wnd_parent != desktop_window)
        m_wnd_parent = GetAncestor(wnd_parent, GA_ROOT);

    if (!m_wnd_parent)
        m_wnd_parent = desktop_window;

    m_message = std::regex_replace(text, std::regex("\n"), "\r\n");
    m_modal_type = modal_type;
    m_type = type;

    if (const auto icon = icon_map.at(type))
        LOG_IF_FAILED(LoadIconMetric(nullptr, MAKEINTRESOURCE(icon), LIM_LARGE, &m_icon));

    if (m_modal_type)
        return modal_dialog_box(reinterpret_cast<LPDLGTEMPLATE>(dlg_template.data()), wnd_parent,
            [this, self = shared_from_this()](
                auto wnd, auto msg, auto wp, auto lp) -> INT_PTR { return on_message(wnd, msg, wp, lp); });

    modeless_dialog_box(reinterpret_cast<LPDLGTEMPLATE>(dlg_template.data()), wnd_parent,
        [this, self = shared_from_this()](
            auto wnd, auto msg, auto wp, auto lp) -> INT_PTR { return on_message(wnd, msg, wp, lp); });
    return 0;
}

INT_PTR InfoBox::on_message(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (m_on_before_message)
        if (const auto result = m_on_before_message(wnd, msg, wp, lp); result)
            return *result;

    switch (msg) {
    case WM_INITDIALOG: {
        m_wnd = wnd;

        m_font.reset(create_icon_font());

        const auto icon_width = GetSystemMetrics(SM_CXICON);

        if (m_icon)
            m_wnd_static = CreateWindowEx(0, WC_STATIC, L"", WS_CHILD | WS_VISIBLE | WS_GROUP | SS_ICON, 0, 0,
                icon_width, GetSystemMetrics(SM_CYICON), wnd, reinterpret_cast<HMENU>(1002),
                wil::GetModuleInstanceHandle(), nullptr);

        constexpr DWORD edit_styles = WS_CHILD | WS_VISIBLE | WS_GROUP | ES_READONLY | ES_MULTILINE | ES_AUTOVSCROLL;
        const auto initial_edit_styles = edit_styles | (m_no_wrap ? WS_HSCROLL : 0);

        m_wnd_edit = CreateWindowEx(0, WC_EDIT, mmh::to_utf16(m_message).c_str(), initial_edit_styles,
            get_large_padding(), get_large_padding(), 100_spx, 100_spx, wnd, reinterpret_cast<HMENU>(1001),
            wil::GetModuleInstanceHandle(), nullptr);
        SetWindowFont(m_wnd_edit, m_font.get(), FALSE);

        const auto cx = calc_width();

        if (edit_styles != initial_edit_styles)
            SetWindowLongPtr(m_wnd_edit, GWL_STYLE, edit_styles);

        m_button_height = get_font_height(m_font.get()) + scale_dpi_value(10);

        if (m_modal_type && *m_modal_type == InfoBoxModalType::YesNo) {
            m_wnd_ok_button = CreateWindowEx(0, WC_BUTTON, L"Yes", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_GROUP, 0, 0,
                get_button_width(), m_button_height, wnd, reinterpret_cast<HMENU>(IDOK), wil::GetModuleInstanceHandle(),
                nullptr);
            SetWindowFont(m_wnd_ok_button, m_font.get(), FALSE);
        }

        const auto cancel_button_text = [this]() {
            if (!m_modal_type)
                return L"Close";

            switch (*m_modal_type) {
            case InfoBoxModalType::YesNo:
                return L"No";
            default:
                return L"OK";
            }
        }();

        m_wnd_cancel_button = CreateWindowEx(0, WC_BUTTON, cancel_button_text,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON | WS_GROUP, 0, 0, get_button_width(), m_button_height,
            wnd, reinterpret_cast<HMENU>(IDCANCEL), wil::GetModuleInstanceHandle(), nullptr);
        SetWindowFont(m_wnd_cancel_button, m_font.get(), FALSE);

        SetWindowPos(wnd, nullptr, 0, 0, cx, 175_spx, SWP_NOZORDER | SWP_NOMOVE);

        if (m_wnd_static)
            SendMessage(m_wnd_static, STM_SETIMAGE, IMAGE_ICON, reinterpret_cast<LPARAM>(m_icon.get()));

        RECT parent_rect{};
        GetWindowRect(m_wnd_parent, &parent_rect);

        const int cy = std::min(calc_height(), std::max(static_cast<int>(wil::rect_height(parent_rect)), 150_spx));
        const int x = parent_rect.left + (wil::rect_width(parent_rect) - cx) / 2;
        const int y = std::max<int>(parent_rect.top + (wil::rect_height(parent_rect) - cy) / 2, parent_rect.top);
        SetWindowPos(wnd, nullptr, x, y, cx, cy, SWP_NOZORDER);

        ShowWindow(wnd, SW_SHOWNORMAL);

        if (const auto sound = sound_map.at(m_type))
            PlaySound(reinterpret_cast<LPCWSTR>(sound), nullptr, SND_ALIAS_ID | SND_ASYNC);

        return TRUE;
    }
    case WM_NCDESTROY:
        m_wnd = nullptr;
        m_font.reset();
        break;
    case WM_SIZE: {
        RECT icon_rect{};

        if (m_wnd_static)
            GetWindowRect(m_wnd_static, &icon_rect);

        HDWP dwp = BeginDeferWindowPos(2 + (m_wnd_static ? 1 : 0) + (m_wnd_ok_button ? 1 : 0));

        const auto x_padding = get_large_padding() * (m_wnd_static ? 2 : 1);
        const auto edit_x = x_padding + (m_wnd_static ? get_small_padding() + wil::rect_width(icon_rect) : 0);

        const WindowPosition edit_position{edit_x, get_large_padding() * 2, LOWORD(lp) - x_padding - edit_x,
            HIWORD(lp) - get_large_padding() * 6 - m_button_height};
        dwp = DeferWindowPos(dwp, m_wnd_edit, nullptr, edit_position.x, edit_position.y, edit_position.cx,
            edit_position.cy, SWP_NOZORDER);

        if (m_wnd_ok_button) {
            const WindowPosition ok_button_position{
                LOWORD(lp) - x_padding - get_button_width() * 2 - get_small_padding(),
                HIWORD(lp) - get_large_padding() - m_button_height, get_button_width(), m_button_height};
            dwp = DeferWindowPos(dwp, m_wnd_ok_button, nullptr, ok_button_position.x, ok_button_position.y,
                ok_button_position.cx, ok_button_position.cy, SWP_NOZORDER);
        }

        const WindowPosition cancel_button_position{LOWORD(lp) - x_padding - get_button_width(),
            HIWORD(lp) - get_large_padding() - m_button_height, get_button_width(), m_button_height};
        dwp = DeferWindowPos(dwp, m_wnd_cancel_button, nullptr, cancel_button_position.x, cancel_button_position.y,
            cancel_button_position.cx, cancel_button_position.cy, SWP_NOZORDER);

        if (m_wnd_static) {
            const WindowPosition static_position{get_large_padding() * 2, get_large_padding() * 2,
                wil::rect_width(icon_rect), wil::rect_height(icon_rect)};
            dwp = DeferWindowPos(dwp, m_wnd_static, nullptr, static_position.x, static_position.y, static_position.cx,
                static_position.cy, SWP_NOZORDER);
        }

        EndDeferWindowPos(dwp);
        RedrawWindow(wnd, nullptr, nullptr, RDW_INVALIDATE);
        break;
    }
    case WM_GETMINMAXINFO: {
        RECT button_rect{};
        GetWindowRect(m_wnd_cancel_button, &button_rect);

        RECT icon_rect{};
        GetWindowRect(m_wnd_static, &icon_rect);

        RECT window_rect{};
        GetWindowRect(wnd, &window_rect);

        RECT client_rect{};
        GetClientRect(wnd, &client_rect);

        auto lpmmi = reinterpret_cast<LPMINMAXINFO>(lp);
        lpmmi->ptMinTrackSize.x = get_large_padding() * 4 + get_small_padding() + wil::rect_width(icon_rect)
            + scale_dpi_value(50) + (wil::rect_width(window_rect) - wil::rect_width(client_rect));
        lpmmi->ptMinTrackSize.y = get_large_padding() * 4 + wil::rect_height(icon_rect) + wil::rect_height(button_rect)
            + (wil::rect_height(window_rect) - wil::rect_height(client_rect));
        break;
    }
    case WM_SHOWWINDOW:
        if (wp == TRUE && lp == 0 && m_wnd_cancel_button)
            SetFocus(m_wnd_cancel_button);
        break;
    case DM_GETDEFID:
        return IDCANCEL | (DC_HASDEFID << 16);
    case WM_CTLCOLORSTATIC:
        SetBkColor(reinterpret_cast<HDC>(wp), GetSysColor(COLOR_WINDOW));
        SetTextColor(reinterpret_cast<HDC>(wp), GetSysColor(COLOR_WINDOWTEXT));
        SetBkColor(reinterpret_cast<HDC>(wp), GetSysColor(COLOR_WINDOW));
        return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
    case WM_ERASEBKGND:
        SetWindowLongPtr(wnd, DWLP_MSGRESULT, FALSE);
        return TRUE;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(wnd, &ps);
        if (dc) {
            RECT rc_client{};
            RECT rc_button{};
            GetClientRect(wnd, &rc_client);
            RECT rc_fill = rc_client;
            if (m_wnd_cancel_button) {
                GetWindowRect(m_wnd_cancel_button, &rc_button);
                rc_fill.bottom -= wil::rect_height(rc_button) + get_large_padding();
                rc_fill.bottom -= get_large_padding();
            }

            FillRect(dc, &rc_fill, GetSysColorBrush(COLOR_WINDOW));

            if (m_wnd_cancel_button) {
                rc_fill.top = rc_fill.bottom;
                rc_fill.bottom += scale_dpi_value(1);
                FillRect(dc, &rc_fill, GetSysColorBrush(COLOR_3DLIGHT));
            }
            rc_fill.top = rc_fill.bottom;
            rc_fill.bottom = rc_client.bottom;
            FillRect(dc, &rc_fill, GetSysColorBrush(COLOR_3DFACE));
            EndPaint(wnd, &ps);
        }
    }
        return TRUE;
    case WM_COMMAND:
        switch (wp) {
        case IDCANCEL:
        case IDOK:
            if (m_modal_type)
                EndDialog(wnd, wp == IDOK);
            else
                DestroyWindow(wnd);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

} // namespace uih

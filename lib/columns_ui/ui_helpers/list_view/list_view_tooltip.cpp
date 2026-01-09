#include "stdafx.h"

#include "list_view.h"
#include "../direct_write_text_out.h"

using namespace uih::literals::spx;

namespace uih {

void ListView::set_show_tooltips(bool b_val)
{
    m_show_tooltips = b_val;
}

void ListView::set_limit_tooltips_to_clipped_items(bool b_val)
{
    m_limit_tooltips_to_clipped_items = b_val;
}

void ListView::create_tooltip(/*size_t index, size_t column, */ const char* str)
{
    destroy_tooltip();

    m_wnd_tooltip = CreateWindowEx(WS_EX_TRANSPARENT, TOOLTIPS_CLASS, nullptr, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, get_wnd(), nullptr, wil::GetModuleInstanceHandle(),
        nullptr);

    if (!m_wnd_tooltip)
        return;

    set_tooltip_window_theme();

    if (IsThemeActive() && IsAppThemed())
        m_tooltip_theme.reset(OpenThemeData(m_wnd_tooltip, L"Tooltip"));

    uih::subclass_window(m_wnd_tooltip, [this](auto _, auto wnd, auto msg, auto wp, auto lp) {
        if (msg == WM_THEMECHANGED && IsThemeActive() && IsAppThemed())
            m_tooltip_theme.reset(OpenThemeData(m_wnd_tooltip, L"Tooltip"));

        return std::nullopt;
    });

    RECT rect;
    GetClientRect(get_wnd(), &rect);

    pfc::stringcvt::string_wide_from_utf8 wstr(str);
    TOOLINFO ti{};

    ti.cbSize = sizeof(TOOLINFO);
    ti.uFlags = TTF_TRANSPARENT | TTF_SUBCLASS;
    ti.hwnd = get_wnd();
    ti.hinst = wil::GetModuleInstanceHandle();
    ti.uId = IDC_TOOLTIP;
    ti.lpszText = const_cast<wchar_t*>(wstr.get_ptr());
    ti.rect = rect;

    uih::tooltip_add_tool(m_wnd_tooltip, &ti);
}

void ListView::destroy_tooltip()
{
    if (m_wnd_tooltip) {
        DestroyWindow(m_wnd_tooltip);
        m_wnd_tooltip = nullptr;
    }
    m_tooltip_theme.reset();
    m_tooltip_last_index = -1;
    m_tooltip_last_column = -1;
}

void ListView::set_tooltip_window_theme() const
{
    if (!m_wnd_tooltip)
        return;

    SetWindowTheme(m_wnd_tooltip, m_use_dark_mode ? L"DarkMode_Explorer" : nullptr, nullptr);
}

void ListView::calculate_tooltip_position(size_t item_index, size_t column_index, std::string_view text)
{
    int cx = get_total_indentation();

    for (auto index : std::ranges::views::iota(size_t{}, column_index))
        cx += m_columns[index].m_display_size;

    POINT top_left;
    top_left.x = cx + 1_spx + 3_spx - m_horizontal_scroll_position;
    top_left.y = (get_item_position(item_index) - m_scroll_position) + get_items_top();
    ClientToScreen(get_wnd(), &top_left);

    const auto& column = m_columns[column_index];

    try {
        m_items_text_format->set_text_alignment(direct_write::get_text_alignment(column.m_alignment));
    }
    CATCH_LOG()

    auto utf16_text = mmh::to_utf16(text);
    const auto text_width = m_items_text_format->measure_text_width(utf16_text);

    const auto max_width = std::max(0.0f,
        static_cast<float>(column.m_display_size - 1_spx - 3_spx * 2) / direct_write::get_default_scaling_factor());

    // Work around DirectWrite not rendering trailing whitespace
    // for centre- and right-aligned text
    if (column.m_alignment != ALIGN_LEFT)
        utf16_text.push_back(L'\u200b');

    const auto metrics = m_items_text_format->measure_text_position(utf16_text, m_item_height, max_width, true);

    m_tooltip_text_left_offset = metrics.left_remainder_dip;

    m_tooltip_placement_rect
        = {top_left.x + metrics.left, top_left.y, top_left.x + metrics.left + text_width, top_left.y + m_item_height};

    m_tooltip_text_rect.top = top_left.y + metrics.top;
    m_tooltip_text_rect.bottom = top_left.y + metrics.height + 4_spx;
    m_tooltip_text_rect.left = top_left.x + metrics.left;
    m_tooltip_text_rect.right = m_tooltip_text_rect.left + text_width;
}

std::optional<LRESULT> ListView::on_wm_notify_tooltip(LPNMHDR lpnm)
{
    switch (lpnm->code) {
    case NM_CUSTOMDRAW: {
        const auto lpttcd = reinterpret_cast<LPNMTTCUSTOMDRAW>(lpnm);
        switch (lpttcd->nmcd.dwDrawStage) {
        case CDDS_PREPAINT: {
            if (!m_tooltip_theme) {
                const auto back_colour
                    = static_cast<COLORREF>(SendMessage(lpttcd->nmcd.hdr.hwndFrom, TTM_GETTIPBKCOLOR, 0, 0));
                SetTextColor(lpttcd->nmcd.hdc, back_colour);
                return CDRF_NOTIFYPOSTPAINT;
            }

            RECT rc_background = lpttcd->nmcd.rc;
            SendMessage(m_wnd_tooltip, TTM_ADJUSTRECT, TRUE, reinterpret_cast<LPARAM>(&rc_background));

            DrawThemeBackground(
                m_tooltip_theme.get(), lpttcd->nmcd.hdc, TTP_STANDARD, TTSS_NORMAL, &rc_background, &rc_background);

            render_tooltip_text(lpttcd->nmcd.hdr.hwndFrom, lpttcd->nmcd.hdc, GetTextColor(lpttcd->nmcd.hdc));

            return CDRF_SKIPDEFAULT;
        }
        case CDDS_POSTPAINT: {
            if (!m_tooltip_theme) {
                const auto text_colour
                    = static_cast<COLORREF>(SendMessage(lpttcd->nmcd.hdr.hwndFrom, TTM_GETTIPTEXTCOLOR, 0, 0));

                render_tooltip_text(lpttcd->nmcd.hdr.hwndFrom, lpttcd->nmcd.hdc, text_colour);
            }

            return CDRF_DODEFAULT;
        }
        }
        break;
    }
    case TTN_SHOW: {
        RECT rc = m_tooltip_text_rect;

        SendMessage(m_wnd_tooltip, TTM_ADJUSTRECT, TRUE, reinterpret_cast<LPARAM>(&rc));

        SetWindowPos(m_wnd_tooltip, nullptr, rc.left, rc.top, RECT_CX(rc), RECT_CY(rc), SWP_NOZORDER | SWP_NOACTIVATE);
        return TRUE;
    }
    }
    return {};
}

void ListView::render_tooltip_text(HWND wnd, HDC dc, COLORREF colour) const
{
    if (!m_items_text_format)
        return;

    auto text = get_window_text(wnd);

    RECT rc_text = m_tooltip_placement_rect;
    MapWindowPoints(HWND_DESKTOP, wnd, reinterpret_cast<LPPOINT>(&rc_text), 2);

    if (m_items_text_format) {
        try {
            m_items_text_format->set_text_alignment();
            const auto scaling_factor = direct_write::get_default_scaling_factor();

            const auto text_layout = m_items_text_format->create_text_layout(text,
                gsl::narrow_cast<float>(wil::rect_width(rc_text)) / scaling_factor,
                gsl::narrow_cast<float>(wil::rect_height(rc_text)) / scaling_factor);

            text_layout.render_with_transparent_background(wnd, dc, rc_text, colour, m_tooltip_text_left_offset);
        }
        CATCH_LOG()
    }
}

} // namespace uih

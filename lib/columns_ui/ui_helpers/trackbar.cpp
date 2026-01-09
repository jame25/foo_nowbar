#include "stdafx.h"

namespace uih {

void Trackbar::get_thumb_rect(unsigned pos, unsigned range, RECT* rc) const
{
    RECT rc_client{};
    GetClientRect(get_wnd(), &rc_client);

    const auto cx = calculate_thumb_size();
    if (get_orientation()) {
        rc->left = 2;
        rc->right = rc_client.right - 2;
        rc->top = range       ? MulDiv(get_direction() ? range - pos : pos, rc_client.bottom - cx, range)
            : get_direction() ? rc_client.bottom - cx
                              : 0;
        rc->bottom = rc->top + cx;
    } else {
        rc->top = 2;
        rc->bottom = rc_client.bottom - 2;
        rc->left = range      ? MulDiv(get_direction() ? range - pos : pos, rc_client.right - cx, range)
            : get_direction() ? rc_client.right - cx
                              : 0;
        rc->right = rc->left + cx;
    }
}

void Trackbar::get_channel_rect(RECT* rc) const
{
    RECT rc_client{};
    GetClientRect(get_wnd(), &rc_client);
    const auto cx = calculate_thumb_size();

    rc->left = get_orientation() ? rc_client.right / 2 - 2 : rc_client.left + cx / 2;
    rc->right = get_orientation() ? rc_client.right / 2 + 2 : rc_client.right - cx + cx / 2;
    rc->top = get_orientation() ? rc_client.top + cx / 2 : rc_client.bottom / 2 - 2;
    rc->bottom = get_orientation() ? rc_client.bottom - cx + cx / 2 : rc_client.bottom / 2 + 2;
}

void Trackbar::draw_background(HDC dc, const RECT* rc) const
{
    HWND wnd_parent = GetParent(get_wnd());
    POINT pt = {0, 0};
    POINT pt_old = {0, 0};
    MapWindowPoints(get_wnd(), wnd_parent, &pt, 1);
    OffsetWindowOrgEx(dc, pt.x, pt.y, &pt_old);
    if (SendMessage(wnd_parent, WM_ERASEBKGND, reinterpret_cast<WPARAM>(dc), 0) == FALSE)
        SendMessage(wnd_parent, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc), PRF_ERASEBKGND);
    SetWindowOrgEx(dc, pt_old.x, pt_old.y, nullptr);
}

void Trackbar::draw_thumb(HDC dc, const RECT* rc) const
{
    if (m_custom_colours) {
        const auto colour = get_enabled()
            ? (get_hot() ? m_custom_colours->thumb_hot_colour : m_custom_colours->thumb_default_colour)
            : m_custom_colours->thumb_disabled_colour;
        const wil::unique_hbrush brush(CreateSolidBrush(colour));
        FillRect(dc, rc, brush.get());
    } else if (get_theme_handle()) {
        const auto part_id = get_orientation() ? TKP_THUMBVERT : TKP_THUMB;
        const auto state_id
            = get_enabled() ? (get_tracking() ? TUS_PRESSED : (get_hot() ? TUS_HOT : TUS_NORMAL)) : TUS_DISABLED;
        DrawThemeBackground(get_theme_handle(), dc, part_id, state_id, rc, nullptr);
    } else {
        HPEN pn_highlight = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_3DHIGHLIGHT));
        HPEN pn_light = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_3DLIGHT));
        HPEN pn_dkshadow = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_3DDKSHADOW));
        HPEN pn_shadow = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_3DSHADOW));

        HPEN pn_old = SelectPen(dc, pn_highlight);

        MoveToEx(dc, rc->left, rc->top, nullptr);
        LineTo(dc, rc->right - 1, rc->top);
        SelectPen(dc, pn_dkshadow);
        LineTo(dc, rc->right - 1, rc->bottom - 1);
        SelectPen(dc, pn_highlight);
        MoveToEx(dc, rc->left, rc->top, nullptr);
        LineTo(dc, rc->left, rc->bottom - 1);
        SelectPen(dc, pn_dkshadow);
        LineTo(dc, rc->right, rc->bottom - 1);

        SelectPen(dc, pn_light);
        MoveToEx(dc, rc->left + 1, rc->top + 1, nullptr);
        LineTo(dc, rc->right - 2, rc->top + 1);
        MoveToEx(dc, rc->left + 1, rc->top + 1, nullptr);
        LineTo(dc, rc->left + 1, rc->bottom - 2);

        SelectPen(dc, pn_shadow);
        LineTo(dc, rc->right - 1, rc->bottom - 2);
        MoveToEx(dc, rc->right - 2, rc->top + 1, nullptr);
        LineTo(dc, rc->right - 2, rc->bottom - 2);

        SelectPen(dc, pn_old);

        DeleteObject(pn_light);
        DeleteObject(pn_highlight);
        DeleteObject(pn_shadow);
        DeleteObject(pn_dkshadow);

        RECT rc_fill = *rc;

        rc_fill.top += 2;
        rc_fill.left += 2;
        rc_fill.right -= 2;
        rc_fill.bottom -= 2;

        HBRUSH br = GetSysColorBrush(COLOR_BTNFACE);
        FillRect(dc, &rc_fill, br);
        if (!get_enabled()) {
            COLORREF cr_btnhighlight = GetSysColor(COLOR_BTNHIGHLIGHT);
            for (int x = rc_fill.left; x < rc_fill.right; x++)
                for (int y = rc_fill.top; y < rc_fill.bottom; y++)
                    if ((x + y) % 2)
                        SetPixel(dc, x, y, cr_btnhighlight); // i dont have anything better than SetPixel
        }
    }
}

void Trackbar::draw_channel(HDC dc, const RECT* rc) const
{
    if (m_custom_colours) {
        const wil::unique_hbrush brush(CreateSolidBrush(m_custom_colours->channel_colour));
        FillRect(dc, rc, brush.get());
    } else if (get_theme_handle()) {
        DrawThemeBackground(
            get_theme_handle(), dc, get_orientation() ? TKP_TRACKVERT : TKP_TRACK, TUTS_NORMAL, rc, nullptr);
    } else {
        RECT rc_temp = *rc;
        DrawEdge(dc, &rc_temp, EDGE_SUNKEN, BF_RECT);
    }
}

int Trackbar::calculate_thumb_size() const
{
    RECT rc_client;
    GetClientRect(get_wnd(), &rc_client);
    return MulDiv(get_orientation() ? rc_client.right : rc_client.bottom, 9, 20);
}

void Trackbar::set_custom_colours(std::optional<TrackbarCustomColours> colours)
{
    m_custom_colours = colours;

    if (get_wnd())
        RedrawWindow(get_wnd(), nullptr, nullptr, RDW_INVALIDATE);
}

} // namespace uih

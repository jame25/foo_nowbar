#include "stdafx.h"

namespace uih {

void paint_subclassed_window_with_buffering(HWND wnd, WNDPROC window_proc)
{
    PAINTSTRUCT ps{};
    const auto paint_dc = wil::BeginPaint(wnd, &ps);
    const auto buffered_dc = BufferedDC(paint_dc.get(), ps.rcPaint);

    CallWindowProc(window_proc, wnd, WM_ERASEBKGND, reinterpret_cast<WPARAM>(buffered_dc.get()), 0);
    CallWindowProc(window_proc, wnd, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(buffered_dc.get()), PRF_CLIENT);
}

std::optional<LRESULT> handle_subclassed_window_buffered_painting(
    WNDPROC wnd_proc, HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_ERASEBKGND:
        if (WindowFromDC(reinterpret_cast<HDC>(wp)) != wnd)
            return {};

        return FALSE;
    case WM_PAINT:
        uih::paint_subclassed_window_with_buffering(wnd, wnd_proc);
        return 0;
    }

    return {};
}

void subclass_window_and_paint_with_buffering(HWND wnd)
{
    subclass_window(wnd, [](auto wnd_proc, auto wnd, auto msg, auto wp, auto lp) -> std::optional<LRESULT> {
        return handle_subclassed_window_buffered_painting(wnd_proc, wnd, msg, wp, lp);
    });
}

void draw_rect_outline(HDC dc, const RECT& rc, COLORREF colour, int width)
{
    const wil::unique_hpen pen(CreatePen(PS_SOLID | PS_INSIDEFRAME, width, colour));
    auto _selected_brush = wil::SelectObject(dc, GetStockObject(NULL_BRUSH));
    auto _selected_pen = wil::SelectObject(dc, pen.get());
    Rectangle(dc, rc.left, rc.top, rc.right, rc.bottom);
}

} // namespace uih

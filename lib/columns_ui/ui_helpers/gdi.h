#pragma once

namespace uih {

class BufferedDC {
public:
    BufferedDC(HDC paint_dc, RECT paint_rect) : m_paint_rect(paint_rect), m_paint_dc(paint_dc)
    {
        m_memory_dc.reset(CreateCompatibleDC(m_paint_dc));
        m_memory_bitmap.reset(CreateCompatibleBitmap(m_paint_dc, RECT_CX(m_paint_rect), RECT_CY(m_paint_rect)));
        m_select_bitmap = wil::SelectObject(m_memory_dc.get(), m_memory_bitmap.get());

        OffsetWindowOrgEx(m_memory_dc.get(), m_paint_rect.left, m_paint_rect.top, nullptr);
    }
    ~BufferedDC()
    {
        BitBlt(m_paint_dc, m_paint_rect.left, m_paint_rect.top, RECT_CX(m_paint_rect), RECT_CY(m_paint_rect),
            m_memory_dc.get(), m_paint_rect.left, m_paint_rect.top, SRCCOPY);
    }

    [[nodiscard]] HDC get() const { return m_memory_dc.get(); }

private:
    RECT m_paint_rect{};
    HDC m_paint_dc;
    wil::unique_hdc m_memory_dc;
    wil::unique_hbitmap m_memory_bitmap;
    wil::unique_select_object m_select_bitmap;
};

class DisableRedrawScope {
public:
    DisableRedrawScope(HWND wnd)
    {
        m_wnd = wnd;
        m_active = m_wnd && IsWindowVisible(m_wnd);
        if (m_active)
            SendMessage(m_wnd, WM_SETREDRAW, FALSE, NULL);
    }
    ~DisableRedrawScope()
    {
        if (m_active) {
            SendMessage(m_wnd, WM_SETREDRAW, TRUE, NULL);
            RedrawWindow(
                m_wnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN | RDW_FRAME | RDW_ERASE);
        }
    }

private:
    bool m_active;
    HWND m_wnd;
};

void paint_subclassed_window_with_buffering(HWND wnd, WNDPROC window_proc);
std::optional<LRESULT> handle_subclassed_window_buffered_painting(
    WNDPROC wnd_proc, HWND wnd, UINT msg, WPARAM wp, LPARAM lp);
void subclass_window_and_paint_with_buffering(HWND wnd);
void draw_rect_outline(HDC dc, const RECT& rc, COLORREF colour, int width);

} // namespace uih

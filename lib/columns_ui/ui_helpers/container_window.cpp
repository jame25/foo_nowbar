#include "stdafx.h"

namespace uih {
std::unordered_map<std::wstring, size_t> ContainerWindow::s_window_count;

void ContainerWindow::on_size() const
{
    if (m_on_size) {
        RECT rc;
        GetClientRect(get_wnd(), &rc);
        m_on_size(rc.right - rc.left, rc.bottom - rc.top);
    }
}

HWND ContainerWindow::create(
    HWND wnd_parent, WindowPosition window_position, LPVOID create_param, bool use_dialog_units)
{
    if (use_dialog_units)
        window_position.convert_from_dialog_units_to_pixels(wnd_parent);

    auto ref = s_window_count.try_emplace(m_config.class_name, 0);
    if ((*ref.first).second++ == 0) {
        register_class();
    }

    LPVOID createparams[2] = {this, create_param};
    m_wnd = CreateWindowEx(m_config.window_ex_styles, m_config.class_name, m_config.window_title,
        m_config.window_styles, window_position.x, window_position.y, window_position.cx, window_position.cy,
        wnd_parent, nullptr, wil::GetModuleInstanceHandle(), &createparams);

    if (!m_wnd)
        throw exception_win32(GetLastError());

    return m_wnd;
}

void ContainerWindow::destroy()
{
    DestroyWindow(m_wnd);
    auto& ref = s_window_count[m_config.class_name];
    if (--ref == 0) {
        deregister_class();
    }
}

LRESULT ContainerWindow::s_on_message(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) noexcept
{
    ContainerWindow* p_this = nullptr;

    if (msg == WM_NCCREATE) {
        auto create_params = reinterpret_cast<LPVOID*>(reinterpret_cast<CREATESTRUCT*>(lp)->lpCreateParams);
        p_this = reinterpret_cast<ContainerWindow*>(create_params[0]);
        SetWindowLongPtr(wnd, GWLP_USERDATA, reinterpret_cast<LPARAM>(p_this));
    } else
        p_this = reinterpret_cast<ContainerWindow*>(GetWindowLongPtr(wnd, GWLP_USERDATA));

    if (msg == WM_NCDESTROY)
        SetWindowLongPtr(wnd, GWLP_USERDATA, reinterpret_cast<LPARAM>(nullptr));

    return p_this ? p_this->on_message(wnd, msg, wp, lp) : DefWindowProc(wnd, msg, wp, lp);
}

LRESULT ContainerWindow::on_message(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_NCCREATE:
        m_wnd = wnd;
        break;
    case WM_NCDESTROY:
        m_wnd = nullptr;
        break;
    case WM_SETTINGCHANGE:
    case WM_SYSCOLORCHANGE:
    case WM_TIMECHANGE:
        send_message_to_direct_children(wnd, msg, wp, lp);
        break;
    case WM_WINDOWPOSCHANGED: {
        auto lpwp = reinterpret_cast<LPWINDOWPOS>(lp);
        if (!(lpwp->flags & SWP_NOSIZE) || (lpwp->flags & SWP_FRAMECHANGED))
            on_size();
    } break;
    }

    if (m_config.transparent_background) {
        if (msg == WM_ERASEBKGND || (msg == WM_PRINTCLIENT && (lp & PRF_ERASEBKGND))) {
            auto dc = reinterpret_cast<HDC>(wp);
            auto b_ret = LRESULT{TRUE};

            auto wnd_parent = GetParent(wnd);
            auto pt = POINT{0, 0};
            auto pt_old = POINT{0, 0};
            MapWindowPoints(wnd, wnd_parent, &pt, 1);
            OffsetWindowOrgEx(dc, pt.x, pt.y, &pt_old);
            if (msg == WM_PRINTCLIENT) {
                SendMessage(wnd_parent, WM_PRINTCLIENT, wp, PRF_ERASEBKGND);
            } else {
                b_ret = SendMessage(wnd_parent, WM_ERASEBKGND, wp, 0);
            }
            SetWindowOrgEx(dc, pt_old.x, pt_old.y, nullptr);

            return b_ret;
        }
        if (msg == WM_WINDOWPOSCHANGED) {
            auto lpwp = reinterpret_cast<LPWINDOWPOS>(lp);
            if (!(lpwp->flags & SWP_NOSIZE) || !(lpwp->flags & SWP_NOMOVE) || (lpwp->flags & SWP_FRAMECHANGED)) {
                RedrawWindow(wnd, nullptr, nullptr, RDW_ERASE | RDW_INVALIDATE | RDW_ALLCHILDREN);
            }
        }
    }

    return m_on_message ? m_on_message(wnd, msg, wp, lp) : DefWindowProc(wnd, msg, wp, lp);
}

void ContainerWindow::register_class()
{
    auto wc = WNDCLASS();
    memset(&wc, 0, sizeof(WNDCLASS));

    wc.lpfnWndProc = static_cast<WNDPROC>(&s_on_message);
    wc.hInstance = wil::GetModuleInstanceHandle();
    wc.hCursor = LoadCursor(nullptr, m_config.class_cursor);
    wc.hbrBackground = m_config.class_background;
    wc.lpszClassName = m_config.class_name;
    wc.style = m_config.class_styles;
    wc.cbWndExtra = m_config.class_extra_wnd_bytes;

    if (!RegisterClass(&wc))
        throw exception_win32(GetLastError());
}

void ContainerWindow::deregister_class()
{
    [[maybe_unused]] const auto unregistered = UnregisterClass(m_config.class_name, wil::GetModuleInstanceHandle());
    assert(unregistered);
}

} // namespace uih

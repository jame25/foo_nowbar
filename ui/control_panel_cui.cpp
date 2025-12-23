#include "pch.h"
#include "control_panel_cui.h"
#include "../preferences.h"

namespace nowbar {

// Register with Columns UI
uie::window_factory<ControlPanelCUI> g_cui_factory;

void ControlPanelCUI::initialize_core(HWND wnd) {
    if (!m_core) {
        m_core = std::make_unique<ControlPanelCore>();
        m_core->initialize(wnd);
        
        // Set artwork request callback
        m_core->set_artwork_request_callback([this]() {
            update_artwork();
        });
        
        // Note: initialize() already calls on_settings_changed() which respects theme mode preferences
        
        // Load artwork for current track
        update_artwork();
    }
}

void ControlPanelCUI::update_artwork() {
    if (!m_core) return;
    
    auto pc = playback_control::get();
    metadb_handle_ptr track;
    if (pc->get_now_playing(track) && track.is_valid()) {
        // Get album art
        auto art_manager = album_art_manager_v3::get();
        try {
            auto extractor = art_manager->open(
                pfc::list_single_ref_t<metadb_handle_ptr>(track),
                pfc::list_single_ref_t<GUID>(album_art_ids::cover_front),
                fb2k::noAbort
            );
            
            if (extractor.is_valid()) {
                album_art_data_ptr data;
                if (extractor->query(album_art_ids::cover_front, data, fb2k::noAbort)) {
                    m_core->set_artwork(data);
                    return;
                }
            }
        } catch (...) {}
    }
    
    m_core->clear_artwork();
}

LRESULT ControlPanelCUI::on_message(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        initialize_core(wnd);
        return 0;
        
    case WM_DESTROY:
        m_core.reset();
        return 0;
        
    case WM_SIZE: {
        if (m_core) {
            InvalidateRect(wnd, nullptr, FALSE);
        }
        return 0;
    }
        
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(wnd, &ps);
        
        // Double buffering
        RECT rect;
        GetClientRect(wnd, &rect);
        
        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBitmap = CreateCompatibleBitmap(hdc, rect.right, rect.bottom);
        HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);
        
        if (m_core) {
            m_core->paint(memDC, rect);
        }
        
        BitBlt(hdc, 0, 0, rect.right, rect.bottom, memDC, 0, 0, SRCCOPY);
        
        SelectObject(memDC, oldBitmap);
        DeleteObject(memBitmap);
        DeleteDC(memDC);
        
        EndPaint(wnd, &ps);
        return 0;
    }
        
    case WM_ERASEBKGND: {
        // Paint background based on current theme setting to prevent flash
        HDC hdc = (HDC)wp;
        RECT rc;
        GetClientRect(wnd, &rc);
        COLORREF bg_color = get_nowbar_initial_bg_color();
        HBRUSH brush = CreateSolidBrush(bg_color);
        FillRect(hdc, &rc, brush);
        DeleteObject(brush);
        return 1;
    }
        
    case WM_MOUSEMOVE: {
        if (!m_tracking_mouse) {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, wnd, 0 };
            TrackMouseEvent(&tme);
            m_tracking_mouse = true;
        }
        if (m_core) {
            m_core->on_mouse_move(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        }
        return 0;
    }
        
    case WM_MOUSELEAVE:
        m_tracking_mouse = false;
        if (m_core) {
            m_core->on_mouse_leave();
        }
        return 0;
        
    case WM_LBUTTONDOWN:
        SetCapture(wnd);
        if (m_core) {
            m_core->on_lbutton_down(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        }
        return 0;
        
    case WM_LBUTTONUP:
        ReleaseCapture();
        if (m_core) {
            m_core->on_lbutton_up(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        }
        return 0;
        
    case WM_LBUTTONDBLCLK:
        if (m_core) {
            m_core->on_lbutton_dblclk(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        }
        return 0;
        
    case WM_MOUSEWHEEL:
        if (m_core) {
            m_core->on_mouse_wheel(GET_WHEEL_DELTA_WPARAM(wp));
        }
        return 0;
        
    case WM_GETMINMAXINFO: {
        if (m_core) {
            LPMINMAXINFO mmi = (LPMINMAXINFO)lp;
            SIZE minSize = m_core->get_min_size();
            mmi->ptMinTrackSize.x = minSize.cx;
            mmi->ptMinTrackSize.y = minSize.cy;
        }
        return 0;
    }
        
    case WM_SETTINGCHANGE:
        // Check for dark mode change
        if (m_core) {
            bool dark = ui_config_manager::g_is_dark_mode();
            m_core->set_dark_mode(dark);
        }
        return 0;
    }
    
    return DefWindowProc(wnd, msg, wp, lp);
}

} // namespace nowbar

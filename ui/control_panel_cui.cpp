#include "pch.h"
#include "control_panel_cui.h"
#include "../preferences.h"

// Windows 11 Build 22000+ DWM backdrop attribute (for older SDK headers)
#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#define DWMWA_SYSTEMBACKDROP_TYPE 38
#endif
// DWM_SYSTEMBACKDROP_TYPE enum values are in dwmapi.h on SDK 10.0.22000+

// Try to enable Windows 11 acrylic backdrop for a window
static bool try_enable_acrylic_backdrop_cui(HWND hwnd) {
    DWORD buildNumber = 0;
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
                      0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        wchar_t buildStr[16] = {};
        DWORD size = sizeof(buildStr);
        if (RegQueryValueExW(hKey, L"CurrentBuildNumber", nullptr, nullptr,
                             reinterpret_cast<LPBYTE>(buildStr), &size) == ERROR_SUCCESS) {
            buildNumber = _wtoi(buildStr);
        }
        RegCloseKey(hKey);
    }
    
    if (buildNumber < 22000) return false;
    
    DWM_SYSTEMBACKDROP_TYPE backdropType = DWMSBT_TRANSIENTWINDOW;
    HRESULT hr = DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE,
                                       &backdropType, sizeof(backdropType));
    return SUCCEEDED(hr);
}

namespace nowbar {

// Register with Columns UI
uie::window_factory<ControlPanelCUI> g_cui_factory;

void ControlPanelCUI::initialize_core(HWND wnd) {
    if (!m_core) {
        m_core = std::make_unique<ControlPanelCore>();
        
        // Set callbacks BEFORE initialize() so they're available when on_settings_changed() is called
        
        // Set artwork request callback
        m_core->set_artwork_request_callback([this]() {
            update_artwork();
        });
        
        // Set color query callback for Custom theme mode (CUI global color scheme sync)
        m_core->set_color_query_callback([](COLORREF& bg, COLORREF& text, COLORREF& highlight) -> bool {
            try {
                // Use empty GUID to get global CUI colors
                cui::colours::helper colour_helper;
                bg = colour_helper.get_colour(cui::colours::colour_background);
                text = colour_helper.get_colour(cui::colours::colour_text);
                highlight = colour_helper.get_colour(cui::colours::colour_selection_background);
                return true;
            } catch (...) {
                return false;
            }
        });
        
        // Register for CUI colour change notifications
        m_colour_callback = std::make_unique<ColourCallback>(this);
        if (fb2k::std_api_try_get(m_colour_manager)) {
            m_colour_manager->register_common_callback(m_colour_callback.get());
        }
        
        // Now initialize (which calls on_settings_changed with callbacks available)
        m_core->initialize(wnd);
        
        // Apply glass effect if enabled in preferences
        if (get_nowbar_glass_effect_enabled()) {
            m_glass_effect_active = try_enable_acrylic_backdrop_cui(wnd);
        }
        
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
        // Unregister colour callback before destroying
        if (m_colour_manager.is_valid() && m_colour_callback) {
            m_colour_manager->deregister_common_callback(m_colour_callback.get());
        }
        m_colour_callback.reset();
        m_colour_manager.release();
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
            
            // Set minimum size
            SIZE minSize = m_core->get_min_size();
            mmi->ptMinTrackSize.x = minSize.cx;
            mmi->ptMinTrackSize.y = minSize.cy;
            
            // Set maximum height: 1.12 inches, scaled by DPI (~38% reduction from 1.8)
            // At 96 DPI: 1.12 * 96 = 108 pixels
            // Keeps panel compact and horizontal-focused (matches DUI)
            HDC hdc = GetDC(wnd);
            if (hdc) {
                int dpi = GetDeviceCaps(hdc, LOGPIXELSY);
                LONG max_height = static_cast<LONG>(1.12 * dpi);
                mmi->ptMaxTrackSize.y = max_height;
                ReleaseDC(wnd, hdc);
            }
        }
        return 0;
    }
        
    case WM_SETTINGCHANGE:
        // System settings changed - trigger theme update
        if (m_core) {
            m_core->on_settings_changed();
        }
        return 0;
    }
    
    return DefWindowProc(wnd, msg, wp, lp);
}

} // namespace nowbar

#include "pch.h"
#include "control_panel_cui.h"
#include "../preferences.h"
#include "../artwork_bridge.h"

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
        m_core->set_color_query_callback([](COLORREF& bg, COLORREF& text, COLORREF& highlight, COLORREF& selection) -> bool {
            try {
                cui::colours::helper colour_helper;
                bg = colour_helper.get_colour(cui::colours::colour_background);
                text = colour_helper.get_colour(cui::colours::colour_text);
                highlight = colour_helper.get_colour(cui::colours::colour_selection_background);
                selection = colour_helper.get_colour(cui::colours::colour_selection_background);
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

    // Check for pending online artwork from foo_artwork callback
    if (has_pending_online_artwork()) {
        HBITMAP bitmap = get_pending_online_artwork();
        if (bitmap) {
            m_core->set_artwork_from_hbitmap(bitmap);
            return;
        }
    }

    auto pc = playback_control::get();
    metadb_handle_ptr track;
    if (pc->get_now_playing(track) && track.is_valid()) {
        // Try local/embedded artwork first
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

        // No local artwork found - try stub image from foobar2000 display settings
        bool stub_set = false;
        try {
            auto stub_extractor = art_manager->open_stub(fb2k::noAbort);
            album_art_data_ptr stub_data;
            if (stub_extractor->query(album_art_ids::cover_front, stub_data, fb2k::noAbort)) {
                m_core->set_artwork(stub_data);
                stub_set = true;
            }
        } catch (...) {}

        // Try online via foo_artwork if enabled (may override stub)
        if (get_nowbar_online_artwork() && is_artwork_bridge_available()) {
            pfc::string8 artist, title;
            service_ptr_t<titleformat_object> script_artist, script_title;
            titleformat_compiler::get()->compile_safe(script_artist, "%artist%");
            titleformat_compiler::get()->compile_safe(script_title, "%title%");
            // Use playback_format_title for streams - it merges dynamic stream metadata
            pc->playback_format_title(nullptr, artist, script_artist, nullptr, playback_control::display_level_all);
            pc->playback_format_title(nullptr, title, script_title, nullptr, playback_control::display_level_all);
            request_online_artwork(artist.c_str(), title.c_str());
            // Don't clear artwork - stub or previous art shows while waiting
            return;
        }

        if (stub_set) return;
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
        // Release cached offscreen bitmap
        if (m_cache_bitmap) { SelectObject(m_cache_dc, m_cache_old_bitmap); DeleteObject(m_cache_bitmap); m_cache_bitmap = nullptr; }
        if (m_cache_dc) { DeleteDC(m_cache_dc); m_cache_dc = nullptr; }
        m_cache_w = m_cache_h = 0;
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

        RECT rect;
        GetClientRect(wnd, &rect);

        // Recreate cached offscreen bitmap only when window size changes
        if (rect.right != m_cache_w || rect.bottom != m_cache_h || !m_cache_dc) {
            if (m_cache_bitmap) { SelectObject(m_cache_dc, m_cache_old_bitmap); DeleteObject(m_cache_bitmap); m_cache_bitmap = nullptr; }
            if (m_cache_dc) { DeleteDC(m_cache_dc); m_cache_dc = nullptr; }
            m_cache_dc = CreateCompatibleDC(hdc);
            m_cache_bitmap = CreateCompatibleBitmap(hdc, rect.right, rect.bottom);
            m_cache_old_bitmap = (HBITMAP)SelectObject(m_cache_dc, m_cache_bitmap);
            m_cache_w = rect.right;
            m_cache_h = rect.bottom;
            if (m_core) m_core->force_full_repaint();
        }

        // Spectrum-only fast path: skip background/artwork/text/buttons redraw
        bool spectrum_fast = m_core && m_core->is_spectrum_animating_only() &&
                             get_nowbar_visualization_mode() == 1;
        // Waveform-only fast path: skip background/artwork/text/buttons redraw
        bool waveform_fast = m_core && m_core->is_waveform_progress_only() &&
                             get_nowbar_visualization_mode() == 2;
        if (spectrum_fast) {
            // Background cache in paint_spectrum_only covers the dirty areas — no clear needed
            m_core->paint_spectrum_only(m_cache_dc, rect);
        } else if (waveform_fast) {
            m_core->clear_waveform_dirty_rects(m_cache_dc, get_nowbar_initial_bg_color());
            m_core->paint_waveform_only(m_cache_dc, rect);
        } else {
            // Full repaint
            HBRUSH bgBrush = CreateSolidBrush(get_nowbar_initial_bg_color());
            FillRect(m_cache_dc, &rect, bgBrush);
            DeleteObject(bgBrush);
            if (m_core) {
                m_core->paint(m_cache_dc, rect);
            }
        }

        BitBlt(hdc, 0, 0, rect.right, rect.bottom, m_cache_dc, 0, 0, SRCCOPY);

        EndPaint(wnd, &ps);
        return 0;
    }
        
    case WM_ERASEBKGND: {
        // All painting is double-buffered (offscreen cache + BitBlt), so erasing
        // the screen DC here is unnecessary.  During resize the system can set the
        // erase flag between spectrum animation frames; if we fill the screen DC
        // and the next WM_PAINT only BitBlts the spectrum area (partial update
        // region), the non-spectrum areas keep the erase fill, producing a visible
        // black box.  Returning 1 without painting avoids this.
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
        
    case WM_SETTINGCHANGE:
        // System settings changed - trigger theme update
        if (m_core) {
            m_core->on_settings_changed();
        }
        return 0;
        
    case WM_TIMER: {
        // Handle different timers
        UINT_PTR timer_id = static_cast<UINT_PTR>(wp);
        if (timer_id == ControlPanelCore::COMMAND_STATE_TIMER_ID) {
            // Command state polling timer - poll for fb2k action states
            if (m_core) m_core->poll_custom_button_states();
        } else {
            // Animation timer fired - trigger a repaint to continue the animation
            const RECT* dirty = m_core ? m_core->get_animation_dirty_rect() : nullptr;
            InvalidateRect(wnd, dirty, FALSE);
            if (m_core) m_core->clear_animation_dirty();
        }
        return 0;
    }

    }

    return DefWindowProc(wnd, msg, wp, lp);
}

} // namespace nowbar

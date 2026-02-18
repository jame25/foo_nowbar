#include "pch.h"
#include "control_panel_dui.h"
#include "../preferences.h"
#include "../artwork_bridge.h"

// Windows 11 Build 22000+ DWM backdrop attribute (for older SDK headers)
#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#define DWMWA_SYSTEMBACKDROP_TYPE 38
#endif
// DWM_SYSTEMBACKDROP_TYPE enum values are in dwmapi.h on SDK 10.0.22000+

// Try to enable Windows 11 acrylic backdrop for a window
// Returns true if successfully applied, false if unsupported or failed
static bool try_enable_acrylic_backdrop(HWND hwnd) {
    // Check Windows 11 Build 22000+
    DWORD buildNumber = 0;
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
                      0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD size = sizeof(DWORD);
        RegQueryValueExW(hKey, L"CurrentBuildNumber", nullptr, nullptr,
                         reinterpret_cast<LPBYTE>(&buildNumber), &size);
        
        // CurrentBuildNumber is stored as string, need to re-query
        wchar_t buildStr[16] = {};
        size = sizeof(buildStr);
        if (RegQueryValueExW(hKey, L"CurrentBuildNumber", nullptr, nullptr,
                             reinterpret_cast<LPBYTE>(buildStr), &size) == ERROR_SUCCESS) {
            buildNumber = _wtoi(buildStr);
        }
        RegCloseKey(hKey);
    }
    
    // Windows 11 Build 22000 or later required for DWMWA_SYSTEMBACKDROP_TYPE
    if (buildNumber < 22000) {
        return false;
    }
    
    // Try to set acrylic backdrop
    DWM_SYSTEMBACKDROP_TYPE backdropType = DWMSBT_TRANSIENTWINDOW;  // Acrylic
    HRESULT hr = DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE,
                                       &backdropType, sizeof(backdropType));
    return SUCCEEDED(hr);
}

// Disable the backdrop effect
static void disable_acrylic_backdrop(HWND hwnd) {
    DWM_SYSTEMBACKDROP_TYPE backdropType = DWMSBT_NONE;
    DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE,
                          &backdropType, sizeof(backdropType));
}

namespace nowbar {

// Window class name
const wchar_t* ControlPanelDUI::get_class_name() {
    return L"foo_nowbar_dui_element";
}

bool ControlPanelDUI::register_class() {
    static bool registered = false;
    if (registered) return true;
    
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = core_api::get_my_instance();
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(24, 24, 24));  // Dark background to prevent white flash
    wc.lpszClassName = get_class_name();
    
    registered = (RegisterClassExW(&wc) != 0);
    return registered;
}

ui_element_config::ptr ControlPanelDUI::g_get_default_configuration() {
    return ui_element_config::g_create_empty(g_get_guid());
}

ControlPanelDUI::ControlPanelDUI(ui_element_config::ptr config, ui_element_instance_callback::ptr callback)
    : m_config(config)
    , m_callback(callback)
{
}

ControlPanelDUI::~ControlPanelDUI() {
    if (m_hwnd) {
        SetWindowLongPtr(m_hwnd, GWLP_USERDATA, 0);
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
}

void ControlPanelDUI::initialize_window(HWND parent) {
    if (!register_class()) return;
    
    m_hwnd = CreateWindowExW(
        0,
        get_class_name(),
        L"",
        WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | WS_VISIBLE,
        0, 0, 100, 100,
        parent,
        nullptr,
        core_api::get_my_instance(),
        this
    );
}

void ControlPanelDUI::set_configuration(ui_element_config::ptr data) {
    m_config = data;
}

ui_element_config::ptr ControlPanelDUI::get_configuration() {
    return m_config;
}

ui_element_min_max_info ControlPanelDUI::get_min_max_info() {
    ui_element_min_max_info info;
    
    // Get DPI for scaling calculations
    int dpi = 96;
    if (m_hwnd) {
        HDC hdc = GetDC(m_hwnd);
        if (hdc) {
            dpi = GetDeviceCaps(hdc, LOGPIXELSY);
            ReleaseDC(m_hwnd, hdc);
        }
    }

    // Minimum height: 0.55 inches, scaled by DPI
    // At 96 DPI: 0.55 * 96 = 53 pixels
    info.m_min_height = static_cast<t_uint32>(0.55 * dpi);
    
    // Maximum height: 1.12 inches, scaled by DPI (~38% total reduction from 1.8)
    // At 96 DPI: 1.12 * 96 = 108 pixels
    // Keeps panel compact and horizontal-focused
    info.m_max_height = static_cast<t_uint32>(1.12 * dpi);
    
    // Fixed minimum width that accommodates all elements at any height
    // Including Super button, spectrum visualizer after Repeat
    info.m_min_width = 1232;
    
    return info;
}

void ControlPanelDUI::update_artwork() {
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

LRESULT CALLBACK ControlPanelDUI::WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    ControlPanelDUI* self = nullptr;
    
    if (msg == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCT*>(lp);
        self = static_cast<ControlPanelDUI*>(cs->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->m_hwnd = hwnd;
    } else {
        self = reinterpret_cast<ControlPanelDUI*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }
    
    if (self) {
        return self->handle_message(msg, wp, lp);
    }
    
    return DefWindowProc(hwnd, msg, wp, lp);
}

LRESULT ControlPanelDUI::handle_message(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        m_core = std::make_unique<ControlPanelCore>();
        
        // Set callbacks BEFORE initialize() so they're available when on_settings_changed() is called
        m_core->set_artwork_request_callback([this]() {
            update_artwork();
        });
        
        // Set color query callback for Custom theme mode (DUI color scheme sync)
        m_core->set_color_query_callback([this](COLORREF& bg, COLORREF& text, COLORREF& highlight, COLORREF& selection) -> bool {
            if (!m_callback.is_valid()) return false;
            try {
                bg = m_callback->query_std_color(ui_color_background);
                text = m_callback->query_std_color(ui_color_text);
                highlight = m_callback->query_std_color(ui_color_highlight);
                selection = m_callback->query_std_color(ui_color_selection);
                return true;
            } catch (...) {
                return false;
            }
        });
        
        // Now initialize (which calls on_settings_changed with callbacks available)
        m_core->initialize(m_hwnd);
        
        // Apply glass effect if enabled in preferences
        if (get_nowbar_glass_effect_enabled()) {
            m_glass_effect_active = try_enable_acrylic_backdrop(m_hwnd);
        }
        
        update_artwork();
        return 0;
        
    case WM_DESTROY:
        m_core.reset();
        // Release cached offscreen bitmap
        if (m_cache_bitmap) { SelectObject(m_cache_dc, m_cache_old_bitmap); DeleteObject(m_cache_bitmap); m_cache_bitmap = nullptr; }
        if (m_cache_dc) { DeleteDC(m_cache_dc); m_cache_dc = nullptr; }
        m_cache_w = m_cache_h = 0;
        return 0;
        
    case WM_SIZE: {
        if (m_core) {
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
        return 0;
    }
        
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(m_hwnd, &ps);

        RECT rect;
        GetClientRect(m_hwnd, &rect);

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

        EndPaint(m_hwnd, &ps);
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
        
    case WM_MOUSEMOVE:
        if (!m_tracking_mouse) {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, m_hwnd, 0 };
            TrackMouseEvent(&tme);
            m_tracking_mouse = true;
        }
        if (m_core) {
            m_core->on_mouse_move(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        }
        return 0;
        
    case WM_MOUSELEAVE:
        m_tracking_mouse = false;
        if (m_core) {
            m_core->on_mouse_leave();
        }
        return 0;
        
    case WM_LBUTTONDOWN:
        SetCapture(m_hwnd);
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
        
    case WM_TIMER: {
        // Handle different timers
        UINT_PTR timer_id = static_cast<UINT_PTR>(wp);
        if (timer_id == ControlPanelCore::COMMAND_STATE_TIMER_ID) {
            // Command state polling timer - poll for fb2k action states
            if (m_core) m_core->poll_custom_button_states();
        } else {
            // Animation timer fired - trigger a repaint to continue the animation
            const RECT* dirty = m_core ? m_core->get_animation_dirty_rect() : nullptr;
            InvalidateRect(m_hwnd, dirty, FALSE);
            if (m_core) m_core->clear_animation_dirty();
        }
        return 0;
    }

    }

    return DefWindowProc(m_hwnd, msg, wp, lp);
}

// DUI factory - simple implementation without WTL
class ControlPanelDUIElement : public ui_element {
public:
    GUID get_guid() override { return ControlPanelDUI::g_get_guid(); }
    GUID get_subclass() override { return ControlPanelDUI::g_get_subclass(); }
    void get_name(pfc::string_base& out) override { ControlPanelDUI::g_get_name(out); }
    
    ui_element_instance::ptr instantiate(HWND parent, ui_element_config::ptr cfg, ui_element_instance_callback::ptr callback) override {
        PFC_ASSERT(cfg->get_guid() == get_guid());
        service_ptr_t<ControlPanelDUI> item = new service_impl_t<ControlPanelDUI>(cfg, callback);
        item->initialize_window(parent);
        return item;
    }
    
    ui_element_config::ptr get_default_configuration() override {
        return ControlPanelDUI::g_get_default_configuration();
    }
    
    ui_element_children_enumerator_ptr enumerate_children(ui_element_config::ptr cfg) override {
        return nullptr;
    }
    
    bool get_description(pfc::string_base& out) override {
        out = ControlPanelDUI::g_get_description();
        return true;
    }
};

static service_factory_single_t<ControlPanelDUIElement> g_dui_factory;

} // namespace nowbar

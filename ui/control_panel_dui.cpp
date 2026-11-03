#include "pch.h"
#include "control_panel_dui.h"
#include "../preferences.h"
#include "../artwork_bridge.h"

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
    // Release cached GDI objects explicitly. WM_DESTROY cleanup is unreachable
    // here because GWLP_USERDATA is zeroed before DestroyWindow, so WindowProc
    // falls through to DefWindowProc instead of handle_message.
    if (m_cache_bitmap) { SelectObject(m_cache_dc, m_cache_old_bitmap); DeleteObject(m_cache_bitmap); m_cache_bitmap = nullptr; }
    if (m_cache_dc) { DeleteDC(m_cache_dc); m_cache_dc = nullptr; }

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

    // Minimum height: 0.55 inches, scaled by DPI (53 pixels at 96 DPI)
    info.m_min_height = static_cast<t_uint32>(0.55 * dpi);

    // Maximum height: 92 pixels at 96 DPI, scaled by DPI
    info.m_max_height = static_cast<t_uint32>(92.0 * dpi / 96.0);

    double dpi_scale = static_cast<double>(dpi) / 96.0;
    double min_scale = 0.60;

    int button_size = static_cast<int>(28.0 * dpi_scale * min_scale);
    int play_button_size = static_cast<int>(36.0 * dpi_scale * min_scale);
    int spacing = static_cast<int>(16.0 * dpi_scale * min_scale);

    int min_info_w = static_cast<int>(140.0 * dpi_scale);
    int total_min_w = 0;

    // Artwork
    if (get_nowbar_cover_artwork_visible()) {
        int art_h = static_cast<int>(info.m_min_height);
        int art_margin = get_nowbar_cover_margin() ? static_cast<int>(8.0 * dpi_scale) : 0;
        int art_size = art_h - art_margin * 2;
        if (art_size > static_cast<int>(128.0 * dpi_scale)) art_size = static_cast<int>(128.0 * dpi_scale);
        if (art_size < 32) art_size = 32;
        total_min_w += art_margin + art_size + spacing;
    } else {
        total_min_w += spacing;
    }

    // Track Info
    total_min_w += min_info_w;

    // Left timer space (in non-spectrum modes)
    int vis_mode = get_nowbar_visualization_mode();
    if (vis_mode != 1) {
        total_min_w += static_cast<int>(50.0 * dpi_scale);
    }

    // Mood icon
    if (get_nowbar_mood_icon_visible()) {
        total_min_w += button_size + spacing;
    }

    // Rating stars (if visible and not line 3)
    if (get_nowbar_rating_visible() && get_nowbar_rating_mode() != 2) {
        int star_size = static_cast<int>(button_size * 0.55f);
        int star_gap = static_cast<int>(2.0 * dpi_scale);
        total_min_w += (star_size * 5 + star_gap * 4) + spacing;
    }

    // Core playback buttons: prev, play, next (always visible) + optional buttons
    bool shuffle_visible = get_nowbar_shuffle_icon_visible();
    bool repeat_visible = get_nowbar_repeat_icon_visible();
    bool stop_visible = get_nowbar_stop_icon_visible();
    bool stop_after_current_visible = get_nowbar_stop_after_current_icon_visible();
    bool super_visible = get_nowbar_super_icon_visible();
    int core_buttons = 3;
    if (shuffle_visible) core_buttons++;
    if (repeat_visible) core_buttons++;
    if (stop_visible) core_buttons++;
    if (stop_after_current_visible) core_buttons++;
    if (super_visible) core_buttons++;
    int core_width = button_size * (core_buttons - 1) + play_button_size + spacing * (core_buttons - 1);
    total_min_w += core_width;

    // Remaining timer space (always included to guarantee room for remaining time display)
    total_min_w += static_cast<int>(50.0 * dpi_scale);

    // Right-side controls (custom buttons, volume, miniplayer)
    bool btn_enabled[6] = {
        get_nowbar_cbutton_enabled(0), get_nowbar_cbutton_enabled(1), get_nowbar_cbutton_enabled(2),
        get_nowbar_cbutton_enabled(3), get_nowbar_cbutton_enabled(4), get_nowbar_cbutton_enabled(5)
    };
    int total_cbuttons = 0;
    for (int i = 0; i < 6; i++) {
        if (btn_enabled[i]) total_cbuttons++;
    }
    int right_group_w = 0;
    bool has_prev_right = false;

    if (total_cbuttons > 0) {
        int cb_w = total_cbuttons * button_size + (total_cbuttons - 1) * spacing;
        right_group_w += cb_w;
        has_prev_right = true;
    }
    if (get_nowbar_volume_bar_visible()) {
        if (has_prev_right) right_group_w += spacing;
        right_group_w += static_cast<int>(192.0 * dpi_scale * min_scale);
        has_prev_right = true;
    } else {
        bool vol_icon = get_nowbar_volume_icon_visible();
        bool vol_num = get_nowbar_volume_number_enabled();
        if (vol_icon && vol_num) {
            if (has_prev_right) right_group_w += spacing;
            int icon_w = static_cast<int>(23.0 * dpi_scale * min_scale);
            int gap_w = static_cast<int>(14.0 * dpi_scale * min_scale);
            int num_w = static_cast<int>(28.0 * dpi_scale);
            right_group_w += icon_w + gap_w + num_w;
            has_prev_right = true;
        } else if (vol_icon) {
            if (has_prev_right) right_group_w += spacing;
            right_group_w += static_cast<int>(23.0 * dpi_scale * min_scale);
            has_prev_right = true;
        } else if (vol_num) {
            if (has_prev_right) right_group_w += spacing;
            right_group_w += static_cast<int>(28.0 * dpi_scale);
            has_prev_right = true;
        }
    }
    if (get_nowbar_miniplayer_icon_visible()) {
        if (has_prev_right) right_group_w += spacing;
        right_group_w += button_size;
        has_prev_right = true;
    }

    if (right_group_w > 0) {
        // Space #2 between Center Controls (including remaining timer) and Right Controls
        total_min_w += spacing + right_group_w;
    }

    // Right margin / inset
    int right_margin_pad = get_nowbar_cover_margin() ? static_cast<int>(8.0 * dpi_scale) : 0;
    total_min_w += right_margin_pad + static_cast<int>(16.0 * dpi_scale);

    info.m_min_width = static_cast<t_uint32>(total_min_w * 0.82f);
    
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

    metadb_handle_ptr track = m_core->get_display_track();
    if (track.is_valid()) {
        auto pc = playback_control::get();
        bool is_playing = pc->is_playing() || pc->is_paused();
        bool is_playing_track = false;
        if (is_playing) {
            metadb_handle_ptr now_playing_track;
            if (pc->get_now_playing(now_playing_track) && now_playing_track == track) {
                is_playing_track = true;
            }
        }

        // Remote internet streams only have dynamic artwork while actively playing
        pfc::string8 path = track->get_path();
        bool is_stream = (strstr(path.c_str(), "://") != nullptr && strstr(path.c_str(), "file://") != path.c_str());
        if (is_stream && !is_playing_track) {
            m_core->clear_artwork();
            return;
        }

        // Opening a live URL here may block on network I/O with noAbort.
        // Stream covers arrive through foo_artwork; extract embedded art only for local files.
        auto art_manager = album_art_manager_v3::get();
        if (!is_stream) {
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

        // Check if foo_artwork has a cached cover image file on disk
        if (get_nowbar_online_artwork()) {
            static service_ptr_t<titleformat_object> tf_cover;
            if (!tf_cover.is_valid()) {
                titleformat_compiler::get()->compile_safe(tf_cover, "%foo_artwork_cover%");
            }
            pfc::string8 cover_path;
            if (is_playing_track) {
                pc->playback_format_title(nullptr, cover_path, tf_cover, nullptr, playback_control::display_level_all);
            } else {
                track->format_title(nullptr, cover_path, tf_cover, nullptr);
            }
            if (!cover_path.is_empty()) {
                pfc::stringcvt::string_wide_from_utf8 wide_cover_path(cover_path.c_str());
                if (GetFileAttributesW(wide_cover_path.get_ptr()) != INVALID_FILE_ATTRIBUTES) {
                    HANDLE hFile = CreateFileW(wide_cover_path.get_ptr(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
                    if (hFile != INVALID_HANDLE_VALUE) {
                        DWORD size = GetFileSize(hFile, NULL);
                        if (size > 0 && size < 64 * 1024 * 1024) {
                            std::vector<uint8_t> buffer(size);
                            DWORD bytesRead = 0;
                            if (ReadFile(hFile, buffer.data(), size, &bytesRead, NULL) && bytesRead == size) {
                                album_art_data_ptr data = album_art_data_impl::g_create(buffer.data(), buffer.size());
                                if (data.is_valid()) {
                                    m_core->set_artwork(data);
                                    CloseHandle(hFile);
                                    return;
                                }
                            }
                        }
                        CloseHandle(hFile);
                    }
                }
            }
        }

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
            if (!m_tf_artist.is_valid())
                titleformat_compiler::get()->compile_safe(m_tf_artist, "$if($or(%artist%,%title%),%artist%,%foo_artwork_artist%)");
            if (!m_tf_title.is_valid())
                titleformat_compiler::get()->compile_safe(m_tf_title, "$if($or(%artist%,%title%),%title%,%foo_artwork_title%)");
            if (is_playing_track) {
                pc->playback_format_title(nullptr, artist, m_tf_artist, nullptr, playback_control::display_level_all);
                pc->playback_format_title(nullptr, title, m_tf_title, nullptr, playback_control::display_level_all);
            } else {
                track->format_title(nullptr, artist, m_tf_artist, nullptr);
                track->format_title(nullptr, title, m_tf_title, nullptr);
            }
            const bool keep_online_art = m_core->has_online_artwork_for_track(track) &&
                                         is_online_artwork_loaded_for(artist.c_str(), title.c_str());
            if (!stub_set && !keep_online_art) {
                m_core->clear_artwork();
            }
            if (!artist.is_empty() || !title.is_empty()) {
                request_online_artwork(artist.c_str(), title.c_str());
            }
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
        
        // Set settings changed callback to notify DUI host of minimum width updates
        m_core->set_settings_changed_callback([this]() {
            if (m_callback.is_valid()) {
                m_callback->on_min_max_info_change();
            }
        });
        
        // Now initialize (which calls on_settings_changed with callbacks available)
        m_core->initialize(m_hwnd);
        
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
            m_core->force_full_repaint();
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
        if (m_callback.is_valid()) {
            m_callback->on_min_max_info_change();
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
            m_core->clear_waveform_dirty_rects(m_cache_dc, m_core->get_bg_colorref());
            m_core->paint_waveform_only(m_cache_dc, rect);
        } else {
            {
                HBRUSH bgBrush = CreateSolidBrush(m_core ? m_core->get_bg_colorref() : get_nowbar_initial_bg_color());
                FillRect(m_cache_dc, &rect, bgBrush);
                DeleteObject(bgBrush);
            }
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
        
    case ControlPanelCore::WM_NOWBAR_ANIMATE: {
        if (m_core) m_core->on_animation_timer_fired();
        const RECT* dirty = m_core ? m_core->get_animation_dirty_rect() : nullptr;
        InvalidateRect(m_hwnd, dirty, FALSE);
        if (m_core) m_core->clear_animation_dirty();
        return 0;
    }

    case WM_TIMER: {
        UINT_PTR timer_id = static_cast<UINT_PTR>(wp);
        if (timer_id == ControlPanelCore::COMMAND_STATE_TIMER_ID) {
            if (m_core) m_core->poll_custom_button_states();
        } else if (timer_id == ControlPanelCore::SHOW_PREFS_TIMER_ID) {
            if (m_core) m_core->do_show_preferences();
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

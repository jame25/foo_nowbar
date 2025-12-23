#include "pch.h"
#include "control_panel_core.h"
#include "../preferences.h"
#include "../resource.h"
#include <vector>
#include <algorithm>
#include <memory>
#include <mutex>
#include <dwmapi.h>
#include <commdlg.h>
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "comdlg32.lib")

namespace nowbar {

// Static instance registry for theme/settings change notifications
static std::vector<ControlPanelCore*> g_instances;
static std::mutex g_instances_mutex;

// Forward declare to allow use before full definition
class theme_change_callback;

// Global instance - will be created when component loads
static std::unique_ptr<theme_change_callback> g_theme_callback;

// ui_config_callback to detect theme changes - uses ui_config_callback_impl for auto-registration
class theme_change_callback : public ui_config_callback_impl {
public:
    void ui_colors_changed() override {
        ControlPanelCore::notify_theme_changed();
    }
};

void ControlPanelCore::register_instance(ControlPanelCore* instance) {
    std::lock_guard<std::mutex> lock(g_instances_mutex);
    // Create theme callback on first registration
    if (!g_theme_callback && core_api::are_services_available()) {
        g_theme_callback = std::make_unique<theme_change_callback>();
    }
    g_instances.push_back(instance);
}

void ControlPanelCore::unregister_instance(ControlPanelCore* instance) {
    std::lock_guard<std::mutex> lock(g_instances_mutex);
    g_instances.erase(std::remove(g_instances.begin(), g_instances.end(), instance), g_instances.end());
}

void ControlPanelCore::notify_theme_changed() {
    std::lock_guard<std::mutex> lock(g_instances_mutex);
    // Call on_settings_changed() which properly respects theme mode preferences
    for (auto* instance : g_instances) {
        instance->on_settings_changed();
    }
}

// Helper: format time as mm:ss or hh:mm:ss
static std::wstring format_time(double seconds) {
    if (seconds < 0) seconds = 0;
    int total_seconds = static_cast<int>(seconds);
    int hours = total_seconds / 3600;
    int minutes = (total_seconds % 3600) / 60;
    int secs = total_seconds % 60;
    
    wchar_t buf[32];
    if (hours > 0) {
        swprintf_s(buf, L"%d:%02d:%02d", hours, minutes, secs);
    } else {
        swprintf_s(buf, L"%d:%02d", minutes, secs);
    }
    return buf;
}

// Helper: convert UTF-8 to wide string
static std::wstring utf8_to_wide(const char* utf8) {
    if (!utf8 || !*utf8) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
    if (len <= 0) return L"";
    std::wstring result(len - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, &result[0], len);
    return result;
}

// Helper: point in rect
static bool pt_in_rect(const RECT& r, int x, int y) {
    return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
}

ControlPanelCore::ControlPanelCore() {
    // Register for playback callbacks
    PlaybackStateManager::get().register_callback(this);
    m_state = PlaybackStateManager::get().get_state();
    
    // Register for theme change notifications
    register_instance(this);
    
    // Default colors (dark mode)
    m_bg_color = Gdiplus::Color(255, 24, 24, 24);
    m_text_color = Gdiplus::Color(255, 255, 255, 255);
    m_text_secondary_color = Gdiplus::Color(255, 160, 160, 160);
    m_accent_color = Gdiplus::Color(255, 100, 180, 255);  // Light blue for active shuffle/repeat
    m_button_hover_color = Gdiplus::Color(40, 255, 255, 255);
}

ControlPanelCore::~ControlPanelCore() {
    unregister_instance(this);
    PlaybackStateManager::get().unregister_callback(this);
}

void ControlPanelCore::initialize(HWND hwnd) {
    m_hwnd = hwnd;

    // Get DPI
    HDC hdc = GetDC(hwnd);
    int dpi = GetDeviceCaps(hdc, LOGPIXELSX);
    ReleaseDC(hwnd, hdc);
    m_dpi_scale = dpi / 96.0f;

    // Scale metrics
    m_metrics = LayoutMetrics();
    m_metrics.scale(m_dpi_scale);

    // Apply theme mode and fonts from preferences
    on_settings_changed();
    
    // Initialize mood state for current track
    update_mood_state();
}

void ControlPanelCore::set_dark_mode(bool dark) {
    m_dark_mode = dark;
    if (dark) {
        m_bg_color = Gdiplus::Color(255, 24, 24, 24);
        m_text_color = Gdiplus::Color(255, 255, 255, 255);
        m_text_secondary_color = Gdiplus::Color(255, 120, 120, 120);  // Darker gray for icons
    } else {
        m_bg_color = Gdiplus::Color(255, 245, 245, 245);
        m_text_color = Gdiplus::Color(255, 0, 0, 0);
        m_text_secondary_color = Gdiplus::Color(255, 70, 70, 70);  // Darker gray for icons
    }
    invalidate();
}

void ControlPanelCore::update_dpi(float scale) {
    m_dpi_scale = scale;
    m_metrics = LayoutMetrics();
    m_metrics.scale(scale);

    // Recreate fonts with new DPI
    update_fonts();

    invalidate();
}

SIZE ControlPanelCore::get_min_size() const {
    // Minimum height: 0.55 inches, scaled by DPI
    // At 96 DPI: 0.55 * 96 = 53 pixels
    LONG min_height = static_cast<LONG>(0.55 * 96 * m_dpi_scale);
    return {
        static_cast<LONG>(200 * m_dpi_scale),  // Reasonable minimum width
        min_height
    };
}

void ControlPanelCore::set_miniplayer_active(bool active) {
    if (m_miniplayer_active != active) {
        m_miniplayer_active = active;
        invalidate();
    }
}

void ControlPanelCore::notify_all_settings_changed() {
    std::lock_guard<std::mutex> lock(g_instances_mutex);
    for (auto* instance : g_instances) {
        instance->on_settings_changed();
    }
}

void ControlPanelCore::on_settings_changed() {
    // Reload theme mode from preferences
    int theme_mode = get_nowbar_theme_mode();

    bool dark;
    switch (theme_mode) {
        case 1:  // Dark
            dark = true;
            break;
        case 2:  // Light
            dark = false;
            break;
        default:  // Auto (0) - follow system/foobar setting
            dark = ui_config_manager::g_is_dark_mode();
            break;
    }

    set_dark_mode(dark);

    // Update fonts from preferences
    update_fonts();
}

void ControlPanelCore::update_fonts() {
    // Get font settings from preferences
    LOGFONT lf_track = get_nowbar_use_custom_fonts() ? get_nowbar_track_font() : get_nowbar_default_font(false);
    LOGFONT lf_artist = get_nowbar_use_custom_fonts() ? get_nowbar_artist_font() : get_nowbar_default_font(true);

    // Create GDI+ fonts from LOGFONT
    // Track title font
    HDC hdc = GetDC(m_hwnd);
    Gdiplus::Font* titleFont = new Gdiplus::Font(hdc, &lf_track);
    Gdiplus::Font* artistFont = new Gdiplus::Font(hdc, &lf_artist);
    ReleaseDC(m_hwnd, hdc);

    m_font_title.reset(titleFont);
    m_font_artist.reset(artistFont);

    // Time font - always use default Microsoft YaHei
    Gdiplus::FontFamily fontFamily(L"Microsoft YaHei");
    m_font_time.reset(new Gdiplus::Font(&fontFamily, 9.0f * m_dpi_scale, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint));

    invalidate();
}

void ControlPanelCore::update_layout(const RECT& rect) {
    int w = rect.right - rect.left;
    int h = rect.bottom - rect.top;
    int y_center = rect.top + h / 2;

    // Artwork (left side) - size based on panel height with margins
    int art_margin = get_nowbar_cover_margin() ? m_metrics.artwork_margin : 0;
    int art_size = h - (art_margin * 2);  // Fit within panel height with margins
    if (art_size > m_metrics.artwork_size) {
        art_size = m_metrics.artwork_size;  // Cap at max size
    }
    if (art_size < 32) {
        art_size = 32;  // Minimum size
    }
    int art_y = y_center - art_size / 2;
    m_rect_artwork = {
        rect.left + art_margin,
        art_y,
        rect.left + art_margin + art_size,
        art_y + art_size
    };
    
    // Calculate size scale factor based on panel height
    int reference_height = static_cast<int>(130 * m_dpi_scale);  // Reference height for full size
    int min_height = static_cast<int>(53 * m_dpi_scale);  // Minimum panel height (0.55 inches)
    float size_ratio = static_cast<float>(h - min_height) / static_cast<float>(reference_height - min_height);
    if (size_ratio < 0) size_ratio = 0;
    if (size_ratio > 1) size_ratio = 1;
    // Scale from 60% at minimum to 100% at reference height
    m_size_scale = 0.60f + 0.40f * size_ratio;

    // Scaled sizes for controls
    int button_size = static_cast<int>(m_metrics.button_size * m_size_scale);
    int play_button_size = static_cast<int>(m_metrics.play_button_size * m_size_scale);
    int spacing = static_cast<int>(m_metrics.spacing * m_size_scale);
    int volume_width = static_cast<int>(m_metrics.volume_width * m_size_scale);

    // Volume and MiniPlayer (right side) - moved up by 10%
    int vol_mp_offset = h / 10;  // Move up by 10%
    int mp_size = static_cast<int>(29 * m_dpi_scale * m_size_scale);  // MiniPlayer icon size (scaled)
    int right_inset = static_cast<int>(16 * m_dpi_scale);  // Extra inset from right edge
    int right_margin = rect.right - art_margin - right_inset;
    
    // MiniPlayer button (only if visible)
    int volume_right_edge;
    if (get_nowbar_miniplayer_icon_visible()) {
        int mp_x = right_margin - mp_size;
        int btn_y_right = y_center - mp_size / 2 - vol_mp_offset;
        m_rect_miniplayer = { mp_x, btn_y_right, mp_x + mp_size, btn_y_right + mp_size };
        volume_right_edge = mp_x - spacing;
    } else {
        m_rect_miniplayer = {};  // Clear rect when hidden
        volume_right_edge = right_margin;
    }

    int vol_bar_height = static_cast<int>(20 * m_dpi_scale * m_size_scale);  // Volume bar vertical size (scaled)
    int vol_x = volume_right_edge - volume_width;
    m_rect_volume = { vol_x, y_center - vol_bar_height / 2 - vol_mp_offset, volume_right_edge, y_center + vol_bar_height / 2 - vol_mp_offset };

    // Control buttons (center) - heart, shuffle, prev, play, next, repeat
    int controls_width = button_size * 5 + play_button_size + spacing * 5;
    int controls_x = rect.left + (w - controls_width) / 2;
    
    // Calculate seekbar and time display heights first to determine available space
    int seek_gap = static_cast<int>(10 * m_dpi_scale * m_size_scale);
    int seekbar_height = static_cast<int>(m_metrics.seekbar_height * m_size_scale);
    int total_seekbar_area = seek_gap + seekbar_height;
    
    // Calculate how much space we need below the play button for seekbar
    // At minimum height, we need to move controls up more to fit everything
    int bottom_margin = static_cast<int>(4 * m_dpi_scale);  // Small margin at bottom
    int available_below_center = (rect.bottom - rect.top) / 2 - bottom_margin;
    int needed_below_center = play_button_size / 2 + total_seekbar_area;
    
    // Increase vertical offset if needed to fit seekbar within panel
    int base_offset = h / 10;  // Base 10% offset
    int extra_offset = 0;
    if (needed_below_center > available_below_center) {
        extra_offset = needed_below_center - available_below_center;
    }
    int vertical_offset = base_offset + extra_offset;
    
    int btn_y = y_center - button_size / 2 - vertical_offset;
    int play_y = y_center - play_button_size / 2 - vertical_offset;

    // Heart button (only if visible)
    if (get_nowbar_mood_icon_visible()) {
        m_rect_heart = { controls_x, btn_y, controls_x + button_size, btn_y + button_size };
        controls_x += button_size + spacing;
    } else {
        m_rect_heart = {};  // Clear rect when hidden
    }

    m_rect_shuffle = { controls_x, btn_y, controls_x + button_size, btn_y + button_size };
    controls_x += button_size + spacing;

    m_rect_prev = { controls_x, btn_y, controls_x + button_size, btn_y + button_size };
    controls_x += button_size + spacing;

    m_rect_play = { controls_x, play_y, controls_x + play_button_size, play_y + play_button_size };
    controls_x += play_button_size + spacing;

    m_rect_next = { controls_x, btn_y, controls_x + button_size, btn_y + button_size };
    controls_x += button_size + spacing;

    m_rect_repeat = { controls_x, btn_y, controls_x + button_size, btn_y + button_size };
    
    // Track info (between artwork and controls) - truly vertically centered on panel
    int info_x = m_rect_artwork.right + spacing;
    // Use heart button for right edge if visible, otherwise use shuffle button
    int info_right = get_nowbar_mood_icon_visible() ? (m_rect_heart.left - spacing) : (m_rect_shuffle.left - spacing);
    int info_height = static_cast<int>((m_metrics.text_height * 2 + 8) * m_size_scale);  // Title + artist + gap (scaled)
    int info_y = y_center - info_height / 2;  // Centered on panel, not offset with controls
    m_rect_track_info = { info_x, info_y, info_right, info_y + info_height };

    // Seekbar (directly below control icons, 20% wider than control buttons)
    int seek_y = m_rect_play.bottom + seek_gap;
    int seekbar_base_width = m_rect_repeat.right - m_rect_shuffle.left;
    int extra_width = seekbar_base_width / 10;  // 10% on each side = 20% total
    m_rect_seekbar = {
        m_rect_shuffle.left - extra_width,     // Extend 10% left
        seek_y,
        m_rect_repeat.right + extra_width,     // Extend 10% right
        seek_y + seekbar_height
    };

    // Time display (beside seekbar) - no longer needed below, times are drawn inline
    int time_gap = static_cast<int>(2 * m_dpi_scale * m_size_scale);
    m_rect_time = {
        m_rect_seekbar.left,
        m_rect_seekbar.bottom + time_gap,
        m_rect_seekbar.right,
        m_rect_seekbar.bottom + static_cast<int>(m_metrics.text_height * m_size_scale)
    };
}

void ControlPanelCore::paint(HDC hdc, const RECT& rect) {
    update_layout(rect);
    
    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
    
    draw_background(g, rect);
    draw_artwork(g);
    draw_track_info(g);
    draw_playback_buttons(g);
    draw_seekbar(g);
    draw_time_display(g);
    draw_volume(g);
    
    // MiniPlayer button (only if visible)
    if (get_nowbar_miniplayer_icon_visible()) {
        bool mp_hovered = (m_hover_region == HitRegion::MiniPlayerButton);
        int mp_w = m_rect_miniplayer.right - m_rect_miniplayer.left;
        int mp_h = m_rect_miniplayer.bottom - m_rect_miniplayer.top;
        if (mp_hovered) {
            Gdiplus::SolidBrush hoverBrush(m_button_hover_color);
            g.FillEllipse(&hoverBrush, m_rect_miniplayer.left, m_rect_miniplayer.top, mp_w, mp_h);
        }
        Gdiplus::Color mpColor = m_miniplayer_active ? m_accent_color : m_text_secondary_color;
        draw_miniplayer_icon(g, m_rect_miniplayer, mpColor);
    }
}

void ControlPanelCore::draw_background(Gdiplus::Graphics& g, const RECT& rect) {
    Gdiplus::SolidBrush brush(m_bg_color);
    Gdiplus::Rect r(rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top);
    g.FillRectangle(&brush, r);
}

void ControlPanelCore::draw_artwork(Gdiplus::Graphics& g) {
    int w = m_rect_artwork.right - m_rect_artwork.left;
    int h = m_rect_artwork.bottom - m_rect_artwork.top;
    
    if (m_artwork_bitmap) {
        g.DrawImage(m_artwork_bitmap.get(), m_rect_artwork.left, m_rect_artwork.top, w, h);
    } else {
        // Draw placeholder
        Gdiplus::SolidBrush brush(Gdiplus::Color(255, 60, 60, 60));
        Gdiplus::Rect artR(m_rect_artwork.left, m_rect_artwork.top, w, h);
        g.FillRectangle(&brush, artR);
        
        // Music note icon placeholder
        Gdiplus::SolidBrush iconBrush(m_text_secondary_color);
        Gdiplus::Font iconFont(L"Segoe MDL2 Assets", 24.0f * m_dpi_scale, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        Gdiplus::StringFormat sf;
        sf.SetAlignment(Gdiplus::StringAlignmentCenter);
        sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        Gdiplus::RectF artRect((float)m_rect_artwork.left, (float)m_rect_artwork.top, (float)w, (float)h);
        g.DrawString(L"\uE8D6", -1, &iconFont, artRect, &sf, &iconBrush);  // Music note
    }
}

void ControlPanelCore::draw_track_info(Gdiplus::Graphics& g) {
    Gdiplus::SolidBrush titleBrush(m_text_color);
    Gdiplus::SolidBrush artistBrush(m_text_secondary_color);
    
    Gdiplus::RectF titleRect(
        (float)m_rect_track_info.left, (float)m_rect_track_info.top,
        (float)(m_rect_track_info.right - m_rect_track_info.left), 
        (float)m_metrics.text_height
    );
    
    int scaled_text_height = static_cast<int>(m_metrics.text_height * m_size_scale);
    int scaled_gap = static_cast<int>(4 * m_size_scale);

    Gdiplus::RectF artistRect(
        (float)m_rect_track_info.left, (float)(m_rect_track_info.top + scaled_text_height + scaled_gap),
        (float)(m_rect_track_info.right - m_rect_track_info.left),
        (float)scaled_text_height
    );
    
    Gdiplus::StringFormat sf;
    sf.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
    sf.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
    
    std::wstring title = utf8_to_wide(m_state.track_title.c_str());
    std::wstring artist = utf8_to_wide(m_state.track_artist.c_str());
    
    g.DrawString(title.c_str(), -1, m_font_title.get(), titleRect, &sf, &titleBrush);
    if (!artist.empty()) {
        g.DrawString(artist.c_str(), -1, m_font_artist.get(), artistRect, &sf, &artistBrush);
    }
}

void ControlPanelCore::draw_playback_buttons(Gdiplus::Graphics& g) {
    // Heart button (mood toggle) - only if visible
    if (get_nowbar_mood_icon_visible()) {
        bool heart_hovered = (m_hover_region == HitRegion::HeartButton);
        int hw = m_rect_heart.right - m_rect_heart.left;
        int hh = m_rect_heart.bottom - m_rect_heart.top;
        if (heart_hovered) {
            Gdiplus::SolidBrush hoverBrush(m_button_hover_color);
            g.FillEllipse(&hoverBrush, m_rect_heart.left, m_rect_heart.top, hw, hh);
        }
        // Red when mood is set, gray when empty
        Gdiplus::Color heartColor = m_mood_active ? Gdiplus::Color(255, 239, 83, 80) : m_text_secondary_color;
        int heart_inset = hw * 15 / 100;
        RECT heartIconRect = { m_rect_heart.left + heart_inset, m_rect_heart.top + heart_inset,
                                m_rect_heart.right - heart_inset, m_rect_heart.bottom - heart_inset };
        draw_heart_icon(g, heartIconRect, heartColor);
    }

    // Shuffle button
    bool shuffle_active = (m_state.playback_order == 4);  // Shuffle tracks
    bool shuffle_hovered = (m_hover_region == HitRegion::ShuffleButton);
    
    int sw = m_rect_shuffle.right - m_rect_shuffle.left;
    int sh = m_rect_shuffle.bottom - m_rect_shuffle.top;
    if (shuffle_hovered) {
        Gdiplus::SolidBrush hoverBrush(m_button_hover_color);
        g.FillEllipse(&hoverBrush, m_rect_shuffle.left, m_rect_shuffle.top, sw, sh);
    }
    Gdiplus::Color shuffleColor = shuffle_active ? m_accent_color : m_text_secondary_color;
    // Make icon 15% smaller - create a centered, smaller rect
    int icon_inset = sw * 15 / 100;  // 15% inset on each side
    RECT shuffleIconRect = { m_rect_shuffle.left + icon_inset, m_rect_shuffle.top + icon_inset, 
                              m_rect_shuffle.right - icon_inset, m_rect_shuffle.bottom - icon_inset };
    draw_shuffle_icon(g, shuffleIconRect, shuffleColor);
    
    // Previous button
    bool prev_hovered = (m_hover_region == HitRegion::PrevButton);
    int pw = m_rect_prev.right - m_rect_prev.left;
    int ph = m_rect_prev.bottom - m_rect_prev.top;
    if (prev_hovered) {
        Gdiplus::SolidBrush hoverBrush(m_button_hover_color);
        g.FillEllipse(&hoverBrush, m_rect_prev.left, m_rect_prev.top, pw, ph);
    }
    draw_prev_icon(g, m_rect_prev, m_text_secondary_color);  // Always grayed
    
    // Play/Pause button (circular with background)
    bool play_hovered = (m_hover_region == HitRegion::PlayButton);
    int play_w = m_rect_play.right - m_rect_play.left;
    int play_h = m_rect_play.bottom - m_rect_play.top;
    
    // White/light background circle
    Gdiplus::Color bgColor = play_hovered ? Gdiplus::Color(255, 255, 255, 255) : Gdiplus::Color(255, 230, 230, 230);
    Gdiplus::SolidBrush bgBrush(bgColor);
    g.FillEllipse(&bgBrush, m_rect_play.left, m_rect_play.top, play_w, play_h);
    
    // Draw play or pause icon (black on white background)
    Gdiplus::Color playIconColor(255, 0, 0, 0);
    if (m_state.is_playing && !m_state.is_paused) {
        draw_pause_icon(g, m_rect_play, playIconColor, false);
    } else {
        draw_play_icon(g, m_rect_play, playIconColor, false);
    }
    
    // Next button
    bool next_hovered = (m_hover_region == HitRegion::NextButton);
    int nw = m_rect_next.right - m_rect_next.left;
    int nh = m_rect_next.bottom - m_rect_next.top;
    if (next_hovered) {
        Gdiplus::SolidBrush hoverBrush(m_button_hover_color);
        g.FillEllipse(&hoverBrush, m_rect_next.left, m_rect_next.top, nw, nh);
    }
    draw_next_icon(g, m_rect_next, m_text_secondary_color);  // Always grayed
    
    // Repeat button
    bool repeat_active = (m_state.playback_order == 1 || m_state.playback_order == 2);
    bool repeat_one = (m_state.playback_order == 2);
    bool repeat_hovered = (m_hover_region == HitRegion::RepeatButton);
    
    int rw = m_rect_repeat.right - m_rect_repeat.left;
    int rh = m_rect_repeat.bottom - m_rect_repeat.top;
    if (repeat_hovered) {
        Gdiplus::SolidBrush hoverBrush(m_button_hover_color);
        g.FillEllipse(&hoverBrush, m_rect_repeat.left, m_rect_repeat.top, rw, rh);
    }
    Gdiplus::Color repeatColor = repeat_active ? m_accent_color : m_text_secondary_color;
    // Make icon 15% smaller - create a centered, smaller rect
    int repeat_inset = rw * 15 / 100;  // 15% inset
    RECT repeatIconRect = { m_rect_repeat.left + repeat_inset, m_rect_repeat.top + repeat_inset,
                             m_rect_repeat.right - repeat_inset, m_rect_repeat.bottom - repeat_inset };
    draw_repeat_icon(g, repeatIconRect, repeatColor, repeat_one);
}

void ControlPanelCore::draw_button(Gdiplus::Graphics& g, const RECT& rect, const wchar_t* icon, bool hovered, bool active) {
    int w = rect.right - rect.left;
    int h = rect.bottom - rect.top;
    
    // Hover background
    if (hovered) {
        Gdiplus::SolidBrush hoverBrush(m_button_hover_color);
        g.FillEllipse(&hoverBrush, rect.left, rect.top, w, h);
    }
    
    // Icon
    Gdiplus::Color iconColor = active ? m_accent_color : m_text_color;
    Gdiplus::SolidBrush iconBrush(iconColor);
    Gdiplus::Font iconFont(L"Segoe MDL2 Assets", 14.0f * m_dpi_scale, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    
    Gdiplus::StringFormat sf;
    sf.SetAlignment(Gdiplus::StringAlignmentCenter);
    sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    
    Gdiplus::RectF btnRect((float)rect.left, (float)rect.top, (float)w, (float)h);
    g.DrawString(icon, -1, &iconFont, btnRect, &sf, &iconBrush);
}

void ControlPanelCore::draw_circular_button(Gdiplus::Graphics& g, const RECT& rect, const wchar_t* icon, bool hovered, bool pressed) {
    int w = rect.right - rect.left;
    int h = rect.bottom - rect.top;
    
    // Background circle
    Gdiplus::Color bgColor = hovered ? Gdiplus::Color(255, 255, 255, 255) : Gdiplus::Color(255, 230, 230, 230);
    Gdiplus::SolidBrush bgBrush(bgColor);
    g.FillEllipse(&bgBrush, rect.left, rect.top, w, h);
    
    // Icon (black on white background)
    Gdiplus::SolidBrush iconBrush(Gdiplus::Color(255, 0, 0, 0));
    Gdiplus::Font iconFont(L"Segoe MDL2 Assets", 18.0f * m_dpi_scale, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    
    Gdiplus::StringFormat sf;
    sf.SetAlignment(Gdiplus::StringAlignmentCenter);
    sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    
    Gdiplus::RectF btnRect((float)rect.left, (float)rect.top, (float)w, (float)h);
    g.DrawString(icon, -1, &iconFont, btnRect, &sf, &iconBrush);
}

void ControlPanelCore::draw_seekbar(Gdiplus::Graphics& g) {
    int w = m_rect_seekbar.right - m_rect_seekbar.left;
    int h = m_rect_seekbar.bottom - m_rect_seekbar.top;

    // Use scaled seekbar_height
    int track_h = static_cast<int>(m_metrics.seekbar_height * m_size_scale);
    int track_y = m_rect_seekbar.top + (h - track_h) / 2;
    int radius = track_h / 2;  // For rounded ends
    bool is_pill = (get_nowbar_bar_style() == 0);  // 0=Pill-shaped, 1=Rectangular

    // Background track - dark gray
    Gdiplus::SolidBrush trackBrush(Gdiplus::Color(255, 60, 60, 60));
    if (is_pill) {
        Gdiplus::GraphicsPath trackPath;
        int r = std::min(radius, w / 2);
        trackPath.AddArc(m_rect_seekbar.left, track_y, r * 2, track_h, 90, 180);
        trackPath.AddArc(m_rect_seekbar.left + w - r * 2, track_y, r * 2, track_h, 270, 180);
        trackPath.CloseFigure();
        g.FillPath(&trackBrush, &trackPath);
    } else {
        g.FillRectangle(&trackBrush, m_rect_seekbar.left, track_y, w, track_h);
    }

    // Progress - light gray
    double progress = (m_state.track_length > 0) ? (m_state.playback_time / m_state.track_length) : 0.0;
    int progress_w = static_cast<int>(w * progress);

    if (progress_w > 0) {
        Gdiplus::SolidBrush progressBrush(Gdiplus::Color(255, 140, 140, 140));  // Darker gray
        if (is_pill && progress_w > radius * 2) {
            Gdiplus::GraphicsPath progressPath;
            int r = std::min(radius, progress_w / 2);
            progressPath.AddArc(m_rect_seekbar.left, track_y, r * 2, track_h, 90, 180);
            progressPath.AddArc(m_rect_seekbar.left + progress_w - r * 2, track_y, r * 2, track_h, 270, 180);
            progressPath.CloseFigure();
            g.FillPath(&progressBrush, &progressPath);
        } else if (is_pill) {
            // Too small for pill, just draw circle
            g.FillEllipse(&progressBrush, m_rect_seekbar.left, track_y, track_h, track_h);
        } else {
            g.FillRectangle(&progressBrush, m_rect_seekbar.left, track_y, progress_w, track_h);
        }
    }

    // Seek handle (only on hover)
    if (m_hover_region == HitRegion::SeekBar || m_seeking) {
        int handle_size = static_cast<int>(14 * m_dpi_scale * m_size_scale);
        int handle_x = m_rect_seekbar.left + progress_w - handle_size / 2;
        int handle_y = m_rect_seekbar.top + (h - handle_size) / 2;
        Gdiplus::SolidBrush handleBrush(Gdiplus::Color(255, 255, 255, 255));
        g.FillEllipse(&handleBrush, handle_x, handle_y, handle_size, handle_size);
    }
}

void ControlPanelCore::draw_time_display(Gdiplus::Graphics& g) {
    Gdiplus::SolidBrush timeBrush(m_text_secondary_color);
    
    std::wstring elapsed = format_time(m_state.playback_time);
    // Show time remaining with minus sign
    double remaining = m_state.track_length - m_state.playback_time;
    if (remaining < 0) remaining = 0;
    std::wstring remaining_str = L"-" + format_time(remaining);
    
    Gdiplus::StringFormat sfLeft;
    sfLeft.SetAlignment(Gdiplus::StringAlignmentNear);
    sfLeft.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    
    Gdiplus::StringFormat sfRight;
    sfRight.SetAlignment(Gdiplus::StringAlignmentFar);
    sfRight.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    
    // Create a scaled time font based on current size scale
    float time_font_size = 9.0f * m_dpi_scale * m_size_scale;
    Gdiplus::FontFamily fontFamily(L"Microsoft YaHei");
    Gdiplus::Font timeFont(&fontFamily, time_font_size, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
    
    // Position time at the ends of the seekbar (vertically centered with seekbar)
    int seekbar_center_y = (m_rect_seekbar.top + m_rect_seekbar.bottom) / 2;
    int time_height = static_cast<int>(m_metrics.text_height * m_size_scale);
    
    // Scale the time offset by size scale
    float time_offset = 60 * m_dpi_scale * m_size_scale;
    float time_width = 55 * m_dpi_scale * m_size_scale;
    
    // Left side: elapsed time (before seekbar)
    Gdiplus::RectF leftTimeRect(
        (float)(m_rect_seekbar.left - time_offset), 
        (float)(seekbar_center_y - time_height / 2),
        time_width,
        (float)time_height
    );
    g.DrawString(elapsed.c_str(), -1, &timeFont, leftTimeRect, &sfRight, &timeBrush);
    
    // Right side: remaining time (after seekbar)
    Gdiplus::RectF rightTimeRect(
        (float)(m_rect_seekbar.right + 5 * m_dpi_scale * m_size_scale),
        (float)(seekbar_center_y - time_height / 2),
        time_offset,
        (float)time_height
    );
    g.DrawString(remaining_str.c_str(), -1, &timeFont, rightTimeRect, &sfLeft, &timeBrush);
}

void ControlPanelCore::draw_volume(Gdiplus::Graphics& g) {
    int w = m_rect_volume.right - m_rect_volume.left;
    int h = m_rect_volume.bottom - m_rect_volume.top;
    
    // Determine volume level based on bar position: 0=mute, 1=low, 2=full
    // Volume bar: -100dB=0%, 0dB=100%
    float level = (m_state.volume_db + 100.0f) / 100.0f;  // 0.0 to 1.0
    if (level < 0) level = 0;
    if (level > 1) level = 1;
    
    int vol_level = 2;  // full (>50%)
    if (level <= 0) vol_level = 0;  // mute (0%)
    else if (level <= 0.5f) vol_level = 1;  // low (0-50%)
    
    // Draw custom volume icon (scaled with panel height) - always grayed
    int icon_size = static_cast<int>(23 * m_dpi_scale * m_size_scale);  // Volume icon (scaled)
    int icon_y = m_rect_volume.top + (h - icon_size) / 2;
    int icon_gap = static_cast<int>(6 * m_dpi_scale * m_size_scale);  // Extra gap to move icon left
    draw_volume_icon(g, m_rect_volume.left - icon_gap, icon_y, icon_size, m_text_secondary_color, vol_level);

    // Volume bar - use same thickness as seekbar
    int bar_offset = icon_size + static_cast<int>(12 * m_dpi_scale * m_size_scale);  // Icon + gap (scaled)
    int bar_x = m_rect_volume.left + bar_offset;
    int bar_w = w - bar_offset;
    int bar_h = static_cast<int>(m_metrics.seekbar_height * m_size_scale);
    int bar_y = m_rect_volume.top + (h - bar_h) / 2;
    int radius = bar_h / 2;
    bool is_pill = (get_nowbar_bar_style() == 0);  // 0=Pill-shaped, 1=Rectangular
    
    // Background
    Gdiplus::SolidBrush trackBrush(Gdiplus::Color(255, 60, 60, 60));  // Same as seekbar track
    if (is_pill) {
        Gdiplus::GraphicsPath trackPath;
        int r = std::min(radius, bar_w / 2);
        trackPath.AddArc(bar_x, bar_y, r * 2, bar_h, 90, 180);
        trackPath.AddArc(bar_x + bar_w - r * 2, bar_y, r * 2, bar_h, 270, 180);
        trackPath.CloseFigure();
        g.FillPath(&trackBrush, &trackPath);
    } else {
        g.FillRectangle(&trackBrush, bar_x, bar_y, bar_w, bar_h);
    }
    
    // Level (convert dB to linear: 0dB = 100%, -100dB = 0%)
    float bar_level = (m_state.volume_db + 100.0f) / 100.0f;
    if (bar_level < 0) bar_level = 0;
    if (bar_level > 1) bar_level = 1;
    int level_w = static_cast<int>(bar_w * bar_level);
    
    if (level_w > 0) {
        Gdiplus::SolidBrush levelBrush(Gdiplus::Color(255, 140, 140, 140));  // Darker gray like seekbar
        if (is_pill && level_w > radius * 2) {
            Gdiplus::GraphicsPath levelPath;
            int r = std::min(radius, level_w / 2);
            levelPath.AddArc(bar_x, bar_y, r * 2, bar_h, 90, 180);
            levelPath.AddArc(bar_x + level_w - r * 2, bar_y, r * 2, bar_h, 270, 180);
            levelPath.CloseFigure();
            g.FillPath(&levelBrush, &levelPath);
        } else if (is_pill) {
            // Too small for pill, just draw circle
            g.FillEllipse(&levelBrush, bar_x, bar_y, bar_h, bar_h);
        } else {
            g.FillRectangle(&levelBrush, bar_x, bar_y, level_w, bar_h);
        }
    }
}

HitRegion ControlPanelCore::hit_test(int x, int y) const {
    if (pt_in_rect(m_rect_play, x, y)) return HitRegion::PlayButton;
    if (pt_in_rect(m_rect_prev, x, y)) return HitRegion::PrevButton;
    if (pt_in_rect(m_rect_next, x, y)) return HitRegion::NextButton;
    if (get_nowbar_mood_icon_visible() && pt_in_rect(m_rect_heart, x, y)) return HitRegion::HeartButton;
    if (pt_in_rect(m_rect_shuffle, x, y)) return HitRegion::ShuffleButton;
    if (pt_in_rect(m_rect_repeat, x, y)) return HitRegion::RepeatButton;
    if (pt_in_rect(m_rect_seekbar, x, y)) return HitRegion::SeekBar;
    // Volume area - check both icon (positioned left of rect) and slider
    int icon_gap = static_cast<int>(6 * m_dpi_scale * m_size_scale);
    int icon_width = static_cast<int>(23 * m_dpi_scale * m_size_scale);  // Match volume icon
    RECT expanded_volume = { m_rect_volume.left - icon_gap, m_rect_volume.top, 
                              m_rect_volume.right, m_rect_volume.bottom };
    if (pt_in_rect(expanded_volume, x, y)) {
        // Check if click is on icon area or slider bar
        if (x < m_rect_volume.left + icon_width - icon_gap) {
            return HitRegion::VolumeIcon;
        }
        return HitRegion::VolumeSlider;
    }
    if (get_nowbar_miniplayer_icon_visible() && pt_in_rect(m_rect_miniplayer, x, y)) return HitRegion::MiniPlayerButton;
    if (pt_in_rect(m_rect_artwork, x, y)) return HitRegion::Artwork;
    if (pt_in_rect(m_rect_track_info, x, y)) return HitRegion::TrackInfo;
    return HitRegion::None;
}

void ControlPanelCore::on_mouse_move(int x, int y) {
    HitRegion new_region = hit_test(x, y);
    
    if (m_seeking) {
        // Calculate seek position
        double pos = static_cast<double>(x - m_rect_seekbar.left) / (m_rect_seekbar.right - m_rect_seekbar.left);
        pos = std::max(0.0, std::min(1.0, pos));
        // Preview position (don't seek yet)
        m_state.playback_time = pos * m_state.track_length;
        invalidate();
        return;
    }
    
    if (m_volume_dragging) {
        int icon_size = static_cast<int>(23 * m_dpi_scale * m_size_scale);  // Match volume icon
        int bar_offset = icon_size + static_cast<int>(12 * m_dpi_scale * m_size_scale);  // Match draw_volume
        int bar_x = m_rect_volume.left + bar_offset;
        int bar_w = (m_rect_volume.right - m_rect_volume.left) - bar_offset;
        double level = static_cast<double>(x - bar_x) / bar_w;
        level = std::max(0.0, std::min(1.0, level));
        float db = static_cast<float>(level * 100.0 - 100.0);
        playback_control::get()->set_volume(db);
        // Update local state for immediate visual feedback
        m_state.volume_db = db;
        invalidate();
        return;
    }
    
    if (new_region != m_hover_region) {
        m_hover_region = new_region;
        invalidate();
    }
}

void ControlPanelCore::on_mouse_leave() {
    if (m_hover_region != HitRegion::None) {
        m_hover_region = HitRegion::None;
        invalidate();
    }
}

void ControlPanelCore::on_lbutton_down(int x, int y) {
    m_pressed_region = hit_test(x, y);
    
    if (m_pressed_region == HitRegion::SeekBar) {
        m_seeking = true;
        on_mouse_move(x, y);
    } else if (m_pressed_region == HitRegion::VolumeSlider) {
        m_volume_dragging = true;
        on_mouse_move(x, y);
    }
}

void ControlPanelCore::on_lbutton_up(int x, int y) {
    HitRegion release_region = hit_test(x, y);
    
    if (m_seeking) {
        m_seeking = false;
        // Commit seek
        double pos = static_cast<double>(x - m_rect_seekbar.left) / (m_rect_seekbar.right - m_rect_seekbar.left);
        pos = std::max(0.0, std::min(1.0, pos));
        do_seek(pos * m_state.track_length);
    } else if (m_volume_dragging) {
        m_volume_dragging = false;
    } else if (release_region == m_pressed_region) {
        switch (release_region) {
        case HitRegion::PlayButton: do_play_pause(); break;
        case HitRegion::PrevButton: do_prev(); break;
        case HitRegion::NextButton: do_next(); break;
        case HitRegion::HeartButton: do_toggle_mood(); break;
        case HitRegion::ShuffleButton: do_shuffle_toggle(); break;
        case HitRegion::RepeatButton: do_repeat_cycle(); break;
        case HitRegion::MiniPlayerButton: {
            // Toggle MiniPlayer active state for icon color
            m_miniplayer_active = !m_miniplayer_active;
            
            // Launch MiniPlayer via mainmenu command
            // Iterate through mainmenu commands to find "Launch MiniPlayer"
            service_enum_t<mainmenu_commands> e;
            mainmenu_commands::ptr ptr;
            while (e.next(ptr)) {
                for (t_uint32 i = 0; i < ptr->get_command_count(); i++) {
                    pfc::string8 name;
                    ptr->get_name(i, name);
                    if (pfc::string_find_first(name, "Launch MiniPlayer") != pfc::infinite_size) {
                        ptr->execute(i, nullptr);
                        break;
                    }
                }
            }
            break;
        }
        case HitRegion::VolumeIcon: {
            // Toggle mute with volume memory
            auto pc = playback_control::get();
            if (m_state.volume_db <= -100.0f) {
                // Currently muted - restore previous volume
                pc->set_volume(m_prev_volume_db);
            } else {
                // Not muted - save current volume and mute
                m_prev_volume_db = m_state.volume_db;
                pc->set_volume(-100.0f);
            }
            break;
        }
        default: break;
        }
    }
    
    m_pressed_region = HitRegion::None;
    invalidate();
}

void ControlPanelCore::on_mouse_wheel(int delta) {
    // Adjust volume with mouse wheel
    auto pc = playback_control::get();
    // delta is typically 120 per notch, positive = scroll up = volume up
    float volume_step = 2.5f;  // dB per scroll notch
    float volume_change = (delta > 0) ? volume_step : -volume_step;
    float new_volume = m_state.volume_db + volume_change;
    // Clamp to valid range (-100 to 0 dB)
    new_volume = std::max(-100.0f, std::min(0.0f, new_volume));
    pc->set_volume(new_volume);
}

void ControlPanelCore::on_lbutton_dblclk(int x, int y) {
    HitRegion region = hit_test(x, y);
    if (region == HitRegion::Artwork) {
        show_picture_viewer();
    }
}

// Picture viewer window class name
static const wchar_t* g_picture_viewer_class = L"foo_nowbar_picture_viewer";
static bool g_picture_viewer_registered = false;
static HWND g_picture_viewer_hwnd = nullptr;
static std::unique_ptr<Gdiplus::Bitmap> g_viewer_bitmap;
static album_art_data_ptr g_viewer_art_data;  // Keep original data for saving
static bool g_viewer_fit_to_window = true;
static bool g_viewer_dark_mode = true;
static HWND g_viewer_dropdown = nullptr;
static HWND g_viewer_save_btn = nullptr;
static const int VIEWER_PANEL_HEIGHT = 36;
static const int IDC_VIEW_MODE = 1001;
static const int IDC_SAVE_BTN = 1002;

// DWM dark mode attribute
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

static void SetViewerDarkMode(HWND hwnd, bool dark) {
    BOOL value = dark ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &value, sizeof(value));
}

static pfc::string8 GetImageTypeFromData(album_art_data_ptr data) {
    if (!data.is_valid() || data->get_size() < 4) return "Unknown";
    const unsigned char* bytes = static_cast<const unsigned char*>(data->data());
    // Check JPEG signature
    if (bytes[0] == 0xFF && bytes[1] == 0xD8 && bytes[2] == 0xFF) return "JPEG";
    // Check PNG signature
    if (bytes[0] == 0x89 && bytes[1] == 0x50 && bytes[2] == 0x4E && bytes[3] == 0x47) return "PNG";
    // Check GIF signature
    if (bytes[0] == 0x47 && bytes[1] == 0x49 && bytes[2] == 0x46) return "GIF";
    // Check BMP signature
    if (bytes[0] == 0x42 && bytes[1] == 0x4D) return "BMP";
    // Check WebP signature
    if (data->get_size() >= 12 && bytes[0] == 0x52 && bytes[1] == 0x49 && bytes[2] == 0x46 && bytes[3] == 0x46 
        && bytes[8] == 0x57 && bytes[9] == 0x45 && bytes[10] == 0x42 && bytes[11] == 0x50) return "WebP";
    return "Unknown";
}

static void SaveArtworkToFile(HWND parent) {
    if (!g_viewer_art_data.is_valid()) return;
    
    pfc::string8 type = GetImageTypeFromData(g_viewer_art_data);
    const wchar_t* filter = L"JPEG Image (*.jpg)\0*.jpg\0PNG Image (*.png)\0*.png\0All Files (*.*)\0*.*\0";
    const wchar_t* defaultExt = L"jpg";
    if (type == "PNG") {
        filter = L"PNG Image (*.png)\0*.png\0JPEG Image (*.jpg)\0*.jpg\0All Files (*.*)\0*.*\0";
        defaultExt = L"png";
    }
    
    wchar_t filename[MAX_PATH] = L"cover";
    
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = parent;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = defaultExt;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    
    if (GetSaveFileNameW(&ofn)) {
        // Save the file
        HANDLE hFile = CreateFileW(filename, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile != INVALID_HANDLE_VALUE) {
            DWORD written;
            WriteFile(hFile, g_viewer_art_data->data(), static_cast<DWORD>(g_viewer_art_data->get_size()), &written, nullptr);
            CloseHandle(hFile);
        }
    }
}

static LRESULT CALLBACK PictureViewerProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        // Create bottom panel controls
        HINSTANCE hInst = core_api::get_my_instance();
        HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        
        g_viewer_dropdown = CreateWindowExW(0, L"COMBOBOX", nullptr,
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
            8, 0, 160, 200, hwnd, (HMENU)IDC_VIEW_MODE, hInst, nullptr);
        SendMessage(g_viewer_dropdown, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(g_viewer_dropdown, CB_ADDSTRING, 0, (LPARAM)L"Fit to window size");
        SendMessageW(g_viewer_dropdown, CB_ADDSTRING, 0, (LPARAM)L"Show in original size");
        SendMessageW(g_viewer_dropdown, CB_SETCURSEL, g_viewer_fit_to_window ? 0 : 1, 0);
        
        g_viewer_save_btn = CreateWindowExW(0, L"BUTTON", L"Save",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 80, 26, hwnd, (HMENU)IDC_SAVE_BTN, hInst, nullptr);
        SendMessage(g_viewer_save_btn, WM_SETFONT, (WPARAM)hFont, TRUE);
        
        return 0;
    }
    
    case WM_SIZE: {
        RECT rect;
        GetClientRect(hwnd, &rect);
        int panelY = rect.bottom - VIEWER_PANEL_HEIGHT;
        
        // Position dropdown on left
        SetWindowPos(g_viewer_dropdown, nullptr, 8, panelY + 6, 160, 200, SWP_NOZORDER);
        
        // Position save button on right
        SetWindowPos(g_viewer_save_btn, nullptr, rect.right - 88, panelY + 5, 80, 26, SWP_NOZORDER);
        
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        
        RECT rect;
        GetClientRect(hwnd, &rect);
        
        // Image area (above panel)
        int imageAreaHeight = rect.bottom - VIEWER_PANEL_HEIGHT;
        
        // Create GDI+ graphics
        Gdiplus::Graphics g(hdc);
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        g.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
        
        // Draw background based on theme
        Gdiplus::Color bgColor = g_viewer_dark_mode ? Gdiplus::Color(255, 24, 24, 24) : Gdiplus::Color(255, 240, 240, 240);
        Gdiplus::Color textColor = g_viewer_dark_mode ? Gdiplus::Color(255, 200, 200, 200) : Gdiplus::Color(255, 60, 60, 60);
        Gdiplus::Color panelColor = g_viewer_dark_mode ? Gdiplus::Color(255, 40, 40, 40) : Gdiplus::Color(255, 220, 220, 220);
        
        g.Clear(bgColor);
        
        if (g_viewer_bitmap) {
            int bw = g_viewer_bitmap->GetWidth();
            int bh = g_viewer_bitmap->GetHeight();
            int cw = rect.right - rect.left;
            int ch = imageAreaHeight;
            
            int dw, dh, dx, dy;
            if (g_viewer_fit_to_window) {
                // Fit to window
                float scale = std::min((float)cw / bw, (float)ch / bh);
                dw = static_cast<int>(bw * scale);
                dh = static_cast<int>(bh * scale);
                dx = (cw - dw) / 2;
                dy = (ch - dh) / 2;
            } else {
                // Original size (centered, may clip)
                dw = bw;
                dh = bh;
                dx = (cw - dw) / 2;
                dy = (ch - dh) / 2;
            }
            
            g.DrawImage(g_viewer_bitmap.get(), dx, dy, dw, dh);
        }
        
        // Draw bottom panel background
        Gdiplus::SolidBrush panelBrush(panelColor);
        g.FillRectangle(&panelBrush, 0, imageAreaHeight, rect.right, VIEWER_PANEL_HEIGHT);
        
        // Draw image info in center
        if (g_viewer_art_data.is_valid() && g_viewer_bitmap) {
            Gdiplus::Font font(L"Segoe UI", 9);
            Gdiplus::SolidBrush textBrush(textColor);
            Gdiplus::StringFormat sf;
            sf.SetAlignment(Gdiplus::StringAlignmentCenter);
            
            // File size
            size_t fileSize = g_viewer_art_data->get_size();
            wchar_t sizeStr[64];
            if (fileSize >= 1024 * 1024) {
                swprintf_s(sizeStr, L"%.2f MB", fileSize / (1024.0 * 1024.0));
            } else {
                swprintf_s(sizeStr, L"%.1f KB", fileSize / 1024.0);
            }
            
            // Resolution and type
            pfc::string8 type = GetImageTypeFromData(g_viewer_art_data);
            wchar_t infoStr[128];
            swprintf_s(infoStr, L"%dx%d %S", g_viewer_bitmap->GetWidth(), g_viewer_bitmap->GetHeight(), type.c_str());
            
            float centerX = rect.right / 2.0f;
            Gdiplus::RectF sizeRect(centerX - 100, (float)(imageAreaHeight + 4), 200, 14);
            Gdiplus::RectF infoRect(centerX - 100, (float)(imageAreaHeight + 18), 200, 14);
            
            g.DrawString(sizeStr, -1, &font, sizeRect, &sf, &textBrush);
            g.DrawString(infoStr, -1, &font, infoRect, &sf, &textBrush);
        }
        
        EndPaint(hwnd, &ps);
        return 0;
    }
    
    case WM_COMMAND:
        if (LOWORD(wp) == IDC_VIEW_MODE && HIWORD(wp) == CBN_SELCHANGE) {
            g_viewer_fit_to_window = (SendMessageW(g_viewer_dropdown, CB_GETCURSEL, 0, 0) == 0);
            InvalidateRect(hwnd, nullptr, FALSE);
        } else if (LOWORD(wp) == IDC_SAVE_BTN) {
            SaveArtworkToFile(hwnd);
        }
        return 0;
    
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
        HDC hdcCtrl = (HDC)wp;
        if (g_viewer_dark_mode) {
            SetBkColor(hdcCtrl, RGB(40, 40, 40));
            SetTextColor(hdcCtrl, RGB(200, 200, 200));
            static HBRUSH darkBrush = CreateSolidBrush(RGB(40, 40, 40));
            return (LRESULT)darkBrush;
        }
        return DefWindowProc(hwnd, msg, wp, lp);
    }
        
    case WM_DESTROY:
        g_picture_viewer_hwnd = nullptr;
        g_viewer_bitmap.reset();
        g_viewer_art_data.release();
        g_viewer_dropdown = nullptr;
        g_viewer_save_btn = nullptr;
        return 0;
    }
    
    return DefWindowProc(hwnd, msg, wp, lp);
}

void ControlPanelCore::show_picture_viewer() {
    if (!m_artwork_bitmap) return;
    
    // Register window class if needed
    if (!g_picture_viewer_registered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.style = 0;
        wc.lpfnWndProc = PictureViewerProc;
        wc.hInstance = core_api::get_my_instance();
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;  // We handle painting
        wc.lpszClassName = g_picture_viewer_class;
        wc.hIcon = LoadIcon(core_api::get_my_instance(), MAKEINTRESOURCE(IDI_PICTURE_VIEWER));
        wc.hIconSm = wc.hIcon;
        
        if (RegisterClassExW(&wc)) {
            g_picture_viewer_registered = true;
        }
    }
    
    if (!g_picture_viewer_registered) return;
    
    // Close existing viewer if open
    if (g_picture_viewer_hwnd) {
        DestroyWindow(g_picture_viewer_hwnd);
        g_picture_viewer_hwnd = nullptr;
    }
    
    // Clone the artwork bitmap for the viewer
    g_viewer_bitmap.reset(m_artwork_bitmap->Clone(0, 0, m_artwork_bitmap->GetWidth(), 
                                                   m_artwork_bitmap->GetHeight(), 
                                                   m_artwork_bitmap->GetPixelFormat()));
    
    // Get current track and artwork data for saving
    auto pc = playback_control::get();
    metadb_handle_ptr track;
    if (pc->get_now_playing(track) && track.is_valid()) {
        try {
            auto art_manager = album_art_manager_v3::get();
            auto extractor = art_manager->open(
                pfc::list_single_ref_t<metadb_handle_ptr>(track),
                pfc::list_single_ref_t<GUID>(album_art_ids::cover_front),
                fb2k::noAbort);
            if (extractor.is_valid()) {
                album_art_data_ptr data;
                if (extractor->query(album_art_ids::cover_front, data, fb2k::noAbort)) {
                    g_viewer_art_data = data;
                }
            }
        } catch (...) {}
    }
    
    // Set dark mode based on current theme
    g_viewer_dark_mode = m_dark_mode;
    
    // Calculate window size based on image size
    int img_width = m_artwork_bitmap->GetWidth();
    int img_height = m_artwork_bitmap->GetHeight();
    
    // Get screen size
    int screen_width = GetSystemMetrics(SM_CXSCREEN);
    int screen_height = GetSystemMetrics(SM_CYSCREEN);
    
    // Scale to fit in 80% of screen while maintaining aspect ratio
    int max_width = screen_width * 80 / 100;
    int max_height = screen_height * 80 / 100;
    
    float scale = std::min((float)max_width / img_width, (float)max_height / img_height);
    scale = std::min(scale, 1.0f);  // Don't upscale
    
    int client_width = static_cast<int>(img_width * scale);
    int client_height = static_cast<int>(img_height * scale) + VIEWER_PANEL_HEIGHT;  // Add panel height
    
    // Minimum size
    client_width = std::max(400, client_width);
    client_height = std::max(350, client_height);
    
    // Adjust for window chrome (title bar, borders)
    RECT win_rect = { 0, 0, client_width, client_height };
    AdjustWindowRectEx(&win_rect, WS_OVERLAPPEDWINDOW & ~(WS_MAXIMIZEBOX | WS_MINIMIZEBOX), FALSE, 0);
    int win_width = win_rect.right - win_rect.left;
    int win_height = win_rect.bottom - win_rect.top;
    
    // Center on screen
    int win_x = (screen_width - win_width) / 2;
    int win_y = (screen_height - win_height) / 2;
    
    // Create the viewer window with standard decorations (title bar, close button, resizable)
    g_picture_viewer_hwnd = CreateWindowExW(
        0,  // No extended styles
        g_picture_viewer_class,
        L"Album Art",
        WS_OVERLAPPEDWINDOW & ~(WS_MAXIMIZEBOX | WS_MINIMIZEBOX),  // Standard window without max/min
        win_x, win_y, win_width, win_height,
        nullptr,
        nullptr,
        core_api::get_my_instance(),
        nullptr
    );
    
    if (g_picture_viewer_hwnd) {
        // Apply dark/light mode to title bar
        SetViewerDarkMode(g_picture_viewer_hwnd, g_viewer_dark_mode);
        
        ShowWindow(g_picture_viewer_hwnd, SW_SHOW);
        SetForegroundWindow(g_picture_viewer_hwnd);
    }
}

void ControlPanelCore::do_play_pause() {
    playback_control::get()->play_or_pause();
}

void ControlPanelCore::do_prev() {
    playback_control::get()->previous();
}

void ControlPanelCore::do_next() {
    playback_control::get()->next();
}

void ControlPanelCore::do_shuffle_toggle() {
    auto pm = playlist_manager::get();
    int order = pm->playback_order_get_active();
    // Toggle between default (0) and shuffle tracks (4)
    pm->playback_order_set_active(order == 4 ? 0 : 4);
    m_state.playback_order = pm->playback_order_get_active();
    invalidate();
}

void ControlPanelCore::do_repeat_cycle() {
    auto pm = playlist_manager::get();
    int order = pm->playback_order_get_active();
    // Cycle: default -> repeat playlist -> repeat track -> default
    int new_order;
    switch (order) {
    case 0: new_order = 1; break;  // default -> repeat playlist
    case 1: new_order = 2; break;  // repeat playlist -> repeat track
    case 2: new_order = 0; break;  // repeat track -> default
    default: new_order = 0; break;
    }
    pm->playback_order_set_active(new_order);
    m_state.playback_order = new_order;
    invalidate();
}

void ControlPanelCore::do_seek(double position) {
    if (m_state.can_seek) {
        playback_control::get()->playback_seek(position);
    }
}

void ControlPanelCore::do_volume_change(float delta) {
    auto pc = playback_control::get();
    if (delta > 0) {
        pc->volume_up();
    } else {
        pc->volume_down();
    }
}

// Mood metadata filter for updating MOOD tag
class MoodMetadataFilter : public file_info_filter {
public:
    MoodMetadataFilter(bool set_mood, const char* value) : m_set_mood(set_mood), m_value(value) {}
    
    bool apply_filter(trackRef track, t_filestats stats, file_info& info) override {
        (void)track; (void)stats;
        if (m_set_mood) {
            info.meta_set("MOOD", m_value.c_str());
        } else {
            info.meta_remove_field("MOOD");
        }
        return true;
    }
private:
    bool m_set_mood;
    pfc::string8 m_value;
};

void ControlPanelCore::do_toggle_mood() {
    auto pc = playback_control::get();
    metadb_handle_ptr track;
    if (!pc->get_now_playing(track) || !track.is_valid()) {
        return;
    }
    
    // Get current date/time
    SYSTEMTIME st;
    GetLocalTime(&st);
    char datetime[64];
    snprintf(datetime, sizeof(datetime), "%04d-%02d-%02d %02d:%02d:%02d",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    
    // Toggle mood state
    bool new_mood = !m_mood_active;
    
    // Create filter
    auto filter = fb2k::service_new<MoodMetadataFilter>(new_mood, datetime);
    
    // Update metadata asynchronously
    metadb_handle_list tracks;
    tracks.add_item(track);
    metadb_io_v2::get()->update_info_async(
        tracks,
        filter,
        core_api::get_main_window(),
        metadb_io_v2::op_flag_partial_info_aware,
        nullptr
    );
    
    m_mood_active = new_mood;
    invalidate();
}

void ControlPanelCore::update_mood_state() {
    auto pc = playback_control::get();
    metadb_handle_ptr track;
    if (!pc->get_now_playing(track) || !track.is_valid()) {
        m_mood_active = false;
        return;
    }
    
    // Check if MOOD tag exists
    metadb_info_container::ptr info_container = track->get_info_ref();
    if (info_container.is_valid()) {
        const file_info& info = info_container->info();
        const char* mood = info.meta_get("MOOD", 0);
        m_mood_active = (mood != nullptr && strlen(mood) > 0);
    } else {
        m_mood_active = false;
    }
}

void ControlPanelCore::on_playback_state_changed(const PlaybackState& state) {
    m_state = state;
    invalidate();
}

void ControlPanelCore::on_playback_time_changed(double time) {
    if (!m_seeking) {
        m_state.playback_time = time;
        invalidate();
    }
}

void ControlPanelCore::on_volume_changed(float volume_db) {
    m_state.volume_db = volume_db;
    if (!m_volume_dragging) {
        invalidate();
    }
}

void ControlPanelCore::on_track_changed() {
    // Update mood state for new track
    update_mood_state();
    
    // Request artwork update from UI wrapper
    if (m_artwork_request_cb) {
        m_artwork_request_cb();
    }
    invalidate();
}

void ControlPanelCore::set_artwork(album_art_data_ptr data) {
    if (!data.is_valid() || data->get_size() == 0) {
        clear_artwork();
        return;
    }
    
    // Create GDI+ bitmap from data
    IStream* stream = nullptr;
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, data->get_size());
    if (hMem) {
        void* pMem = GlobalLock(hMem);
        if (pMem) {
            memcpy(pMem, data->get_ptr(), data->get_size());
            GlobalUnlock(hMem);
            
            if (SUCCEEDED(CreateStreamOnHGlobal(hMem, TRUE, &stream))) {
                m_artwork_bitmap.reset(Gdiplus::Bitmap::FromStream(stream));
                stream->Release();
            }
        }
    }
    
    invalidate();
}

void ControlPanelCore::clear_artwork() {
    m_artwork_bitmap.reset();
    invalidate();
}

void ControlPanelCore::invalidate() {
    if (m_hwnd) {
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

// Helper to convert SVG coordinates (viewBox 0 -960 960 960) to normalized 0-24 space
// SVG Y is inverted: -960 = top (0 in our space), -160 = bottom (20 in our space)
static Gdiplus::PointF svgToNorm(float x, float y) {
    // viewBox: 0 -960 960 960 means x: 0-960, y: -960 to 0
    // Convert to 0-24 range
    float nx = x / 40.0f;  // 960/24 = 40
    float ny = (y + 960) / 40.0f;  // shift y by 960 then scale
    return Gdiplus::PointF(nx, ny);
}

// Draw shuffle icon from Material Design SVG
// Path: M560-160v-80h104L537-367l57-57 126 126v-102h80v240H560Zm-344 0-56-56 504-504H560v-80h240v240h-80v-104L216-160Zm151-377L160-744l56-56 207 207-56 56Z
void ControlPanelCore::draw_shuffle_icon(Gdiplus::Graphics& g, const RECT& rect, const Gdiplus::Color& color) {
    float iconSize = static_cast<float>(std::min(rect.right - rect.left, rect.bottom - rect.top)) * 1.0f;  // Full size
    float cx = (rect.left + rect.right) / 2.0f;
    float cy = (rect.top + rect.bottom) / 2.0f;
    float scale = iconSize / 24.0f;
    
    Gdiplus::Matrix oldMatrix;
    g.GetTransform(&oldMatrix);
    
    Gdiplus::Matrix matrix;
    matrix.Translate(cx - 12 * scale, cy - 12 * scale);
    matrix.Scale(scale, scale);
    g.SetTransform(&matrix);
    
    Gdiplus::SolidBrush brush(color);
    Gdiplus::GraphicsPath path;
    
    // Converted from SVG path - filled shape
    // Part 1: Bottom-right arrow (M560-160v-80h104L537-367l57-57 126 126v-102h80v240H560Z)
    path.StartFigure();
    path.AddLine(svgToNorm(560, -160), svgToNorm(560, -240));  // v-80
    path.AddLine(svgToNorm(560, -240), svgToNorm(664, -240));  // h104
    path.AddLine(svgToNorm(664, -240), svgToNorm(537, -367));  // L537-367
    path.AddLine(svgToNorm(537, -367), svgToNorm(594, -424));  // l57-57
    path.AddLine(svgToNorm(594, -424), svgToNorm(720, -298));  // 126 126
    path.AddLine(svgToNorm(720, -298), svgToNorm(720, -400));  // v-102
    path.AddLine(svgToNorm(720, -400), svgToNorm(800, -400));  // h80
    path.AddLine(svgToNorm(800, -400), svgToNorm(800, -160));  // v240
    path.AddLine(svgToNorm(800, -160), svgToNorm(560, -160));  // H560
    path.CloseFigure();
    
    // Part 2: Top-right arrow (m-344 0-56-56 504-504H560v-80h240v240h-80v-104L216-160Z)
    // Starting from 560-344=216, -160+0=-160
    path.StartFigure();
    path.AddLine(svgToNorm(216, -160), svgToNorm(160, -216));   // -56-56
    path.AddLine(svgToNorm(160, -216), svgToNorm(664, -720));   // 504-504
    path.AddLine(svgToNorm(664, -720), svgToNorm(560, -720));   // H560
    path.AddLine(svgToNorm(560, -720), svgToNorm(560, -800));   // v-80
    path.AddLine(svgToNorm(560, -800), svgToNorm(800, -800));   // h240
    path.AddLine(svgToNorm(800, -800), svgToNorm(800, -560));   // v240
    path.AddLine(svgToNorm(800, -560), svgToNorm(720, -560));   // h-80
    path.AddLine(svgToNorm(720, -560), svgToNorm(720, -664));   // v-104
    path.AddLine(svgToNorm(720, -664), svgToNorm(216, -160));   // L216-160
    path.CloseFigure();
    
    // Part 3: Bottom-left small piece (m151-377L160-744l56-56 207 207-56 56Z)
    // Starting from 216+151=367, -160-377=-537
    path.StartFigure();
    path.AddLine(svgToNorm(367, -537), svgToNorm(160, -744));   // L160-744
    path.AddLine(svgToNorm(160, -744), svgToNorm(216, -800));   // l56-56
    path.AddLine(svgToNorm(216, -800), svgToNorm(423, -593));   // 207 207
    path.AddLine(svgToNorm(423, -593), svgToNorm(367, -537));   // -56 56
    path.CloseFigure();
    
    g.FillPath(&brush, &path);
    g.SetTransform(&oldMatrix);
}

// Draw repeat icon from Material Design SVG  
// Path: M280-80 120-240l160-160 56 58-62 62h406v-160h80v240H274l62 62-56 58Zm-80-440v-240h486l-62-62 56-58 160 160-160 160-56-58 62-62H280v160h-80Z
void ControlPanelCore::draw_repeat_icon(Gdiplus::Graphics& g, const RECT& rect, const Gdiplus::Color& color, bool repeat_one) {
    float iconSize = static_cast<float>(std::min(rect.right - rect.left, rect.bottom - rect.top)) * 1.0f;  // Full size
    float cx = (rect.left + rect.right) / 2.0f;
    float cy = (rect.top + rect.bottom) / 2.0f;
    float scale = iconSize / 24.0f;
    
    Gdiplus::Matrix oldMatrix;
    g.GetTransform(&oldMatrix);
    
    Gdiplus::Matrix matrix;
    matrix.Translate(cx - 12 * scale, cy - 12 * scale);
    matrix.Scale(scale, scale);
    g.SetTransform(&matrix);
    
    Gdiplus::SolidBrush brush(color);
    Gdiplus::GraphicsPath path;
    
    // Part 1: Bottom arrow (M280-80 120-240l160-160 56 58-62 62h406v-160h80v240H274l62 62-56 58Z)
    path.StartFigure();
    path.AddLine(svgToNorm(280, -80), svgToNorm(120, -240));    // 120-240 (relative: -160, -160)
    path.AddLine(svgToNorm(120, -240), svgToNorm(280, -400));   // l160-160
    path.AddLine(svgToNorm(280, -400), svgToNorm(336, -342));   // 56 58
    path.AddLine(svgToNorm(336, -342), svgToNorm(274, -280));   // -62 62
    path.AddLine(svgToNorm(274, -280), svgToNorm(680, -280));   // h406
    path.AddLine(svgToNorm(680, -280), svgToNorm(680, -440));   // v-160
    path.AddLine(svgToNorm(680, -440), svgToNorm(760, -440));   // h80
    path.AddLine(svgToNorm(760, -440), svgToNorm(760, -200));   // v240
    path.AddLine(svgToNorm(760, -200), svgToNorm(274, -200));   // H274
    path.AddLine(svgToNorm(274, -200), svgToNorm(336, -138));   // l62 62
    path.AddLine(svgToNorm(336, -138), svgToNorm(280, -80));    // -56 58
    path.CloseFigure();
    
    // Part 2: Top arrow (m-80-440v-240h486l-62-62 56-58 160 160-160 160-56-58 62-62H280v160h-80Z)
    // Starting from 280-80=200, -80-440=-520
    path.StartFigure();
    path.AddLine(svgToNorm(200, -520), svgToNorm(200, -760));   // v-240
    path.AddLine(svgToNorm(200, -760), svgToNorm(686, -760));   // h486
    path.AddLine(svgToNorm(686, -760), svgToNorm(624, -822));   // l-62-62
    path.AddLine(svgToNorm(624, -822), svgToNorm(680, -880));   // 56-58
    path.AddLine(svgToNorm(680, -880), svgToNorm(840, -720));   // 160 160
    path.AddLine(svgToNorm(840, -720), svgToNorm(680, -560));   // -160 160
    path.AddLine(svgToNorm(680, -560), svgToNorm(624, -618));   // -56-58
    path.AddLine(svgToNorm(624, -618), svgToNorm(686, -680));   // 62-62
    path.AddLine(svgToNorm(686, -680), svgToNorm(280, -680));   // H280
    path.AddLine(svgToNorm(280, -680), svgToNorm(280, -520));   // v160
    path.AddLine(svgToNorm(280, -520), svgToNorm(200, -520));   // h-80
    path.CloseFigure();
    
    g.FillPath(&brush, &path);
    
    // Draw "1" in center if repeat_one
    // SVG: M460-360v-180h-60v-60h120v240h-60Z
    if (repeat_one) {
        Gdiplus::GraphicsPath onePath;
        onePath.StartFigure();
        onePath.AddLine(svgToNorm(460, -360), svgToNorm(460, -540));  // v-180
        onePath.AddLine(svgToNorm(460, -540), svgToNorm(400, -540));  // h-60
        onePath.AddLine(svgToNorm(400, -540), svgToNorm(400, -600));  // v-60
        onePath.AddLine(svgToNorm(400, -600), svgToNorm(520, -600));  // h120
        onePath.AddLine(svgToNorm(520, -600), svgToNorm(520, -360));  // v240
        onePath.AddLine(svgToNorm(520, -360), svgToNorm(460, -360));  // h-60
        onePath.CloseFigure();
        g.FillPath(&brush, &onePath);
    }
    
    g.SetTransform(&oldMatrix);
}

// Draw previous track icon from Material Design SVG
// Path: M220-240v-480h80v480h-80Zm520 0L380-480l360-240v480Zm-80-240Zm0 90v-180l-136 90 136 90Z
void ControlPanelCore::draw_prev_icon(Gdiplus::Graphics& g, const RECT& rect, const Gdiplus::Color& color) {
    float iconSize = static_cast<float>(std::min(rect.right - rect.left, rect.bottom - rect.top)) * 1.0f;  // Full size
    float cx = (rect.left + rect.right) / 2.0f;
    float cy = (rect.top + rect.bottom) / 2.0f;
    float scale = iconSize / 24.0f;
    
    Gdiplus::Matrix oldMatrix;
    g.GetTransform(&oldMatrix);
    
    Gdiplus::Matrix matrix;
    matrix.Translate(cx - 12 * scale, cy - 12 * scale);
    matrix.Scale(scale, scale);
    g.SetTransform(&matrix);
    
    Gdiplus::SolidBrush brush(color);
    Gdiplus::GraphicsPath path;
    
    // Bar on left: M220-240v-480h80v480h-80Z
    path.StartFigure();
    path.AddLine(svgToNorm(220, -240), svgToNorm(220, -720));
    path.AddLine(svgToNorm(220, -720), svgToNorm(300, -720));
    path.AddLine(svgToNorm(300, -720), svgToNorm(300, -240));
    path.AddLine(svgToNorm(300, -240), svgToNorm(220, -240));
    path.CloseFigure();
    
    // Triangle: m520 0L380-480l360-240v480Z -> starting at 220+520=740, -240+0=-240
    path.StartFigure();
    path.AddLine(svgToNorm(740, -240), svgToNorm(380, -480));
    path.AddLine(svgToNorm(380, -480), svgToNorm(740, -720));
    path.AddLine(svgToNorm(740, -720), svgToNorm(740, -240));
    path.CloseFigure();
    
    g.FillPath(&brush, &path);
    g.SetTransform(&oldMatrix);
}

// Draw next track icon from Material Design SVG
// Path: M660-240v-480h80v480h-80Zm-440 0v-480l360 240-360 240Zm80-240Zm0 90 136-90-136-90v180Z
void ControlPanelCore::draw_next_icon(Gdiplus::Graphics& g, const RECT& rect, const Gdiplus::Color& color) {
    float iconSize = static_cast<float>(std::min(rect.right - rect.left, rect.bottom - rect.top)) * 1.0f;  // Full size
    float cx = (rect.left + rect.right) / 2.0f;
    float cy = (rect.top + rect.bottom) / 2.0f;
    float scale = iconSize / 24.0f;
    
    Gdiplus::Matrix oldMatrix;
    g.GetTransform(&oldMatrix);
    
    Gdiplus::Matrix matrix;
    matrix.Translate(cx - 12 * scale, cy - 12 * scale);
    matrix.Scale(scale, scale);
    g.SetTransform(&matrix);
    
    Gdiplus::SolidBrush brush(color);
    Gdiplus::GraphicsPath path;
    
    // Bar on right: M660-240v-480h80v480h-80Z
    path.StartFigure();
    path.AddLine(svgToNorm(660, -240), svgToNorm(660, -720));
    path.AddLine(svgToNorm(660, -720), svgToNorm(740, -720));
    path.AddLine(svgToNorm(740, -720), svgToNorm(740, -240));
    path.AddLine(svgToNorm(740, -240), svgToNorm(660, -240));
    path.CloseFigure();
    
    // Triangle: m-440 0v-480l360 240-360 240Z -> starting at 660-440=220, -240
    path.StartFigure();
    path.AddLine(svgToNorm(220, -240), svgToNorm(220, -720));
    path.AddLine(svgToNorm(220, -720), svgToNorm(580, -480));
    path.AddLine(svgToNorm(580, -480), svgToNorm(220, -240));
    path.CloseFigure();
    
    g.FillPath(&brush, &path);
    g.SetTransform(&oldMatrix);
}

// Draw heart icon from Material Design SVG (favorite_24dp.svg)
// Path uses viewBox 0 -960 960 960 (same as other icons)
void ControlPanelCore::draw_heart_icon(Gdiplus::Graphics& g, const RECT& rect, const Gdiplus::Color& color) {
    float iconSize = static_cast<float>(std::min(rect.right - rect.left, rect.bottom - rect.top)) * 1.0f;
    float cx = (rect.left + rect.right) / 2.0f;
    float cy = (rect.top + rect.bottom) / 2.0f;
    float scale = iconSize / 24.0f;
    
    Gdiplus::Matrix oldMatrix;
    g.GetTransform(&oldMatrix);
    
    Gdiplus::Matrix matrix;
    matrix.Translate(cx - 12 * scale, cy - 12 * scale);
    matrix.Scale(scale, scale);
    g.SetTransform(&matrix);
    
    Gdiplus::SolidBrush brush(color);
    Gdiplus::GraphicsPath path;
    
    // Heart shape from Material Design favorite icon
    // Simplified solid heart - two rounded lobes meeting at bottom point
    // Based on viewBox 0 -960 960 960, center at 480
    path.StartFigure();
    
    // Bottom point of heart
    float bottomX = 480, bottomY = -120;
    // Left lobe center
    float leftLobeX = 300, leftLobeY = -634;
    // Right lobe center  
    float rightLobeX = 660, rightLobeY = -634;
    // Top indent
    float topIndentX = 480, topIndentY = -500;
    
    // Draw heart using beziers - left side
    path.AddBezier(
        svgToNorm(bottomX, bottomY),           // Bottom point
        svgToNorm(200, -300),                   // Control 1
        svgToNorm(80, -500),                    // Control 2
        svgToNorm(80, leftLobeY)                // Left side
    );
    path.AddBezier(
        svgToNorm(80, leftLobeY),
        svgToNorm(80, -780),                    // Top left control
        svgToNorm(200, -854),                   // Top control
        svgToNorm(leftLobeX, -854)              // Top of left lobe
    );
    path.AddBezier(
        svgToNorm(leftLobeX, -854),
        svgToNorm(400, -854),                   // Control toward center
        svgToNorm(480, -800),                   // Center top control
        svgToNorm(topIndentX, -720)             // Top indent
    );
    // Right side (mirror)
    path.AddBezier(
        svgToNorm(topIndentX, -720),
        svgToNorm(480, -800),
        svgToNorm(560, -854),
        svgToNorm(rightLobeX, -854)             // Top of right lobe
    );
    path.AddBezier(
        svgToNorm(rightLobeX, -854),
        svgToNorm(760, -854),
        svgToNorm(880, -780),
        svgToNorm(880, rightLobeY)              // Right side
    );
    path.AddBezier(
        svgToNorm(880, rightLobeY),
        svgToNorm(880, -500),
        svgToNorm(760, -300),
        svgToNorm(bottomX, bottomY)             // Back to bottom
    );
    path.CloseFigure();
    
    g.FillPath(&brush, &path);
    g.SetTransform(&oldMatrix);
}

// Draw play icon from Material Design SVG (without circle - just the triangle)
// Triangle: m380-300 280-180-280-180v360Z
void ControlPanelCore::draw_play_icon(Gdiplus::Graphics& g, const RECT& rect, const Gdiplus::Color& color, bool with_circle) {
    float iconSize = static_cast<float>(std::min(rect.right - rect.left, rect.bottom - rect.top)) * 1.0f;  // Full size
    float cx = (rect.left + rect.right) / 2.0f;
    float cy = (rect.top + rect.bottom) / 2.0f;
    float scale = iconSize / 24.0f;
    
    Gdiplus::Matrix oldMatrix;
    g.GetTransform(&oldMatrix);
    
    Gdiplus::Matrix matrix;
    matrix.Translate(cx - 12 * scale, cy - 12 * scale);
    matrix.Scale(scale, scale);
    g.SetTransform(&matrix);
    
    Gdiplus::SolidBrush brush(color);
    Gdiplus::GraphicsPath path;
    
    // Play triangle centered in 24x24 space (scaled from 960 viewbox)
    // Original: m380-300 280-180-280-180v360Z at SVG scale
    // At normalized 24 scale: roughly center triangle
    path.StartFigure();
    path.AddLine(svgToNorm(380, -300), svgToNorm(660, -480));  // right point
    path.AddLine(svgToNorm(660, -480), svgToNorm(380, -660));  // top
    path.AddLine(svgToNorm(380, -660), svgToNorm(380, -300));  // back down
    path.CloseFigure();
    
    g.FillPath(&brush, &path);
    g.SetTransform(&oldMatrix);
}

// Draw pause icon from Material Design SVG (without circle - just the bars)
// Bars: M360-320h80v-320h-80v320Zm160 0h80v-320h-80v320Z
void ControlPanelCore::draw_pause_icon(Gdiplus::Graphics& g, const RECT& rect, const Gdiplus::Color& color, bool with_circle) {
    float iconSize = static_cast<float>(std::min(rect.right - rect.left, rect.bottom - rect.top)) * 1.0f;  // Full size
    float cx = (rect.left + rect.right) / 2.0f;
    float cy = (rect.top + rect.bottom) / 2.0f;
    float scale = iconSize / 24.0f;
    
    Gdiplus::Matrix oldMatrix;
    g.GetTransform(&oldMatrix);
    
    Gdiplus::Matrix matrix;
    matrix.Translate(cx - 12 * scale, cy - 12 * scale);
    matrix.Scale(scale, scale);
    g.SetTransform(&matrix);
    
    Gdiplus::SolidBrush brush(color);
    Gdiplus::GraphicsPath path;
    
    // Left bar
    path.StartFigure();
    path.AddLine(svgToNorm(360, -320), svgToNorm(440, -320));
    path.AddLine(svgToNorm(440, -320), svgToNorm(440, -640));
    path.AddLine(svgToNorm(440, -640), svgToNorm(360, -640));
    path.AddLine(svgToNorm(360, -640), svgToNorm(360, -320));
    path.CloseFigure();
    
    // Right bar
    path.StartFigure();
    path.AddLine(svgToNorm(520, -320), svgToNorm(600, -320));
    path.AddLine(svgToNorm(600, -320), svgToNorm(600, -640));
    path.AddLine(svgToNorm(600, -640), svgToNorm(520, -640));
    path.AddLine(svgToNorm(520, -640), svgToNorm(520, -320));
    path.CloseFigure();
    
    g.FillPath(&brush, &path);
    g.SetTransform(&oldMatrix);
}

// Draw MiniPlayer (Picture-in-Picture) icon from Material Design SVG
// Path: M80-520v-80h144L52-772l56-56 172 172v-144h80v280H80Zm80 360q-33 0-56.5-23.5T80-240v-200h80v200h320v80H160Zm640-280v-280H440v-80h360q33 0 56.5 23.5T880-720v280h-80ZM560-160v-200h320v200H560Z
void ControlPanelCore::draw_miniplayer_icon(Gdiplus::Graphics& g, const RECT& rect, const Gdiplus::Color& color) {
    float iconSize = static_cast<float>(std::min(rect.right - rect.left, rect.bottom - rect.top)) * 0.9f;  // 90% size
    float cx = (rect.left + rect.right) / 2.0f;
    float cy = (rect.top + rect.bottom) / 2.0f;
    float scale = iconSize / 24.0f;
    
    Gdiplus::Matrix oldMatrix;
    g.GetTransform(&oldMatrix);
    
    Gdiplus::Matrix matrix;
    matrix.Translate(cx - 12 * scale, cy - 12 * scale);
    matrix.Scale(scale, scale);
    g.SetTransform(&matrix);
    
    Gdiplus::SolidBrush brush(color);
    Gdiplus::GraphicsPath path;
    
    // Top-left arrow part: M80-520v-80h144L52-772l56-56 172 172v-144h80v280H80Z
    path.StartFigure();
    path.AddLine(svgToNorm(80, -520), svgToNorm(80, -600));
    path.AddLine(svgToNorm(80, -600), svgToNorm(224, -600));
    path.AddLine(svgToNorm(224, -600), svgToNorm(52, -772));
    path.AddLine(svgToNorm(52, -772), svgToNorm(108, -828));
    path.AddLine(svgToNorm(108, -828), svgToNorm(280, -656));
    path.AddLine(svgToNorm(280, -656), svgToNorm(280, -800));
    path.AddLine(svgToNorm(280, -800), svgToNorm(360, -800));
    path.AddLine(svgToNorm(360, -800), svgToNorm(360, -520));
    path.AddLine(svgToNorm(360, -520), svgToNorm(80, -520));
    path.CloseFigure();
    
    // Bottom-left outline: m80 360 -> starting at 80+80=160, -520+360=-160 (but using absolute from SVG)
    // M160-160 q-33 0-56.5-23.5T80-240v-200h80v200h320v80H160Z
    // Simplified: rect at bottom-left
    path.StartFigure();
    path.AddLine(svgToNorm(80, -240), svgToNorm(80, -440));
    path.AddLine(svgToNorm(80, -440), svgToNorm(160, -440));
    path.AddLine(svgToNorm(160, -440), svgToNorm(160, -240));
    path.AddLine(svgToNorm(160, -240), svgToNorm(480, -240));
    path.AddLine(svgToNorm(480, -240), svgToNorm(480, -160));
    path.AddLine(svgToNorm(480, -160), svgToNorm(160, -160));
    path.AddLine(svgToNorm(160, -160), svgToNorm(80, -240));
    path.CloseFigure();
    
    // Top-right outline: M800-440v-280H440v-80h360q33...
    // Simplified: rect at top-right
    path.StartFigure();
    path.AddLine(svgToNorm(440, -720), svgToNorm(440, -800));
    path.AddLine(svgToNorm(440, -800), svgToNorm(800, -800));
    path.AddLine(svgToNorm(800, -800), svgToNorm(880, -720));
    path.AddLine(svgToNorm(880, -720), svgToNorm(880, -440));
    path.AddLine(svgToNorm(880, -440), svgToNorm(800, -440));
    path.AddLine(svgToNorm(800, -440), svgToNorm(800, -720));
    path.AddLine(svgToNorm(800, -720), svgToNorm(440, -720));
    path.CloseFigure();
    
    // Bottom-right filled rectangle: M560-160v-200h320v200H560Z
    path.StartFigure();
    path.AddLine(svgToNorm(560, -160), svgToNorm(560, -360));
    path.AddLine(svgToNorm(560, -360), svgToNorm(880, -360));
    path.AddLine(svgToNorm(880, -360), svgToNorm(880, -160));
    path.AddLine(svgToNorm(880, -160), svgToNorm(560, -160));
    path.CloseFigure();
    
    g.FillPath(&brush, &path);
    g.SetTransform(&oldMatrix);
}

// Draw volume icon from Material Design SVGs
// level: 0=mute, 1=low, 2=full
void ControlPanelCore::draw_volume_icon(Gdiplus::Graphics& g, int x, int y, int size, const Gdiplus::Color& color, int level) {
    float scale = static_cast<float>(size) / 24.0f;
    
    Gdiplus::SolidBrush brush(color);
    
    // Draw speaker body (common to all)
    // Speaker rectangle: left part
    Gdiplus::PointF speaker[6];
    speaker[0] = Gdiplus::PointF(x + 3 * scale, y + 8 * scale);
    speaker[1] = Gdiplus::PointF(x + 8 * scale, y + 8 * scale);
    speaker[2] = Gdiplus::PointF(x + 14 * scale, y + 2 * scale);
    speaker[3] = Gdiplus::PointF(x + 14 * scale, y + 22 * scale);
    speaker[4] = Gdiplus::PointF(x + 8 * scale, y + 16 * scale);
    speaker[5] = Gdiplus::PointF(x + 3 * scale, y + 16 * scale);
    g.FillPolygon(&brush, speaker, 6);
    
    if (level == 0) {
        // Mute: draw X
        Gdiplus::Pen pen(color, 2.0f * scale);
        g.DrawLine(&pen, x + 16 * scale, y + 8 * scale, x + 22 * scale, y + 16 * scale);
        g.DrawLine(&pen, x + 22 * scale, y + 8 * scale, x + 16 * scale, y + 16 * scale);
    } else if (level == 1) {
        // Low: draw single arc wave
        Gdiplus::Pen pen(color, 2.0f * scale);
        g.DrawArc(&pen, x + 14 * scale, y + 7 * scale, 6 * scale, 10 * scale, -60, 120);
    } else {
        // Full: draw double arc waves
        Gdiplus::Pen pen(color, 2.0f * scale);
        g.DrawArc(&pen, x + 14 * scale, y + 7 * scale, 6 * scale, 10 * scale, -60, 120);
        g.DrawArc(&pen, x + 14 * scale, y + 3 * scale, 10 * scale, 18 * scale, -50, 100);
    }
}

} // namespace nowbar

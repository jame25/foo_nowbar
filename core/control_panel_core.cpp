#include "pch.h"
#include "control_panel_core.h"
#include "../artwork_bridge.h"
#include "../preferences.h"
#include "../nowbar_color_service.h"
#include "../resource.h"
#include <algorithm>
#include <commdlg.h>
#include <memory>
#include <mutex>
#include <fstream>
#include <shellapi.h>
#include <shlobj.h>
#include <string>
#include <vector>
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "msimg32.lib")

// === Spectrum hotspot gain toggle ===
// Enable to restore the synthetic wandering-hotspot gain that boosts
// neighboring bars together (legacy "plateaued curve" look). Off by default.
// #define ENABLE_SPECTRUM_HOTSPOT_GAIN

// === SDFT transform toggle ===
// When defined, spectrum uses per-bar Sliding DFT (SDFT) resonators instead
// of the foobar2000 FFT. Each bar has an independent complex resonator at
// its center frequency, so there is no FFT bin collision and bars move
// independently. Uses get_chunk_absolute (raw PCM) instead of
// get_spectrum_absolute (FFT).
#define USE_SDFT_TRANSFORM

namespace nowbar {

HBITMAP create_argb_dib_section(HDC hdc, int w, int h) {
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;  // Top-down DIB
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* pvBits = nullptr;
    return CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &pvBits, nullptr, 0);
}

} // namespace nowbar

// Resolve U+XXXX notation to actual UTF-8 character.
// If the input matches "U+XXXX" (4-6 hex digits), returns the UTF-8 encoding
// of that codepoint. Otherwise returns the input unchanged.
static pfc::string8 resolve_unicode_notation(const pfc::string8& input) {
    const char* s = input.c_str();
    if (s[0] == 'U' && s[1] == '+') {
        const char* hex_start = s + 2;
        size_t hex_len = strlen(hex_start);
        if (hex_len >= 4 && hex_len <= 6) {
            char* end = nullptr;
            unsigned long cp = strtoul(hex_start, &end, 16);
            if (end == hex_start + hex_len && cp <= 0x10FFFF) {
                // Encode codepoint as UTF-8
                wchar_t wbuf[3] = {};
                int wlen = 0;
                if (cp <= 0xFFFF) {
                    wbuf[0] = (wchar_t)cp;
                    wlen = 1;
                } else {
                    // Supplementary plane: encode as surrogate pair
                    cp -= 0x10000;
                    wbuf[0] = (wchar_t)(0xD800 + (cp >> 10));
                    wbuf[1] = (wchar_t)(0xDC00 + (cp & 0x3FF));
                    wlen = 2;
                }
                pfc::string8 result;
                result = pfc::stringcvt::string_utf8_from_wide(wbuf, wlen);
                return result;
            }
        }
    }
    return input;
}

// Title formatting text filter for URL execution:
// Encodes metadata values (& to %26, spaces to %20, and other unsafe URL characters)
// while preserving static URL structure and query parameter delimiters.
class url_titleformat_text_filter : public titleformat_text_filter {
public:
    void write(const GUID & p_inputType, pfc::string_receiver & p_out, const char * p_data, t_size p_dataLength) override {
        p_dataLength = pfc::strlen_max(p_data, p_dataLength);
        if (p_inputType == titleformat_inputtypes::meta) {
            for (t_size i = 0; i < p_dataLength; ++i) {
                unsigned char c = static_cast<unsigned char>(p_data[i]);
                if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                    c == '-' || c == '_' || c == '.' || c == '~') {
                    p_out.add_byte(static_cast<char>(c));
                } else if (c == ' ') {
                    p_out.add_string("%20", 3);
                } else {
                    static const char hex_chars[] = "0123456789ABCDEF";
                    p_out.add_byte('%');
                    p_out.add_byte(hex_chars[(c >> 4) & 0x0F]);
                    p_out.add_byte(hex_chars[c & 0x0F]);
                }
            }
        } else {
            for (t_size i = 0; i < p_dataLength; ++i) {
                unsigned char c = static_cast<unsigned char>(p_data[i]);
                if (c == ' ') {
                    p_out.add_string("%20", 3);
                } else {
                    p_out.add_byte(static_cast<char>(c));
                }
            }
        }
    }
};

namespace nowbar {

// Static instance registry for theme/settings change notifications
static std::vector<ControlPanelCore *> g_instances;
static std::mutex g_instances_mutex;
static bool g_shutdown = false;  // Prevents access to statics during shutdown

// Forward declare to allow use before full definition
class theme_change_callback;

// Global instance - will be created when component loads
static std::unique_ptr<theme_change_callback> g_theme_callback;

// ui_config_callback to detect theme changes - uses ui_config_callback_impl for
// auto-registration
class theme_change_callback : public ui_config_callback_impl {
public:
  void ui_colors_changed() override {
    ControlPanelCore::notify_theme_changed();
  }
};

void ControlPanelCore::register_instance(ControlPanelCore *instance) {
  std::lock_guard<std::mutex> lock(g_instances_mutex);
  // Create theme callback on first registration
  if (!g_theme_callback && core_api::are_services_available()) {
    g_theme_callback = std::make_unique<theme_change_callback>();
  }
  g_instances.push_back(instance);
}

void ControlPanelCore::unregister_instance(ControlPanelCore *instance) {
  if (g_shutdown) return;  // Static mutex may be destroyed
  std::lock_guard<std::mutex> lock(g_instances_mutex);
  g_instances.erase(
      std::remove(g_instances.begin(), g_instances.end(), instance),
      g_instances.end());
}

ControlPanelCore* ControlPanelCore::get_first_instance() {
    std::lock_guard<std::mutex> lock(g_instances_mutex);
    return g_instances.empty() ? nullptr : g_instances.front();
}

void ControlPanelCore::notify_theme_changed() {
  if (g_shutdown) return;
  // Copy under lock, then iterate without holding it.
  // on_settings_changed() -> nowbar_notify_color_changed() can re-enter
  // g_instances_mutex via get_first_instance(), so we must not hold it.
  std::vector<ControlPanelCore*> snapshot;
  {
    std::lock_guard<std::mutex> lock(g_instances_mutex);
    snapshot = g_instances;
  }
  for (auto *instance : snapshot) {
    instance->on_settings_changed();
  }
}

void ControlPanelCore::shutdown() {
  g_shutdown = true;
  // Destroy theme callback while services are still available
  // (ui_config_callback_impl destructor needs to unregister)
  g_theme_callback.reset();
  // Clear instances while mutex is still valid
  std::lock_guard<std::mutex> lock(g_instances_mutex);
  g_instances.clear();
}

// Live streams have no known duration. Require an active track so stopped playback is not labelled Live.
static bool is_live_stream(const nowbar::PlaybackState& state) {
  return (state.is_playing || state.is_paused) &&
         state.current_track.is_valid() && state.track_length <= 0.0;
}

// Helper: format time as mm:ss or hh:mm:ss
static std::wstring format_time(double seconds) {
  if (seconds < 0)
    seconds = 0;
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
static std::wstring utf8_to_wide(const char *utf8) {
  if (!utf8 || !*utf8)
    return L"";
  int len = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
  if (len <= 0)
    return L"";
  std::wstring result(len - 1, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8, -1, &result[0], len);
  return result;
}

// Helper: point in rect
static bool pt_in_rect(const RECT &r, int x, int y) {
  return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
}

// Convert dB to slider position (0.0 to 1.0) using the same logarithmic curve
// as foobar2000's default volume control: position = 2^(dB/10).
// This gives perceptually-even volume distribution (more resolution at lower volumes).
static inline float db_to_slider(float db) {
    auto position = std::pow(2.0, db / 10.0);
    if (position < 0.0) position = 0.0;
    if (position > 1.0) position = 1.0;
    return static_cast<float>(position);
}

// Convert slider position (0.0 to 1.0) to dB using the same logarithmic curve
// as foobar2000's default volume control: dB = 10 * log2(position).
// Position 0 = mute (-100 dB). Inverse of db_to_slider.
static inline float slider_to_db(float slider_pos) {
    if (slider_pos <= 0.0f) return -100.0f;
    auto volume = 10.0 * std::log2(static_cast<double>(slider_pos));
    if (volume < -100.0) volume = -100.0;
    return static_cast<float>(volume);
}

// UI selection callback - updates display when selection changes in playlist/library
class ControlPanelCore::SelectionCallback : public ui_selection_callback_impl_base_ex<ui_selection_manager_v2::flag_no_now_playing> {
public:
  SelectionCallback(ControlPanelCore* owner) : m_owner(owner) {}

  void on_selection_changed(metadb_handle_list_cref) override {
    m_owner->on_selection_or_focus_changed();
  }

private:
  ControlPanelCore* m_owner;
};

// Playlist focus and order callback - updates rating display, selection mode, and playback order buttons
class ControlPanelCore::PlaylistFocusCallback : public playlist_callback_impl_base {
public:
  PlaylistFocusCallback(ControlPanelCore* owner)
      : playlist_callback_impl_base(
            playlist_callback::flag_on_item_focus_change |
            playlist_callback::flag_on_items_selection_change |
            playlist_callback::flag_on_playlist_activate |
            playlist_callback::flag_on_playback_order_changed)
      , m_owner(owner) {}

  void on_item_focus_change(t_size, t_size, t_size) override {
    m_owner->on_selection_or_focus_changed();
  }

  void on_items_selection_change(t_size, const bit_array&, const bit_array&) override {
    m_owner->on_selection_or_focus_changed();
  }

  void on_playlist_activate(t_size, t_size) override {
    m_owner->on_selection_or_focus_changed();
  }

  void on_playback_order_changed(t_size new_order) override {
    m_owner->m_state.playback_order = new_order;
    m_owner->invalidate();
  }

private:
  ControlPanelCore* m_owner;
};

// Metadb change callback - updates rating/mood when track metadata changes
// (e.g. foo_playcount updates its rating database and calls dispatch_refresh)
class ControlPanelCore::MetadbChangeCallback : public metadb_io_callback_dynamic_impl_base {
public:
  MetadbChangeCallback(ControlPanelCore* owner) : m_owner(owner) {}

  void on_changed_sorted(metadb_handle_list_cref p_items_sorted, bool p_fromhook) override {
    // Check if the currently displayed track is among the changed items
    metadb_handle_ptr track = m_owner->get_display_track();
    if (!track.is_valid()) return;

    if (t_size index{}; p_items_sorted.bsearch_t(
            pfc::compare_t<metadb_handle_ptr, metadb_handle_ptr>, track, index)) {
      m_owner->evaluate_title_formats();
      m_owner->update_rating_state();
      m_owner->update_mood_state();
      if (m_owner->m_artwork_request_cb) {
        m_owner->m_artwork_request_cb();
      }
      m_owner->m_needs_full_repaint = true;
      m_owner->m_bg_cache_valid = false;
      m_owner->invalidate();
    }
  }

private:
  ControlPanelCore* m_owner;
};

ControlPanelCore::ControlPanelCore() {
  // Register for playback callbacks
  PlaybackStateManager::get().register_callback(this);
  m_state = PlaybackStateManager::get().get_state();

  // Register for UI selection changes
  m_selection_callback = std::make_unique<SelectionCallback>(this);

  // Register for playlist focus changes (rating display)
  m_playlist_focus_callback = std::make_unique<PlaylistFocusCallback>(this);

  // Register for metadb changes (rating/mood updates from foo_playcount etc.)
  m_metadb_change_callback = std::make_unique<MetadbChangeCallback>(this);

  // Register for theme change notifications
  register_instance(this);

  // Default colors (dark mode)
  m_bg_color = Gdiplus::Color(255, 24, 24, 24);
  m_text_color = Gdiplus::Color(255, 255, 255, 255);
  m_text_secondary_color = Gdiplus::Color(255, 160, 160, 160);
  m_accent_color = Gdiplus::Color(255, 100, 180,
                                  255); // Light blue for active shuffle/repeat
  m_button_hover_color = Gdiplus::Color(40, 255, 255, 255);
}

// Thread-pool timer callback — fires on a worker thread and posts a
// normal-priority message so the animation frame is dispatched ahead of
// WM_MOUSEMOVE input from neighbouring panels.
static VOID CALLBACK anim_tp_callback(PVOID ctx, BOOLEAN) {
  PostMessage(reinterpret_cast<HWND>(ctx),
              ControlPanelCore::WM_NOWBAR_ANIMATE, 0, 0);
}

void ControlPanelCore::on_animation_timer_fired() {
  if (!m_animation_timer_active) return;
  // Non-blocking delete — safe because the callback has already completed
  // (we're handling the message it posted).
  DeleteTimerQueueTimer(nullptr, m_anim_tp_timer, nullptr);
  m_anim_tp_timer = nullptr;
  m_animation_timer_active = false;
  m_last_invalidate_time = std::chrono::steady_clock::now();
}

ControlPanelCore::~ControlPanelCore() {
  // Cancel animation timer — blocking wait ensures the callback won't
  // PostMessage to a destroyed HWND.
  if (m_animation_timer_active) {
    DeleteTimerQueueTimer(nullptr, m_anim_tp_timer, INVALID_HANDLE_VALUE);
    m_anim_tp_timer = nullptr;
    m_animation_timer_active = false;
  }

  // Destroy callbacks before services are gone
  m_metadb_change_callback.reset();
  m_playlist_focus_callback.reset();
  m_selection_callback.reset();

  // Stop command state polling timer
  stop_command_state_timer();
  
  // Cancel any running waveform computation
  cancel_waveform_computation();

  // Release spectrum visualizer stream
  release_vis_stream();

  // Clean up GDI caches
  destroy_spectrum_bg_cache();
  destroy_spectrum_overlay();

  // Destroy tooltip control
  if (m_tooltip_hwnd) {
    DestroyWindow(m_tooltip_hwnd);
    m_tooltip_hwnd = nullptr;
  }

  unregister_instance(this);

  // Only access PlaybackStateManager if it hasn't been shut down
  if (PlaybackStateManager::is_available()) {
    PlaybackStateManager::get().unregister_callback(this);
  }
}

void ControlPanelCore::destroy_spectrum_bg_cache() {
  if (m_spectrum_bg_hdc) {
    SelectObject(m_spectrum_bg_hdc, m_spectrum_bg_old);
    DeleteObject(m_spectrum_bg_hbitmap);
    DeleteDC(m_spectrum_bg_hdc);
    m_spectrum_bg_hdc = nullptr;
    m_spectrum_bg_hbitmap = nullptr;
    m_spectrum_bg_old = nullptr;
  }
  m_spectrum_bg_cache_valid = false;
}

void ControlPanelCore::destroy_spectrum_overlay() {
  if (m_spectrum_overlay_hdc) {
    SelectObject(m_spectrum_overlay_hdc, m_spectrum_overlay_old);
    DeleteObject(m_spectrum_overlay_hbitmap);
    DeleteDC(m_spectrum_overlay_hdc);
    m_spectrum_overlay_hdc = nullptr;
    m_spectrum_overlay_hbitmap = nullptr;
    m_spectrum_overlay_old = nullptr;
    m_spectrum_overlay_bits = nullptr;
  }
}

void ControlPanelCore::ensure_spectrum_overlay(HDC ref_dc, int w, int h) {
  if (m_spectrum_overlay_hdc && m_spectrum_overlay_cx == w && m_spectrum_overlay_cy == h)
    return;
  destroy_spectrum_overlay();
  BITMAPINFO bmi = {};
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = w;
  bmi.bmiHeader.biHeight = -h;  // top-down
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;
  m_spectrum_overlay_hdc = CreateCompatibleDC(ref_dc);
  m_spectrum_overlay_hbitmap = CreateDIBSection(
      ref_dc, &bmi, DIB_RGB_COLORS, (void**)&m_spectrum_overlay_bits, nullptr, 0);
  m_spectrum_overlay_old = (HBITMAP)SelectObject(m_spectrum_overlay_hdc, m_spectrum_overlay_hbitmap);
  m_spectrum_overlay_cx = w;
  m_spectrum_overlay_cy = h;
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

  // Create Windows tooltip control for custom buttons
  // TTS_NOPREFIX - don't strip ampersands, TTS_ALWAYSTIP - show even when window is inactive
  m_tooltip_hwnd = CreateWindowExW(
      WS_EX_TOPMOST,
      TOOLTIPS_CLASSW,
      NULL,
      WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
      CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
      hwnd,
      NULL,
      NULL,
      NULL);
  
  if (m_tooltip_hwnd) {
    // Set maximum tooltip width to enable multi-line tooltips if needed
    SendMessage(m_tooltip_hwnd, TTM_SETMAXTIPWIDTH, 0, 300);
    
    // Add a single tool for the entire window - we'll update text dynamically
    TOOLINFOW ti = {};
    ti.cbSize = sizeof(TOOLINFOW);
    ti.uFlags = TTF_SUBCLASS | TTF_IDISHWND;
    ti.hwnd = hwnd;
    ti.uId = (UINT_PTR)hwnd;
    ti.lpszText = LPSTR_TEXTCALLBACKW;  // We'll provide text via notification
    GetClientRect(hwnd, &ti.rect);
    SendMessage(m_tooltip_hwnd, TTM_ADDTOOLW, 0, (LPARAM)&ti);
  }

  // Apply theme mode and fonts from preferences
  on_settings_changed();

  // Initialize mood state for current track
  update_mood_state();
  
  // Start command state polling timer for custom button toggle states
  start_command_state_timer();
}

void ControlPanelCore::set_dark_mode(bool dark) {
  bool mode_changed = (m_dark_mode != dark);
  m_dark_mode = dark;
  if (dark) {
    m_bg_color = Gdiplus::Color(255, 24, 24, 24);
    m_text_color = Gdiplus::Color(255, 255, 255, 255);
    m_text_secondary_color =
        Gdiplus::Color(255, 120, 120, 120); // Darker gray for icons
    // White hover circle for dark mode
    m_button_hover_color = Gdiplus::Color(40, 255, 255, 255);
  } else {
    m_bg_color = Gdiplus::Color(255, 245, 245, 245);
    m_text_color = Gdiplus::Color(255, 0, 0, 0);
    m_text_secondary_color =
        Gdiplus::Color(255, 70, 70, 70); // Darker gray for icons
    // Dark hover circle for light mode
    m_button_hover_color = Gdiplus::Color(40, 0, 0, 0);
  }

  // Update theme colors from DUI/CUI for preference fallbacks
  if (m_color_query_cb) {
    COLORREF bg, text, highlight, selection;
    if (m_color_query_cb(bg, text, highlight, selection)) {
      m_theme_highlight = highlight;
      m_theme_selection = selection;
    }
  }
  // m_theme_text must match the panel's own text color, not the DUI text color,
  // because Dark/Light mode uses hardcoded colors that may differ from the system theme.
  m_theme_text = RGB(m_text_color.GetR(), m_text_color.GetG(), m_text_color.GetB());
  // Track (unfilled bar) color: matches the artwork placeholder color
  // so the bar track blends with the empty artwork area
  m_track_color = dark ? RGB(60, 60, 60) : RGB(200, 200, 200);

  // Invalidate caches
  m_bg_cache_valid = false;
  m_target_background.reset();

  // When dark/light mode changes, discard the cached background so that
  // draw_background() does an instant switch instead of a crossfade.
  // A crossfade across dark↔light boundaries looks wrong because foreground
  // elements (text, icons) switch instantly while the background transitions
  // gradually — resulting in dark text on a still-dark background (or vice versa).
  if (mode_changed) {
    m_prev_background.reset();
    m_bg_transition_active = false;
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
  // Get DPI for scaling
  int dpi = static_cast<int>(96 * m_dpi_scale);
  
  // Minimum height: 0.55 inches, scaled by DPI
  // At 96 DPI: 0.55 * 96 = 53 pixels
  LONG min_height = static_cast<LONG>(0.55 * dpi);

  double dpi_scale = m_dpi_scale;
  double min_scale = 0.60;
  if (get_nowbar_button_scaling_enabled()) {
    double fixed_scale = static_cast<double>(get_nowbar_button_scaling_percent()) / 100.0;
    if (fixed_scale > min_scale) min_scale = fixed_scale;
  }

  int button_size = static_cast<int>(28.0 * dpi_scale * min_scale);
  int play_button_size = static_cast<int>(36.0 * dpi_scale * min_scale);
  int spacing = static_cast<int>(16.0 * dpi_scale * min_scale);

  int min_info_w = static_cast<int>(140.0 * dpi_scale);
  int total_min_w = 0;

  // Artwork
  if (get_nowbar_cover_artwork_visible()) {
    int art_h = static_cast<int>(min_height);
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

  LONG min_width = static_cast<LONG>(total_min_w * 0.82f);
  
  return {min_width, min_height};
}



void ControlPanelCore::notify_all_settings_changed() {
  std::vector<ControlPanelCore*> snapshot;
  {
    std::lock_guard<std::mutex> lock(g_instances_mutex);
    snapshot = g_instances;
  }
  for (auto *instance : snapshot) {
    instance->on_settings_changed();
  }
}

void ControlPanelCore::notify_online_artwork_received() {
  std::vector<ControlPanelCore*> snapshot;
  {
    std::lock_guard<std::mutex> lock(g_instances_mutex);
    snapshot = g_instances;
  }
  HBITMAP pending_bitmap = get_pending_online_artwork();
  for (auto *instance : snapshot) {
    if (pending_bitmap) {
      instance->evaluate_title_formats();
      instance->m_needs_full_repaint = true;
      instance->set_artwork_from_hbitmap(pending_bitmap);
    } else {
      instance->on_online_artwork_received();
    }
  }
}

void ControlPanelCore::on_online_artwork_received() {
  // Re-evaluate title formats with fresh metadata from foo_artwork
  evaluate_title_formats();
  m_needs_full_repaint = true;

  // Request artwork update - this will check for pending online artwork
  if (m_artwork_request_cb) {
    m_artwork_request_cb();
  }
}

void ControlPanelCore::apply_theme() {
  int theme_mode = get_nowbar_theme_mode();
  int bg_style = get_nowbar_background_style();

  // Check if we have artwork-based background (Artwork Colors or Blurred)
  bool has_artwork_bg = (bg_style == 1 || bg_style == 2);

  // Determine if playback is stopped (not playing AND not paused)
  bool is_stopped = !m_state.is_playing && !m_state.is_paused;

  // Special case: Light theme + artwork background
  // Use Light theme only when stopped (showing placeholder), Dark when playing/paused
  if (theme_mode == 2 && has_artwork_bg) {
    set_dark_mode(!is_stopped);  // Light when stopped, Dark when playing/paused
    return;
  }

  // Special case: Auto theme + artwork background
  // When stopped (no artwork visible), follow DUI theme; when playing/paused, force dark
  // (artwork backgrounds have dark overlays that require light text)
  if (theme_mode == 0 && has_artwork_bg) {
    if (is_stopped) {
      // No artwork visible, follow DUI theme setting
      set_dark_mode(ui_config_manager::g_is_dark_mode());
    } else {
      // Artwork visible with dark overlay, force dark mode for readability
      set_dark_mode(true);
    }
    return;
  }

  // Special case: Custom theme + artwork background
  // When stopped (no artwork visible), use DUI colors; when playing/paused, force dark
  // (artwork backgrounds have dark overlays that require light text)
  if (theme_mode == 3 && has_artwork_bg) {
    if (is_stopped) {
      // No artwork visible, use DUI color scheme
      apply_custom_colors();
    } else {
      // Artwork visible with dark overlay, force dark mode for readability
      set_dark_mode(true);
    }
    return;
  }

  if (theme_mode == 3) {
    // Custom - use DUI color scheme via callback
    apply_custom_colors();
  } else {
    // At this point, artwork backgrounds are already handled above,
    // so we only need to handle solid backgrounds
    bool dark;
    switch (theme_mode) {
    case 1: // Dark
      dark = true;
      break;
    case 2: // Light
      dark = false;
      break;
    default: // Auto (0) - follow system/foobar setting
      dark = ui_config_manager::g_is_dark_mode();
      break;
    }
    set_dark_mode(dark);
  }
}

void ControlPanelCore::on_settings_changed() {
  m_needs_full_repaint = true;
  m_spectrum_bg_cache_valid = false;
  m_bg_cache_valid = false;

  // Apply theme based on current settings and playback state
  apply_theme();
  nowbar_notify_color_changed();

  m_waveform_brushes_dirty = true;

  // Update fonts from preferences
  update_fonts();
  
  // Update and recompile title format strings
  update_title_formats();
  
  // Invalidate target background to prevent stale content during transition
  m_target_background.reset();

  // Refresh mood, rating, format, and artwork state in case track targeting or formatting changed
  update_mood_state();
  update_rating_state();
  evaluate_title_formats();
  if (m_artwork_request_cb) {
    m_artwork_request_cb();
  }

  // Handle visualization mode setting changes
  int settings_vis_mode = get_nowbar_visualization_mode();
  bool spectrum_needed = (settings_vis_mode == 1);

  if (spectrum_needed) {
    bool is_playing_now = m_state.is_playing && !m_state.is_paused;
    if (is_playing_now && !m_vis_stream.is_valid()) {
      create_vis_stream();
      m_spectrum_start_opacity = m_spectrum_opacity;
      m_spectrum_target_opacity = 1.0f;
      m_spectrum_fade_start_time = std::chrono::steady_clock::now();
      m_spectrum_fade_active = true;
      m_spectrum_animating = true;
    }
  } else {
    if (m_vis_stream.is_valid() || m_spectrum_opacity > 0.0f) {
      release_vis_stream();
      m_spectrum_opacity = 0.0f;
      m_spectrum_hover_opacity = 1.0f;
      m_spectrum_hover_target = 1.0f;
      m_spectrum_hover_last_time = {};
      m_spectrum_fade_active = false;
      m_spectrum_animating = false;
    }
  }

  // Handle waveform mode transitions
  if (settings_vis_mode == 2) {
    bool is_playing_now = m_state.is_playing && !m_state.is_paused;
    if (is_playing_now && !m_waveform_valid && !m_waveform_computing.load()) {
      start_waveform_computation();
    }
    // Release vis stream if not needed
    if (m_vis_stream.is_valid()) {
      release_vis_stream();
      m_spectrum_opacity = 0.0f;
      m_spectrum_fade_active = false;
      m_spectrum_animating = false;
    }
  } else {
    // Not in waveform mode - cancel computation and clear
    if (m_waveform_computing.load() || m_waveform_valid) {
      cancel_waveform_computation();
      std::lock_guard<std::mutex> lock(m_waveform_mutex);
      m_waveform_valid = false;
      m_waveform_peaks.clear();
      m_waveform_track_path = "";
    }
  }

  // Invalidate cached command references and poll immediately so state is up-to-date before repainting
  for (int i = 0; i < 6; i++) {
    m_cbutton_states[i].cache_valid = false;
  }
  poll_custom_button_states();

  // Update custom button auto-hide opacity and animation state
  if (get_nowbar_cbutton_autohide()) {
    bool is_playing_now = m_state.is_playing && !m_state.is_paused;
    float target = is_playing_now ? 0.0f : 1.0f;
    if (m_cbutton_opacity != target || m_cbutton_target_opacity != target) {
      m_cbutton_start_opacity = m_cbutton_opacity;
      m_cbutton_target_opacity = target;
      if (get_nowbar_smooth_animations_enabled()) {
        m_cbutton_fade_start_time = std::chrono::steady_clock::now();
        m_cbutton_fade_active = true;
        m_cbutton_animating = true;
        request_animation();
      } else {
        m_cbutton_opacity = target;
        m_cbutton_fade_active = false;
        m_cbutton_animating = false;
      }
    }
  } else {
    m_cbutton_opacity = 1.0f;
    m_cbutton_target_opacity = 1.0f;
    m_cbutton_fade_active = false;
    m_cbutton_animating = false;
  }

  // Reset 3D button press state
  for (int i = 0; i < 6; i++) {
    m_cbutton_press_progress[i] = 0.0f;
    m_cbutton_pressed[i] = false;
  }

  // Reset thin progress bar hover state if leaving spectrum mode or seekbar is hidden
  if (get_nowbar_visualization_mode() != 1 || !get_nowbar_seekbar_visible()) {
    m_thin_progress_hover_progress = 0.0f;
    m_thin_progress_target_progress = 0.0f;
    m_thin_progress_anim_active = false;
    m_seekbar_animating = false;
  }

  // Repaint to reflect any visual changes
  invalidate();

  if (m_settings_changed_cb) {
    m_settings_changed_cb();
  }
}

void ControlPanelCore::set_color_query_callback(ColorQueryCallback callback) {
  m_color_query_cb = callback;
}

void ControlPanelCore::apply_custom_colors() {
  if (!m_color_query_cb) {
    // Fallback to auto mode if no callback
    set_dark_mode(ui_config_manager::g_is_dark_mode());
    return;
  }
  
  COLORREF bg, text, highlight, selection;
  if (m_color_query_cb(bg, text, highlight, selection)) {

    // Determine if this is a dark or light theme based on background luminance
    // Using relative luminance formula: 0.299*R + 0.587*G + 0.114*B
    int luminance = (299 * GetRValue(bg) + 587 * GetGValue(bg) + 114 * GetBValue(bg)) / 1000;
    m_dark_mode = (luminance < 128);

    // Apply custom colors from DUI color scheme
    m_bg_color = Gdiplus::Color(255, GetRValue(bg), GetGValue(bg), GetBValue(bg));
    m_text_color = Gdiplus::Color(255, GetRValue(text), GetGValue(text), GetBValue(text));
    
    // Calculate secondary color (60% opacity approximation)
    int r = GetRValue(text), g = GetGValue(text), b = GetBValue(text);
    int br = GetRValue(bg), bg_ = GetGValue(bg), bb = GetBValue(bg);
    m_text_secondary_color = Gdiplus::Color(255, 
        (r * 60 + br * 40) / 100,
        (g * 60 + bg_ * 40) / 100, 
        (b * 60 + bb * 40) / 100);
    
    // Accent color from highlight
    m_accent_color = Gdiplus::Color(255, GetRValue(highlight), GetGValue(highlight), GetBValue(highlight));

    // Cache theme colors for preference fallback
    m_theme_highlight = highlight;
    m_theme_selection = selection;
    m_theme_text = text;
    // Track color: 20% blend from background toward text for subtle visibility
    m_track_color = RGB(
        br + (r - br) * 20 / 100,
        bg_ + (g - bg_) * 20 / 100,
        bb + (b - bb) * 20 / 100);

    // Hover color - semi-transparent version of text color
    m_button_hover_color = Gdiplus::Color(40, GetRValue(text), GetGValue(text), GetBValue(text));
    
    // Invalidate caches
    m_bg_cache_valid = false;
    m_target_background.reset();
    invalidate();
  } else {
    // Callback failed, fallback to auto
    set_dark_mode(ui_config_manager::g_is_dark_mode());
  }
}

void ControlPanelCore::update_fonts() {
  // Get font settings from preferences
  LOGFONT lf_track = get_nowbar_use_custom_fonts()
                         ? get_nowbar_track_font()
                         : get_nowbar_default_font(false);
  LOGFONT lf_artist = get_nowbar_use_custom_fonts()
                          ? get_nowbar_artist_font()
                          : get_nowbar_default_font(true);

  // Line 3 font (defaults to same as artist)
  LOGFONT lf_line3 = get_nowbar_use_custom_fonts()
                         ? get_nowbar_line3_font()
                         : get_nowbar_default_font(true);

  // Time font
  LOGFONT lf_time = get_nowbar_use_custom_fonts()
                        ? get_nowbar_time_font()
                        : get_nowbar_default_time_font();

  // Build each font defensively. A GetDC failure or a transient GDI+ state
  // (observed during early DUI layout restoration) can make the first font
  // allocation return an invalid or null pointer. We always end with a usable
  // Gdiplus::Font (last-resort: GenericSansSerif, emHeight=12) so downstream
  // GetHeight()/DrawString() calls never dereference null.
  HDC hdc = GetDC(m_hwnd);

  auto make_font = [&](const LOGFONT& lf, const LOGFONT& def) -> Gdiplus::Font* {
    Gdiplus::Font* f = nullptr;
    try { f = new Gdiplus::Font(hdc, &lf); } catch (...) { f = nullptr; }
    if (f && f->GetLastStatus() == Gdiplus::Ok) return f;
    delete f;
    try { f = new Gdiplus::Font(hdc, &def); } catch (...) { f = nullptr; }
    if (f && f->GetLastStatus() == Gdiplus::Ok) return f;
    delete f;
    // Last resort — family-based constructor does not need an HDC and
    // cannot fail in practice when GDI+ itself is available.
    try { f = new Gdiplus::Font(Gdiplus::FontFamily::GenericSansSerif(), 12.0f); }
    catch (...) { f = nullptr; }
    return f;
  };

  m_font_title.reset(make_font(lf_track,  get_nowbar_default_font(false)));
  m_font_artist.reset(make_font(lf_artist, get_nowbar_default_font(true)));
  m_font_line3.reset(make_font(lf_line3,  get_nowbar_default_font(true)));
  m_font_time.reset(make_font(lf_time,   get_nowbar_default_time_font()));

  // Pre-measure volume number width and glyph vertical centering for "100"
  if (m_font_time) {
    Gdiplus::Graphics g_temp(hdc);
    Gdiplus::StringFormat sfMeasure(Gdiplus::StringFormat::GenericTypographic());
    sfMeasure.SetFormatFlags(sfMeasure.GetFormatFlags() | Gdiplus::StringFormatFlagsNoWrap | Gdiplus::StringFormatFlagsNoClip);
    Gdiplus::RectF bounds;
    g_temp.MeasureString(L"100", -1, m_font_time.get(), Gdiplus::PointF(0, 0), &sfMeasure, &bounds);
    m_volume_number_width = static_cast<int>(std::ceil(bounds.Width)) + static_cast<int>(4 * m_dpi_scale);

    Gdiplus::FontFamily time_family;
    m_font_time->GetFamily(&time_family);
    int time_style = m_font_time->GetStyle();
    UINT16 em_height = time_family.GetEmHeight(time_style);
    UINT16 cell_ascent = time_family.GetCellAscent(time_style);
    UINT16 cell_descent = time_family.GetCellDescent(time_style);
    float font_size = m_font_time->GetSize();
    float ascent_px = em_height ? font_size * cell_ascent / em_height : 0.0f;
    float descent_px = em_height ? font_size * cell_descent / em_height : 0.0f;
    m_volume_number_line_h = ascent_px + descent_px;

    Gdiplus::GraphicsPath measure_path;
    measure_path.AddString(L"100", -1, &time_family, time_style, font_size,
                           Gdiplus::PointF(0.0f, 0.0f), &sfMeasure);
    Gdiplus::RectF glyph_bounds;
    measure_path.GetBounds(&glyph_bounds);
    m_volume_number_glyph_y = glyph_bounds.Y;
    m_volume_number_glyph_h = glyph_bounds.Height;
  } else {
    m_volume_number_width = static_cast<int>(28 * m_dpi_scale);
    m_volume_number_glyph_y = 0.0f;
    m_volume_number_glyph_h = 10.0f;
    m_volume_number_line_h = 14.0f;
  }

  ReleaseDC(m_hwnd, hdc);

  // Measure actual font pixel heights for layout. Each GetHeight() call is
  // guarded because an extreme failure (GDI+ uninitialized, OOM) could still
  // leave a null pointer here.
  float dpi = 96.0f * m_dpi_scale;
  m_title_font_height  = m_font_title  ? static_cast<int>(m_font_title->GetHeight(dpi)  + 0.99f) : 16;
  m_artist_font_height = m_font_artist ? static_cast<int>(m_font_artist->GetHeight(dpi) + 0.99f) : 14;
  m_line3_font_height  = m_font_line3  ? static_cast<int>(m_font_line3->GetHeight(dpi)  + 0.99f) : 14;

  invalidate();
}

void ControlPanelCore::create_artwork_thumbnail() {
    m_artwork_thumbnail.reset();

    if (!m_artwork_bitmap || m_artwork_bitmap->GetLastStatus() != Gdiplus::Ok) {
        return;
    }

    int srcW = m_artwork_bitmap->GetWidth();
    int srcH = m_artwork_bitmap->GetHeight();

    const int max_dim = 512;

    // Only create thumbnail if source exceeds max_dim in either dimension
    if (srcW <= max_dim && srcH <= max_dim) {
        // Source is small enough - just move it directly
        m_artwork_thumbnail = std::move(m_artwork_bitmap);
        return;
    }

    // Calculate thumbnail dimensions preserving aspect ratio
    int thumbW, thumbH;
    if (srcW >= srcH) {
        thumbW = max_dim;
        thumbH = static_cast<int>(static_cast<float>(srcH) / srcW * max_dim);
    } else {
        thumbH = max_dim;
        thumbW = static_cast<int>(static_cast<float>(srcW) / srcH * max_dim);
    }
    if (thumbW < 1) thumbW = 1;
    if (thumbH < 1) thumbH = 1;

    m_artwork_thumbnail.reset(new Gdiplus::Bitmap(thumbW, thumbH, PixelFormat32bppARGB));
    Gdiplus::Graphics gfx(m_artwork_thumbnail.get());
    // Bilinear is much faster than HighQualityBicubic for large downscales and
    // visually equivalent at this target size (thumbnail is used for small panel
    // artwork display and as the source for further downscales like blur/colors).
    gfx.SetInterpolationMode(Gdiplus::InterpolationModeBilinear);
    gfx.DrawImage(m_artwork_bitmap.get(), 0, 0, thumbW, thumbH);

    // Release full-resolution bitmap to save memory
    m_artwork_bitmap.reset();
}

void ControlPanelCore::extract_artwork_colors() {
  m_artwork_colors_valid = false;

  // Prefer thumbnail (already downscaled to <=512x512) over full-res bitmap.
  // create_artwork_thumbnail() should be called before this function so that
  // we avoid an expensive full-resolution downscale here.
  Gdiplus::Bitmap* source = m_artwork_thumbnail ? m_artwork_thumbnail.get()
                          : m_artwork_bitmap     ? m_artwork_bitmap.get()
                          : nullptr;
  if (!source || source->GetLastStatus() != Gdiplus::Ok) {
    return;
  }

  // Downscale artwork to 8x8 for fast color averaging
  const int sample_size = 8;
  std::unique_ptr<Gdiplus::Bitmap> downscaled(new Gdiplus::Bitmap(sample_size, sample_size, PixelFormat32bppARGB));

  {
    Gdiplus::Graphics gfx(downscaled.get());
    gfx.SetInterpolationMode(Gdiplus::InterpolationModeBilinear);
    gfx.DrawImage(source, 0, 0, sample_size, sample_size);
  }
  
  // Calculate average color from all pixels
  int total_r = 0, total_g = 0, total_b = 0;
  int pixel_count = 0;
  
  for (int y = 0; y < sample_size; y++) {
    for (int x = 0; x < sample_size; x++) {
      Gdiplus::Color color;
      downscaled->GetPixel(x, y, &color);
      
      // Skip very transparent pixels
      if (color.GetA() < 128) continue;
      
      total_r += color.GetR();
      total_g += color.GetG();
      total_b += color.GetB();
      pixel_count++;
    }
  }
  
  if (pixel_count == 0) return;
  
  // Calculate average (primary color)
  int avg_r = total_r / pixel_count;
  int avg_g = total_g / pixel_count;
  int avg_b = total_b / pixel_count;
  
  // Primary color - the average artwork color
  m_artwork_color_primary = Gdiplus::Color(255, avg_r, avg_g, avg_b);
  
  // Secondary color - darker variant for gradient (30% darker)
  int dark_r = avg_r * 70 / 100;
  int dark_g = avg_g * 70 / 100;
  int dark_b = avg_b * 70 / 100;
  m_artwork_color_secondary = Gdiplus::Color(255, dark_r, dark_g, dark_b);
  
  m_artwork_colors_valid = true;
  nowbar_notify_color_changed();
}

void ControlPanelCore::create_blurred_artwork(int target_width, int target_height) {
  m_blurred_artwork.reset();
  m_blurred_artwork_size = {0, 0};

  if (!m_artwork_thumbnail || m_artwork_thumbnail->GetLastStatus() != Gdiplus::Ok) {
    return;
  }
  
  if (target_width <= 0 || target_height <= 0) {
    return;
  }
  
  // First, scale source artwork to intermediate size for blur using GDI+ (fast)
  const int blur_size = 64;
  std::unique_ptr<Gdiplus::Bitmap> scaled(new Gdiplus::Bitmap(blur_size, blur_size, PixelFormat32bppARGB));
  
  {
    Gdiplus::Graphics gfx(scaled.get());
    gfx.SetInterpolationMode(Gdiplus::InterpolationModeBilinear);
    gfx.DrawImage(m_artwork_thumbnail.get(), 0, 0, blur_size, blur_size);
  }
  
  // Lock the scaled bitmap for direct memory access
  Gdiplus::Rect lockRect(0, 0, blur_size, blur_size);
  Gdiplus::BitmapData scaledData;
  if (scaled->LockBits(&lockRect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &scaledData) != Gdiplus::Ok) {
    return;  // Failed to lock, fall back gracefully
  }
  
  // Create temp buffer for blur (we need two passes: horizontal then vertical)
  std::vector<BYTE> tempBuffer(blur_size * blur_size * 4);
  std::vector<BYTE> blurBuffer(blur_size * blur_size * 4);
  
  BYTE* srcPixels = static_cast<BYTE*>(scaledData.Scan0);
  int srcStride = scaledData.Stride;
  
  const int radius = 4;
  const int diameter = radius * 2 + 1;
  
  // Pass 1: Horizontal blur into tempBuffer
  for (int y = 0; y < blur_size; y++) {
    for (int x = 0; x < blur_size; x++) {
      int total_r = 0, total_g = 0, total_b = 0, total_a = 0;
      int count = 0;
      
      for (int dx = -radius; dx <= radius; dx++) {
        int sx = x + dx;
        if (sx >= 0 && sx < blur_size) {
          BYTE* pixel = srcPixels + y * srcStride + sx * 4;
          total_b += pixel[0];
          total_g += pixel[1];
          total_r += pixel[2];
          total_a += pixel[3];
          count++;
        }
      }
      
      int dstIdx = (y * blur_size + x) * 4;
      tempBuffer[dstIdx + 0] = static_cast<BYTE>(total_b / count);
      tempBuffer[dstIdx + 1] = static_cast<BYTE>(total_g / count);
      tempBuffer[dstIdx + 2] = static_cast<BYTE>(total_r / count);
      tempBuffer[dstIdx + 3] = static_cast<BYTE>(total_a / count);
    }
  }
  
  // Pass 2: Vertical blur from tempBuffer into blurBuffer
  for (int y = 0; y < blur_size; y++) {
    for (int x = 0; x < blur_size; x++) {
      int total_r = 0, total_g = 0, total_b = 0, total_a = 0;
      int count = 0;
      
      for (int dy = -radius; dy <= radius; dy++) {
        int sy = y + dy;
        if (sy >= 0 && sy < blur_size) {
          int srcIdx = (sy * blur_size + x) * 4;
          total_b += tempBuffer[srcIdx + 0];
          total_g += tempBuffer[srcIdx + 1];
          total_r += tempBuffer[srcIdx + 2];
          total_a += tempBuffer[srcIdx + 3];
          count++;
        }
      }
      
      int dstIdx = (y * blur_size + x) * 4;
      blurBuffer[dstIdx + 0] = static_cast<BYTE>(total_b / count);
      blurBuffer[dstIdx + 1] = static_cast<BYTE>(total_g / count);
      blurBuffer[dstIdx + 2] = static_cast<BYTE>(total_r / count);
      blurBuffer[dstIdx + 3] = static_cast<BYTE>(total_a / count);
    }
  }
  
  scaled->UnlockBits(&scaledData);
  
  // Create output bitmap at exact target size
  m_blurred_artwork.reset(new Gdiplus::Bitmap(target_width, target_height, PixelFormat32bppARGB));
  m_blurred_artwork->SetResolution(96.0f * m_dpi_scale, 96.0f * m_dpi_scale);
  m_blurred_artwork_size = {(LONG)target_width, (LONG)target_height};
  
  // Lock output bitmap for direct write
  Gdiplus::Rect outRect(0, 0, target_width, target_height);
  Gdiplus::BitmapData outData;
  if (m_blurred_artwork->LockBits(&outRect, Gdiplus::ImageLockModeWrite, PixelFormat32bppARGB, &outData) != Gdiplus::Ok) {
    m_blurred_artwork.reset();
    return;
  }
  
  BYTE* outPixels = static_cast<BYTE*>(outData.Scan0);
  int outStride = outData.Stride;
  
  // Calculate source crop from the blurred buffer (center-crop to match target aspect)
  float targetAspect = static_cast<float>(target_width) / static_cast<float>(target_height);
  float srcX = 0, srcY = 0, srcW = static_cast<float>(blur_size), srcH = static_cast<float>(blur_size);
  
  if (targetAspect > 1.0f) {
    // Target is wider than source - crop vertically from center
    srcH = blur_size / targetAspect;
    srcY = (blur_size - srcH) / 2.0f;
  } else {
    // Target is taller than source - crop horizontally from center
    srcW = blur_size * targetAspect;
    srcX = (blur_size - srcW) / 2.0f;
  }
  
  // Direct pixel sampling with bilinear interpolation using memory access
  for (int y = 0; y < target_height; y++) {
    BYTE* outRow = outPixels + y * outStride;
    float sy_base = srcY + (y / static_cast<float>(target_height)) * srcH;
    
    for (int x = 0; x < target_width; x++) {
      // Map target coordinates to source coordinates
      float sx = srcX + (x / static_cast<float>(target_width)) * srcW;
      float sy = sy_base;
      
      // Bilinear interpolation coordinates
      int x0 = static_cast<int>(sx);
      int y0 = static_cast<int>(sy);
      int x1 = (std::min)(x0 + 1, blur_size - 1);
      int y1 = (std::min)(y0 + 1, blur_size - 1);
      x0 = (std::max)(0, (std::min)(x0, blur_size - 1));
      y0 = (std::max)(0, (std::min)(y0, blur_size - 1));
      
      float fx = sx - static_cast<int>(sx);
      float fy = sy - static_cast<int>(sy);
      float fx_inv = 1.0f - fx;
      float fy_inv = 1.0f - fy;
      
      // Get pointers to the 4 source pixels
      int idx00 = (y0 * blur_size + x0) * 4;
      int idx10 = (y0 * blur_size + x1) * 4;
      int idx01 = (y1 * blur_size + x0) * 4;
      int idx11 = (y1 * blur_size + x1) * 4;
      
      // Bilinear interpolation for each channel (BGRA format)
      for (int c = 0; c < 4; c++) {
        float top = blurBuffer[idx00 + c] * fx_inv + blurBuffer[idx10 + c] * fx;
        float bottom = blurBuffer[idx01 + c] * fx_inv + blurBuffer[idx11 + c] * fx;
        outRow[x * 4 + c] = static_cast<BYTE>(top * fy_inv + bottom * fy);
      }
    }
  }
  
  m_blurred_artwork->UnlockBits(&outData);
}

void ControlPanelCore::update_layout(const RECT &rect) {
  int w = rect.right - rect.left;
  int h = rect.bottom - rect.top;
  m_panel_width = w;
  int y_center = rect.top + h / 2;

  // Artwork (left side) - size based on panel height with margins.
  // In spectrum visualizer mode, the thin progress bar overlays directly on top
  // of the artwork, keeping artwork height constant regardless of seekbar visibility.
  int art_margin = get_nowbar_cover_margin() ? m_metrics.artwork_margin : 0;
  int art_size = h - (art_margin * 2); // Fit within available height with margins
  if (art_size > m_metrics.artwork_size) {
    art_size = m_metrics.artwork_size; // Cap at max size
  }
  if (art_size < 32) {
    art_size = 32; // Minimum size
  }
  int art_y = rect.top + (h - art_size) / 2;
  if (get_nowbar_cover_artwork_visible()) {
    m_rect_artwork = {rect.left + art_margin, art_y,
                      rect.left + art_margin + art_size, art_y + art_size};
  } else {
    m_rect_artwork = {};  // Clear rect when hidden
  }

  // Calculate size scale factor: fixed if button scaling is enabled, else dynamic based on panel height
  if (get_nowbar_button_scaling_enabled()) {
    int pct = get_nowbar_button_scaling_percent();
    pct = std::clamp(pct, 50, 100);
    m_size_scale = static_cast<float>(pct) / 100.0f;
  } else {
    int reference_height =
        static_cast<int>(92 * m_dpi_scale); // Reference/maximum panel height for full size
    int min_height =
        static_cast<int>(53 * m_dpi_scale); // Minimum panel height (0.55 inches = 53px at 96 DPI)
    float size_ratio = (reference_height > min_height)
        ? static_cast<float>(h - min_height) / static_cast<float>(reference_height - min_height)
        : 1.0f;
    if (size_ratio < 0)
      size_ratio = 0;
    if (size_ratio > 1)
      size_ratio = 1;
    // Scale from 60% at minimum to 100% at reference height
    m_size_scale = 0.60f + 0.40f * size_ratio;
  }

  // Scaled sizes for controls
  int button_size = static_cast<int>(m_metrics.button_size * m_size_scale);
  int play_button_size =
      static_cast<int>(m_metrics.play_button_size * m_size_scale);
  int spacing = static_cast<int>(m_metrics.spacing * m_size_scale);

  // Determine vertical offsets
  int pre_vis_mode = get_nowbar_visualization_mode();
  bool seekbar_visible = get_nowbar_seekbar_visible();
  int seek_gap = static_cast<int>(10 * m_dpi_scale * m_size_scale);
  int seekbar_height =
      static_cast<int>(m_metrics.seekbar_height * m_size_scale);
  int total_seekbar_area = seek_gap + seekbar_height;

  int vertical_offset;
  if (pre_vis_mode == 1 || !seekbar_visible) {
    vertical_offset = 0;  // Buttons centered without seekbar offset
  } else {
    // Calculate how much space we need below the play button for seekbar
    // At minimum height, we need to move controls up more to fit everything
    int bottom_margin =
        static_cast<int>(4 * m_dpi_scale); // Small margin at bottom
    int available_below_center = (rect.bottom - rect.top) / 2 - bottom_margin;
    int needed_below_center = play_button_size / 2 + total_seekbar_area;

    // Increase vertical offset if needed to fit seekbar within panel
    int base_offset = h / 10; // Base 10% offset
    int extra_offset = 0;
    if (needed_below_center > available_below_center) {
      extra_offset = needed_below_center - available_below_center;
    }
    vertical_offset = base_offset + extra_offset;
  }

  int btn_y = y_center - button_size / 2 - vertical_offset;
  int play_y = y_center - play_button_size / 2 - vertical_offset;
  
  // Right-side elements (volume, miniplayer, custom buttons) should be vertically
  // centered without the seekbar offset since they don't have seekbar below them
  int right_btn_y = y_center - button_size / 2;

  // Track info (between artwork and controls) - determine locked dimensions
  int vis_mode = get_nowbar_visualization_mode();
  int art_right = get_nowbar_cover_artwork_visible()
      ? m_rect_artwork.right
      : rect.left;
  int info_x = art_right + spacing;

  // Use actual font heights when available, fall back to metric default
  int title_h = m_title_font_height > 0
      ? m_title_font_height
      : static_cast<int>(m_metrics.text_height);
  int artist_h = m_artist_font_height > 0
      ? m_artist_font_height
      : static_cast<int>(m_metrics.text_height);
  int line3_font_h = m_line3_font_height > 0
      ? m_line3_font_height
      : static_cast<int>(m_metrics.text_height);
  int text_gap = static_cast<int>(4 * m_dpi_scale);
  bool rating_on_line3 = (get_nowbar_rating_mode() == 2);
  bool line3_active = !rating_on_line3 && (get_nowbar_line3_format().get_length() > 0);
  // Hide line 3 when panel height is reduced by more than 10% from default
  bool line3_height_ok = (h >= static_cast<int>(m_metrics.panel_height * 0.90f));
  if (!line3_height_ok) {
    line3_active = false;
    rating_on_line3 = false;
  }
  m_line3_visible = line3_active || rating_on_line3;
  if (!m_line3_visible && get_nowbar_rating_mode() == 2) {
    // Clear stale rating rects when line 3 is suppressed by height
    m_rect_rating = {};
    for (int i = 0; i < 5; i++) m_rect_stars[i] = {};
  }
  int line3_h = line3_active ? (line3_font_h + text_gap) : 0;
  int rating_line_h = rating_on_line3 ? (artist_h + text_gap) : 0;
  int info_height = title_h + artist_h + text_gap + line3_h + rating_line_h;
  int info_nudge = static_cast<int>(2 * m_dpi_scale);
  int info_y = y_center - info_height / 2 + info_nudge;

  // Control buttons (center) - positioned after Track Info area and left timer space
  bool shuffle_visible = get_nowbar_shuffle_icon_visible();
  bool repeat_visible = get_nowbar_repeat_icon_visible();
  bool stop_visible = get_nowbar_stop_icon_visible();
  bool stop_after_current_visible = get_nowbar_stop_after_current_icon_visible();
  bool super_visible = get_nowbar_super_icon_visible();
  int core_buttons = 3; // prev, play, next (always visible)
  if (shuffle_visible) core_buttons++;
  if (repeat_visible) core_buttons++;
  if (stop_visible) core_buttons++;
  if (stop_after_current_visible) core_buttons++;
  if (super_visible) core_buttons++;

  int left_timer_space = (pre_vis_mode == 1) ? 0 : static_cast<int>(65 * m_dpi_scale);
  int left_buttons_width = 0;
  if (get_nowbar_mood_icon_visible()) {
    left_buttons_width += button_size + spacing;
  }
  if (get_nowbar_rating_visible() && get_nowbar_rating_mode() != 2) {
    int star_size = static_cast<int>(button_size * 0.55f);
    int star_gap = static_cast<int>(2 * m_dpi_scale);
    left_buttons_width += (star_size * 5 + star_gap * 4) + spacing;
  }

  int core_width =
      button_size * (core_buttons - 1) + play_button_size + spacing * (core_buttons - 1);

  // Measure rendered text width of title, artist, and line 3
  int measured_text_width = static_cast<int>(180 * m_dpi_scale);
  if (m_hwnd) {
    HDC hdc = GetDC(m_hwnd);
    if (hdc) {
      Gdiplus::Graphics g(hdc);
      Gdiplus::StringFormat sf(Gdiplus::StringFormat::GenericTypographic());
      sf.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap | Gdiplus::StringFormatFlagsMeasureTrailingSpaces);

      std::wstring line1 = utf8_to_wide(m_layout_line1.c_str());
      std::wstring line2 = utf8_to_wide(m_layout_line2.c_str());
      std::wstring line3 = utf8_to_wide(m_layout_line3.c_str());

      float max_w = 0.0f;
      if (m_font_title && !line1.empty()) {
        Gdiplus::RectF bounds;
        g.MeasureString(line1.c_str(), -1, m_font_title.get(), Gdiplus::PointF(0, 0), &sf, &bounds);
        if (bounds.Width > max_w) max_w = bounds.Width;
      }
      if (m_font_artist && !line2.empty()) {
        Gdiplus::RectF bounds;
        g.MeasureString(line2.c_str(), -1, m_font_artist.get(), Gdiplus::PointF(0, 0), &sf, &bounds);
        if (bounds.Width > max_w) max_w = bounds.Width;
      }
      if (m_font_line3 && !line3.empty() && m_line3_visible) {
        Gdiplus::RectF bounds;
        g.MeasureString(line3.c_str(), -1, m_font_line3.get(), Gdiplus::PointF(0, 0), &sf, &bounds);
        if (bounds.Width > max_w) max_w = bounds.Width;
      }
      // Also guard against the actual formatted text exceeding the stabilized layout string
      if (m_lines_have_dynamic_time) {
        std::wstring dyn1 = utf8_to_wide(m_formatted_line1.c_str());
        std::wstring dyn2 = utf8_to_wide(m_formatted_line2.c_str());
        std::wstring dyn3 = utf8_to_wide(m_formatted_line3.c_str());
        if (m_font_title && !dyn1.empty()) {
          Gdiplus::RectF bounds;
          g.MeasureString(dyn1.c_str(), -1, m_font_title.get(), Gdiplus::PointF(0, 0), &sf, &bounds);
          if (bounds.Width > max_w) max_w = bounds.Width;
        }
        if (m_font_artist && !dyn2.empty()) {
          Gdiplus::RectF bounds;
          g.MeasureString(dyn2.c_str(), -1, m_font_artist.get(), Gdiplus::PointF(0, 0), &sf, &bounds);
          if (bounds.Width > max_w) max_w = bounds.Width;
        }
        if (m_font_line3 && !dyn3.empty() && m_line3_visible) {
          Gdiplus::RectF bounds;
          g.MeasureString(dyn3.c_str(), -1, m_font_line3.get(), Gdiplus::PointF(0, 0), &sf, &bounds);
          if (bounds.Width > max_w) max_w = bounds.Width;
        }
      }
      if (rating_on_line3) {
        int star_size = static_cast<int>(artist_h * 0.85f);
        int star_gap = static_cast<int>(2 * m_dpi_scale);
        int total_rating_width = star_size * 5 + star_gap * 4;
        if (static_cast<float>(total_rating_width) > max_w) max_w = static_cast<float>(total_rating_width);
      }

      if (max_w > 0.0f) {
        int needed = static_cast<int>(std::ceil(max_w + 20 * m_dpi_scale));
        measured_text_width = std::max(measured_text_width, needed);
      }
      ReleaseDC(m_hwnd, hdc);
    }
  }

  // Minimum core start position so controls do not overlap track info text:
  int min_info_w = std::min(measured_text_width, static_cast<int>(240 * m_dpi_scale));
  int min_core_start_x = info_x + min_info_w + left_timer_space + left_buttons_width;

  // Natural centered position:
  int centered_core_start_x = rect.left + (w - core_width) / 2;

  // Area #1 shrinks as panel width is reduced towards min_core_start_x:
  int core_start_x = std::max(centered_core_start_x, min_core_start_x);

  // Track Info bounding area fills from info_x up to the start of elapsed timer / left buttons:
  int info_right = core_start_x - left_timer_space - left_buttons_width - spacing;
  if (info_right < info_x) info_right = info_x;
  m_rect_track_info = {info_x, info_y, info_right, info_y + info_height};

  // In Line 3 mode, position rating stars within the track info area
  if (rating_on_line3) {
    int star_size = static_cast<int>(artist_h * 0.85f);
    int star_gap = static_cast<int>(2 * m_dpi_scale);
    int total_rating_width = star_size * 5 + star_gap * 4;
    int rating_y = info_y + title_h + text_gap + artist_h + line3_h + text_gap;
    int star_y_center = rating_y + (artist_h - star_size) / 2;
    m_rect_rating = {info_x, star_y_center, info_x + total_rating_width, star_y_center + star_size};
    for (int i = 0; i < 5; i++) {
      int sx = info_x + i * (star_size + star_gap);
      m_rect_stars[i] = {sx, star_y_center, sx + star_size, star_y_center + star_size};
    }
  }

  // Position core controls
  int controls_x = core_start_x;

  if (shuffle_visible) {
    m_rect_shuffle = {controls_x, btn_y, controls_x + button_size,
                      btn_y + button_size};
    controls_x += button_size + spacing;
  } else {
    m_rect_shuffle = {};  // Clear when hidden
  }

  m_rect_prev = {controls_x, btn_y, controls_x + button_size,
                 btn_y + button_size};
  controls_x += button_size + spacing;

  m_rect_play = {controls_x, play_y, controls_x + play_button_size,
                 play_y + play_button_size};
  controls_x += play_button_size + spacing;

  m_rect_next = {controls_x, btn_y, controls_x + button_size,
                 btn_y + button_size};
  controls_x += button_size + spacing;

  // Stop button - optional, positioned after Next, same size as Shuffle
  if (stop_visible) {
    m_rect_stop = {controls_x, btn_y, controls_x + button_size,
                   btn_y + button_size};
    controls_x += button_size + spacing;
  } else {
    m_rect_stop = {};  // Clear when hidden
  }

  // Stop After Current button - optional, positioned after Stop, same size
  if (stop_after_current_visible) {
    m_rect_stop_after_current = {controls_x, btn_y, controls_x + button_size,
                                 btn_y + button_size};
    controls_x += button_size + spacing;
  } else {
    m_rect_stop_after_current = {};  // Clear when hidden
  }

  if (repeat_visible) {
    m_rect_repeat = {controls_x, btn_y, controls_x + button_size,
                     btn_y + button_size};
    controls_x += button_size + spacing;
  } else {
    m_rect_repeat = {};  // Clear when hidden
  }
  
  // Super button - positioned after Repeat (cosmetic only)
  if (super_visible) {
    m_rect_super = {controls_x, btn_y, controls_x + button_size,
                    btn_y + button_size};
    controls_x += button_size + spacing;
  } else {
    m_rect_super = {};  // Clear rect when hidden
  }

  // Heart button - positioned to the left of the first core button (shuffle or prev)
  int core_left_edge = shuffle_visible ? m_rect_shuffle.left : m_rect_prev.left;
  if (get_nowbar_mood_icon_visible()) {
    int heart_x = core_left_edge - spacing - button_size;
    m_rect_heart = {heart_x, btn_y, heart_x + button_size, btn_y + button_size};
  } else {
    m_rect_heart = {}; // Clear rect when hidden
  }

  // Rating stars - positioned to the left of heart (or shuffle if heart hidden)
  // In "Line 3" mode (rating_mode==2), stars are drawn inside track info instead
  if (get_nowbar_rating_visible() && get_nowbar_rating_mode() != 2) {
    int star_size = static_cast<int>(button_size * 0.55f);
    int star_gap = static_cast<int>(2 * m_dpi_scale);
    int total_rating_width = star_size * 5 + star_gap * 4;

    // Position to the left of heart icon, or first core button if heart is hidden
    int rating_right;
    if (get_nowbar_mood_icon_visible() && m_rect_heart.right > m_rect_heart.left) {
      rating_right = m_rect_heart.left - spacing;
    } else {
      rating_right = core_left_edge - spacing;
    }
    int rating_x = rating_right - total_rating_width;
    m_rect_rating = {rating_x, btn_y, rating_x + total_rating_width, btn_y + button_size};

    // Compute individual star rects
    for (int i = 0; i < 5; i++) {
      int sx = rating_x + i * (star_size + star_gap);
      int star_y = btn_y + (button_size - star_size) / 2;
      m_rect_stars[i] = {sx, star_y, sx + star_size, star_y + star_size};
    }
  } else if (!rating_on_line3) {
    m_rect_rating = {};
    for (int i = 0; i < 5; i++) m_rect_stars[i] = {};
  }

  // Compute union bounding rect for the button row (used for smooth spectrum hover dimming without gap flickering)
  int row_left = INT_MAX, row_top = INT_MAX, row_right = INT_MIN, row_bottom = INT_MIN;
  auto expand_row = [&](const RECT &r) {
    if (r.right > r.left && r.bottom > r.top) {
      if (r.left < row_left) row_left = r.left;
      if (r.top < row_top) row_top = r.top;
      if (r.right > row_right) row_right = r.right;
      if (r.bottom > row_bottom) row_bottom = r.bottom;
    }
  };
  if (!rating_on_line3) {
    expand_row(m_rect_rating);
  }
  expand_row(m_rect_heart);
  expand_row(m_rect_shuffle);
  expand_row(m_rect_prev);
  expand_row(m_rect_play);
  expand_row(m_rect_next);
  expand_row(m_rect_stop);
  expand_row(m_rect_stop_after_current);
  expand_row(m_rect_repeat);
  expand_row(m_rect_super);

  if (row_left < row_right && row_top < row_bottom) {
    int pad = static_cast<int>(2 * m_dpi_scale);
    m_rect_button_row = {row_left - pad, row_top - pad, row_right + pad, row_bottom + pad};
  } else {
    m_rect_button_row = {};
  }

  // Right-side controls (Custom buttons, Volume, MiniPlayer)
  // Calculate total dimensions of all enabled right-side elements
  bool volume_bar_vis = get_nowbar_volume_bar_visible();
  bool volume_icon_vis = get_nowbar_volume_icon_visible();
  bool volume_number_vis = get_nowbar_volume_number_enabled();
  bool volume_visible = volume_icon_vis || volume_bar_vis || volume_number_vis;
  int volume_width = 0;
  int icon_size = volume_icon_vis ? static_cast<int>(23 * m_dpi_scale * m_size_scale) : 0;
  int vol_gap = static_cast<int>(14 * m_dpi_scale * m_size_scale);
  int num_width = volume_number_vis
      ? ((m_volume_number_width > 0) ? m_volume_number_width : static_cast<int>(28 * m_dpi_scale))
      : 0;
  if (volume_bar_vis) {
    volume_width = static_cast<int>(m_metrics.volume_width * m_size_scale);
  } else if (volume_icon_vis && volume_number_vis) {
    volume_width = icon_size + vol_gap + num_width;
  } else if (volume_icon_vis) {
    volume_width = icon_size;
  } else if (volume_number_vis) {
    volume_width = num_width;
  }

  // Balance the distance from the center of the volume number to the mini-player icon
  // with the distance from the left speaker icon to the custom buttons
  int mp_spacing = (volume_number_vis && volume_bar_vis)
      ? std::max(static_cast<int>(4 * m_dpi_scale), spacing - static_cast<int>(8 * m_dpi_scale * m_size_scale))
      : spacing;

  bool mp_visible = get_nowbar_miniplayer_icon_visible();
  int mp_width = mp_visible ? button_size : 0;

  bool btn_enabled[6] = {
    get_nowbar_cbutton_enabled(0), get_nowbar_cbutton_enabled(1), get_nowbar_cbutton_enabled(2),
    get_nowbar_cbutton_enabled(3), get_nowbar_cbutton_enabled(4), get_nowbar_cbutton_enabled(5)
  };
  int total_enabled = 0;
  for (int i = 0; i < 6; i++) {
    if (btn_enabled[i]) total_enabled++;
  }

  int max_glyph_pct = 80;
  for (int i = 0; i < 6; i++) {
    if (btn_enabled[i]) {
      int pct = get_nowbar_cbutton_glyph_size(i);
      if (pct > max_glyph_pct) max_glyph_pct = pct;
    }
  }
  int cbutton_size = (max_glyph_pct > 80)
      ? static_cast<int>(button_size * max_glyph_pct / 80.0f)
      : button_size;

  bool use_single_row = (m_size_scale < 0.75f);
  int row1_count = (btn_enabled[0] ? 1 : 0) + (btn_enabled[1] ? 1 : 0) + (btn_enabled[2] ? 1 : 0);
  int row2_count = (btn_enabled[3] ? 1 : 0) + (btn_enabled[4] ? 1 : 0) + (btn_enabled[5] ? 1 : 0);
  int cbuttons_width = 0;
  if (total_enabled > 0) {
    if (use_single_row) {
      cbuttons_width = total_enabled * cbutton_size + (total_enabled - 1) * spacing;
    } else {
      int max_cols = std::max(row1_count, row2_count);
      cbuttons_width = (max_cols > 0) ? (max_cols * cbutton_size + (max_cols - 1) * spacing) : 0;
    }
  }

  int right_group_width = 0;
  bool has_prev_right = false;
  if (cbuttons_width > 0) {
    right_group_width += cbuttons_width;
    has_prev_right = true;
  }
  if (volume_visible) {
    if (has_prev_right) right_group_width += spacing;
    right_group_width += volume_width;
    has_prev_right = true;
  }
  if (mp_visible) {
    if (has_prev_right) right_group_width += (volume_visible ? mp_spacing : spacing);
    right_group_width += mp_width;
    has_prev_right = true;
  }

  int core_right_edge = controls_x - spacing;
  int timer_reserve = static_cast<int>(70 * m_dpi_scale);
  int center_group_right = core_right_edge + timer_reserve;
  int min_right_group_left = center_group_right + spacing;

  int right_inset = static_cast<int>(16 * m_dpi_scale);
  int right_margin = rect.right - art_margin - right_inset;

  int right_group_left;
  if (right_group_width > 0) {
    int natural_left = right_margin - right_group_width;
    right_group_left = std::max(natural_left, min_right_group_left);
  } else {
    right_group_left = min_right_group_left;
  }

  // Position custom buttons #1-6
  m_rect_cbutton1 = {};
  m_rect_cbutton2 = {};
  m_rect_cbutton3 = {};
  m_rect_cbutton4 = {};
  m_rect_cbutton5 = {};
  m_rect_cbutton6 = {};
  m_rect_custom = {};

  int cur_right_x = right_group_left;
  if (total_enabled > 0) {
    int cb_start_x = cur_right_x;
    RECT* rects[6] = {&m_rect_cbutton1, &m_rect_cbutton2, &m_rect_cbutton3,
                      &m_rect_cbutton4, &m_rect_cbutton5, &m_rect_cbutton6};

    if (use_single_row) {
      int cbutton_y = y_center - cbutton_size / 2;
      int cb_x = cb_start_x;
      for (int i = 0; i < 6; i++) {
        if (btn_enabled[i]) {
          *rects[i] = {cb_x, cbutton_y, cb_x + cbutton_size, cbutton_y + cbutton_size};
          cb_x += cbutton_size + spacing;
        }
      }
    } else {
      int row_spacing = 2;
      int row1_y, row2_y;
      if (row2_count > 0) {
        int total_h = cbutton_size * 2 + row_spacing;
        row1_y = y_center - total_h / 2;
        row2_y = row1_y + cbutton_size + row_spacing;
      } else {
        row1_y = y_center - cbutton_size / 2;
        row2_y = 0;
      }

      int r1_x = cb_start_x;
      for (int i = 0; i < 3; i++) {
        if (btn_enabled[i]) {
          *rects[i] = {r1_x, row1_y, r1_x + cbutton_size, row1_y + cbutton_size};
          r1_x += cbutton_size + spacing;
        }
      }
      int r2_x = cb_start_x;
      for (int i = 3; i < 6; i++) {
        if (btn_enabled[i]) {
          *rects[i] = {r2_x, row2_y, r2_x + cbutton_size, row2_y + cbutton_size};
          r2_x += cbutton_size + spacing;
        }
      }
    }
    cur_right_x += cbuttons_width + spacing;
  }

  // Position volume
  if (volume_visible) {
    int vol_x = cur_right_x;
    int vol_right = vol_x + volume_width;
    m_rect_volume = {vol_x, right_btn_y, vol_right, right_btn_y + button_size};
    cur_right_x += volume_width + (mp_visible ? mp_spacing : spacing);
  } else {
    m_rect_volume = {};
  }

  // Position MiniPlayer button
  if (mp_visible) {
    int mp_x = cur_right_x;
    m_rect_miniplayer = {mp_x, right_btn_y, mp_x + button_size, right_btn_y + button_size};
    cur_right_x += button_size + spacing;
  } else {
    m_rect_miniplayer = {};
  }

  // Calculate full-scale button positions for seekbar/spectrum extent
  int full_button_size = static_cast<int>(m_metrics.button_size);
  int full_play_size = static_cast<int>(m_metrics.play_button_size);
  int full_spacing = static_cast<int>(m_metrics.spacing);
  int full_core_start_x = core_start_x;

  int full_core_left = full_core_start_x;
  int full_heart_left = full_core_left - full_spacing - full_button_size;

  int full_x = full_core_start_x;
  if (shuffle_visible) full_x += full_button_size + full_spacing; // past shuffle
  full_x += full_button_size + full_spacing; // past prev
  full_x += full_play_size + full_spacing;   // past play
  full_x += full_button_size + full_spacing; // past next
  if (stop_visible) full_x += full_button_size + full_spacing; // past stop
  if (stop_after_current_visible) full_x += full_button_size + full_spacing; // past stop after current
  int full_last_core_right;
  if (repeat_visible) {
    full_last_core_right = full_x + full_button_size;
    // full_x past repeat for super calculation
    full_x += full_button_size;
  } else {
    full_last_core_right = full_x - full_spacing; // back up to previous button's right edge
  }
  int full_super_right = full_last_core_right + full_spacing + full_button_size;

  // Compute seekbar bounds centered on the visible button span.
  // The leftmost visible button edge and rightmost visible button edge
  // define the center; the seekbar extends equally from that center.
  int buttons_left = core_left_edge;
  int buttons_right = core_right_edge;
  if (get_nowbar_mood_icon_visible())
    buttons_left = m_rect_heart.left;
  if (get_nowbar_super_icon_visible())
    buttons_right = m_rect_super.right;
  int buttons_center = (buttons_left + buttons_right) / 2;
  int buttons_half = (buttons_right - buttons_left) / 2;
  int seekbar_left = buttons_center - buttons_half;
  int seekbar_right = buttons_center + buttons_half;

  // Adjust seekbar extent based on Seekbar Length preference
  int seekbar_length_mode = get_nowbar_seekbar_length();
  int text_bound_x = (m_rect_track_info.right > info_x)
      ? std::min(info_x + measured_text_width, static_cast<int>(m_rect_track_info.right))
      : (info_x + measured_text_width);
  if (get_nowbar_cover_artwork_visible() && m_rect_artwork.right > text_bound_x) {
    text_bound_x = m_rect_artwork.right;
  }

  if (seekbar_length_mode == 1) {
    // Scaling: extend seekbar to fill available space proportionally
    int sp = static_cast<int>(full_spacing);
    int avail_left = (vis_mode == 1) ? (text_bound_x + sp) : (text_bound_x + timer_reserve);
    // Right boundary: right group start (custom buttons > volume > miniplayer) or panel right margin
    int avail_right = (right_group_width > 0) ? (right_group_left - timer_reserve) : (rect.right - sp - timer_reserve);
    int left_gap = seekbar_left - avail_left;
    int right_gap = avail_right - seekbar_right;
    // Use the smaller gap for both sides so the seekbar extends
    // symmetrically even when L1 != L2 (e.g. custom buttons enabled
    // on one side).  This keeps the progress bar visually centred
    // around the Play/Pause button by default.
    int min_gap = (left_gap < right_gap) ? left_gap : right_gap;
    if (min_gap > 0) {
      seekbar_left -= min_gap;
      seekbar_right += min_gap;
    }
  }
  // seekbar_length_mode == 0: Fixed (current Normal behavior, no change)

  // Apply seekbar position offset (shifts entire seekbar+timers block)
  int seekbar_offset = static_cast<int>(get_nowbar_seekbar_position() * m_dpi_scale);
  seekbar_left += seekbar_offset;
  seekbar_right += seekbar_offset;

  // Clear new rects by default
  m_rect_thin_progress = {};
  m_rect_waveform = {};
  m_rect_spectrum_full = {};

  if (!seekbar_visible) {
    // Seekbar hidden: clear all seekbar/time rects across all modes
    m_rect_seekbar = {};
    m_rect_time = {};
    if (vis_mode == 1) {
      // Still set up spectrum area even with seekbar off
      int spectrum_left = get_nowbar_mood_icon_visible() ? m_rect_heart.left : core_left_edge;
      int spectrum_right = get_nowbar_super_icon_visible() ? m_rect_super.right : core_right_edge;
      if (seekbar_length_mode == 1) {
        int sp = static_cast<int>(full_spacing);
        int avail_left = text_bound_x + sp;
        int avail_right = (right_group_width > 0) ? (right_group_left - sp) : (rect.right - sp);
        int left_gap = spectrum_left - avail_left;
        int right_gap = avail_right - spectrum_right;
        int min_gap = (left_gap < right_gap) ? left_gap : right_gap;
        if (min_gap > 0) {
          spectrum_left -= min_gap;
          spectrum_right += min_gap;
        }
      }
      int panel_h = rect.bottom - rect.top;
      int spec_height_mode = get_nowbar_spectrum_height();
      int spectrum_top;
      if (spec_height_mode == 0)       spectrum_top = rect.top + panel_h * 4 / 5;
      else if (spec_height_mode == 1)  spectrum_top = rect.top + panel_h * 2 / 3;
      else                             spectrum_top = rect.top + panel_h / 2;
      m_rect_spectrum_full = {spectrum_left, spectrum_top, spectrum_right, rect.bottom};
    }
  } else if (vis_mode == 1) {
    // Mode 1 (Spectrum): thin progress bar at top, full spectrum at bottom behind buttons
    // No seekbar in this mode
    m_rect_seekbar = {};

    // Thin progress bar: full panel width, 3px at top
    int thin_h = static_cast<int>(3 * m_dpi_scale);
    m_rect_thin_progress = {rect.left, rect.top, rect.right, rect.top + thin_h};

    // Full spectrum area: extent follows seekbar length setting
    int spectrum_left = get_nowbar_mood_icon_visible() ? m_rect_heart.left : core_left_edge;
    int spectrum_right = get_nowbar_super_icon_visible() ? m_rect_super.right : core_right_edge;
    if (seekbar_length_mode == 1) {
      // Scaling: extend spectrum to fill available space proportionally
      int sp = static_cast<int>(full_spacing);
      int avail_left = text_bound_x + sp;
      int avail_right = (right_group_width > 0) ? (right_group_left - sp) : (rect.right - sp);
      int left_gap = spectrum_left - avail_left;
      int right_gap = avail_right - spectrum_right;
      int min_gap = (left_gap < right_gap) ? left_gap : right_gap;
      if (min_gap > 0) {
        spectrum_left -= min_gap;
        spectrum_right += min_gap;
      }
    }
    // Spectrum area extends from a configurable fraction down to the bottom.
    // Height setting: High=bottom 50%, Normal=bottom 33%, Low=bottom 20%.
    // Track info and buttons are drawn on top of the spectrum overlay.
    int panel_h = rect.bottom - rect.top;
    int spec_height_mode = get_nowbar_spectrum_height();
    int spectrum_top;
    if (spec_height_mode == 0)       spectrum_top = rect.top + panel_h * 4 / 5;  // Low: 20%
    else if (spec_height_mode == 1)  spectrum_top = rect.top + panel_h * 2 / 3;  // Normal: 33%
    else                             spectrum_top = rect.top + panel_h / 2;       // High: 50%
    m_rect_spectrum_full = {spectrum_left, spectrum_top, spectrum_right, rect.bottom};

    // Time display in top-right corner, just below thin progress bar
    if (get_nowbar_playback_time_visible()) {
      int time_height = static_cast<int>(m_metrics.text_height * m_size_scale);
      int time_width = static_cast<int>(120 * m_dpi_scale);
      int time_margin = static_cast<int>(8 * m_dpi_scale);
      m_rect_time = {rect.right - time_width - time_margin,
                     m_rect_thin_progress.bottom + static_cast<int>(2 * m_dpi_scale),
                     rect.right - time_margin,
                     m_rect_thin_progress.bottom + static_cast<int>(2 * m_dpi_scale) + time_height};
    } else {
      m_rect_time = {};
    }

  } else if (vis_mode == 2) {
    // Always use tall height — reveal animation fills bars progressively
    int waveform_height = static_cast<int>(m_metrics.seekbar_height * m_size_scale * 4) - 1;
    int bottom_margin = static_cast<int>(4 * m_dpi_scale);
    int waveform_top = rect.bottom - bottom_margin - waveform_height;
    m_rect_waveform = {seekbar_left, waveform_top, seekbar_right, rect.bottom - bottom_margin};
    // Alias seekbar to waveform so existing SeekBar hit_test works for seeking
    m_rect_seekbar = m_rect_waveform;

    // Time display at the sides of the waveform bar (draw_time_display draws
    // elapsed to the left and remaining to the right of m_rect_seekbar).
    // Extend horizontally to cover both timer areas for correct clipping
    // in the waveform fast paint path and invalidate_progress dirty rect.
    int time_height = static_cast<int>(m_metrics.text_height * m_size_scale);
    int seekbar_center_y = (m_rect_seekbar.top + m_rect_seekbar.bottom) / 2;
    int timer_extent = static_cast<int>(120 * m_dpi_scale);
    int time_left = m_rect_seekbar.left - timer_extent;
    // Clamp left edge so the waveform fast-paint path (which clears and
    // redraws only m_rect_time + m_rect_waveform) does not erase track info
    // text that is never redrawn in that path.
    if (time_left < m_rect_track_info.right)
        time_left = m_rect_track_info.right;
    m_rect_time = {time_left,
                   seekbar_center_y - time_height / 2,
                   m_rect_seekbar.right + timer_extent,
                   seekbar_center_y + time_height / 2};

  } else {
    // Mode 0 (Disabled): normal seekbar
    int seek_y = m_rect_play.bottom + seek_gap;
    m_rect_seekbar = {seekbar_left, seek_y, seekbar_right, seek_y + seekbar_height};

    // Time display at the sides of the seekbar (draw_time_display draws
    // elapsed to the left and remaining to the right of m_rect_seekbar).
    // Extend horizontally to cover both timer areas so invalidate_progress
    // correctly invalidates where the time text is actually drawn.
    int time_height = static_cast<int>(m_metrics.text_height * m_size_scale);
    int seekbar_center_y = (m_rect_seekbar.top + m_rect_seekbar.bottom) / 2;
    int timer_extent = static_cast<int>(120 * m_dpi_scale);
    m_rect_time = {m_rect_seekbar.left - timer_extent,
                   seekbar_center_y - time_height / 2,
                   m_rect_seekbar.right + timer_extent,
                   seekbar_center_y + time_height / 2};
  }
}

void ControlPanelCore::paint(HDC hdc, const RECT &rect) {
  // On first paint, re-query theme settings to fix startup timing issue
  // where g_is_dark_mode() may return incorrect value before DUI is ready
  if (m_pending_theme_refresh) {
    m_pending_theme_refresh = false;
    on_settings_changed();
  }
  
  m_needs_full_repaint = false;
  m_spectrum_bg_cache_valid = false;

  update_layout(rect);
  update_thin_progress_hover_animation();

  Gdiplus::Graphics g(hdc);
  g.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
  g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

  // Draw background with aliased mode to prevent a 1px border artifact on the
  // left and top edges. SmoothingModeHighQuality applies sub-pixel coverage to
  // FillRectangle edges at y=0 and x=0, producing partially-filled edge pixels.
  // SmoothingModeNone disables anti-aliasing so rectangle fills have crisp edges.
  // PixelOffsetModeNone prevents the -0.5px coordinate shift.
  // NearestNeighbor prevents bilinear edge sampling on cached bitmap blits.
  g.SetSmoothingMode(Gdiplus::SmoothingModeNone);
  g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeNone);
  g.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
  draw_background(g, rect);
  g.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
  g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBilinear);
  g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
  if (get_nowbar_cover_artwork_visible()) {
    draw_artwork(g);
  }
  int paint_vis_mode = get_nowbar_visualization_mode();
  bool paint_seekbar_visible = get_nowbar_seekbar_visible();
  if (paint_vis_mode == 1) {
    // Mode 1: spectrum first, then track info on top, center buttons, thin progress bar + time (behind right-side controls),
    // and finally custom buttons, volume, and miniplayer on top
    g.Flush();
    draw_full_spectrum(hdc);
    draw_track_info(g);
    draw_playback_buttons(g, false);
    if (paint_seekbar_visible) {
      draw_thin_progress_bar(g);
      if (get_nowbar_playback_time_visible()) {
        draw_time_display_top_right(g);
      }
    }
    draw_custom_buttons(g);
  } else if (paint_vis_mode == 2) {
    draw_track_info(g);
    if (paint_seekbar_visible) {
      // Mode 2: waveform behind buttons, then buttons, then time display
      draw_waveform_bar(g);
      draw_playback_buttons(g);
      draw_time_display(g);
    } else {
      draw_playback_buttons(g);
    }
  } else {
    // Mode 0: normal seekbar, no visualization
    draw_track_info(g);
    draw_playback_buttons(g);
    if (paint_seekbar_visible) {
      draw_seekbar(g);
      draw_time_display(g);
    }
  }
  draw_volume(g);

  draw_miniplayer_button(g);

  // Draw tooltips last so they render on top of all other elements
  if (paint_vis_mode == 1) {
    if (paint_seekbar_visible) {
      draw_thin_progress_tooltip(g);
    }
  } else if (paint_vis_mode == 2) {
    if (paint_seekbar_visible) {
      draw_waveform_tooltip(g);
    }
  } else {
    if (paint_seekbar_visible) {
      draw_seekbar_tooltip(g);
    }
  }

  // Centralized animation loop: manage animation timer based on active animations
  if (get_nowbar_smooth_animations_enabled()) {
    bool any_animation_active = m_seekbar_animating || m_hover_animating ||
                                 m_cbutton_animating || m_bg_animating ||
                                 m_spectrum_animating || m_waveform_animating;

    if (any_animation_active) {
      request_animation();
    } else {
      // No animations active - make sure timer is stopped to avoid wasting CPU
      if (m_animation_timer_active) {
        DeleteTimerQueueTimer(nullptr, m_anim_tp_timer, nullptr);
        m_anim_tp_timer = nullptr;
        m_animation_timer_active = false;
      }
      m_animation_requested = false;
    }
  } else if (m_animation_timer_active) {
    // Spectrum uses the animation timer even when smooth animations is disabled.
    // Kill the timer when spectrum is no longer animating to prevent orphaned timers
    // (e.g., after switching from spectrum to waveform mode).
    if (!m_spectrum_animating) {
      DeleteTimerQueueTimer(nullptr, m_anim_tp_timer, nullptr);
      m_anim_tp_timer = nullptr;
      m_animation_timer_active = false;
      m_animation_requested = false;
    }
  }
}

void ControlPanelCore::paint_spectrum_only(HDC hdc, const RECT& panel_rect) {
  update_layout(panel_rect);

  int art_right = (get_nowbar_cover_artwork_visible() && m_rect_artwork.right > m_rect_artwork.left)
      ? m_rect_artwork.right
      : panel_rect.left;
  RECT cache_rect = {art_right, panel_rect.top, panel_rect.right, panel_rect.bottom};
  int cache_w = cache_rect.right - cache_rect.left;
  int cache_h = cache_rect.bottom - cache_rect.top;
  if (cache_w <= 0 || cache_h <= 0) return;

  // 1. Ensure clean background cache exists
  if (!m_spectrum_bg_cache_valid || !m_spectrum_bg_hdc ||
      m_spectrum_bg_cache_cx != cache_w || m_spectrum_bg_cache_cy != cache_h) {
    if (m_spectrum_bg_cache_cx != cache_w || m_spectrum_bg_cache_cy != cache_h || !m_spectrum_bg_hdc) {
      destroy_spectrum_bg_cache();
      m_spectrum_bg_hdc = CreateCompatibleDC(hdc);
      m_spectrum_bg_hbitmap = CreateCompatibleBitmap(hdc, cache_w, cache_h);
      m_spectrum_bg_old = (HBITMAP)SelectObject(m_spectrum_bg_hdc, m_spectrum_bg_hbitmap);
      m_spectrum_bg_cache_cx = cache_w;
      m_spectrum_bg_cache_cy = cache_h;
    }

    // Render clean background DIRECTLY into m_spectrum_bg_hdc — NOT onto hdc/m_cache_dc!
    Gdiplus::Graphics g(m_spectrum_bg_hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeNone);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeNone);
    g.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
    g.TranslateTransform((float)-art_right, (float)-panel_rect.top);
    draw_background(g, panel_rect);
    g.Flush();

    m_spectrum_bg_cache_valid = true;
  }

  // 2. Restore clean background for the entire center column (spectrum + playback buttons full height)
  int clean_left = m_rect_spectrum_full.left;
  int clean_right = m_rect_spectrum_full.right;
  if (m_rect_button_row.right > m_rect_button_row.left) {
    if (m_rect_button_row.left < clean_left) clean_left = m_rect_button_row.left;
    if (m_rect_button_row.right > clean_right) clean_right = m_rect_button_row.right;
  }

  bool seekbar_vis = get_nowbar_seekbar_visible();
  int clean_top = (seekbar_vis && m_rect_thin_progress.bottom > panel_rect.top)
                      ? m_rect_thin_progress.bottom
                      : panel_rect.top;

  if (clean_right > clean_left && panel_rect.bottom > clean_top) {
    BitBlt(hdc, clean_left, clean_top,
           clean_right - clean_left,
           panel_rect.bottom - clean_top,
           m_spectrum_bg_hdc,
           clean_left - art_right,
           clean_top - panel_rect.top,
           SRCCOPY);
  }

  if (seekbar_vis) {
    if (get_nowbar_playback_time_visible() &&
        m_rect_time.right > m_rect_time.left &&
        m_rect_time.bottom > m_rect_time.top) {
      int right_clean_left = m_rect_time.left;
      auto check_left = [&](const RECT& r, int pad_left = 0) {
        if (r.right > r.left) {
          int candidate = r.left - pad_left;
          if (candidate < right_clean_left) {
            right_clean_left = candidate;
          }
        }
      };

      // Custom buttons expand hover circle by 5% plus antialiasing fringe
      auto check_cbutton = [&](const RECT& r) {
        if (r.right > r.left) {
          int cw = r.right - r.left;
          int expand = cw * 5 / 100;
          int pad = expand + static_cast<int>(3 * m_dpi_scale);
          check_left(r, pad);
        }
      };
      check_cbutton(m_rect_cbutton1);
      check_cbutton(m_rect_cbutton2);
      check_cbutton(m_rect_cbutton3);
      check_cbutton(m_rect_cbutton4);
      check_cbutton(m_rect_cbutton5);
      check_cbutton(m_rect_cbutton6);

      // Volume icon hover circle uses diameter = rect height 'h' centered on icon
      int vol_pad = static_cast<int>(3 * m_dpi_scale);
      if (m_rect_volume.right > m_rect_volume.left && m_rect_volume.bottom > m_rect_volume.top) {
        int v_h = m_rect_volume.bottom - m_rect_volume.top;
        int v_icon_size = static_cast<int>(23 * m_dpi_scale * m_size_scale);
        if (v_h > v_icon_size) {
          vol_pad += (v_h - v_icon_size) / 2;
        }
      }
      check_left(m_rect_volume, vol_pad);

      // MiniPlayer button hover circle antialiasing margin
      check_left(m_rect_miniplayer, static_cast<int>(3 * m_dpi_scale));

      // Clamp to spectrum background cache boundaries
      right_clean_left = std::max(right_clean_left, static_cast<int>(art_right));
      right_clean_left = std::min(right_clean_left, static_cast<int>(panel_rect.right));

      int time_clean_w = panel_rect.right - right_clean_left;
      int time_clean_h = panel_rect.bottom - clean_top;
      if (time_clean_w > 0 && time_clean_h > 0) {
        BitBlt(hdc, right_clean_left, clean_top,
               time_clean_w, time_clean_h,
               m_spectrum_bg_hdc,
               right_clean_left - art_right,
               clean_top - panel_rect.top,
               SRCCOPY);
      }
    }
  }

  // 3. Spectrum bars rendered via direct pixel writes (or curve GDI+ path) + AlphaBlend
  draw_full_spectrum(hdc);

  // 4. Render center playback controls and right controls (playback timer rendered behind buttons/icons)
  {
    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBilinear);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

    draw_playback_buttons(g, false);

    if (seekbar_vis && get_nowbar_playback_time_visible()) {
      draw_time_display_top_right(g);
      draw_custom_buttons(g);
      draw_volume(g);
      draw_miniplayer_button(g);
    }
  }
}

void ControlPanelCore::paint_waveform_only(HDC hdc, const RECT& panel_rect) {
    update_layout(panel_rect);

    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeNone);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeNone);
    g.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);

    // Clip to waveform + time display regions only
    Gdiplus::Region clip;
    clip.MakeEmpty();
    clip.Union(Gdiplus::Rect(m_rect_waveform.left, m_rect_waveform.top,
        m_rect_waveform.right - m_rect_waveform.left,
        m_rect_waveform.bottom - m_rect_waveform.top));
    clip.Union(Gdiplus::Rect(m_rect_time.left, m_rect_time.top,
        m_rect_time.right - m_rect_time.left,
        m_rect_time.bottom - m_rect_time.top));
    g.SetClip(&clip);

    // Redraw background within clipped areas
    draw_background(g, panel_rect);

    g.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBilinear);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

    draw_waveform_bar(g);
    draw_playback_buttons(g);
    draw_waveform_tooltip(g);
    draw_time_display(g);

    // Volume icon is NOT redrawn here — it is already in the cache bitmap
    // from the last full paint, and any volume change triggers a full repaint
    // via invalidate().  Drawing it again would overdraw anti-aliased edges,
    // making the icon appear briefly bold on track change.

    // Kill orphaned animation timer from a previous spectrum mode session,
    // but keep it alive if the waveform reveal animation is still in progress.
    if (m_animation_timer_active && !m_waveform_animating) {
        DeleteTimerQueueTimer(nullptr, m_anim_tp_timer, nullptr);
        m_anim_tp_timer = nullptr;
        m_animation_timer_active = false;
        m_animation_requested = false;
    }
}

float ControlPanelCore::get_hover_opacity(HitRegion region) {
  if (region == HitRegion::None) return 0.0f;
  
  // Hover effects are instant (no fade animation) for performance
  // The Smooth Animations setting does not affect hover effects
  return (region == m_hover_region) ? 1.0f : 0.0f;
}

void ControlPanelCore::draw_background(Gdiplus::Graphics &g, const RECT &rect) {
  int width = rect.right - rect.left;
  int height = rect.bottom - rect.top;
  Gdiplus::Rect r(rect.left, rect.top, width, height);
  
  int bg_style = get_nowbar_background_style();

  // Detect if background style or content has changed (needs transition)
  bool style_changed = (m_prev_bg_style != -1 && m_prev_bg_style != bg_style);
  bool size_changed = (m_prev_background && 
                       (m_prev_background_size.cx != width || m_prev_background_size.cy != height));
  
  // Start transition if style changed and we have a cached previous background
  // Only start if smooth animations are enabled and no transition is already running
  if (style_changed && m_prev_background && !size_changed &&
      get_nowbar_smooth_animations_enabled() && !m_bg_transition_active) {
    m_bg_transition_active = true;
    m_bg_transition_start_time = std::chrono::steady_clock::now();
  } else if (style_changed && !get_nowbar_smooth_animations_enabled()) {
    // Smooth animations disabled - skip transition
    m_prev_background.reset();
    m_bg_transition_active = false;
  }
  
  // If size changed, invalidate cache (can't crossfade different sizes)
  if (size_changed) {
    m_prev_background.reset();
    m_bg_transition_active = false;
  }

  
  // Calculate transition progress if active
  float transition_alpha = 1.0f;  // 1.0 = fully new background
  if (m_bg_transition_active) {
    auto now = std::chrono::steady_clock::now();
    float elapsed_ms = std::chrono::duration<float, std::milli>(now - m_bg_transition_start_time).count();
    float progress = std::min(1.0f, elapsed_ms / BG_TRANSITION_DURATION_MS);
    
    // Ease-out interpolation for smooth deceleration
    float ease = 1.0f - (1.0f - progress) * (1.0f - progress);
    transition_alpha = ease;
    
    if (progress >= 1.0f) {
      m_bg_transition_active = false;
    }
  }
  
  // Lambda to draw current background content to a Graphics context
  // use_local_coords: true when drawing to a cache bitmap (0,0 origin), false for screen coordinates
  auto draw_current_bg = [&](Gdiplus::Graphics& target, bool use_local_coords = false) {
    int draw_x = use_local_coords ? 0 : rect.left;
    int draw_y = use_local_coords ? 0 : rect.top;
    Gdiplus::Rect draw_rect(draw_x, draw_y, width, height);
    
    if (bg_style == 1 && m_artwork_colors_valid) {
      // Artwork Colors mode: horizontal gradient
      Gdiplus::LinearGradientBrush gradientBrush(
          Gdiplus::Point(draw_x, 0),
          Gdiplus::Point(draw_x + width, 0),
          m_artwork_color_secondary,
          m_artwork_color_primary
      );
      target.FillRectangle(&gradientBrush, draw_rect);
      
      BYTE overlay_alpha = m_dark_mode ? 120 : 80;
      Gdiplus::Color overlayColor(overlay_alpha, 0, 0, 0);
      Gdiplus::SolidBrush overlayBrush(overlayColor);
      target.FillRectangle(&overlayBrush, draw_rect);
    } else if (bg_style == 2 && m_artwork_thumbnail) {
      // Blurred Artwork mode
      if (!m_blurred_artwork || 
          m_blurred_artwork_size.cx != width || 
          m_blurred_artwork_size.cy != height) {
        const_cast<ControlPanelCore*>(this)->create_blurred_artwork(width, height);
      }
      
      if (m_blurred_artwork) {
        target.DrawImage(m_blurred_artwork.get(), draw_x, draw_y, width, height);
      }
      
      BYTE overlay_alpha = m_dark_mode ? 140 : 180;
      Gdiplus::Color overlayColor(overlay_alpha, 0, 0, 0);
      Gdiplus::SolidBrush overlayBrush(overlayColor);
      target.FillRectangle(&overlayBrush, draw_rect);
    } else {
      // Solid background
      Gdiplus::SolidBrush brush(m_bg_color);
      target.FillRectangle(&brush, draw_rect);
    }
  };

  // Draw with transition if active
  if (m_bg_transition_active && m_prev_background && transition_alpha < 1.0f) {
    // 1. Ensure target background cache exists and is up to date
    bool target_needs_update = (!m_target_background || 
                                m_target_background_size.cx != width || 
                                m_target_background_size.cy != height);
    
    if (target_needs_update) {
      m_target_background.reset(new Gdiplus::Bitmap(width, height, PixelFormat32bppARGB));
      m_target_background->SetResolution(96.0f * m_dpi_scale, 96.0f * m_dpi_scale);
      m_target_background_size = {(LONG)width, (LONG)height};
      
      Gdiplus::Graphics target_g(m_target_background.get());
      // Use aliased mode to prevent anti-aliased edges from being baked into cache
      target_g.SetSmoothingMode(Gdiplus::SmoothingModeNone);
      target_g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeNone);
      target_g.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
      
      // Draw current background content to target cache ONCE (use local coords for bitmap)
      draw_current_bg(target_g, true);
    }

    // 2. Draw cached previous background (opaque) — explicit dimensions
    // to avoid DPI metadata mismatch leaving gaps at panel edges
    g.DrawImage(m_prev_background.get(), r.X, r.Y, width, height);
    
    // 3. Draw cached target background (with alpha) - fast blit
    Gdiplus::ColorMatrix cm = {
      1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
      0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
      0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
      0.0f, 0.0f, 0.0f, transition_alpha, 0.0f,
      0.0f, 0.0f, 0.0f, 0.0f, 1.0f
    };
    Gdiplus::ImageAttributes ia;
    ia.SetColorMatrix(&cm);
    
    g.DrawImage(m_target_background.get(), r, 0, 0, width, height, Gdiplus::UnitPixel, &ia);
    
    // Track whether background animation is still in progress
    // The centralized animation loop in paint() will request the next frame
    m_bg_animating = m_bg_transition_active;
  } else {
    // No transition - use cached background if available for performance
    m_bg_animating = false;  // No transition in progress
    
    if (m_bg_cache_valid && m_prev_background && 
        m_prev_background_size.cx == width && 
        m_prev_background_size.cy == height) {
      // Draw from cache - much faster than regenerating.
      // Explicit dimensions prevent DPI metadata mismatch gaps at panel edges.
      g.DrawImage(m_prev_background.get(), r.X, r.Y, width, height);
    } else {
      // Cache miss - draw directly (will be cached below)
      draw_current_bg(g);
    }
  }
  
  // Cache current background for next transition (if not already transitioning)
  // Only cache when stable (not during transition) AND cache needs updating
  if (!m_bg_transition_active) {
    // Check if cache needs to be created or resized
    bool size_mismatch = (!m_prev_background || 
                          m_prev_background_size.cx != width || 
                          m_prev_background_size.cy != height);
    
    // Only re-render cache if it's invalid or size changed
    if (!m_bg_cache_valid || size_mismatch) {
      // Create or resize cache bitmap if needed
      if (size_mismatch) {
        m_prev_background.reset(new Gdiplus::Bitmap(width, height, PixelFormat32bppARGB));
        m_prev_background->SetResolution(96.0f * m_dpi_scale, 96.0f * m_dpi_scale);
        m_prev_background_size = {(LONG)width, (LONG)height};
      }
      
      // Render current background to cache
      {
        Gdiplus::Graphics cache_g(m_prev_background.get());
        // Use aliased mode to prevent anti-aliased edges from being baked into cache
        cache_g.SetSmoothingMode(Gdiplus::SmoothingModeNone);
        cache_g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeNone);
        cache_g.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
        
        Gdiplus::Rect cache_r(0, 0, width, height);
        
        if (bg_style == 1 && m_artwork_colors_valid) {
          Gdiplus::LinearGradientBrush gradientBrush(
              Gdiplus::Point(0, 0),
              Gdiplus::Point(width, 0),
              m_artwork_color_secondary,
              m_artwork_color_primary
          );
          cache_g.FillRectangle(&gradientBrush, cache_r);
          
          BYTE overlay_alpha = m_dark_mode ? 120 : 80;
          Gdiplus::Color overlayColor(overlay_alpha, 0, 0, 0);
          Gdiplus::SolidBrush overlayBrush(overlayColor);
          cache_g.FillRectangle(&overlayBrush, cache_r);
        } else if (bg_style == 2 && m_artwork_thumbnail) {
          // Ensure blurred artwork is created if it doesn't exist or needs resize
          if (!m_blurred_artwork || 
              m_blurred_artwork_size.cx != width || 
              m_blurred_artwork_size.cy != height) {
            create_blurred_artwork(width, height);
          }
          if (m_blurred_artwork) {
            cache_g.DrawImage(m_blurred_artwork.get(), 0, 0);
          }
          BYTE overlay_alpha = m_dark_mode ? 140 : 180;
          Gdiplus::Color overlayColor(overlay_alpha, 0, 0, 0);
          Gdiplus::SolidBrush overlayBrush(overlayColor);
          cache_g.FillRectangle(&overlayBrush, cache_r);
        } else {
          Gdiplus::SolidBrush brush(m_bg_color);
          cache_g.FillRectangle(&brush, cache_r);
        }
      }
      
      m_bg_cache_valid = true;  // Mark cache as valid
      m_prev_bg_style = bg_style;
    }
  }
}

void ControlPanelCore::draw_artwork(Gdiplus::Graphics &g) {
  int w = m_rect_artwork.right - m_rect_artwork.left;
  int h = m_rect_artwork.bottom - m_rect_artwork.top;

  bool rounded = (get_nowbar_cover_style() == 1);
  float radius = rounded ? 8.0f * m_dpi_scale : 0.0f;

  if (rounded) {
    // Draw artwork into an off-screen bitmap, then composite onto the panel
    // using FillPath with a TextureBrush. FillPath respects SmoothingModeAntiAlias,
    // giving smooth rounded corners — unlike SetClip which is always binary/aliased.
    Gdiplus::Bitmap offscreen(w, h, PixelFormat32bppPARGB);
    {
      Gdiplus::Graphics og(&offscreen);
      og.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
      og.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);

      if (m_artwork_thumbnail) {
        int srcW = m_artwork_thumbnail->GetWidth();
        int srcH = m_artwork_thumbnail->GetHeight();
        float srcAspect = (float)srcW / (float)srcH;
        float dstAspect = (float)w / (float)h;
        int cropX, cropY, cropW, cropH;
        if (srcAspect > dstAspect) {
          cropH = srcH;
          cropW = (int)(srcH * dstAspect);
          cropX = (srcW - cropW) / 2;
          cropY = 0;
        } else {
          cropW = srcW;
          cropH = (int)(srcW / dstAspect);
          cropX = 0;
          cropY = (srcH - cropH) / 2;
        }
        Gdiplus::Rect destRect(0, 0, w, h);
        og.DrawImage(m_artwork_thumbnail.get(), destRect, cropX, cropY, cropW, cropH,
                     Gdiplus::UnitPixel);
      } else {
        Gdiplus::Color placeholderBg = m_dark_mode
            ? Gdiplus::Color(255, 60, 60, 60)
            : Gdiplus::Color(255, 200, 200, 200);
        Gdiplus::SolidBrush brush(placeholderBg);
        og.FillRectangle(&brush, 0, 0, w, h);

        bool is_stream = false;
        metadb_handle_ptr art_track = get_display_track();
        if (art_track.is_valid()) {
          const char* path = art_track->get_path();
          is_stream = (strstr(path, "http://") == path) ||
                      (strstr(path, "https://") == path) ||
                      (strstr(path, "mms://") == path) ||
                      (strstr(path, "rtsp://") == path) ||
                      (strstr(path, "rtmp://") == path);
        }

        if (is_stream) {
          RECT iconRect = {0, 0, w, h};
          draw_radio_icon(og, iconRect, m_text_secondary_color);
        } else {
          Gdiplus::SolidBrush iconBrush(m_text_secondary_color);
          Gdiplus::Font iconFont(L"Segoe MDL2 Assets", 24.0f * m_dpi_scale,
                                 Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
          Gdiplus::StringFormat sf;
          sf.SetAlignment(Gdiplus::StringAlignmentCenter);
          sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
          Gdiplus::RectF artRect(0.0f, 0.0f, (float)w, (float)h);
          og.DrawString(L"\uE8D6", -1, &iconFont, artRect, &sf, &iconBrush);
        }
      }
    }

    // Build rounded-rect path in panel coordinates
    float x = (float)m_rect_artwork.left;
    float y = (float)m_rect_artwork.top;
    float fw = (float)w;
    float fh = (float)h;
    float d = radius * 2.0f;
    Gdiplus::GraphicsPath roundedPath;
    roundedPath.AddArc(x, y, d, d, 180, 90);
    roundedPath.AddArc(x + fw - d, y, d, d, 270, 90);
    roundedPath.AddArc(x + fw - d, y + fh - d, d, d, 0, 90);
    roundedPath.AddArc(x, y + fh - d, d, d, 90, 90);
    roundedPath.CloseFigure();

    // Use TextureBrush to paint the offscreen bitmap through FillPath,
    // which applies anti-aliasing to the path edges for smooth corners.
    Gdiplus::TextureBrush texBrush(&offscreen, Gdiplus::WrapModeClamp);
    texBrush.TranslateTransform((float)m_rect_artwork.left, (float)m_rect_artwork.top);
    Gdiplus::SmoothingMode prevSmoothing = g.GetSmoothingMode();
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.FillPath(&texBrush, &roundedPath);
    g.SetSmoothingMode(prevSmoothing);
  } else {
    // Square mode — draw directly, no offscreen bitmap needed
    if (m_artwork_thumbnail) {
      int srcW = m_artwork_thumbnail->GetWidth();
      int srcH = m_artwork_thumbnail->GetHeight();
      float srcAspect = (float)srcW / (float)srcH;
      float dstAspect = (float)w / (float)h;
      int cropX, cropY, cropW, cropH;
      if (srcAspect > dstAspect) {
        cropH = srcH;
        cropW = (int)(srcH * dstAspect);
        cropX = (srcW - cropW) / 2;
        cropY = 0;
      } else {
        cropW = srcW;
        cropH = (int)(srcW / dstAspect);
        cropX = 0;
        cropY = (srcH - cropH) / 2;
      }
      Gdiplus::Rect destRect(m_rect_artwork.left, m_rect_artwork.top, w, h);
      g.DrawImage(m_artwork_thumbnail.get(), destRect, cropX, cropY, cropW, cropH,
                  Gdiplus::UnitPixel);
    } else {
      Gdiplus::Color placeholderBg = m_dark_mode
          ? Gdiplus::Color(255, 60, 60, 60)
          : Gdiplus::Color(255, 200, 200, 200);
      Gdiplus::SolidBrush brush(placeholderBg);
      Gdiplus::Rect artR(m_rect_artwork.left, m_rect_artwork.top, w, h);
      g.FillRectangle(&brush, artR);

      bool is_stream = false;
      metadb_handle_ptr art_track = get_display_track();
      if (art_track.is_valid()) {
        const char* path = art_track->get_path();
        is_stream = (strstr(path, "http://") == path) ||
                    (strstr(path, "https://") == path) ||
                    (strstr(path, "mms://") == path) ||
                    (strstr(path, "rtsp://") == path) ||
                    (strstr(path, "rtmp://") == path);
      }

      if (is_stream) {
        RECT iconRect = m_rect_artwork;
        draw_radio_icon(g, iconRect, m_text_secondary_color);
      } else {
        Gdiplus::SolidBrush iconBrush(m_text_secondary_color);
        Gdiplus::Font iconFont(L"Segoe MDL2 Assets", 24.0f * m_dpi_scale,
                               Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        Gdiplus::StringFormat sf;
        sf.SetAlignment(Gdiplus::StringAlignmentCenter);
        sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        Gdiplus::RectF artRect((float)m_rect_artwork.left,
                               (float)m_rect_artwork.top, (float)w, (float)h);
        g.DrawString(L"\uE8D6", -1, &iconFont, artRect, &sf, &iconBrush);
      }
    }
  }
}

void ControlPanelCore::draw_track_info(Gdiplus::Graphics &g) {
  // Set clipping region to prevent text overflow beyond boundaries.
  Gdiplus::Rect clipRect(
      m_rect_track_info.left,
      m_rect_track_info.top,
      m_rect_track_info.right - m_rect_track_info.left,
      m_rect_track_info.bottom - m_rect_track_info.top
  );
  g.SetClip(clipRect);
  
  // When artwork-based backgrounds are active, always use white text for readability
  // (both Artwork Colors and Blurred Artwork apply dark overlays)
  int bg_style = get_nowbar_background_style();
  bool use_light_foreground = (bg_style == 1 && m_artwork_colors_valid) || 
                              (bg_style == 2 && m_blurred_artwork);
  
  Gdiplus::Color textColor = use_light_foreground ? Gdiplus::Color(255, 255, 255, 255) : m_text_color;
  Gdiplus::Color textSecondary = use_light_foreground ? Gdiplus::Color(255, 200, 200, 200) : m_text_secondary_color;

  // Apply custom font colors if set
  COLORREF custom_color;
  if (get_nowbar_track_font_color(custom_color))
      textColor = Gdiplus::Color(255, GetRValue(custom_color), GetGValue(custom_color), GetBValue(custom_color));
  if (get_nowbar_artist_font_color(custom_color))
      textSecondary = Gdiplus::Color(255, GetRValue(custom_color), GetGValue(custom_color), GetBValue(custom_color));

  Gdiplus::SolidBrush titleBrush(textColor);
  Gdiplus::SolidBrush artistBrush(textSecondary);

  // Use actual font heights for text layout rects
  int title_h = m_title_font_height > 0
      ? m_title_font_height
      : static_cast<int>(m_metrics.text_height);
  int artist_h = m_artist_font_height > 0
      ? m_artist_font_height
      : static_cast<int>(m_metrics.text_height);
  int text_gap = static_cast<int>(4 * m_dpi_scale);

  float text_w = (float)(m_rect_track_info.right - m_rect_track_info.left);
  float title_w = text_w;

  Gdiplus::RectF titleRect(
      (float)m_rect_track_info.left, (float)m_rect_track_info.top,
      title_w,
      (float)title_h);

  Gdiplus::RectF artistRect(
      (float)m_rect_track_info.left,
      (float)(m_rect_track_info.top + title_h + text_gap),
      text_w,
      (float)artist_h);

  Gdiplus::StringFormat sf(Gdiplus::StringFormat::GenericTypographic());
  sf.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
  sf.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap | Gdiplus::StringFormatFlagsMeasureTrailingSpaces);

  // Use formatted strings from title formatting
  std::wstring line1 = utf8_to_wide(m_formatted_line1.c_str());
  std::wstring line2 = utf8_to_wide(m_formatted_line2.c_str());
  std::wstring line3 = utf8_to_wide(m_formatted_line3.c_str());

  g.DrawString(line1.c_str(), -1, m_font_title.get(), titleRect, &sf,
               &titleBrush);
  if (!line2.empty()) {
    g.DrawString(line2.c_str(), -1, m_font_artist.get(), artistRect, &sf,
                 &artistBrush);
  }

  bool rating_on_line3_draw = (get_nowbar_rating_mode() == 2);
  if (!line3.empty() && !rating_on_line3_draw && m_line3_visible) {
    int line3_font_h = m_line3_font_height > 0
        ? m_line3_font_height
        : static_cast<int>(m_metrics.text_height);

    // Apply custom Line 3 font color if set, otherwise use artist/secondary color
    Gdiplus::SolidBrush* line3BrushPtr = &artistBrush;
    COLORREF line3_custom_color;
    Gdiplus::SolidBrush line3CustomBrush(Gdiplus::Color(0, 0, 0, 0));
    if (get_nowbar_line3_font_color(line3_custom_color)) {
        line3CustomBrush.SetColor(Gdiplus::Color(255, GetRValue(line3_custom_color), GetGValue(line3_custom_color), GetBValue(line3_custom_color)));
        line3BrushPtr = &line3CustomBrush;
    }

    Gdiplus::RectF line3Rect(
        (float)m_rect_track_info.left,
        artistRect.Y + (float)artist_h + (float)text_gap,
        text_w,
        (float)line3_font_h);
    g.DrawString(line3.c_str(), -1, m_font_line3.get(), line3Rect, &sf,
                 line3BrushPtr);
  }

  // Rating stars on Line 3 mode — draw within track info area (when a track is loaded/selected)
  if (get_nowbar_rating_mode() == 2 && m_line3_visible && get_display_track().is_valid()) {
    Gdiplus::Color ratingAccentColor(255, 255, 193, 7);  // Material Design Amber

    // Determine secondary color for unrated stars (same logic as draw_playback_buttons)
    int bg_style_local = get_nowbar_background_style();
    bool use_light_fg = (bg_style_local == 1 && m_artwork_colors_valid) ||
                        (bg_style_local == 2 && m_blurred_artwork);
    Gdiplus::Color star_secondary = use_light_fg
        ? Gdiplus::Color(255, 200, 200, 200)
        : m_text_secondary_color;

    for (int i = 0; i < 5; i++) {
      int star_num = i + 1;
      RECT star_rect = m_rect_stars[i];
      if (star_rect.right <= star_rect.left) continue;

      Gdiplus::Color star_color;
      if (m_rating_hover_star > 0) {
        star_color = (star_num <= m_rating_hover_star) ? ratingAccentColor : star_secondary;
      } else if (m_rating_value > 0) {
        star_color = (star_num <= m_rating_value) ? ratingAccentColor : star_secondary;
      } else {
        star_color = star_secondary;
      }
      draw_star_filled_icon(g, star_rect, star_color);
    }
  }

  // Reset clipping region after drawing text
  g.ResetClip();
}

void ControlPanelCore::draw_playback_buttons(Gdiplus::Graphics &g, bool include_custom_buttons) {
  bool show_hover = get_nowbar_hover_circles_enabled();
  
  // Check if hover animation is in progress (for repaint scheduling)
  auto now = std::chrono::steady_clock::now();
  float elapsed_ms = std::chrono::duration<float, std::milli>(now - m_hover_change_time).count();
  bool hover_animating = (elapsed_ms < HOVER_FADE_DURATION_MS);
  
  // Determine if we're using artwork-based background that needs light icons
  int bg_style = get_nowbar_background_style();
  bool use_light_foreground = (bg_style == 1 && m_artwork_colors_valid) || 
                              (bg_style == 2 && m_blurred_artwork);
  
  // Override icon colors for artwork-based backgrounds (they have dark overlays)
  Gdiplus::Color icon_secondary_color = use_light_foreground
      ? Gdiplus::Color(255, 200, 200, 200)  // Light gray for visibility
      : m_text_secondary_color;

  // Get custom button accent color from preferences
  COLORREF btn_accent = get_nowbar_custom_button_accent_enabled()
      ? get_nowbar_button_accent_color() : m_theme_highlight;
  Gdiplus::Color custom_accent(255, GetRValue(btn_accent), GetGValue(btn_accent), GetBValue(btn_accent));
  Gdiplus::Color icon_accent_color = custom_accent;
  Gdiplus::Color icon_hover_color;
  if (use_light_foreground) {
      icon_hover_color = Gdiplus::Color(40, 255, 255, 255);
  } else if (get_nowbar_custom_hover_color_enabled()) {
      COLORREF hc = get_nowbar_hover_color();
      icon_hover_color = Gdiplus::Color(40, GetRValue(hc), GetGValue(hc), GetBValue(hc));
  } else {
      icon_hover_color = Gdiplus::Color(40, GetRValue(m_theme_selection), GetGValue(m_theme_selection), GetBValue(m_theme_selection));
  }
  
  // Heart button (mood toggle) - only if visible
  if (get_nowbar_mood_icon_visible()) {
    bool heart_hovered = (m_hover_region == HitRegion::HeartButton);
    float heart_opacity = get_hover_opacity(HitRegion::HeartButton);
    int hw = m_rect_heart.right - m_rect_heart.left;
    int hh = m_rect_heart.bottom - m_rect_heart.top;
    if (heart_opacity > 0.01f && show_hover) {
      BYTE alpha = static_cast<BYTE>(heart_opacity * icon_hover_color.GetA());
      Gdiplus::Color hoverColor(alpha, icon_hover_color.GetR(), icon_hover_color.GetG(), icon_hover_color.GetB());
      Gdiplus::SolidBrush hoverBrush(hoverColor);
      g.FillEllipse(&hoverBrush, m_rect_heart.left, m_rect_heart.top, hw, hh);
    }
    // Red when mood is set, gray when empty
    Gdiplus::Color heartColor = m_mood_active ? Gdiplus::Color(255, 239, 83, 80)
                                              : icon_secondary_color;
    // Calculate icon inset - smaller when hovered (for enlarge effect) like other buttons
    float heart_scale = heart_hovered ? HOVER_SCALE_FACTOR : 1.0f;
    int heart_inset = static_cast<int>(hw * (1.0f - 0.70f * heart_scale) / 2.0f);
    RECT heartIconRect = {
        m_rect_heart.left + heart_inset, m_rect_heart.top + heart_inset,
        m_rect_heart.right - heart_inset, m_rect_heart.bottom - heart_inset};
    draw_heart_icon(g, heartIconRect, heartColor);
  }

  // Rating stars - 5 clickable stars (not drawn here in Line 3 mode)
  if (get_nowbar_rating_visible() && get_nowbar_rating_mode() != 2) {
    // Gold color for rated/hovered stars
    Gdiplus::Color ratingAccentColor(255, 255, 193, 7);  // Material Design Amber

    for (int i = 0; i < 5; i++) {
      int star_num = i + 1;  // 1-based star number
      RECT star_rect = m_rect_stars[i];
      if (star_rect.right <= star_rect.left) continue;

      // Determine star color: accent for rated/hovered stars, secondary for the rest
      Gdiplus::Color star_color;

      if (m_rating_hover_star > 0) {
        // Hover preview: accent for stars 1..hover_star, secondary for the rest
        star_color = (star_num <= m_rating_hover_star) ? ratingAccentColor : icon_secondary_color;
      } else if (m_rating_value > 0) {
        // Rated: accent for stars 1..rating, secondary for the rest
        star_color = (star_num <= m_rating_value) ? ratingAccentColor : icon_secondary_color;
      } else {
        // Unrated: all 5 in secondary color
        star_color = icon_secondary_color;
      }

      draw_star_filled_icon(g, star_rect, star_color);
    }
  }

  // Shuffle button
  if (get_nowbar_shuffle_icon_visible()) {
    bool shuffle_active = (m_state.playback_order == 4); // Shuffle tracks
    bool shuffle_hovered = (m_hover_region == HitRegion::ShuffleButton);
    float shuffle_opacity = get_hover_opacity(HitRegion::ShuffleButton);

    int sw = m_rect_shuffle.right - m_rect_shuffle.left;
    int sh = m_rect_shuffle.bottom - m_rect_shuffle.top;
    if (shuffle_opacity > 0.01f && show_hover) {
      BYTE alpha = static_cast<BYTE>(shuffle_opacity * icon_hover_color.GetA());
      Gdiplus::Color hoverColor(alpha, icon_hover_color.GetR(), icon_hover_color.GetG(), icon_hover_color.GetB());
      Gdiplus::SolidBrush hoverBrush(hoverColor);
      g.FillEllipse(&hoverBrush, m_rect_shuffle.left, m_rect_shuffle.top, sw, sh);
    }
    Gdiplus::Color shuffleColor =
        shuffle_active ? icon_accent_color : icon_secondary_color;
    float shuffle_scale = shuffle_hovered ? HOVER_SCALE_FACTOR : 1.0f;
    int shuffle_icon_inset = static_cast<int>(sw * (1.0f - 0.70f * shuffle_scale) / 2.0f);
    RECT shuffleIconRect = {
        m_rect_shuffle.left + shuffle_icon_inset, m_rect_shuffle.top + shuffle_icon_inset,
        m_rect_shuffle.right - shuffle_icon_inset, m_rect_shuffle.bottom - shuffle_icon_inset};
    draw_shuffle_icon(g, shuffleIconRect, shuffleColor);
  }

  // Previous button
  bool prev_hovered = (m_hover_region == HitRegion::PrevButton);
  float prev_opacity = get_hover_opacity(HitRegion::PrevButton);
  int pw = m_rect_prev.right - m_rect_prev.left;
  int ph = m_rect_prev.bottom - m_rect_prev.top;
  if (prev_opacity > 0.01f && show_hover) {
    BYTE alpha = static_cast<BYTE>(prev_opacity * icon_hover_color.GetA());
    Gdiplus::Color hoverColor(alpha, icon_hover_color.GetR(), icon_hover_color.GetG(), icon_hover_color.GetB());
    Gdiplus::SolidBrush hoverBrush(hoverColor);
    g.FillEllipse(&hoverBrush, m_rect_prev.left, m_rect_prev.top, pw, ph);
  }
  // Enlarge icon when hovered
  float prev_scale = prev_hovered ? HOVER_SCALE_FACTOR : 1.0f;
  int prev_icon_inset = static_cast<int>(pw * (1.0f - 0.85f * prev_scale) / 2.0f);
  RECT prevIconRect = {
      m_rect_prev.left + prev_icon_inset, m_rect_prev.top + prev_icon_inset,
      m_rect_prev.right - prev_icon_inset, m_rect_prev.bottom - prev_icon_inset};
  draw_prev_icon(g, prevIconRect, icon_secondary_color);

  // Play/Pause button (circular with background)
  bool play_hovered = (m_hover_region == HitRegion::PlayButton);
  int play_w = m_rect_play.right - m_rect_play.left;
  int play_h = m_rect_play.bottom - m_rect_play.top;
  
  // Calculate enlarged rect when hovered
  float play_scale = play_hovered ? HOVER_SCALE_FACTOR : 1.0f;
  int play_inset = static_cast<int>(play_w * (1.0f - play_scale) / 2.0f);
  RECT playRect = {
      m_rect_play.left + play_inset, m_rect_play.top + play_inset,
      m_rect_play.right - play_inset, m_rect_play.bottom - play_inset};
  int play_rect_w = playRect.right - playRect.left;
  int play_rect_h = playRect.bottom - playRect.top;

  int icon_style = get_nowbar_alternate_icons_style();

  if (icon_style == 1) {
    // Style 2: no background circle, outline icons
    float play_opacity = get_hover_opacity(HitRegion::PlayButton);
    if (play_opacity > 0.01f && show_hover) {
      BYTE alpha = static_cast<BYTE>(play_opacity * icon_hover_color.GetA());
      Gdiplus::Color hoverColor(alpha, icon_hover_color.GetR(), icon_hover_color.GetG(), icon_hover_color.GetB());
      Gdiplus::SolidBrush hoverBrush(hoverColor);
      g.FillEllipse(&hoverBrush, playRect.left, playRect.top, play_rect_w, play_rect_h);
    }
    if (m_state.is_playing && !m_state.is_paused) {
      draw_alternate_pause_icon(g, playRect, icon_secondary_color);
    } else {
      draw_alternate_play_icon(g, playRect, icon_secondary_color);
    }
  } else if (icon_style == 2) {
    // Style 3: no background circle, solid filled icons (Material Icons style)
    float play_opacity = get_hover_opacity(HitRegion::PlayButton);
    if (play_opacity > 0.01f && show_hover) {
      BYTE alpha = static_cast<BYTE>(play_opacity * icon_hover_color.GetA());
      Gdiplus::Color hoverColor(alpha, icon_hover_color.GetR(), icon_hover_color.GetG(), icon_hover_color.GetB());
      Gdiplus::SolidBrush hoverBrush(hoverColor);
      g.FillEllipse(&hoverBrush, playRect.left, playRect.top, play_rect_w, play_rect_h);
    }
    if (m_state.is_playing && !m_state.is_paused) {
      draw_style3_pause_icon(g, playRect, icon_secondary_color);
    } else {
      draw_style3_play_icon(g, playRect, icon_secondary_color);
    }
  } else {
    // Style 1 (default): background circle + filled icon
    // Normal (style 0): accent circle, dark icon
    // Inverted (style 1): accent circle, white icon
    bool inverted = (get_nowbar_play_icon_style() == 1);
    COLORREF play_accent = get_nowbar_custom_play_accent_enabled()
        ? get_nowbar_play_accent_color() : m_theme_highlight;
    Gdiplus::Color bgColor;
    if (play_hovered && show_hover) {
      // Use hover color settings (same as other buttons but fully opaque)
      if (get_nowbar_custom_hover_color_enabled()) {
        COLORREF hc = get_nowbar_hover_color();
        bgColor = Gdiplus::Color(255, GetRValue(hc), GetGValue(hc), GetBValue(hc));
      } else {
        bgColor = Gdiplus::Color(255, GetRValue(m_theme_selection), GetGValue(m_theme_selection), GetBValue(m_theme_selection));
      }
    } else {
      bgColor = Gdiplus::Color(255, GetRValue(play_accent), GetGValue(play_accent), GetBValue(play_accent));
    }
    Gdiplus::SolidBrush bgBrush(bgColor);
    g.FillEllipse(&bgBrush, playRect.left, playRect.top, play_rect_w, play_rect_h);

    // Icon color: inverted swaps black/white
    Gdiplus::Color playIconColor = inverted
        ? Gdiplus::Color(255, 255, 255, 255)
        : Gdiplus::Color(255, 0, 0, 0);
    if (m_state.is_playing && !m_state.is_paused) {
      draw_pause_icon(g, playRect, playIconColor, false);
    } else {
      draw_play_icon(g, playRect, playIconColor, false);
    }
  }

  // Next button
  bool next_hovered = (m_hover_region == HitRegion::NextButton);
  float next_opacity = get_hover_opacity(HitRegion::NextButton);
  int nw = m_rect_next.right - m_rect_next.left;
  int nh = m_rect_next.bottom - m_rect_next.top;
  if (next_opacity > 0.01f && show_hover) {
    BYTE alpha = static_cast<BYTE>(next_opacity * icon_hover_color.GetA());
    Gdiplus::Color hoverColor(alpha, icon_hover_color.GetR(), icon_hover_color.GetG(), icon_hover_color.GetB());
    Gdiplus::SolidBrush hoverBrush(hoverColor);
    g.FillEllipse(&hoverBrush, m_rect_next.left, m_rect_next.top, nw, nh);
  }
  // Enlarge icon when hovered
  float next_scale = next_hovered ? HOVER_SCALE_FACTOR : 1.0f;
  int next_icon_inset = static_cast<int>(nw * (1.0f - 0.85f * next_scale) / 2.0f);
  RECT nextIconRect = {
      m_rect_next.left + next_icon_inset, m_rect_next.top + next_icon_inset,
      m_rect_next.right - next_icon_inset, m_rect_next.bottom - next_icon_inset};
  draw_next_icon(g, nextIconRect, icon_secondary_color);

  // Stop button (optional - positioned after Next, always outline style, no background circle)
  if (get_nowbar_stop_icon_visible()) {
    bool stop_hovered = (m_hover_region == HitRegion::StopButton);
    int stop_w = m_rect_stop.right - m_rect_stop.left;
    int stop_h = m_rect_stop.bottom - m_rect_stop.top;
    
    // Calculate enlarged rect when hovered
    float stop_scale = stop_hovered ? HOVER_SCALE_FACTOR : 1.0f;
    int stop_inset = static_cast<int>(stop_w * (1.0f - stop_scale) / 2.0f);
    RECT stopRect = {
        m_rect_stop.left + stop_inset, m_rect_stop.top + stop_inset,
        m_rect_stop.right - stop_inset, m_rect_stop.bottom - stop_inset};
    int stop_rect_w = stopRect.right - stopRect.left;
    int stop_rect_h = stopRect.bottom - stopRect.top;

    // Draw hover circle effect if hovered
    float stop_opacity = get_hover_opacity(HitRegion::StopButton);
    if (stop_opacity > 0.01f && show_hover) {
      BYTE alpha = static_cast<BYTE>(stop_opacity * icon_hover_color.GetA());
      Gdiplus::Color hoverColor(alpha, icon_hover_color.GetR(), icon_hover_color.GetG(), icon_hover_color.GetB());
      Gdiplus::SolidBrush hoverBrush(hoverColor);
      g.FillEllipse(&hoverBrush, stopRect.left, stopRect.top, stop_rect_w, stop_rect_h);
    }
    
    // Always draw outline (not filled) stop icon
    draw_stop_icon(g, stopRect, icon_secondary_color, false);
  }

  // Stop After Current button (optional - toggle with accent color when active)
  if (get_nowbar_stop_after_current_icon_visible()) {
    bool sac_hovered = (m_hover_region == HitRegion::StopAfterCurrentButton);
    int sac_w = m_rect_stop_after_current.right - m_rect_stop_after_current.left;
    int sac_h = m_rect_stop_after_current.bottom - m_rect_stop_after_current.top;

    // Calculate enlarged rect when hovered
    float sac_scale = sac_hovered ? HOVER_SCALE_FACTOR : 1.0f;
    int sac_inset = static_cast<int>(sac_w * (1.0f - sac_scale) / 2.0f);
    RECT sacRect = {
        m_rect_stop_after_current.left + sac_inset, m_rect_stop_after_current.top + sac_inset,
        m_rect_stop_after_current.right - sac_inset, m_rect_stop_after_current.bottom - sac_inset};
    int sac_rect_w = sacRect.right - sacRect.left;
    int sac_rect_h = sacRect.bottom - sacRect.top;

    // Draw hover circle effect if hovered
    float sac_opacity = get_hover_opacity(HitRegion::StopAfterCurrentButton);
    if (sac_opacity > 0.01f && show_hover) {
      BYTE alpha = static_cast<BYTE>(sac_opacity * icon_hover_color.GetA());
      Gdiplus::Color hoverColor(alpha, icon_hover_color.GetR(), icon_hover_color.GetG(), icon_hover_color.GetB());
      Gdiplus::SolidBrush hoverBrush(hoverColor);
      g.FillEllipse(&hoverBrush, sacRect.left, sacRect.top, sac_rect_w, sac_rect_h);
    }

    // Accent color when active, secondary when inactive
    Gdiplus::Color sacColor = m_stop_after_current_active ? icon_accent_color : icon_secondary_color;
    draw_stop_after_current_icon(g, sacRect, sacColor);
  }

  // Repeat button
  if (get_nowbar_repeat_icon_visible()) {
    bool repeat_active =
        (m_state.playback_order == 1 || m_state.playback_order == 2);
    bool repeat_one = (m_state.playback_order == 2);
    bool repeat_hovered = (m_hover_region == HitRegion::RepeatButton);
    float repeat_opacity = get_hover_opacity(HitRegion::RepeatButton);

    int rw = m_rect_repeat.right - m_rect_repeat.left;
    int rh = m_rect_repeat.bottom - m_rect_repeat.top;
    if (repeat_opacity > 0.01f && show_hover) {
      BYTE alpha = static_cast<BYTE>(repeat_opacity * icon_hover_color.GetA());
      Gdiplus::Color hoverColor(alpha, icon_hover_color.GetR(), icon_hover_color.GetG(), icon_hover_color.GetB());
      Gdiplus::SolidBrush hoverBrush(hoverColor);
      g.FillEllipse(&hoverBrush, m_rect_repeat.left, m_rect_repeat.top, rw, rh);
    }
    Gdiplus::Color repeatColor =
        repeat_active ? icon_accent_color : icon_secondary_color;
    float repeat_scale = repeat_hovered ? HOVER_SCALE_FACTOR : 1.0f;
    int repeat_icon_inset = static_cast<int>(rw * (1.0f - 0.70f * repeat_scale) / 2.0f);
    RECT repeatIconRect = {
        m_rect_repeat.left + repeat_icon_inset, m_rect_repeat.top + repeat_icon_inset,
        m_rect_repeat.right - repeat_icon_inset, m_rect_repeat.bottom - repeat_icon_inset};
    draw_repeat_icon(g, repeatIconRect, repeatColor, repeat_one);
  }

  // Super button (cosmetic - no functionality)
  if (get_nowbar_super_icon_visible()) {
    bool super_hovered = (m_hover_region == HitRegion::SuperButton);
    float super_opacity = get_hover_opacity(HitRegion::SuperButton);
    int supw = m_rect_super.right - m_rect_super.left;
    int suph = m_rect_super.bottom - m_rect_super.top;
    if (super_opacity > 0.01f && show_hover) {
      BYTE alpha = static_cast<BYTE>(super_opacity * icon_hover_color.GetA());
      Gdiplus::Color hoverColor(alpha, icon_hover_color.GetR(), icon_hover_color.GetG(), icon_hover_color.GetB());
      Gdiplus::SolidBrush hoverBrush(hoverColor);
      g.FillEllipse(&hoverBrush, m_rect_super.left, m_rect_super.top, supw, suph);
    }
    // Calculate icon inset - smaller when hovered (for enlarge effect) like other buttons
    float super_scale = super_hovered ? HOVER_SCALE_FACTOR : 1.0f;
    int super_inset = static_cast<int>(supw * (1.0f - 0.70f * super_scale) / 2.0f);
    RECT superIconRect = {
        m_rect_super.left + super_inset, m_rect_super.top + super_inset,
        m_rect_super.right - super_inset, m_rect_super.bottom - super_inset};
    draw_super_icon(g, superIconRect, icon_secondary_color);
  }
  
  // Hover fade animation is disabled for performance - don't drive continuous repaints
  // The hover effect still works when get_hover_opacity() is called during paint 
  // triggered by hover region changes
  m_hover_animating = false;

  if (include_custom_buttons) {
    draw_custom_buttons(g);
  }
}

void ControlPanelCore::draw_custom_buttons(Gdiplus::Graphics& g) {
  int total_enabled = 0;
  for (int i = 0; i < 6; i++) {
    if (get_nowbar_cbutton_enabled(i)) total_enabled++;
  }
  if (total_enabled == 0) return;

  bool show_hover = get_nowbar_hover_circles_enabled();

  // Determine if we're using artwork-based background that needs light icons
  int bg_style = get_nowbar_background_style();
  bool use_light_foreground = (bg_style == 1 && m_artwork_colors_valid) || 
                              (bg_style == 2 && m_blurred_artwork);

  // Override icon colors for artwork-based backgrounds (they have dark overlays)
  Gdiplus::Color icon_secondary_color = use_light_foreground
      ? Gdiplus::Color(255, 200, 200, 200)  // Light gray for visibility
      : m_text_secondary_color;

  // Get custom button accent color from preferences
  COLORREF btn_accent = get_nowbar_custom_button_accent_enabled()
      ? get_nowbar_button_accent_color() : m_theme_highlight;
  Gdiplus::Color icon_accent_color(255, GetRValue(btn_accent), GetGValue(btn_accent), GetBValue(btn_accent));
  Gdiplus::Color icon_hover_color;
  if (use_light_foreground) {
      icon_hover_color = Gdiplus::Color(40, 255, 255, 255);
  } else if (get_nowbar_custom_hover_color_enabled()) {
      COLORREF hc = get_nowbar_hover_color();
      icon_hover_color = Gdiplus::Color(40, GetRValue(hc), GetGValue(hc), GetBValue(hc));
  } else {
      icon_hover_color = Gdiplus::Color(40, GetRValue(m_theme_selection), GetGValue(m_theme_selection), GetBValue(m_theme_selection));
  }

  // Update fade animation if active
  if (get_nowbar_cbutton_autohide()) {
    bool should_hide = m_state.is_playing && !m_state.is_paused;
    float target = should_hide ? 0.0f : 1.0f;
    
    if (target != m_cbutton_target_opacity || (target != m_cbutton_opacity && !m_cbutton_fade_active)) {
      // Start new fade animation - save current opacity as starting point
      m_cbutton_start_opacity = m_cbutton_opacity;
      m_cbutton_target_opacity = target;
      m_cbutton_fade_start_time = std::chrono::steady_clock::now();
      m_cbutton_fade_active = true;
      
      // Immediately kick off the animation loop (in case no other animations are running)
      if (get_nowbar_smooth_animations_enabled()) {
        m_cbutton_animating = true;
        request_animation();
      }
    }
    
    // When smooth animations disabled, use instant opacity change
    if (!get_nowbar_smooth_animations_enabled()) {
      m_cbutton_opacity = m_cbutton_target_opacity;
      m_cbutton_fade_active = false;
    } else if (m_cbutton_fade_active) {
      auto now = std::chrono::steady_clock::now();
      float elapsed_ms = std::chrono::duration<float, std::milli>(now - m_cbutton_fade_start_time).count();
      float progress = std::min(1.0f, elapsed_ms / CBUTTON_FADE_DURATION_MS);
      
      // Ease-out interpolation for smoother animation
      float ease_progress = 1.0f - (1.0f - progress) * (1.0f - progress);
      
      // Lerp from start opacity to target opacity
      m_cbutton_opacity = m_cbutton_start_opacity + ease_progress * (m_cbutton_target_opacity - m_cbutton_start_opacity);
      
      if (progress >= 1.0f) {
        m_cbutton_opacity = m_cbutton_target_opacity;
        m_cbutton_fade_active = false;
      }
    }
  } else {
    // Auto-hide disabled, always fully visible
    m_cbutton_opacity = 1.0f;
    m_cbutton_target_opacity = 1.0f;
    m_cbutton_fade_active = false;
  }
  
  // Skip rendering if fully transparent
  if (m_cbutton_opacity <= 0.01f) {
    // Animation tracking is handled at end of function
    // Just skip drawing the buttons
    m_cbutton_animating = m_cbutton_fade_active && get_nowbar_smooth_animations_enabled();
    return;  // Don't draw any custom buttons
  }
  
  auto draw_cbutton = [&](int index, const RECT& rect, HitRegion region) {
    if (!get_nowbar_cbutton_enabled(index)) return;
    if (rect.right <= rect.left || rect.bottom <= rect.top) return;  // Skip empty rects
    bool use_3d = get_nowbar_3d_buttons_enabled();

    bool hovered = (m_hover_region == region);
    int cw = rect.right - rect.left;
    int ch = rect.bottom - rect.top;
    
    // Calculate alpha from opacity
    BYTE alpha = static_cast<BYTE>(m_cbutton_opacity * 255.0f);
    
    // Determine icon color based on command state (for fb2k actions)
    Gdiplus::Color baseIconColor = icon_secondary_color;
    bool is_fb2k_action = (get_nowbar_cbutton_action(index) == 3);
    if (is_fb2k_action) {
      const auto& state = m_cbutton_states[index];
      if (state.disabled) {
        // Dimmed appearance for disabled commands (30% opacity)
        baseIconColor = Gdiplus::Color(80, icon_secondary_color.GetR(), 
                                            icon_secondary_color.GetG(), 
                                            icon_secondary_color.GetB());
      } else if (state.checked) {
        // Accent color for active toggle commands
        baseIconColor = icon_accent_color;
      }
    }
    
    // 3D button press animation
    float press_progress = m_cbutton_press_progress[index];
    if (use_3d) {
      auto now = std::chrono::steady_clock::now();
      if (m_cbutton_pressed[index]) {
        // Pressing: ease-in to 1.0
        float elapsed = std::chrono::duration<float, std::milli>(
            now - m_cbutton_press_time[index]).count();
        float t = std::min(elapsed / CBUTTON_PRESS_DURATION_MS, 1.0f);
        press_progress = t * t;  // ease-in (quadratic)
      } else if (std::abs(m_cbutton_press_progress[index]) > 0.001f ||
                 std::chrono::duration<float, std::milli>(
                     now - m_cbutton_release_time[index]).count() < CBUTTON_RELEASE_DURATION_MS * 1.5f) {
        // Releasing: spring back with overshoot
        float elapsed = std::chrono::duration<float, std::milli>(
            now - m_cbutton_release_time[index]).count();
        float t = std::min(elapsed / CBUTTON_RELEASE_DURATION_MS, 1.5f);
        float k = 6.0f;
        // Damped spring with overshoot: goes from 1.0 -> 0.0 with brief dip below zero
        press_progress = std::exp(-t * k) * std::cos(t * k * 1.5f);
        if (std::abs(press_progress) < 0.001f && t >= 1.0f) press_progress = 0.0f;
      } else {
        press_progress = 0.0f;
      }
      m_cbutton_press_progress[index] = press_progress;

      if (m_cbutton_pressed[index] || std::abs(press_progress) > 0.001f) {
        m_cbutton_animating = true;
      }

      // Draw 3D pad
      float hover_op = get_hover_opacity(region);
      draw_3d_button_pad(g, rect, press_progress, hover_op);
    }

    if (!use_3d && hovered && show_hover && m_cbutton_opacity > 0.5f) {
      // Original flat hover circle code
      float hover_opacity = get_hover_opacity(region);
      if (hover_opacity > 0.01f) {
        // Slightly larger hover circle (5% expansion) to match perceived size of curved icons
        int expand = cw * 5 / 100;
        BYTE hover_alpha = static_cast<BYTE>(hover_opacity * icon_hover_color.GetA());
        Gdiplus::Color hoverWithAlpha(hover_alpha, icon_hover_color.GetR(), icon_hover_color.GetG(), icon_hover_color.GetB());
        Gdiplus::SolidBrush hoverBrush(hoverWithAlpha);
        g.FillEllipse(&hoverBrush, rect.left - expand, rect.top - expand, cw + expand * 2, ch + expand * 2);
      }
    }
    // Calculate icon inset - smaller when hovered (for enlarge effect) like other buttons
    float cbutton_scale = hovered ? HOVER_SCALE_FACTOR : 1.0f;
    int inset = static_cast<int>(cw * (1.0f - 0.70f * cbutton_scale) / 2.0f);
    RECT iconRect = {rect.left + inset, rect.top + inset, rect.right - inset, rect.bottom - inset};

    // Apply sink offset to icon when 3D is active
    if (use_3d && std::abs(press_progress) > 0.001f) {
      int max_sink = static_cast<int>(2.0f * m_dpi_scale);
      int sink = static_cast<int>(press_progress * max_sink);
      iconRect.top += sink;
      iconRect.bottom += sink;
    }

    // Check if a glyph character is set for this button
    pfc::string8 glyph_utf8 = resolve_unicode_notation(get_nowbar_cbutton_icon_path(index));
    if (!glyph_utf8.is_empty()) {
      // Render glyph via GDI DrawTextW for font linking (emoji/symbol fallback),
      // then composite onto GDI+ Graphics with alpha support.

      // Determine glyph color from command state
      Gdiplus::Color glyphColor;
      if (is_fb2k_action && m_cbutton_states[index].disabled) {
        glyphColor = Gdiplus::Color(static_cast<BYTE>(alpha * 0.5f),
            icon_secondary_color.GetR() * 40 / 100,
            icon_secondary_color.GetG() * 40 / 100,
            icon_secondary_color.GetB() * 40 / 100);
      } else if (is_fb2k_action && m_cbutton_states[index].checked) {
        glyphColor = Gdiplus::Color(alpha,
            icon_accent_color.GetR(), icon_accent_color.GetG(), icon_accent_color.GetB());
      } else {
        glyphColor = Gdiplus::Color(alpha,
            icon_secondary_color.GetR(), icon_secondary_color.GetG(), icon_secondary_color.GetB());
      }

      // Convert glyph to wide string
      pfc::stringcvt::string_wide_from_utf8 wide_glyph(glyph_utf8);

      // Calculate font size from glyph_size percentage
      int glyph_size_pct = get_nowbar_cbutton_glyph_size(index);
      int icon_size = std::min(iconRect.right - iconRect.left, iconRect.bottom - iconRect.top);
      int font_height = (int)(icon_size * glyph_size_pct / 100.0f);

      int gw = iconRect.right - iconRect.left;
      int gh = iconRect.bottom - iconRect.top;

      // Create a 32bpp DIB for GDI text rendering
      BITMAPINFO bmi = {};
      bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
      bmi.bmiHeader.biWidth = gw;
      bmi.bmiHeader.biHeight = -gh;  // top-down
      bmi.bmiHeader.biPlanes = 1;
      bmi.bmiHeader.biBitCount = 32;
      bmi.bmiHeader.biCompression = BI_RGB;

      BYTE* pBits = nullptr;
      HDC memDC = CreateCompatibleDC(NULL);
      HBITMAP hBmp = CreateDIBSection(memDC, &bmi, DIB_RGB_COLORS, (void**)&pBits, NULL, 0);
      HBITMAP hOldBmp = (HBITMAP)SelectObject(memDC, hBmp);

      // GDI font with DEFAULT_CHARSET triggers Windows font linking
      pfc::string8 font_name = get_nowbar_cbutton_font(index);
      pfc::stringcvt::string_wide_from_utf8 wide_font(font_name);
      HFONT hFont = CreateFontW(-font_height, 0, 0, 0, FW_NORMAL,
          FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
          CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
          DEFAULT_PITCH | FF_DONTCARE, wide_font.get_ptr());
      HFONT hOldFont = (HFONT)SelectObject(memDC, hFont);

      SetBkMode(memDC, TRANSPARENT);
      SetTextColor(memDC, RGB(255, 255, 255));

      const wchar_t* glyph_str = wide_glyph.get_ptr();
      int charCount = (int)wcslen(glyph_str);

      // Check if the string contains surrogate pairs (codepoints above U+FFFF).
      // GetGlyphIndicesW doesn't handle surrogates, so skip it in that case.
      bool has_surrogates = false;
      for (int i = 0; i < charCount; i++) {
        if (IS_HIGH_SURROGATE(glyph_str[i]) || IS_LOW_SURROGATE(glyph_str[i])) { has_surrogates = true; break; }
      }

      // Try to render using glyph indices to bypass font linking.
      // DrawTextW uses Uniscribe which applies font linking — even when the user
      // explicitly selects an icon font (e.g. Material Icons), Windows may substitute
      // a different font for PUA codepoints, causing wrong/missing characters.
      // ExtTextOutW with ETO_GLYPH_INDEX renders the exact glyph from the selected font.
      bool use_glyph_index = false;
      WORD glyphIndices[8] = {};
      if (!has_surrogates && charCount > 0 && charCount <= 8) {
        DWORD res = GetGlyphIndicesW(memDC, glyph_str, charCount,
                                      glyphIndices, GGI_MARK_NONEXISTING_GLYPHS);
        if (res != GDI_ERROR) {
          use_glyph_index = true;
          for (int i = 0; i < charCount; i++) {
            if (glyphIndices[i] == 0xFFFF) { use_glyph_index = false; break; }
          }
        }
      }

      if (use_glyph_index) {
        // Glyph found in font — render via glyph index (no font linking)
        SIZE textSize;
        GetTextExtentPointI(memDC, glyphIndices, charCount, &textSize);
        int x = (gw - textSize.cx) / 2;
        int y = (gh - textSize.cy) / 2;
        ExtTextOutW(memDC, x, y, ETO_GLYPH_INDEX, NULL,
                    (LPCWSTR)glyphIndices, charCount, NULL);
      } else {
        // Fallback: DrawTextW with font linking for characters not in the selected font
        RECT textRect = {0, 0, gw, gh};
        DrawTextW(memDC, glyph_str, -1, &textRect,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
      }

      SelectObject(memDC, hOldFont);
      DeleteObject(hFont);

      // Find the actual pixel bounding box of the rendered glyph.
      // Icon fonts often have unusual ascent/descent metrics that cause DT_VCENTER
      // (and GetTextExtentPoint-based centering) to place glyphs off-center.
      // Scanning for the true visual bounds lets us correct the offset.
      int actual_top = gh, actual_bottom = -1;
      int actual_left = gw, actual_right = -1;
      for (int py = 0; py < gh; py++) {
        BYTE* row = pBits + py * gw * 4;
        for (int px = 0; px < gw; px++) {
          if (row[px * 4 + 1] > 0) {  // green channel intensity
            if (py < actual_top) actual_top = py;
            if (py > actual_bottom) actual_bottom = py;
            if (px < actual_left) actual_left = px;
            if (px > actual_right) actual_right = px;
          }
        }
      }

      // Calculate offset to visually center the glyph's actual bounding box
      int center_offset_x = 0, center_offset_y = 0;
      if (actual_top <= actual_bottom) {
        int rendered_cx = (actual_left + actual_right + 1) / 2;
        int rendered_cy = (actual_top + actual_bottom + 1) / 2;
        center_offset_x = gw / 2 - rendered_cx;
        center_offset_y = gh / 2 - rendered_cy;
      }

      // Colorize: white text intensity becomes alpha mask for desired color
      BYTE cr = glyphColor.GetR(), cg = glyphColor.GetG(), cb = glyphColor.GetB();
      BYTE ca = glyphColor.GetA();
      for (int py = 0; py < gh; py++) {
        BYTE* row = pBits + py * gw * 4;
        for (int px = 0; px < gw; px++) {
          BYTE* p = row + px * 4;
          BYTE intensity = p[1];  // Green channel (uniform with ANTIALIASED_QUALITY)
          if (intensity > 0) {
            p[0] = cb;
            p[1] = cg;
            p[2] = cr;
            p[3] = (BYTE)((intensity * ca) / 255);
          }
        }
      }

      // Composite onto main graphics with visual centering offset.
      Gdiplus::Bitmap glyphBmp(gw, gh, gw * 4, PixelFormat32bppARGB, pBits);
      g.DrawImage(&glyphBmp,
                  (INT)iconRect.left + center_offset_x,
                  (INT)iconRect.top + center_offset_y);

      SelectObject(memDC, hOldBmp);
      DeleteObject(hBmp);
      DeleteDC(memDC);
    } else {
      // Fallback to default numbered square icon with state-based color
      Gdiplus::Color iconColor(alpha, baseIconColor.GetR(), baseIconColor.GetG(), baseIconColor.GetB());
      draw_numbered_square_icon(g, iconRect, iconColor, index + 1);  // 1-based number
    }
  };
  
  draw_cbutton(0, m_rect_cbutton1, HitRegion::CButton1);
  draw_cbutton(1, m_rect_cbutton2, HitRegion::CButton2);
  draw_cbutton(2, m_rect_cbutton3, HitRegion::CButton3);
  draw_cbutton(3, m_rect_cbutton4, HitRegion::CButton4);
  draw_cbutton(4, m_rect_cbutton5, HitRegion::CButton5);
  draw_cbutton(5, m_rect_cbutton6, HitRegion::CButton6);
  
  // Track whether custom button animation is still in progress
  // The centralized animation loop in paint() will request the next frame
  m_cbutton_animating = m_cbutton_fade_active && get_nowbar_smooth_animations_enabled();
}

void ControlPanelCore::draw_button(Gdiplus::Graphics &g, const RECT &rect,
                                   const wchar_t *icon, bool hovered,
                                   bool active) {
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
  Gdiplus::Font iconFont(L"Segoe MDL2 Assets", 14.0f * m_dpi_scale,
                         Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);

  Gdiplus::StringFormat sf;
  sf.SetAlignment(Gdiplus::StringAlignmentCenter);
  sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);

  Gdiplus::RectF btnRect((float)rect.left, (float)rect.top, (float)w, (float)h);
  g.DrawString(icon, -1, &iconFont, btnRect, &sf, &iconBrush);
}

void ControlPanelCore::draw_circular_button(Gdiplus::Graphics &g,
                                            const RECT &rect,
                                            const wchar_t *icon, bool hovered,
                                            bool pressed) {
  int w = rect.right - rect.left;
  int h = rect.bottom - rect.top;

  // Background circle
  Gdiplus::Color bgColor = hovered ? Gdiplus::Color(255, 255, 255, 255)
                                   : Gdiplus::Color(255, 230, 230, 230);
  Gdiplus::SolidBrush bgBrush(bgColor);
  g.FillEllipse(&bgBrush, rect.left, rect.top, w, h);

  // Icon (black on white background)
  Gdiplus::SolidBrush iconBrush(Gdiplus::Color(255, 0, 0, 0));
  Gdiplus::Font iconFont(L"Segoe MDL2 Assets", 18.0f * m_dpi_scale,
                         Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);

  Gdiplus::StringFormat sf;
  sf.SetAlignment(Gdiplus::StringAlignmentCenter);
  sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);

  Gdiplus::RectF btnRect((float)rect.left, (float)rect.top, (float)w, (float)h);
  g.DrawString(icon, -1, &iconFont, btnRect, &sf, &iconBrush);
}

void ControlPanelCore::draw_3d_button_pad(Gdiplus::Graphics& g, const RECT& rect,
                                          float press_progress, float hover_opacity) {
    int w = rect.right - rect.left;
    int h = rect.bottom - rect.top;
    if (w <= 0 || h <= 0) return;
    if (m_cbutton_opacity < 0.01f) return;

    // Pad dimensions: 90% of button rect, centered
    int pad_margin = w * 5 / 100;
    int pad_x = rect.left + pad_margin;
    int pad_y = rect.top + pad_margin;
    int pad_w = w - pad_margin * 2;
    int pad_h = h - pad_margin * 2;

    // Corner radius scaled by DPI
    int corner = static_cast<int>(4.0f * m_dpi_scale);
    corner = std::min(corner, std::min(pad_w, pad_h) / 2);
    int diameter = corner * 2;

    // --- Compute base color from background ---
    int bg_r, bg_g, bg_b;
    int bg_style = get_nowbar_background_style();
    bool has_artwork_bg = (bg_style == 1 || bg_style == 2) && m_artwork_colors_valid;

    if (has_artwork_bg) {
        int r = m_artwork_color_primary.GetR();
        int gc = m_artwork_color_primary.GetG();
        int b = m_artwork_color_primary.GetB();
        BYTE ov = (bg_style == 1) ? (m_dark_mode ? 120 : 80)
                                   : (m_dark_mode ? 140 : 180);
        bg_r = r * (255 - ov) / 255;
        bg_g = gc * (255 - ov) / 255;
        bg_b = b * (255 - ov) / 255;
    } else {
        bg_r = m_bg_color.GetRed();
        bg_g = m_bg_color.GetGreen();
        bg_b = m_bg_color.GetBlue();
    }

    // Tonal shift: lighter in dark mode, darker in light mode
    int shift = m_dark_mode ? 15 : -15;
    int base_r = std::clamp(bg_r + shift, 0, 255);
    int base_g = std::clamp(bg_g + shift, 0, 255);
    int base_b = std::clamp(bg_b + shift, 0, 255);

    // Hover brightening
    int hover_shift = static_cast<int>(hover_opacity * 12.0f);
    base_r = std::clamp(base_r + hover_shift, 0, 255);
    base_g = std::clamp(base_g + hover_shift, 0, 255);
    base_b = std::clamp(base_b + hover_shift, 0, 255);

    // --- Gradient colors ---
    int grad_offset = 10;
    float invert = press_progress;

    int top_delta = static_cast<int>(grad_offset * (1.0f - 2.0f * invert));
    int bot_delta = -top_delta;

    Gdiplus::Color top_color(
        static_cast<BYTE>(m_cbutton_opacity * 255),
        std::clamp(base_r + top_delta, 0, 255),
        std::clamp(base_g + top_delta, 0, 255),
        std::clamp(base_b + top_delta, 0, 255));
    Gdiplus::Color bot_color(
        static_cast<BYTE>(m_cbutton_opacity * 255),
        std::clamp(base_r + bot_delta, 0, 255),
        std::clamp(base_g + bot_delta, 0, 255),
        std::clamp(base_b + bot_delta, 0, 255));

    // --- Vertical offset for press sink ---
    int max_sink = static_cast<int>(2.0f * m_dpi_scale);
    int sink_offset = static_cast<int>(press_progress * max_sink);
    pad_y += sink_offset;

    // --- Build rounded rect path ---
    Gdiplus::GraphicsPath pad_path;
    pad_path.AddArc(pad_x, pad_y, diameter, diameter, 180, 90);
    pad_path.AddArc(pad_x + pad_w - diameter, pad_y, diameter, diameter, 270, 90);
    pad_path.AddArc(pad_x + pad_w - diameter, pad_y + pad_h - diameter, diameter, diameter, 0, 90);
    pad_path.AddArc(pad_x, pad_y + pad_h - diameter, diameter, diameter, 90, 90);
    pad_path.CloseFigure();

    // --- Drop shadow ---
    float shadow_scale = 1.0f - press_progress * 0.9f;
    shadow_scale += hover_opacity * 0.15f;
    shadow_scale = std::clamp(shadow_scale, 0.0f, 1.3f);

    if (shadow_scale > 0.05f) {
        int shadow_offset_y = static_cast<int>(2.0f * m_dpi_scale * shadow_scale);
        int shadow_blur = static_cast<int>(3.0f * m_dpi_scale * shadow_scale);
        BYTE shadow_alpha = static_cast<BYTE>(40.0f * m_cbutton_opacity * shadow_scale);

        Gdiplus::GraphicsPath shadow_path;
        int sx = pad_x - shadow_blur / 2;
        int sy = pad_y + shadow_offset_y;
        int sw = pad_w + shadow_blur;
        int sh = pad_h + shadow_blur / 2;
        int sd = std::min(diameter + shadow_blur / 2, std::min(sw, sh) / 2) * 2;
        sd = std::min(sd, std::min(sw, sh));
        shadow_path.AddArc(sx, sy, sd, sd, 180, 90);
        shadow_path.AddArc(sx + sw - sd, sy, sd, sd, 270, 90);
        shadow_path.AddArc(sx + sw - sd, sy + sh - sd, sd, sd, 0, 90);
        shadow_path.AddArc(sx, sy + sh - sd, sd, sd, 90, 90);
        shadow_path.CloseFigure();

        Gdiplus::SolidBrush shadow_brush(Gdiplus::Color(shadow_alpha, 0, 0, 0));
        g.FillPath(&shadow_brush, &shadow_path);
    }

    // --- Fill pad with gradient ---
    Gdiplus::RectF grad_rect((float)pad_x, (float)pad_y, (float)pad_w, (float)pad_h);
    Gdiplus::LinearGradientBrush grad_brush(grad_rect, top_color, bot_color,
                                             Gdiplus::LinearGradientModeVertical);
    g.FillPath(&grad_brush, &pad_path);
}

void ControlPanelCore::draw_seekbar(Gdiplus::Graphics &g) {
  int w = m_rect_seekbar.right - m_rect_seekbar.left;
  int h = m_rect_seekbar.bottom - m_rect_seekbar.top;

  // Use scaled seekbar_height
  int track_h = static_cast<int>(m_metrics.seekbar_height * m_size_scale);
  int track_y = m_rect_seekbar.top + (h - track_h) / 2;
  int radius = track_h / 2;                     // For rounded ends
  bool is_pill = (get_nowbar_bar_style() == 0); // 0=Pill-shaped, 1=Rectangular

  // Background track - theme-aware color
  Gdiplus::Color trackColor;
  int bg_style = get_nowbar_background_style();
  bool has_artwork_bg = (bg_style == 1 || bg_style == 2) && m_artwork_colors_valid;

  if (get_nowbar_custom_progress_track_enabled()) {
    // Custom color overrides everything
    COLORREF tc = get_nowbar_progress_track_color();
    trackColor = Gdiplus::Color(255, GetRValue(tc), GetGValue(tc), GetBValue(tc));
  } else if (has_artwork_bg) {
    // Artwork backgrounds: approximate the rendered background color
    // by applying the same black overlay darkening used in draw_background()
    int r = m_artwork_color_primary.GetR();
    int g = m_artwork_color_primary.GetG();
    int b = m_artwork_color_primary.GetB();
    BYTE ov = (bg_style == 1) ? (m_dark_mode ? 120 : 80)
                               : (m_dark_mode ? 140 : 180);
    r = r * (255 - ov) / 255 + 15;
    g = g * (255 - ov) / 255 + 15;
    b = b * (255 - ov) / 255 + 15;
    trackColor = Gdiplus::Color(255, std::min(r, 255), std::min(g, 255), std::min(b, 255));
  } else {
    // Solid background: use theme-appropriate track color
    trackColor = Gdiplus::Color(255, GetRValue(m_track_color),
        GetGValue(m_track_color), GetBValue(m_track_color));
  }
  Gdiplus::SolidBrush trackBrush(trackColor);
  if (is_pill) {
    Gdiplus::GraphicsPath trackPath;
    int r = std::min(radius, w / 2);
    trackPath.AddArc(m_rect_seekbar.left, track_y, r * 2, track_h, 90, 180);
    trackPath.AddArc(m_rect_seekbar.left + w - r * 2, track_y, r * 2, track_h,
                     270, 180);
    trackPath.CloseFigure();
    g.FillPath(&trackBrush, &trackPath);
  } else {
    g.FillRectangle(&trackBrush, m_rect_seekbar.left, track_y, w, track_h);
  }

  // Progress bar always uses direct value (no interpolation) for performance
  // The Smooth Animations setting does not affect the progress bar
  double progress;
  if (m_seeking) {
    // During seeking, use the current playback time
    progress = (m_state.track_length > 0)
                   ? (m_state.playback_time / m_state.track_length)
                   : 0.0;
  } else {
    // Normal playback - use target progress directly
    progress = m_target_progress;
  }
  
  // Clamp progress
  progress = std::max(0.0, std::min(1.0, progress));
  int progress_w = static_cast<int>(w * progress);

  if (progress_w > 0) {
    // Get custom progress accent color from preferences
    COLORREF prog_accent = get_nowbar_custom_progress_accent_enabled()
        ? get_nowbar_progress_accent_color() : m_theme_highlight;
    Gdiplus::SolidBrush progressBrush(
        Gdiplus::Color(255, GetRValue(prog_accent), GetGValue(prog_accent), GetBValue(prog_accent)));
    if (is_pill && progress_w > radius * 2) {
      Gdiplus::GraphicsPath progressPath;
      int r = std::min(radius, progress_w / 2);
      progressPath.AddArc(m_rect_seekbar.left, track_y, r * 2, track_h, 90,
                          180);
      progressPath.AddArc(m_rect_seekbar.left + progress_w - r * 2, track_y,
                          r * 2, track_h, 270, 180);
      progressPath.CloseFigure();
      g.FillPath(&progressBrush, &progressPath);
    } else if (is_pill) {
      // Too small for pill, just draw circle
      g.FillEllipse(&progressBrush, m_rect_seekbar.left, track_y, track_h,
                    track_h);
    } else {
      g.FillRectangle(&progressBrush, m_rect_seekbar.left, track_y, progress_w,
                      track_h);
    }
  }
  
  // Seekbar never drives the animation loop (no interpolation = no need for continuous frames)
  m_seekbar_animating = false;

  // Seek handle (only on hover) - uses highlight color
  if (m_hover_region == HitRegion::SeekBar || m_seeking) {
    int handle_size = track_h * 2;
    int handle_x = m_rect_seekbar.left + progress_w - handle_size / 2;
    int handle_y = m_rect_seekbar.top + (h - handle_size) / 2;
    COLORREF handle_accent = get_nowbar_custom_progress_accent_enabled()
        ? get_nowbar_progress_accent_color() : m_theme_highlight;
    Gdiplus::SolidBrush handleBrush(Gdiplus::Color(255,
        GetRValue(handle_accent), GetGValue(handle_accent), GetBValue(handle_accent)));
    g.FillEllipse(&handleBrush, handle_x, handle_y, handle_size, handle_size);
  }
  
}

void ControlPanelCore::draw_miniplayer_button(Gdiplus::Graphics &g) {
  if (!get_nowbar_miniplayer_icon_visible()) return;

  bool mp_hovered = (m_hover_region == HitRegion::MiniPlayerButton);
  int mp_w = m_rect_miniplayer.right - m_rect_miniplayer.left;
  int mp_h = m_rect_miniplayer.bottom - m_rect_miniplayer.top;
  if (mp_w <= 0 || mp_h <= 0) return;

  int bg_style = get_nowbar_background_style();
  bool use_light_foreground = (bg_style == 1 && m_artwork_colors_valid) ||
                              (bg_style == 2 && m_blurred_artwork);
  Gdiplus::Color mp_hover_color;
  if (use_light_foreground) {
    mp_hover_color = Gdiplus::Color(40, 255, 255, 255);
  } else if (get_nowbar_custom_hover_color_enabled()) {
    COLORREF hc = get_nowbar_hover_color();
    mp_hover_color = Gdiplus::Color(40, GetRValue(hc), GetGValue(hc), GetBValue(hc));
  } else {
    mp_hover_color = Gdiplus::Color(40, GetRValue(m_theme_selection),
        GetGValue(m_theme_selection), GetBValue(m_theme_selection));
  }
  Gdiplus::Color mp_color = use_light_foreground
      ? Gdiplus::Color(255, 200, 200, 200) : m_text_secondary_color;

  if (mp_hovered && get_nowbar_hover_circles_enabled()) {
    Gdiplus::SolidBrush hoverBrush(mp_hover_color);
    g.FillEllipse(&hoverBrush, m_rect_miniplayer.left, m_rect_miniplayer.top,
                  mp_w, mp_h);
  }
  float mp_scale = mp_hovered ? HOVER_SCALE_FACTOR : 1.0f;
  int mp_inset = static_cast<int>(mp_w * (1.0f - 0.70f * mp_scale) / 2.0f);
  RECT miniplayerIconRect = {
      m_rect_miniplayer.left + mp_inset, m_rect_miniplayer.top + mp_inset,
      m_rect_miniplayer.right - mp_inset, m_rect_miniplayer.bottom - mp_inset};
  draw_miniplayer_icon(g, miniplayerIconRect, mp_color);
}

void ControlPanelCore::draw_seekbar_tooltip(Gdiplus::Graphics &g) {
  if (!(m_hover_region == HitRegion::SeekBar || m_seeking) || m_state.track_length <= 0)
    return;

  // Recompute progress for cursor positioning
  int w = m_rect_seekbar.right - m_rect_seekbar.left;
  double progress;
  if (m_seeking) {
    progress = (m_state.track_length > 0)
                   ? (m_state.playback_time / m_state.track_length)
                   : 0.0;
  } else {
    progress = m_target_progress;
  }
  progress = std::max(0.0, std::min(1.0, progress));
  int progress_w = static_cast<int>(w * progress);

  // Format the preview time
  std::wstring timeStr = format_time(m_seeking ? m_state.playback_time : m_preview_time);

  // Measure text size
  Gdiplus::Font tooltipFont(L"Segoe UI", 10.0f * m_dpi_scale, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
  Gdiplus::RectF textBounds;
  g.MeasureString(timeStr.c_str(), -1, &tooltipFont, Gdiplus::PointF(0, 0), &textBounds);

  // Tooltip dimensions with padding
  int padding_h = static_cast<int>(6 * m_dpi_scale);
  int padding_v = static_cast<int>(3 * m_dpi_scale);
  int tooltip_w = static_cast<int>(textBounds.Width) + padding_h * 2;
  int tooltip_h = static_cast<int>(textBounds.Height) + padding_v * 2;

  // Position: centered on cursor X, above seekbar
  int cursor_x = m_seeking ? (m_rect_seekbar.left + progress_w) : m_seekbar_hover_x;
  int tooltip_x = cursor_x - tooltip_w / 2;
  int tooltip_y = m_rect_seekbar.top - tooltip_h - static_cast<int>(6 * m_dpi_scale);

  // Clamp to stay within seekbar bounds
  int seekbar_left = static_cast<int>(m_rect_seekbar.left);
  int seekbar_right = static_cast<int>(m_rect_seekbar.right);
  tooltip_x = std::max(seekbar_left, std::min(tooltip_x, seekbar_right - tooltip_w));

  // Draw tooltip background (semi-transparent, themed)
  Gdiplus::Color bgColor = m_dark_mode ? Gdiplus::Color(220, 60, 60, 60) : Gdiplus::Color(220, 40, 40, 40);
  Gdiplus::SolidBrush bgBrush(bgColor);
  int corner = static_cast<int>(4 * m_dpi_scale);
  Gdiplus::GraphicsPath tooltipPath;
  tooltipPath.AddArc(tooltip_x, tooltip_y, corner * 2, corner * 2, 180, 90);
  tooltipPath.AddArc(tooltip_x + tooltip_w - corner * 2, tooltip_y, corner * 2, corner * 2, 270, 90);
  tooltipPath.AddArc(tooltip_x + tooltip_w - corner * 2, tooltip_y + tooltip_h - corner * 2, corner * 2, corner * 2, 0, 90);
  tooltipPath.AddArc(tooltip_x, tooltip_y + tooltip_h - corner * 2, corner * 2, corner * 2, 90, 90);
  tooltipPath.CloseFigure();
  g.FillPath(&bgBrush, &tooltipPath);

  // Draw tooltip text
  Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, 255, 255, 255));
  Gdiplus::StringFormat sf;
  sf.SetAlignment(Gdiplus::StringAlignmentCenter);
  sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
  Gdiplus::RectF textRect(static_cast<float>(tooltip_x), static_cast<float>(tooltip_y),
                          static_cast<float>(tooltip_w), static_cast<float>(tooltip_h));
  g.DrawString(timeStr.c_str(), -1, &tooltipFont, textRect, &sf, &textBrush);
}

void ControlPanelCore::draw_time_display(Gdiplus::Graphics &g) {
  // Hide timers in waveform mode when no track is loaded (e.g. after restart)
  if (get_nowbar_visualization_mode() == 2 && m_state.track_length <= 0 &&
      !is_live_stream(m_state)) return;

  // Determine if we're using artwork-based background that needs light text
  int bg_style = get_nowbar_background_style();
  bool use_light_foreground = (bg_style == 1 && m_artwork_colors_valid) ||
                              (bg_style == 2 && m_blurred_artwork);
  Gdiplus::Color time_color = use_light_foreground
      ? Gdiplus::Color(255, 200, 200, 200) : m_text_secondary_color;
  COLORREF custom_time_color;
  if (get_nowbar_time_font_color(custom_time_color))
      time_color = Gdiplus::Color(255, GetRValue(custom_time_color), GetGValue(custom_time_color), GetBValue(custom_time_color));
  Gdiplus::SolidBrush timeBrush(time_color);

  std::wstring elapsed = format_time(m_state.playback_time);
  // Show Live for streams without a duration; otherwise show the countdown.
  double remaining = m_state.track_length - m_state.playback_time;
  if (remaining < 0)
    remaining = 0;
  std::wstring remaining_str = is_live_stream(m_state)
      ? L"Live" : L"-" + format_time(remaining);

  // Use GenericTypographic to eliminate variable internal padding that
  // MeasureString adds by default — this ensures measurement matches rendering
  // and prevents timer position shifting when the font changes.
  // Use StringAlignmentNear for line alignment and position the rect top
  // manually using font ascent/descent. StringAlignmentCenter would center
  // the em box, but fonts with asymmetric ascent/descent (e.g. Segoe UI)
  // render digit glyphs visually low relative to the geometric rect center.
  // This format must stay default-constructed (GenericDefault family), NOT
  // GenericTypographic: in this font/size combination GDI+ reports Ok but
  // emits no pixels for pure CJK strings under GenericTypographic (ASCII and
  // mixed ASCII+CJK strings render fine). NoClip absorbs the extra internal
  // padding the default format adds versus GenericTypographic.
  // 此格式必须保持默认构造 (GenericDefault 系列)，而非
  // GenericTypographic: 在此字体/字号组合下，GDI+ 报告正常，但
  // 对于纯 CJK 字符串，在 GenericTypographic 下不输出任何像素 (ASCII 和
  // 混合 ASCII+CJK 字符串渲染正常)。NoClip 会吸收默认格式相对于 GenericTypographic 添加的额外内部填充。
  // 默认格式相对于 GenericTypographic 添加的额外填充。
  Gdiplus::StringFormat sfLeft;
  // Gdiplus::StringFormat sfLeft(Gdiplus::StringFormat::GenericTypographic());
  sfLeft.SetAlignment(Gdiplus::StringAlignmentNear);
  sfLeft.SetLineAlignment(Gdiplus::StringAlignmentNear);
  sfLeft.SetFormatFlags(sfLeft.GetFormatFlags() | Gdiplus::StringFormatFlagsNoClip);

  Gdiplus::StringFormat sfRight(Gdiplus::StringFormat::GenericTypographic());
  sfRight.SetAlignment(Gdiplus::StringAlignmentFar);
  sfRight.SetLineAlignment(Gdiplus::StringAlignmentNear);
  sfRight.SetFormatFlags(sfRight.GetFormatFlags() | Gdiplus::StringFormatFlagsNoClip);

  Gdiplus::StringFormat sfMeasure(Gdiplus::StringFormat::GenericTypographic());

  // Position time at the ends of the seekbar (vertically centered with seekbar).
  // Compute the rect top using font ascent/descent so the glyph center aligns
  // with seekbar_center_y, independent of font ascent/descent asymmetry.
  int seekbar_center_y = (m_rect_seekbar.top + m_rect_seekbar.bottom) / 2;
  Gdiplus::FontFamily time_family;
  m_font_time->GetFamily(&time_family);
  int time_style = m_font_time->GetStyle();
  UINT16 em_height = time_family.GetEmHeight(time_style);
  UINT16 cell_ascent = time_family.GetCellAscent(time_style);
  UINT16 cell_descent = time_family.GetCellDescent(time_style);
  float font_size = m_font_time->GetSize();
  float ascent_px = em_height ? font_size * cell_ascent / em_height : 0.0f;
  float descent_px = em_height ? font_size * cell_descent / em_height : 0.0f;
  float line_px = ascent_px + descent_px;
  // Measure the actual visible glyph bounds so the digit center aligns with
  // the seekbar center regardless of font metrics. Centering on em ascent or
  // (ascent+descent) biases the glyph downward by (ascent - cap_height)/2 or
  // descent/2 respectively; both scale with font size and become visible
  // beyond size 9. Path measurement gives the true ink rect.
  Gdiplus::StringFormat sfPathMeasure(Gdiplus::StringFormat::GenericTypographic());
  Gdiplus::GraphicsPath measure_path;
  measure_path.AddString(L"9:59:59", -1, &time_family, time_style, font_size,
                         Gdiplus::PointF(0.0f, 0.0f), &sfPathMeasure);
  Gdiplus::RectF glyph_bounds;
  measure_path.GetBounds(&glyph_bounds);
  float time_top = (float)seekbar_center_y - glyph_bounds.Y - glyph_bounds.Height / 2.0f;
  float time_height = line_px;

  // Measure actual text width needed for longest possible time strings
  // Use "9:59:59" as reference for elapsed time and "-9:59:59" for remaining time
  // This handles tracks over 59 minutes that use h:mm:ss format
  Gdiplus::RectF refBoundRect;
  g.MeasureString(L"9:59:59", -1, m_font_time.get(), Gdiplus::PointF(0, 0), &sfMeasure, &refBoundRect);
  float time_width = refBoundRect.Width + 4 * m_dpi_scale; // Add small padding

  Gdiplus::RectF refBoundRectRemaining;
  g.MeasureString(L"-9:59:59", -1, m_font_time.get(), Gdiplus::PointF(0, 0), &sfMeasure, &refBoundRectRemaining);
  float time_offset = refBoundRectRemaining.Width + 4 * m_dpi_scale; // Add small padding

  // Left side: elapsed time (before seekbar)
  // Position timer to end just before the seekbar (with gap for seek handle)
  // Text is right-aligned within the rect so it appears adjacent to the seekbar
  int timer_gap = static_cast<int>(12 * m_dpi_scale * m_size_scale); // Gap to avoid seek handle overlap
  float timer_right = (float)(m_rect_seekbar.left - timer_gap);
  float timer_left = timer_right - time_width;
  
  Gdiplus::RectF leftTimeRect(timer_left, time_top,
                              time_width, time_height);
  g.DrawString(elapsed.c_str(), -1, m_font_time.get(), leftTimeRect, &sfRight,
               &timeBrush);

  // Right side: remaining time (after seekbar)
  // Use same gap as left side to avoid seek handle overlap
  Gdiplus::RectF rightTimeRect(
      (float)(m_rect_seekbar.right + timer_gap),
      time_top, time_offset, time_height);
  g.DrawString(remaining_str.c_str(), -1, m_font_time.get(), rightTimeRect, &sfLeft,
               &timeBrush);
}

void ControlPanelCore::create_vis_stream() {
  if (m_vis_stream.is_valid()) return;
  try {
    if (!core_api::are_services_available()) return;
    auto vis_mgr = visualisation_manager::get();
    visualisation_stream_v3::ptr stream;
    vis_mgr->create_stream(stream, visualisation_manager::KStreamFlagNewFFT);
    m_vis_stream = stream;
  } catch (...) {}
}

void ControlPanelCore::release_vis_stream() {
  m_vis_stream.release();
  std::fill(m_spectrum_bars.begin(), m_spectrum_bars.end(), 0.0f);
  std::fill(m_spectrum_peaks.begin(), m_spectrum_peaks.end(), 0.0f);
  std::fill(m_spectrum_peak_velocity.begin(), m_spectrum_peak_velocity.end(), 0.0f);
  std::fill(m_spectrum_bars_right.begin(), m_spectrum_bars_right.end(), 0.0f);
  std::fill(m_spectrum_peaks_right.begin(), m_spectrum_peaks_right.end(), 0.0f);
  std::fill(m_spectrum_peak_velocity_right.begin(), m_spectrum_peak_velocity_right.end(), 0.0f);
  // Reset SDFT resonator states so stale energy doesn't carry over
  for (auto& bin : m_sdft_bins) { bin.real = 0.0f; bin.imag = 0.0f; }
  m_sdft_sample_rate = 0;
}

int ControlPanelCore::compute_spectrum_bar_count(int area_w) const {
  int spec_width = get_nowbar_spectrum_width();
  int bar_w, gap;
  if (spec_width == 0) { bar_w = 2; gap = 1; }       // Thin
  else if (spec_width == 2) { bar_w = 8; gap = 3; }   // Wide
  else { bar_w = 4; gap = 2; }                          // Normal
  // Dominoes mode: tight 1px gap between bars
  if (get_nowbar_spectrum_style() == 2) gap = 1;
  int unit = bar_w + gap;
  if (unit < 1) unit = 1;
  return std::max(1, area_w / unit);
}

// A-weighting approximation for perceptual frequency weighting
static float a_weight(float freq) {
  if (freq < 10.0f) return 0.0f;
  float f2 = freq * freq;
  float f4 = f2 * f2;
  float num = 12194.0f * 12194.0f * f4;
  float denom = (f2 + 20.6f * 20.6f) *
      sqrtf((f2 + 107.7f * 107.7f) * (f2 + 737.9f * 737.9f)) *
      (f2 + 12194.0f * 12194.0f);
  if (denom < 1e-12f) return 0.0f;
  float ra = num / denom;
  // Normalize so that 1kHz ≈ 1.0 (ra * 10^(2/20) = ra * 1.2589254)
  float linear = ra * 1.2589254f;
  return std::max(0.0f, std::min(2.0f, linear));
}

// 5-tap neighbor smoothing kernel [1,3,5,3,1] / 13
static void apply_neighbor_smoothing(std::vector<float>& values, std::vector<float>& scratch) {
  int n = (int)values.size();
  if (n < 3) return;
  static const float kernel[] = {1.0f, 3.0f, 5.0f, 3.0f, 1.0f};
  scratch.resize(n);
  for (int i = 0; i < n; i++) {
    float sum = 0.0f;
    float wsum = 0.0f;
    for (int k = -2; k <= 2; k++) {
      int j = i + k;
      if (j >= 0 && j < n) {
        float w = kernel[k + 2];
        sum += values[j] * w;
        wsum += w;
      }
    }
    scratch[i] = (wsum > 0.0f) ? sum / wsum : values[i];
  }
  values = scratch;
}

void ControlPanelCore::update_spectrum_bar_configs(int sample_count) {
  if (m_spectrum_bar_count <= 0 || sample_count <= 0) return;
  if (m_spectrum_config_bar_count == m_spectrum_bar_count &&
      m_spectrum_config_sample_count == sample_count)
    return;

  m_spectrum_config_bar_count = m_spectrum_bar_count;
  m_spectrum_config_sample_count = sample_count;
  m_spectrum_bar_configs.resize(m_spectrum_bar_count);
  m_spectrum_normalized_values.resize(m_spectrum_bar_count, 0.0f);

  float freq_min = 60.0f;
  float freq_max = 16000.0f;
  float bin_freq_step = 44100.0f / (float)SPECTRUM_FFT_SIZE;
  float log_min = std::log10(freq_min);
  float log_max = std::log10(freq_max);

  for (int i = 0; i < m_spectrum_bar_count; i++) {
    float f_lo = std::pow(10.0f, log_min + (log_max - log_min) * i / m_spectrum_bar_count);
    float f_hi = std::pow(10.0f, log_min + (log_max - log_min) * (i + 1) / m_spectrum_bar_count);
    float f_center = (f_lo + f_hi) * 0.5f;

    float fbin_lo = f_lo / bin_freq_step;
    float fbin_hi = f_hi / bin_freq_step;
    if (fbin_lo < 0.0f) fbin_lo = 0.0f;
    if (fbin_hi < 0.0f) fbin_hi = 0.0f;
    if (fbin_lo >= (float)(sample_count - 1)) fbin_lo = (float)(sample_count - 1) - 0.001f;
    if (fbin_hi >= (float)(sample_count - 1)) fbin_hi = (float)(sample_count - 1) - 0.001f;

    float fbin_center = f_center / bin_freq_step;
    if (fbin_center < 0.0f) fbin_center = 0.0f;
    if (fbin_center >= (float)(sample_count - 1)) fbin_center = (float)(sample_count - 1) - 0.001f;

    int bin_lo = (int)fbin_lo;
    int bin_hi = (int)fbin_hi;

    auto& cfg = m_spectrum_bar_configs[i];
    cfg.fbin_lo = fbin_lo;
    cfg.fbin_hi = fbin_hi;
    cfg.fbin_center = fbin_center;
    cfg.bin_lo = bin_lo;
    cfg.bin_hi = bin_hi;
    cfg.is_narrow = (bin_lo == bin_hi);
    cfg.a_weight = a_weight(f_center);
  }
}

// Process FFT bins into normalized bar values for one channel
static void process_channel_bars(const audio_sample* data, int sample_count, int nch, int channel,
                                  int bar_count, float freq_min, float freq_max, float bin_freq_step,
                                  std::vector<float>& out_values) {
  out_values.resize(bar_count);
  float log_min = std::log10(freq_min);
  float log_max = std::log10(freq_max);

  for (int i = 0; i < bar_count; i++) {
    float f_lo = std::pow(10.0f, log_min + (log_max - log_min) * i / bar_count);
    float f_hi = std::pow(10.0f, log_min + (log_max - log_min) * (i + 1) / bar_count);
    float f_center = (f_lo + f_hi) * 0.5f;

    int bin_lo = (int)(f_lo / bin_freq_step);
    int bin_hi = (int)(f_hi / bin_freq_step);
    if (bin_lo < 0) bin_lo = 0;
    if (bin_hi >= sample_count) bin_hi = sample_count - 1;
    if (bin_lo > bin_hi) bin_lo = bin_hi;

    float aw = a_weight(f_center);

    float magnitude = 0.0f;
    for (int b = bin_lo; b <= bin_hi; b++) {
      float val = data[b * nch + channel] * aw;
      if (val > magnitude) magnitude = val;
    }

    // Amplify and compress: sqrt for perceptual scaling, 3x boost
    float normalized = std::sqrt(magnitude) * 3.0f;
    if (normalized > 1.0f) normalized = 1.0f;
    out_values[i] = normalized;
  }
}

// === Sliding DFT (SDFT) per-bar resonators ===
// Initialize one complex resonator per bar at its logarithmic center
// frequency. decay=0.9999 gives τ≈227ms @ 44100Hz — long enough for low
// frequencies (60Hz) to accumulate over ~13 periods so adjacent low bars
// don't collapse to similar amplitudes.
void ControlPanelCore::update_sdft_bins(int sample_rate, int bar_count) {
  if (bar_count <= 0 || sample_rate <= 0) return;
  if (m_sdft_sample_rate == sample_rate &&
      (int)m_sdft_bins.size() == bar_count)
    return;

  m_sdft_sample_rate = sample_rate;
  m_sdft_bins.resize(bar_count);

  float freq_min = 60.0f;
  float freq_max = 16000.0f;
  float log_min = std::log10(freq_min);
  float log_max = std::log10(freq_max);
  constexpr float PI = 3.14159265358979323846f;
  float decay = 0.9999f;

  for (int i = 0; i < bar_count; i++) {
    float f_lo = std::pow(10.0f, log_min + (log_max - log_min) * i / bar_count);
    float f_hi = std::pow(10.0f, log_min + (log_max - log_min) * (i + 1) / bar_count);
    float f_center = (f_lo + f_hi) * 0.5f;

    float omega = 2.0f * PI * f_center / (float)sample_rate;
    auto& bin = m_sdft_bins[i];
    bin.cos_w = std::cos(omega);
    bin.sin_w = std::sin(omega);
    bin.decay = decay;
    bin.norm = 1.0f / (1.0f - decay);
    bin.a_weight_val = a_weight(f_center);
    bin.freq = f_center;
    bin.real = 0.0f;
    bin.imag = 0.0f;
  }
}

// Process PCM samples through all resonators (sample-by-sample update).
// Per-sample cost: 4 multiplies + 2 adds per bar.
void ControlPanelCore::process_sdft(const audio_sample* data, int sample_count, int nch,
                          std::vector<SdftBin>& bins, int bar_count) {
  if (!data || sample_count <= 0 || bar_count <= 0) return;

  for (int s = 0; s < sample_count; s++) {
    // Average across channels (mono mixdown)
    float x;
    if (nch >= 2) {
      float sum = 0.0f;
      for (int ch = 0; ch < nch; ch++) sum += data[s * nch + ch];
      x = sum / (float)nch;
    } else {
      x = data[s];
    }

    // Update each resonator: S = decay * S * e^(jw) + x
    for (int i = 0; i < bar_count; i++) {
      auto& b = bins[i];
      float new_real = b.decay * (b.real * b.cos_w - b.imag * b.sin_w) + x;
      float new_imag = b.decay * (b.real * b.sin_w + b.imag * b.cos_w);
      b.real = new_real;
      b.imag = new_imag;
    }
  }
}

// Apply smoothing, floor, and peak tracking to one channel's bar data
static void apply_bar_dynamics(const std::vector<float>& normalized_values, int bar_count,
                                std::vector<float>& bars, std::vector<float>& peaks,
                                std::vector<float>& peak_velocity) {
  for (int i = 0; i < bar_count; i++) {
    float target = normalized_values[i];
    float current = bars[i];
    if (target > current) {
      bars[i] = current + (target - current) * 0.92f;  // Fast attack
    } else {
      bars[i] = current + (target - current) * 0.25f;  // Smooth decay
    }
  }

  // Peak tracking with gravity
  for (int i = 0; i < bar_count; i++) {
    float bar_val = bars[i];
    if (bar_val >= peaks[i]) {
      peaks[i] = bar_val;
      peak_velocity[i] = 0.0f;
    } else {
      peak_velocity[i] += 0.004f;
      peaks[i] -= peak_velocity[i];
      if (peaks[i] < 0.0f) peaks[i] = 0.0f;
    }
  }
}

void ControlPanelCore::update_spectrum_hotspots(float dt) {
  if (!m_spectrum_hotspots_initialized) {
    m_spectrum_hotspots_initialized = true;
    std::uniform_real_distribution<float> pos_dist(0.1f, 0.9f);
    std::uniform_real_distribution<float> hold_dist(0.5f, 2.0f);
    for (int i = 0; i < SPECTRUM_HOTSPOT_COUNT; i++) {
      float p = pos_dist(m_spectrum_rng);
      m_spectrum_hotspots[i].position = p;
      m_spectrum_hotspots[i].start = p;
      m_spectrum_hotspots[i].target = p;
      m_spectrum_hotspots[i].transition = 1.0f;
      m_spectrum_hotspots[i].hold_remaining = hold_dist(m_spectrum_rng);
    }
  }

  std::uniform_real_distribution<float> pos_dist(0.05f, 0.95f);
  std::uniform_real_distribution<float> hold_dist(0.5f, 2.0f);

  for (int i = 0; i < SPECTRUM_HOTSPOT_COUNT; i++) {
    auto& h = m_spectrum_hotspots[i];
    if (h.transition < 1.0f) {
      h.transition += dt / 0.15f;  // 150ms snappy transition
      if (h.transition >= 1.0f) {
        h.transition = 1.0f;
        h.position = h.target;
        h.hold_remaining = hold_dist(m_spectrum_rng);
      } else {
        float t = h.transition;
        float ease = 1.0f - (1.0f - t) * (1.0f - t);  // ease-out
        h.position = h.start + (h.target - h.start) * ease;
      }
    } else {
      h.hold_remaining -= dt;
      if (h.hold_remaining <= 0.0f) {
        h.start = h.position;
        h.target = pos_dist(m_spectrum_rng);
        h.transition = 0.0f;
      }
    }
  }
}

static void apply_hotspot_gain(std::vector<float>& values, int bar_count,
                                const float* hotspot_positions, int hotspot_count) {
  if (bar_count <= 1) return;
  for (int i = 0; i < bar_count; i++) {
    float bar_pos = (float)i / (float)(bar_count - 1);
    float gain = 0.7f;
    for (int h = 0; h < hotspot_count; h++) {
      float dist = bar_pos - hotspot_positions[h];
      float sigma = 0.15f;
      float boost = 0.7f * std::exp(-(dist * dist) / (2.0f * sigma * sigma));
      gain += boost;
    }
    if (gain > 1.5f) gain = 1.5f;
    values[i] *= gain;
    if (values[i] > 1.0f) values[i] = 1.0f;
  }
}

void ControlPanelCore::update_spectrum_data() {
  // Advance hotspot wandering regardless of audio data availability
#ifdef ENABLE_SPECTRUM_HOTSPOT_GAIN
  auto now = std::chrono::steady_clock::now();
  float dt = std::chrono::duration<float>(now - m_spectrum_hotspot_last_time).count();
  if (dt > 0.1f) dt = 0.033f;  // clamp on first frame or after pause
  m_spectrum_hotspot_last_time = now;
  update_spectrum_hotspots(dt);
#endif  // ENABLE_SPECTRUM_HOTSPOT_GAIN

  // Resize bars array if panel width changed
  int area_w = m_rect_spectrum_full.right - m_rect_spectrum_full.left;
  if (area_w <= 0) area_w = m_rect_spectrum.right - m_rect_spectrum.left;
  int new_count = compute_spectrum_bar_count(area_w);
  if (new_count != m_spectrum_bar_count) {
    m_spectrum_bar_count = new_count;
    m_spectrum_bars.resize(new_count, 0.0f);
    m_spectrum_peaks.resize(new_count, 0.0f);
    m_spectrum_peak_velocity.resize(new_count, 0.0f);
    m_spectrum_bars_right.resize(new_count, 0.0f);
    m_spectrum_peaks_right.resize(new_count, 0.0f);
    m_spectrum_peak_velocity_right.resize(new_count, 0.0f);
    m_spectrum_normalized_values.resize(new_count, 0.0f);
  }
  if (m_spectrum_bar_count <= 0) return;

  // Try to get audio data; if unavailable, decay bars toward zero instead of freezing
  bool have_data = false;

#ifdef USE_SDFT_TRANSFORM
  // SDFT path: get raw PCM samples and process through per-bar resonators.
  audio_chunk_impl chunk;
  if (m_vis_stream.is_valid()) {
    double abs_time;
    if (m_vis_stream->get_absolute_time(abs_time)) {
      // Request ~50ms of PCM audio (covers at least one 30fps frame)
      if (m_vis_stream->get_chunk_absolute(chunk, abs_time, 0.05)) {
        const audio_sample* pcm_data = chunk.get_data();
        t_size pcm_count = chunk.get_sample_count();
        int pcm_nch = chunk.get_channels();
        int pcm_rate = (int)chunk.get_sample_rate();
        if (pcm_data && pcm_count > 0 && pcm_rate > 0) {
          update_sdft_bins(pcm_rate, m_spectrum_bar_count);
          process_sdft(pcm_data, (int)pcm_count, pcm_nch,
                       m_sdft_bins, m_spectrum_bar_count);
          have_data = true;
        }
      }
    }
  }

  if (!have_data) {
    m_spectrum_normalized_values.assign(m_spectrum_bar_count, 0.0f);
    apply_bar_dynamics(m_spectrum_normalized_values, m_spectrum_bar_count,
                        m_spectrum_bars, m_spectrum_peaks, m_spectrum_peak_velocity);
    return;
  }

  // Extract magnitudes from SDFT resonator complex states
  for (int i = 0; i < m_spectrum_bar_count; i++) {
    const auto& bin = m_sdft_bins[i];
    float magnitude = std::sqrt(bin.real * bin.real + bin.imag * bin.imag);
    // Normalize by resonator steady-state gain and apply A-weighting
    float normalized = magnitude / bin.norm * bin.a_weight_val;
    // Amplify and compress: sqrt for perceptual scaling.
    // SDFT output is smaller than FFT bin magnitudes, so boost 14x.
    float compressed = std::sqrt(normalized) * 14.0f;
    if (compressed > 0.7f)
      compressed = 0.7f + 0.3f * (1.0f - std::exp(-(compressed - 0.7f) / 0.3f));
    m_spectrum_normalized_values[i] = compressed;
  }

  apply_bar_dynamics(m_spectrum_normalized_values, m_spectrum_bar_count,
                      m_spectrum_bars, m_spectrum_peaks, m_spectrum_peak_velocity);

#else  // !USE_SDFT_TRANSFORM — original FFT path
  audio_chunk_impl chunk;
  const audio_sample* data = nullptr;
  t_size sample_count = 0;
  int nch = 1;

  if (m_vis_stream.is_valid()) {
    double abs_time;
    if (m_vis_stream->get_absolute_time(abs_time)) {
      if (m_vis_stream->get_spectrum_absolute(chunk, abs_time, SPECTRUM_FFT_SIZE)) {
        data = chunk.get_data();
        sample_count = chunk.get_sample_count();
        nch = chunk.get_channels();
        if (data && sample_count > 0) have_data = true;
      }
    }
  }

  if (!have_data) {
    // No audio data — apply dynamics with zero input so bars decay smoothly
    m_spectrum_normalized_values.assign(m_spectrum_bar_count, 0.0f);
    apply_bar_dynamics(m_spectrum_normalized_values, m_spectrum_bar_count,
                        m_spectrum_bars, m_spectrum_peaks, m_spectrum_peak_velocity);
    return;
  }

  // Precompute frequency bins and A-weighting factors if needed
  update_spectrum_bar_configs((int)sample_count);

  // Style 0=Mono, 1=Curve (both use mono data)
  bool stereo = false;

  float freq_min = 60.0f;
  float freq_max = 16000.0f;
  float bin_freq_step = 44100.0f / (float)SPECTRUM_FFT_SIZE;

  if (stereo) {
    // Stereo mirrored: process left and right channels separately
    std::vector<float> left_values, right_values;
    process_channel_bars(data, (int)sample_count, nch, 0,
                          m_spectrum_bar_count, freq_min, freq_max, bin_freq_step, left_values);
    process_channel_bars(data, (int)sample_count, nch, 1,
                          m_spectrum_bar_count, freq_min, freq_max, bin_freq_step, right_values);

    apply_bar_dynamics(left_values, m_spectrum_bar_count,
                        m_spectrum_bars, m_spectrum_peaks, m_spectrum_peak_velocity);
    apply_bar_dynamics(right_values, m_spectrum_bar_count,
                        m_spectrum_bars_right, m_spectrum_peaks_right, m_spectrum_peak_velocity_right);
  } else {
    // Mono: average all channels (with precalculated frequency bands and A-weighting)
    for (int i = 0; i < m_spectrum_bar_count; i++) {
      const auto& cfg = m_spectrum_bar_configs[i];
      float aw = cfg.a_weight;

      // Helper: interpolate a single bin value at a fractional position
      auto interp_bin = [&](float fbin) -> float {
        int b0 = (int)fbin;
        int b1 = b0 + 1;
        if (b1 >= (int)sample_count) b1 = b0;
        float t = fbin - (float)b0;
        if (nch >= 2) {
          float avg0 = 0.0f, avg1 = 0.0f;
          for (int ch = 0; ch < nch; ch++) { avg0 += data[b0 * nch + ch]; avg1 += data[b1 * nch + ch]; }
          avg0 /= (float)nch; avg1 /= (float)nch;
          return (avg0 + (avg1 - avg0) * t) * aw;
        } else {
          return (data[b0] + (data[b1] - data[b0]) * t) * aw;
        }
      };

      float magnitude = 0.0f;
      if (cfg.is_narrow) {
        // Narrow band: interpolate at center frequency for smooth transitions
        magnitude = interp_bin(cfg.fbin_center);
      } else {
        // Wide band: interpolate at edges, peak across whole interior bins
        magnitude = std::max(interp_bin(cfg.fbin_lo), interp_bin(cfg.fbin_hi));
        for (int b = cfg.bin_lo + 1; b < cfg.bin_hi; b++) {
          float val;
          if (nch >= 2) {
            float avg = 0.0f;
            for (int ch = 0; ch < nch; ch++) avg += data[b * nch + ch];
            avg /= (float)nch;
            val = avg * aw;
          } else {
            val = data[b] * aw;
          }
          if (val > magnitude) magnitude = val;
        }
      }

      // Amplify and compress: sqrt for perceptual scaling, 3x boost.
      // Soft ceiling: linear up to 0.7, then exponential taper so bars
      // never quite hit the top of the spectrum area (no hard cutoff).
      float normalized = std::sqrt(magnitude) * 3.0f;
      if (normalized > 0.7f)
        normalized = 0.7f + 0.3f * (1.0f - std::exp(-(normalized - 0.7f) / 0.3f));
      m_spectrum_normalized_values[i] = normalized;
    }

#ifdef ENABLE_SPECTRUM_HOTSPOT_GAIN
    float hotspot_positions[SPECTRUM_HOTSPOT_COUNT];
    for (int i = 0; i < SPECTRUM_HOTSPOT_COUNT; i++)
      hotspot_positions[i] = m_spectrum_hotspots[i].position;
    apply_hotspot_gain(m_spectrum_normalized_values, m_spectrum_bar_count,
                        hotspot_positions, SPECTRUM_HOTSPOT_COUNT);
#endif  // ENABLE_SPECTRUM_HOTSPOT_GAIN

    apply_bar_dynamics(m_spectrum_normalized_values, m_spectrum_bar_count,
                        m_spectrum_bars, m_spectrum_peaks, m_spectrum_peak_velocity);
  }
#endif  // USE_SDFT_TRANSFORM
}

static COLORREF hsl_to_rgb(float h, float s, float l) {
    float c = (1.0f - fabsf(2.0f * l - 1.0f)) * s;
    float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
    float m = l - c / 2.0f;
    float r1, g1, b1;
    if (h < 60)       { r1=c; g1=x; b1=0; }
    else if (h < 120) { r1=x; g1=c; b1=0; }
    else if (h < 180) { r1=0; g1=c; b1=x; }
    else if (h < 240) { r1=0; g1=x; b1=c; }
    else if (h < 300) { r1=x; g1=0; b1=c; }
    else               { r1=c; g1=0; b1=x; }
    return RGB((BYTE)((r1+m)*255), (BYTE)((g1+m)*255), (BYTE)((b1+m)*255));
}

static void rgb_to_hsl(BYTE r, BYTE g, BYTE b, float& h, float& s, float& l) {
    float rf = r / 255.0f;
    float gf = g / 255.0f;
    float bf = b / 255.0f;
    float max_val = std::max({rf, gf, bf});
    float min_val = std::min({rf, gf, bf});
    float delta = max_val - min_val;

    l = (max_val + min_val) / 2.0f;

    if (delta <= 0.0001f) {
        h = 0.0f;
        s = 0.0f;
    } else {
        s = (l > 0.5f) ? (delta / (2.0f - max_val - min_val))
                       : (delta / (max_val + min_val));
        if (max_val == rf) {
            h = (gf - bf) / delta + (gf < bf ? 6.0f : 0.0f);
        } else if (max_val == gf) {
            h = (bf - rf) / delta + 2.0f;
        } else {
            h = (rf - gf) / delta + 4.0f;
        }
        h *= 60.0f;
    }
}

static COLORREF adjust_adaptive_artwork_color(const Gdiplus::Color& src_color, bool is_dark_bg, bool is_secondary = false) {
    float h, s, l;
    rgb_to_hsl(src_color.GetR(), src_color.GetG(), src_color.GetB(), h, s, l);

    if (is_dark_bg) {
        // On dark backgrounds (Dark mode, Blurred Artwork, Artwork Colors)
        if (s < 0.12f) {
            // Grayscale / monochrome artwork (e.g. black & white covers)
            // Elevate lightness to a bright, readable silver/white
            float target_l = is_secondary ? 0.55f : 0.70f;
            l = std::max(l, target_l);
        } else {
            // Colored artwork: boost saturation slightly if diluted and ensure vivid lightness
            s = std::max(s, 0.45f);
            float min_l = is_secondary ? 0.42f : 0.55f;
            float max_l = is_secondary ? 0.65f : 0.78f;
            l = std::clamp(l, min_l, max_l);
        }
    } else {
        // On light backgrounds (Light theme with solid background)
        if (s < 0.12f) {
            // Grayscale: darken to charcoal/slate
            float target_l = is_secondary ? 0.40f : 0.28f;
            l = std::min(l, target_l);
        } else {
            s = std::max(s, 0.45f);
            float min_l = is_secondary ? 0.25f : 0.30f;
            float max_l = is_secondary ? 0.48f : 0.45f;
            l = std::clamp(l, min_l, max_l);
        }
    }

    return hsl_to_rgb(h, s, l);
}

COLORREF ControlPanelCore::get_spectrum_primary_color() const {
  int mode = get_nowbar_spectrum_gradient_mode();
  if (mode != 0 && get_nowbar_custom_spectrum_color_enabled()) {
    return get_nowbar_spectrum_color();
  }
  if (m_artwork_colors_valid) {
    int bg_style = get_nowbar_background_style();
    bool is_dark_bg = m_dark_mode || (bg_style == 1) || (bg_style == 2);
    return adjust_adaptive_artwork_color(m_artwork_color_primary, is_dark_bg, false);
  }
  return m_theme_highlight;
}

COLORREF ControlPanelCore::get_spectrum_secondary_color() const {
  int mode = get_nowbar_spectrum_gradient_mode();
  if (mode != 0 && get_nowbar_custom_spectrum_color_enabled()) {
    return get_nowbar_spectrum_color2();
  }
  if (m_artwork_colors_valid) {
    int bg_style = get_nowbar_background_style();
    bool is_dark_bg = m_dark_mode || (bg_style == 1) || (bg_style == 2);
    return adjust_adaptive_artwork_color(m_artwork_color_secondary, is_dark_bg, true);
  }
  return get_nowbar_spectrum_color2();
}

void ControlPanelCore::draw_spectrum(Gdiplus::Graphics& g) {
  if (!get_nowbar_spectrum_visible()) return;
  if (m_rect_spectrum.right <= m_rect_spectrum.left) return;

  // Process fade animation
  if (m_spectrum_fade_active) {
    auto now = std::chrono::steady_clock::now();
    float elapsed = std::chrono::duration<float, std::milli>(now - m_spectrum_fade_start_time).count();
    float t = elapsed / SPECTRUM_FADE_DURATION_MS;
    if (t >= 1.0f) {
      t = 1.0f;
      m_spectrum_fade_active = false;
      m_spectrum_opacity = m_spectrum_target_opacity;
      // Release stream after fade-out completes
      if (m_spectrum_target_opacity == 0.0f) {
        release_vis_stream();
        m_spectrum_animating = false;
      }
    } else {
      // Ease-out: t' = 1 - (1-t)^2
      float ease = 1.0f - (1.0f - t) * (1.0f - t);
      m_spectrum_opacity = m_spectrum_start_opacity + (m_spectrum_target_opacity - m_spectrum_start_opacity) * ease;
    }
  }

  if (m_spectrum_opacity <= 0.001f) return;

  // Update spectrum data from vis stream
  update_spectrum_data();

  // Keep animating while spectrum is visible (need continuous redraws for FFT data)
  if (m_spectrum_opacity > 0.0f && m_vis_stream.is_valid()) {
    m_spectrum_animating = true;
    request_animation();
  }

  int area_w = m_rect_spectrum.right - m_rect_spectrum.left;
  int area_h = m_rect_spectrum.bottom - m_rect_spectrum.top;

  // Fixed-pixel bar sizing
  int spec_width = get_nowbar_spectrum_width();
  int bar_w_i, gap_w_i;
  if (spec_width == 0) { bar_w_i = 2; gap_w_i = 1; }
  else if (spec_width == 2) { bar_w_i = 8; gap_w_i = 3; }
  else { bar_w_i = 4; gap_w_i = 2; }

  int unit_w = bar_w_i + gap_w_i;
  int bar_count = m_spectrum_bar_count;
  if (bar_count <= 0) return;

  int spec_style = get_nowbar_spectrum_style();

  // Curve mode: delegate to curve renderer
  if (spec_style == 1) {
    draw_spectrum_curve(g, m_rect_spectrum);
    return;
  }

  bool stereo = false;

  // Colors from preferences
  COLORREF spec_color = get_spectrum_primary_color();
  int user_alpha = get_nowbar_spectrum_opacity() * 255 / 100;
  int alpha = (int)(user_alpha * m_spectrum_opacity);
  if (alpha > 255) alpha = 255;
  if (alpha < 0) alpha = 0;
  BYTE r1 = GetRValue(spec_color), g1 = GetGValue(spec_color), b1 = GetBValue(spec_color);

  int gradient_mode = get_nowbar_spectrum_gradient_mode();
  COLORREF spec_color2 = get_spectrum_secondary_color();
  BYTE r2 = GetRValue(spec_color2), g2 = GetGValue(spec_color2), b2 = GetBValue(spec_color2);

  // Stereo default colors: warm left (red-orange), cool right (teal-green)
  BYTE lr1 = 255, lg1 = 100, lb1 = 50;   // warm top
  BYTE lr2 = 255, lg2 = 180, lb2 = 50;   // warm bottom
  BYTE rr1 = 50, rg1 = 200, rb1 = 180;   // cool top
  BYTE rr2 = 50, rg2 = 255, rb2 = 200;   // cool bottom

  float bar_w = (float)bar_w_i;

  // Disable anti-aliasing so bar edges don't bleed into the gaps
  Gdiplus::SmoothingMode oldSmoothing = g.GetSmoothingMode();
  Gdiplus::PixelOffsetMode oldPixelOffset = g.GetPixelOffsetMode();
  g.SetSmoothingMode(Gdiplus::SmoothingModeNone);
  g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeNone);

  float bottom_f = (float)m_rect_spectrum.bottom;

  // Lambda to draw one bar given position, value, color params, and channel side
  auto draw_bar = [&](float x, float value, int bar_idx, int half_count, bool is_right) {
    float height = value * (float)area_h;
    if (height < 1.0f) return;
    float y = bottom_f - height;

    // Determine bar color
    BYTE cr1, cg1, cb1, cr2_g, cg2_g, cb2_g;
    if (stereo && gradient_mode != 3) {
      if (gradient_mode == 2) {
        // Gradient mode: use user colors for left, color2 for right
        cr1 = is_right ? r2 : r1; cg1 = is_right ? g2 : g1; cb1 = is_right ? b2 : b1;
        cr2_g = is_right ? r2 : r1; cg2_g = is_right ? g2 : g1; cb2_g = is_right ? b2 : b1;
      } else {
        // Solid/Adaptive: use built-in warm/cool colors
        cr1 = is_right ? rr1 : lr1; cg1 = is_right ? rg1 : lg1; cb1 = is_right ? rb1 : lb1;
        cr2_g = cr1; cg2_g = cg1; cb2_g = cb1;
      }
    } else {
      cr1 = r1; cg1 = g1; cb1 = b1;
      cr2_g = r2; cg2_g = g2; cb2_g = b2;
    }

    if (gradient_mode == 3) {
      float hue = (half_count > 1) ? (float)bar_idx / (float)(half_count - 1) * 300.0f : 0.0f;
      if (stereo && is_right) hue = 300.0f - hue;  // Mirror frequency colors
      COLORREF freq_color = hsl_to_rgb(hue, 0.9f, 0.55f);
      Gdiplus::SolidBrush barBrush(Gdiplus::Color(alpha, GetRValue(freq_color), GetGValue(freq_color), GetBValue(freq_color)));
      g.FillRectangle(&barBrush, x, y, bar_w, height);
    } else if (gradient_mode == 2 && !stereo) {
      Gdiplus::LinearGradientBrush barBrush(
          Gdiplus::PointF(x, y), Gdiplus::PointF(x, bottom_f),
          Gdiplus::Color(alpha, cr1, cg1, cb1),
          Gdiplus::Color(alpha, cr2_g, cg2_g, cb2_g));
      g.FillRectangle(&barBrush, x, y, bar_w, height);
    } else {
      Gdiplus::SolidBrush barBrush(Gdiplus::Color(alpha, cr1, cg1, cb1));
      g.FillRectangle(&barBrush, x, y, bar_w, height);
    }
  };

  // Lambda to draw peak indicator (not used in Dominoes mode)
  auto draw_peak = [&](float x, float peak_val, float bar_val, int bar_idx, int half_count, bool is_right) {
    if (spec_style == 2) return;  // No peaks in Dominoes mode
    if (peak_val <= bar_val) return;
    float peak_y = bottom_f - peak_val * (float)area_h;
    BYTE pr, pg, pb;
    if (gradient_mode == 3) {
      float hue = (half_count > 1) ? (float)bar_idx / (float)(half_count - 1) * 300.0f : 0.0f;
      if (stereo && is_right) hue = 300.0f - hue;
      COLORREF freq_color = hsl_to_rgb(hue, 0.9f, 0.55f);
      pr = GetRValue(freq_color); pg = GetGValue(freq_color); pb = GetBValue(freq_color);
    } else if (stereo && gradient_mode != 2) {
      pr = is_right ? rr1 : lr1; pg = is_right ? rg1 : lg1; pb = is_right ? rb1 : lb1;
    } else if (stereo && gradient_mode == 2) {
      pr = is_right ? r2 : r1; pg = is_right ? g2 : g1; pb = is_right ? b2 : b1;
    } else {
      pr = r1; pg = g1; pb = b1;
    }
    Gdiplus::SolidBrush peakBrush(Gdiplus::Color(alpha, pr, pg, pb));
    g.FillRectangle(&peakBrush, x, peak_y, bar_w, 2.0f);
  };

  if (stereo) {
    int half = bar_count / 2;
    if (half <= 0) half = 1;
    int left_used = half * unit_w - gap_w_i;
    int right_used = half * unit_w - gap_w_i;
    int center_x = m_rect_spectrum.left + area_w / 2;
    int left_start = center_x - left_used;
    int right_start = center_x;

    // Left half: bars grow from center toward left (bar 0 at center)
    for (int i = 0; i < half; i++) {
      if (i >= (int)m_spectrum_bars.size()) break;
      float x = (float)(center_x - (i + 1) * unit_w + gap_w_i);
      draw_bar(x, m_spectrum_bars[i], i, half, false);
      if (i < (int)m_spectrum_peaks.size())
        draw_peak(x, m_spectrum_peaks[i], m_spectrum_bars[i], i, half, false);
    }

    // Right half: bars grow from center toward right (bar 0 at center)
    for (int i = 0; i < half; i++) {
      if (i >= (int)m_spectrum_bars_right.size()) break;
      float x = (float)(center_x + i * unit_w);
      draw_bar(x, m_spectrum_bars_right[i], i, half, true);
      if (i < (int)m_spectrum_peaks_right.size())
        draw_peak(x, m_spectrum_peaks_right[i], m_spectrum_bars_right[i], i, half, true);
    }
  } else {
    int total_used = bar_count * unit_w - gap_w_i;
    int margin_left = (area_w - total_used) / 2;
    if (margin_left < 0) margin_left = 0;

    for (int i = 0; i < bar_count; i++) {
      if (i >= (int)m_spectrum_bars.size()) break;
      float x = (float)(m_rect_spectrum.left + margin_left + i * unit_w);
      draw_bar(x, m_spectrum_bars[i], i, bar_count, false);
      if (i < (int)m_spectrum_peaks.size())
        draw_peak(x, m_spectrum_peaks[i], m_spectrum_bars[i], i, bar_count, false);
    }
  }

  g.SetSmoothingMode(oldSmoothing);
  g.SetPixelOffsetMode(oldPixelOffset);
}

void ControlPanelCore::draw_spectrum_curve(Gdiplus::Graphics& g, const RECT& area_rect) {
  int area_w = area_rect.right - area_rect.left;
  int area_h = area_rect.bottom - area_rect.top;
  if (area_w <= 0 || area_h <= 0 || m_spectrum_bar_count <= 1) return;

  // Resample bar heights to SPECTRUM_CURVE_POINTS control points
  int num_pts = SPECTRUM_CURVE_POINTS;
  if (num_pts > m_spectrum_bar_count) num_pts = m_spectrum_bar_count;
  if (num_pts < 2) return;

  Gdiplus::PointF points[SPECTRUM_CURVE_POINTS];
  float bottom_f = (float)area_rect.bottom;
  float left_f = (float)area_rect.left;

  for (int i = 0; i < num_pts; i++) {
    float t = (float)i / (float)(num_pts - 1);
    float bar_idx_f = t * (m_spectrum_bar_count - 1);
    int idx0 = (int)bar_idx_f;
    int idx1 = std::min(idx0 + 1, m_spectrum_bar_count - 1);
    float frac = bar_idx_f - (float)idx0;
    float value = m_spectrum_bars[idx0] * (1.0f - frac) + m_spectrum_bars[idx1] * frac;

    float x = left_f + t * (float)area_w;
    float y = bottom_f - value * (float)area_h;
    points[i] = Gdiplus::PointF(x, y);
  }

  // Build Bezier control points from Catmull-Rom spline
  // For N points we get (N-1) cubic segments = 1 + (N-1)*3 Bezier points
  constexpr int max_bezier_pts = 1 + (SPECTRUM_CURVE_POINTS - 1) * 3;
  Gdiplus::PointF bezier[max_bezier_pts];
  int bezier_count = 0;
  bezier[bezier_count++] = points[0];

  for (int i = 0; i < num_pts - 1; i++) {
    // Catmull-Rom tangents with boundary clamping
    Gdiplus::PointF pm1 = (i > 0) ? points[i - 1] : points[0];
    Gdiplus::PointF p0 = points[i];
    Gdiplus::PointF p1 = points[i + 1];
    Gdiplus::PointF p2 = (i + 2 < num_pts) ? points[i + 2] : points[num_pts - 1];

    // Convert Catmull-Rom to cubic Bezier control points
    Gdiplus::PointF cp1(
      p0.X + (p1.X - pm1.X) / 6.0f,
      p0.Y + (p1.Y - pm1.Y) / 6.0f
    );
    Gdiplus::PointF cp2(
      p1.X - (p2.X - p0.X) / 6.0f,
      p1.Y - (p2.Y - p0.Y) / 6.0f
    );

    bezier[bezier_count++] = cp1;
    bezier[bezier_count++] = cp2;
    bezier[bezier_count++] = p1;
  }

  // Build filled path: curve on top, straight line along baseline
  Gdiplus::GraphicsPath curvePath;
  curvePath.AddBeziers(bezier, bezier_count);
  // Close along baseline
  curvePath.AddLine(points[num_pts - 1].X, bottom_f, left_f, bottom_f);
  curvePath.CloseFigure();

  // Color setup
  COLORREF spec_color = get_spectrum_primary_color();
  int user_alpha = get_nowbar_spectrum_opacity() * 255 / 100;
  int alpha = (int)(user_alpha * m_spectrum_opacity * m_spectrum_hover_opacity);
  if (alpha > 255) alpha = 255;
  if (alpha < 0) alpha = 0;
  BYTE cr = GetRValue(spec_color), cg = GetGValue(spec_color), cb = GetBValue(spec_color);

  int gradient_mode = get_nowbar_spectrum_gradient_mode();
  COLORREF spec_color2 = get_spectrum_secondary_color();
  BYTE cr2 = GetRValue(spec_color2), cg2 = GetGValue(spec_color2), cb2 = GetBValue(spec_color2);

  // Enable anti-aliasing for smooth curves
  Gdiplus::SmoothingMode oldSmoothing = g.GetSmoothingMode();
  Gdiplus::PixelOffsetMode oldPixelOffset = g.GetPixelOffsetMode();
  g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
  g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);

  // Fill with gradient: full alpha at top fading to transparent at bottom
  if (gradient_mode == 3) {
    // Frequency-mapped: use a multi-stop horizontal gradient
    Gdiplus::LinearGradientBrush fillBrush(
        Gdiplus::PointF(left_f, 0), Gdiplus::PointF(left_f + (float)area_w, 0),
        Gdiplus::Color(alpha, 255, 80, 50),
        Gdiplus::Color(alpha, 80, 50, 255));
    // Multi-stop rainbow
    Gdiplus::Color colors[] = {
        Gdiplus::Color(alpha, 255, 50, 50),    // red (bass)
        Gdiplus::Color(alpha, 255, 180, 50),   // orange
        Gdiplus::Color(alpha, 50, 220, 100),   // green (mid)
        Gdiplus::Color(alpha, 50, 150, 255),   // blue
        Gdiplus::Color(alpha, 140, 50, 255)    // violet (treble)
    };
    float positions[] = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
    fillBrush.SetInterpolationColors(colors, positions, 5);
    g.FillPath(&fillBrush, &curvePath);
  } else if (gradient_mode == 2) {
    // User gradient: top color to bottom color (vertical)
    Gdiplus::LinearGradientBrush fillBrush(
        Gdiplus::PointF(0, (float)area_rect.top),
        Gdiplus::PointF(0, bottom_f),
        Gdiplus::Color(alpha, cr, cg, cb),
        Gdiplus::Color(alpha / 4, cr2, cg2, cb2));
    g.FillPath(&fillBrush, &curvePath);
  } else {
    // Solid: top = full alpha, bottom = faded
    Gdiplus::LinearGradientBrush fillBrush(
        Gdiplus::PointF(0, (float)area_rect.top),
        Gdiplus::PointF(0, bottom_f),
        Gdiplus::Color(alpha, cr, cg, cb),
        Gdiplus::Color(alpha / 4, cr, cg, cb));
    g.FillPath(&fillBrush, &curvePath);
  }

  // Stroke the curve top edge (brighter, 2px)
  int stroke_alpha = std::min(255, alpha + 40);
  BYTE bright_r = (BYTE)std::min(255, (int)cr + 30);
  BYTE bright_g = (BYTE)std::min(255, (int)cg + 30);
  BYTE bright_b = (BYTE)std::min(255, (int)cb + 30);

  if (gradient_mode == 3) {
    // Rainbow stroke
    Gdiplus::LinearGradientBrush strokeBrush(
        Gdiplus::PointF(left_f, 0), Gdiplus::PointF(left_f + (float)area_w, 0),
        Gdiplus::Color(stroke_alpha, 255, 80, 50),
        Gdiplus::Color(stroke_alpha, 80, 50, 255));
    Gdiplus::Color colors[] = {
        Gdiplus::Color(stroke_alpha, 255, 80, 80),
        Gdiplus::Color(stroke_alpha, 255, 200, 80),
        Gdiplus::Color(stroke_alpha, 80, 240, 130),
        Gdiplus::Color(stroke_alpha, 80, 170, 255),
        Gdiplus::Color(stroke_alpha, 160, 80, 255)
    };
    float positions[] = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
    strokeBrush.SetInterpolationColors(colors, positions, 5);
    Gdiplus::Pen strokePen(&strokeBrush, 2.0f);
    Gdiplus::GraphicsPath strokePath;
    strokePath.AddBeziers(bezier, bezier_count);
    g.DrawPath(&strokePen, &strokePath);
  } else {
    Gdiplus::Pen strokePen(Gdiplus::Color(stroke_alpha, bright_r, bright_g, bright_b), 2.0f);
    Gdiplus::GraphicsPath strokePath;
    strokePath.AddBeziers(bezier, bezier_count);
    g.DrawPath(&strokePen, &strokePath);
  }

  g.SetSmoothingMode(oldSmoothing);
  g.SetPixelOffsetMode(oldPixelOffset);
}

void ControlPanelCore::update_thin_progress_hover_state() {
  bool is_thin_mode = (get_nowbar_visualization_mode() == 1) && get_nowbar_seekbar_visible();
  bool hovered = (m_hover_region == HitRegion::ThinProgressBar);
  bool active = (hovered || (m_seeking && m_pressed_region == HitRegion::ThinProgressBar)) && is_thin_mode;

  float target = active ? 1.0f : 0.0f;
  if (m_thin_progress_target_progress != target) {
    m_thin_progress_target_progress = target;
    m_thin_progress_start_progress = m_thin_progress_hover_progress;
    if (get_nowbar_smooth_animations_enabled()) {
      m_thin_progress_anim_start_time = std::chrono::steady_clock::now();
      float diff = std::abs(target - m_thin_progress_start_progress);
      m_thin_progress_anim_duration_ms = THIN_PROGRESS_ANIM_DURATION_MS * std::max(0.3f, diff);
      m_thin_progress_anim_active = true;
      m_seekbar_animating = true;
      m_needs_full_repaint = true;
      request_animation();
      invalidate();
    } else {
      m_thin_progress_hover_progress = target;
      m_thin_progress_anim_active = false;
      m_seekbar_animating = false;
      m_needs_full_repaint = true;
      invalidate();
    }
  }
}

void ControlPanelCore::update_thin_progress_hover_animation() {
  if (!get_nowbar_smooth_animations_enabled()) {
    m_thin_progress_hover_progress = m_thin_progress_target_progress;
    m_thin_progress_anim_active = false;
    m_seekbar_animating = false;
    return;
  }

  if (!m_thin_progress_anim_active) {
    m_seekbar_animating = false;
    return;
  }

  auto now = std::chrono::steady_clock::now();
  float elapsed_ms = std::chrono::duration<float, std::milli>(now - m_thin_progress_anim_start_time).count();
  float dur = m_thin_progress_anim_duration_ms > 0.0f ? m_thin_progress_anim_duration_ms : THIN_PROGRESS_ANIM_DURATION_MS;
  float t = std::min(1.0f, elapsed_ms / dur);

  // Smooth quadratic ease-out interpolation
  float ease = 1.0f - (1.0f - t) * (1.0f - t);

  m_thin_progress_hover_progress = m_thin_progress_start_progress +
      ease * (m_thin_progress_target_progress - m_thin_progress_start_progress);

  if (t >= 1.0f) {
    m_thin_progress_hover_progress = m_thin_progress_target_progress;
    m_thin_progress_anim_active = false;
    m_seekbar_animating = false;
  } else {
    m_seekbar_animating = true;
    m_needs_full_repaint = true;
    request_animation();
  }
}

void ControlPanelCore::draw_thin_progress_bar(Gdiplus::Graphics& g) {
  if (m_rect_thin_progress.right <= m_rect_thin_progress.left) return;

  bool hovered = (m_hover_region == HitRegion::ThinProgressBar);
  bool active = hovered || (m_seeking && m_pressed_region == HitRegion::ThinProgressBar);

  int w = m_rect_thin_progress.right - m_rect_thin_progress.left;
  int base_h = m_rect_thin_progress.bottom - m_rect_thin_progress.top;
  // Smoothly animated height: interpolates from base_h to base_h * 2
  int h = base_h + static_cast<int>(std::round(base_h * m_thin_progress_hover_progress));
  int top = m_rect_thin_progress.top;

  // Background: semi-transparent, theme-aware (including artwork colors)
  Gdiplus::Color bgColor;
  int bg_style = get_nowbar_background_style();
  bool has_artwork_bg = (bg_style == 1 || bg_style == 2) && m_artwork_colors_valid;
  BYTE alpha = static_cast<BYTE>(140 + std::round(60.0f * m_thin_progress_hover_progress));

  if (get_nowbar_custom_progress_track_enabled()) {
    // Custom color overrides everything
    COLORREF tc = get_nowbar_progress_track_color();
    bgColor = Gdiplus::Color(alpha, GetRValue(tc), GetGValue(tc), GetBValue(tc));
  } else if (has_artwork_bg) {
    // Artwork backgrounds: approximate the rendered background color
    int r = m_artwork_color_primary.GetR();
    int g = m_artwork_color_primary.GetG();
    int b = m_artwork_color_primary.GetB();
    BYTE ov = (bg_style == 1) ? (m_dark_mode ? 120 : 80)
                               : (m_dark_mode ? 140 : 180);
    r = r * (255 - ov) / 255 + 15;
    g = g * (255 - ov) / 255 + 15;
    b = b * (255 - ov) / 255 + 15;
    bgColor = Gdiplus::Color(alpha, std::min(r, 255), std::min(g, 255), std::min(b, 255));
  } else {
    // Solid background: use theme-appropriate track color
    bgColor = Gdiplus::Color(alpha, GetRValue(m_track_color),
        GetGValue(m_track_color), GetBValue(m_track_color));
  }
  Gdiplus::SolidBrush bgBrush(bgColor);
  g.FillRectangle(&bgBrush, m_rect_thin_progress.left, top, w, h);

  if (!m_artwork_thumbnail && get_nowbar_cover_artwork_visible() &&
      m_rect_artwork.right > m_rect_artwork.left) {
    int cover_x = std::max(static_cast<int>(m_rect_thin_progress.left), static_cast<int>(m_rect_artwork.left));
    int cover_right = std::min(static_cast<int>(m_rect_thin_progress.right), static_cast<int>(m_rect_artwork.right));
    int cover_w = cover_right - cover_x;
    int cover_top = std::max(top, static_cast<int>(m_rect_artwork.top));
    int cover_bottom = std::min(top + h, static_cast<int>(m_rect_artwork.bottom));
    int cover_h = cover_bottom - cover_top;
    if (cover_w > 0 && cover_h > 0) {
      BYTE r = static_cast<BYTE>((bgColor.GetR() * alpha + m_bg_color.GetR() * (255 - alpha)) / 255);
      BYTE g_val = static_cast<BYTE>((bgColor.GetG() * alpha + m_bg_color.GetG() * (255 - alpha)) / 255);
      BYTE b = static_cast<BYTE>((bgColor.GetB() * alpha + m_bg_color.GetB() * (255 - alpha)) / 255);
      Gdiplus::SolidBrush coverTrackBrush(Gdiplus::Color(255, r, g_val, b));
      g.FillRectangle(&coverTrackBrush, cover_x, cover_top, cover_w, cover_h);
    }
  }

  // Progress fill
  double progress;
  if (m_seeking && m_pressed_region == HitRegion::ThinProgressBar) {
    progress = (m_state.track_length > 0)
                   ? (m_state.playback_time / m_state.track_length)
                   : 0.0;
  } else {
    progress = m_target_progress;
  }
  progress = std::max(0.0, std::min(1.0, progress));
  int progress_w = static_cast<int>(w * progress);

  if (progress_w > 0) {
    COLORREF prog_accent = get_nowbar_custom_progress_accent_enabled()
        ? get_nowbar_progress_accent_color() : m_theme_highlight;
    Gdiplus::SolidBrush progressBrush(
        Gdiplus::Color(255, GetRValue(prog_accent), GetGValue(prog_accent), GetBValue(prog_accent)));
    g.FillRectangle(&progressBrush, m_rect_thin_progress.left, top, progress_w, h);
  }

  // Seek handle (solid block at progress position on hover/seeking) - uses highlight color
  if (m_thin_progress_hover_progress > 0.01f && m_state.track_length > 0) {
    int handle_w = static_cast<int>(8 * m_dpi_scale);
    int handle_x = m_rect_thin_progress.left + progress_w - handle_w / 2;
    COLORREF handle_accent = get_nowbar_custom_progress_accent_enabled()
        ? get_nowbar_progress_accent_color() : m_theme_highlight;
    BYTE handle_alpha = static_cast<BYTE>(std::round(255.0f * m_thin_progress_hover_progress));
    Gdiplus::SolidBrush handleBrush(Gdiplus::Color(handle_alpha,
        GetRValue(handle_accent), GetGValue(handle_accent), GetBValue(handle_accent)));
    g.FillRectangle(&handleBrush, handle_x, top, handle_w, h);
  }
}

void ControlPanelCore::draw_thin_progress_tooltip(Gdiplus::Graphics& g) {
  if (m_rect_thin_progress.right <= m_rect_thin_progress.left) return;
  if (m_state.track_length <= 0) return;

  bool hovered = (m_hover_region == HitRegion::ThinProgressBar);
  bool active = hovered || (m_seeking && m_pressed_region == HitRegion::ThinProgressBar);
  if (!active) return;

  int w = m_rect_thin_progress.right - m_rect_thin_progress.left;
  int base_h = m_rect_thin_progress.bottom - m_rect_thin_progress.top;
  int h = base_h + static_cast<int>(std::round(base_h * m_thin_progress_hover_progress));
  int top = m_rect_thin_progress.top;

  double progress;
  if (m_seeking && m_pressed_region == HitRegion::ThinProgressBar) {
    progress = (m_state.track_length > 0)
                   ? (m_state.playback_time / m_state.track_length)
                   : 0.0;
  } else {
    progress = m_target_progress;
  }
  progress = std::max(0.0, std::min(1.0, progress));
  int progress_w = static_cast<int>(w * progress);

  double preview = m_seeking ? m_state.playback_time : m_target_progress * m_state.track_length;
  // On hover (not seeking), compute from cursor X
  if (hovered && !m_seeking) {
    double pos = static_cast<double>(m_seekbar_hover_x - m_rect_thin_progress.left) / w;
    pos = std::max(0.0, std::min(1.0, pos));
    preview = pos * m_state.track_length;
  }
  std::wstring timeStr = format_time(preview);
  Gdiplus::Font tooltipFont(L"Segoe UI", 10.0f * m_dpi_scale, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
  Gdiplus::RectF textBounds;
  g.MeasureString(timeStr.c_str(), -1, &tooltipFont, Gdiplus::PointF(0, 0), &textBounds);
  int padding_h = static_cast<int>(6 * m_dpi_scale);
  int padding_v = static_cast<int>(3 * m_dpi_scale);
  int tooltip_w = static_cast<int>(textBounds.Width) + padding_h * 2;
  int tooltip_h = static_cast<int>(textBounds.Height) + padding_v * 2;
  int cursor_x = m_seeking ? (m_rect_thin_progress.left + progress_w) : m_seekbar_hover_x;
  int tooltip_x = cursor_x - tooltip_w / 2;
  int tooltip_y = top + h + static_cast<int>(20 * m_dpi_scale);
  tooltip_x = std::max((int)m_rect_thin_progress.left, std::min(tooltip_x, (int)m_rect_thin_progress.right - tooltip_w));
  Gdiplus::Color bgColor = m_dark_mode ? Gdiplus::Color(220, 60, 60, 60) : Gdiplus::Color(220, 40, 40, 40);
  Gdiplus::SolidBrush tooltipBgBrush(bgColor);
  int corner = static_cast<int>(4 * m_dpi_scale);
  Gdiplus::GraphicsPath tooltipPath;
  tooltipPath.AddArc(tooltip_x, tooltip_y, corner * 2, corner * 2, 180, 90);
  tooltipPath.AddArc(tooltip_x + tooltip_w - corner * 2, tooltip_y, corner * 2, corner * 2, 270, 90);
  tooltipPath.AddArc(tooltip_x + tooltip_w - corner * 2, tooltip_y + tooltip_h - corner * 2, corner * 2, corner * 2, 0, 90);
  tooltipPath.AddArc(tooltip_x, tooltip_y + tooltip_h - corner * 2, corner * 2, corner * 2, 90, 90);
  tooltipPath.CloseFigure();
  g.FillPath(&tooltipBgBrush, &tooltipPath);
  Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, 255, 255, 255));
  Gdiplus::StringFormat sf;
  sf.SetAlignment(Gdiplus::StringAlignmentCenter);
  sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
  Gdiplus::RectF textRect(static_cast<float>(tooltip_x), static_cast<float>(tooltip_y),
                          static_cast<float>(tooltip_w), static_cast<float>(tooltip_h));
  g.DrawString(timeStr.c_str(), -1, &tooltipFont, textRect, &sf, &textBrush);
}

void ControlPanelCore::update_spectrum_hover_opacity() {
  if (!get_nowbar_smooth_animations_enabled()) {
    m_spectrum_hover_opacity = m_spectrum_hover_target;
    return;
  }

  auto now = std::chrono::steady_clock::now();
  if (m_spectrum_hover_last_time.time_since_epoch().count() == 0) {
    m_spectrum_hover_last_time = now;
  }
  float dt_ms = std::chrono::duration<float, std::milli>(now - m_spectrum_hover_last_time).count();
  m_spectrum_hover_last_time = now;

  // Clamp dt to avoid large jumps across pauses / stalls (max 100ms)
  if (dt_ms > 100.0f) dt_ms = 100.0f;
  if (dt_ms <= 0.0f) dt_ms = 1.0f;

  float target = m_spectrum_hover_target;
  if (std::abs(m_spectrum_hover_opacity - target) > 0.001f) {
    // Asymmetric tau: fast dim (100ms) for responsive feedback,
    // smooth restore (180ms) to prevent flicker on rapid movements
    float tau = (target < m_spectrum_hover_opacity) ? 100.0f : 180.0f;
    float factor = 1.0f - std::exp(-dt_ms / tau);
    m_spectrum_hover_opacity += (target - m_spectrum_hover_opacity) * factor;

    if (std::abs(m_spectrum_hover_opacity - target) < 0.005f) {
      m_spectrum_hover_opacity = target;
    } else {
      m_spectrum_animating = true;
      request_animation();
    }
  }
}

// High-performance spectrum renderer using direct pixel writes + AlphaBlend.
// Eliminates all per-bar GDI+ calls (the main CPU bottleneck).
void ControlPanelCore::draw_full_spectrum(HDC hdc) {
  if (m_rect_spectrum_full.right <= m_rect_spectrum_full.left) return;

  // Update hover opacity with frame-rate independent dt exponential interpolation
  update_spectrum_hover_opacity();

  // Process fade animation
  if (m_spectrum_fade_active) {
    auto now = std::chrono::steady_clock::now();
    float elapsed = std::chrono::duration<float, std::milli>(now - m_spectrum_fade_start_time).count();
    float t = elapsed / SPECTRUM_FADE_DURATION_MS;
    if (t >= 1.0f) {
      t = 1.0f;
      m_spectrum_fade_active = false;
      m_spectrum_opacity = m_spectrum_target_opacity;
      if (m_spectrum_target_opacity == 0.0f) {
        release_vis_stream();
        m_spectrum_animating = false;
      }
    } else {
      float ease = 1.0f - (1.0f - t) * (1.0f - t);
      m_spectrum_opacity = m_spectrum_start_opacity + (m_spectrum_target_opacity - m_spectrum_start_opacity) * ease;
    }
  }

  if (m_spectrum_opacity <= 0.001f) return;

  int area_w = m_rect_spectrum_full.right - m_rect_spectrum_full.left;
  int area_h = m_rect_spectrum_full.bottom - m_rect_spectrum_full.top;
  if (area_w <= 0 || area_h <= 0) return;

  update_spectrum_data();

  if (m_spectrum_opacity > 0.0f && m_vis_stream.is_valid()) {
    m_spectrum_animating = true;
    request_animation();
  }

  // Ensure overlay DIBSECTION exists at the right size
  ensure_spectrum_overlay(hdc, area_w, area_h);
  if (!m_spectrum_overlay_bits) return;

  // Clear overlay to fully transparent
  memset(m_spectrum_overlay_bits, 0, area_w * area_h * 4);

  // Fixed-pixel bar sizing
  int spec_width = get_nowbar_spectrum_width();
  int bar_w, gap_w;
  if (spec_width == 0) { bar_w = 2; gap_w = 1; }       // Thin
  else if (spec_width == 2) { bar_w = 8; gap_w = 3; }   // Wide
  else { bar_w = 4; gap_w = 2; }                          // Normal

  int unit_w = bar_w + gap_w;
  int bar_count = m_spectrum_bar_count;
  if (bar_count <= 0) return;

  int spec_style = get_nowbar_spectrum_style();

  // Dominoes mode: override inter-bar gap to 1px for tight columns
  bool is_dominoes = (spec_style == 2);
  if (is_dominoes) {
    gap_w = 1;
    unit_w = bar_w + gap_w;
  }

  // Curve mode: render into the overlay DIBSECTION via a GDI+ Bitmap
  // wrapper, then let AlphaBlend composite it onto the paint surface.
  // Drawing directly onto the paint HDC caused "tide mark" ghost artifacts
  // on Windows 11 because GDI+ SourceOver on a device-compatible DC
  // corrupts the alpha channel, and DWM interprets stale alpha as
  // transparency.
  if (spec_style == 1) {
    Gdiplus::Bitmap bmp(area_w, area_h, area_w * 4,
                        PixelFormat32bppPARGB,
                        reinterpret_cast<BYTE*>(m_spectrum_overlay_bits));
    Gdiplus::Graphics g(&bmp);
    RECT overlay_rect = { 0, 0, area_w, area_h };
    draw_spectrum_curve(g, overlay_rect);
  } else {

  bool stereo = false;

  // Colors from preferences
  COLORREF spec_color = get_spectrum_primary_color();
  int user_alpha = get_nowbar_spectrum_opacity() * 255 / 100;
  int alpha = (int)(user_alpha * m_spectrum_opacity * m_spectrum_hover_opacity);
  if (alpha > 255) alpha = 255;
  if (alpha < 0) alpha = 0;

  BYTE r1 = GetRValue(spec_color);
  BYTE g1 = GetGValue(spec_color);
  BYTE b1 = GetBValue(spec_color);

  int gradient_mode = get_nowbar_spectrum_gradient_mode();
  COLORREF spec_color2 = get_spectrum_secondary_color();
  BYTE r2 = GetRValue(spec_color2);
  BYTE g2 = GetGValue(spec_color2);
  BYTE b2 = GetBValue(spec_color2);

  // Stereo default colors: warm left (red-orange), cool right (teal-green)
  BYTE lr1 = 255, lg1 = 100, lb1 = 50;
  BYTE rr1 = 50, rg1 = 200, rb1 = 180;

  int stride = area_w;  // pixels per row (DIBSECTION is tightly packed)

  // Lambda to render one bar into the overlay
  auto render_dib_bar = [&](int bx, float value, int bar_idx, int half_count, bool is_right,
                             const std::vector<float>& peaks_vec, const std::vector<float>& bars_vec) {
    int bar_h = (int)(value * area_h);
    if (bar_h < 1) return;

    int bw = bar_w;
    if (bx + bw > area_w) bw = area_w - bx;
    if (bw < 1 || bx < 0) return;
    int by = area_h - bar_h;

    // Dominoes segmentation: 4px segments with 2px gaps, aligned from bottom
    constexpr int domino_seg_h = 4;
    constexpr int domino_gap_h = 2;
    constexpr int domino_cell_h = domino_seg_h + domino_gap_h;

    // Determine bar color
    int base_r, base_g, base_b;
    if (gradient_mode == 3) {
      float hue = (half_count > 1) ? (float)bar_idx / (float)(half_count - 1) * 300.0f : 0.0f;
      if (stereo && is_right) hue = 300.0f - hue;
      COLORREF freq_color = hsl_to_rgb(hue, 0.9f, 0.55f);
      base_r = GetRValue(freq_color); base_g = GetGValue(freq_color); base_b = GetBValue(freq_color);
    } else if (stereo) {
      base_r = is_right ? rr1 : lr1; base_g = is_right ? rg1 : lg1; base_b = is_right ? rb1 : lb1;
    } else {
      base_r = r1; base_g = g1; base_b = b1;
    }

    uint32_t bar_pixel = ((uint32_t)alpha << 24) |
        ((uint32_t)((base_r * alpha) / 255) << 16) |
        ((uint32_t)((base_g * alpha) / 255) << 8) |
        ((uint32_t)((base_b * alpha) / 255));

    float dr = 0.0f, dg = 0.0f, db = 0.0f;
    float cur_r = (float)r1, cur_g = (float)g1, cur_b = (float)b1;
    if (gradient_mode == 2 && bar_h > 1 && !stereo) {
      float inv_h = 1.0f / (float)(bar_h - 1);
      dr = ((float)r2 - (float)r1) * inv_h;
      dg = ((float)g2 - (float)g1) * inv_h;
      db = ((float)b2 - (float)b1) * inv_h;
    }

    for (int row = by; row < area_h; row++) {
      if (row < 0 || row >= area_h) continue;

      // Dominoes: skip rows that fall in the gap between segments
      if (is_dominoes) {
        int dist_from_bottom = area_h - 1 - row;
        int pos_in_cell = dist_from_bottom % domino_cell_h;
        if (pos_in_cell >= domino_seg_h) continue;  // In the gap
      }

      int left = bx;
      int right = bx + bw;

      uint32_t row_pixel;
      if (gradient_mode == 2 && bar_h > 1 && !stereo) {
        int r = std::max(0, std::min(255, (int)cur_r));
        int g = std::max(0, std::min(255, (int)cur_g));
        int b = std::max(0, std::min(255, (int)cur_b));
        cur_r += dr; cur_g += dg; cur_b += db;
        row_pixel = ((uint32_t)alpha << 24) |
            ((uint32_t)((r * alpha) / 255) << 16) |
            ((uint32_t)((g * alpha) / 255) << 8) |
            ((uint32_t)((b * alpha) / 255));
      } else {
        row_pixel = bar_pixel;
      }

      uint32_t* pixel = m_spectrum_overlay_bits + row * stride + left;
      for (int col = left; col < right; col++) {
        *pixel++ = row_pixel;
      }
    }

    // Peak indicator (2px line above bar) — skip in Dominoes mode
    if (!is_dominoes && bar_idx < (int)peaks_vec.size() && bar_idx < (int)bars_vec.size() &&
        peaks_vec[bar_idx] > bars_vec[bar_idx]) {
      int peak_y = area_h - (int)(peaks_vec[bar_idx] * area_h);
      uint32_t peak_pixel = bar_pixel;
      for (int py = peak_y; py < peak_y + 2 && py < area_h; py++) {
        if (py < 0) continue;
        uint32_t* pixel = m_spectrum_overlay_bits + py * stride + bx;
        for (int col = 0; col < bw; col++) {
          *pixel++ = peak_pixel;
        }
      }
    }
  };

  if (stereo) {
    int half = bar_count / 2;
    if (half <= 0) half = 1;
    int center_px = area_w / 2;

    // Left half: bars grow from center toward left (bar 0 at center)
    for (int i = 0; i < half; i++) {
      if (i >= (int)m_spectrum_bars.size()) break;
      int bx = center_px - (i + 1) * unit_w + gap_w;
      render_dib_bar(bx, m_spectrum_bars[i], i, half, false, m_spectrum_peaks, m_spectrum_bars);
    }

    // Right half: bars grow from center toward right (bar 0 at center)
    for (int i = 0; i < half; i++) {
      if (i >= (int)m_spectrum_bars_right.size()) break;
      int bx = center_px + i * unit_w;
      render_dib_bar(bx, m_spectrum_bars_right[i], i, half, true, m_spectrum_peaks_right, m_spectrum_bars_right);
    }
  } else {
    int total_used = bar_count * unit_w - gap_w;
    int margin_left = (area_w - total_used) / 2;
    if (margin_left < 0) margin_left = 0;

    for (int i = 0; i < bar_count; i++) {
      if (i >= (int)m_spectrum_bars.size()) break;
      int bx = margin_left + i * unit_w;
      render_dib_bar(bx, m_spectrum_bars[i], i, bar_count, false, m_spectrum_peaks, m_spectrum_bars);
    }
  }
  } // end curve/bar if-else

  // Single AlphaBlend call composites the entire spectrum overlay onto the paint surface
  BLENDFUNCTION bf;
  bf.BlendOp = AC_SRC_OVER;
  bf.BlendFlags = 0;
  bf.SourceConstantAlpha = 255;
  bf.AlphaFormat = AC_SRC_ALPHA;
  AlphaBlend(hdc, m_rect_spectrum_full.left, m_rect_spectrum_full.top, area_w, area_h,
             m_spectrum_overlay_hdc, 0, 0, area_w, area_h, bf);
}

// GDI+ version used by paint() for full repaints (not performance-critical)
void ControlPanelCore::draw_full_spectrum_gdiplus(Gdiplus::Graphics& g) {
  if (m_rect_spectrum_full.right <= m_rect_spectrum_full.left) return;

  // Update hover opacity with frame-rate independent dt exponential interpolation
  update_spectrum_hover_opacity();

  if (m_spectrum_fade_active) {
    auto now = std::chrono::steady_clock::now();
    float elapsed = std::chrono::duration<float, std::milli>(now - m_spectrum_fade_start_time).count();
    float t = elapsed / SPECTRUM_FADE_DURATION_MS;
    if (t >= 1.0f) {
      t = 1.0f;
      m_spectrum_fade_active = false;
      m_spectrum_opacity = m_spectrum_target_opacity;
      if (m_spectrum_target_opacity == 0.0f) {
        release_vis_stream();
        m_spectrum_animating = false;
      }
    } else {
      float ease = 1.0f - (1.0f - t) * (1.0f - t);
      m_spectrum_opacity = m_spectrum_start_opacity + (m_spectrum_target_opacity - m_spectrum_start_opacity) * ease;
    }
  }

  if (m_spectrum_opacity <= 0.001f) return;

  int area_w = m_rect_spectrum_full.right - m_rect_spectrum_full.left;
  int area_h = m_rect_spectrum_full.bottom - m_rect_spectrum_full.top;
  if (area_w <= 0 || area_h <= 0) return;

  update_spectrum_data();

  if (m_spectrum_opacity > 0.0f && m_vis_stream.is_valid()) {
    m_spectrum_animating = true;
    request_animation();
  }

  // Fixed-pixel bar sizing
  int spec_width = get_nowbar_spectrum_width();
  int bar_w_i, gap_w_i;
  if (spec_width == 0) { bar_w_i = 2; gap_w_i = 1; }
  else if (spec_width == 2) { bar_w_i = 8; gap_w_i = 3; }
  else { bar_w_i = 4; gap_w_i = 2; }

  int unit_w = bar_w_i + gap_w_i;
  int bar_count = m_spectrum_bar_count;
  if (bar_count <= 0) return;

  int spec_style = get_nowbar_spectrum_style();

  // Dominoes mode: override inter-bar gap to 1px for tight columns
  bool is_dominoes = (spec_style == 2);
  if (is_dominoes) {
    gap_w_i = 1;
    unit_w = bar_w_i + gap_w_i;
  }

  // Curve mode: delegate to curve renderer
  if (spec_style == 1) {
    draw_spectrum_curve(g, m_rect_spectrum_full);
    return;
  }

  bool stereo = false;

  float bar_w = (float)bar_w_i;

  // Colors from preferences
  COLORREF spec_color = get_spectrum_primary_color();
  int user_alpha = get_nowbar_spectrum_opacity() * 255 / 100;
  int alpha = (int)(user_alpha * m_spectrum_opacity * m_spectrum_hover_opacity);
  if (alpha > 255) alpha = 255;
  if (alpha < 0) alpha = 0;
  BYTE r1 = GetRValue(spec_color), g1 = GetGValue(spec_color), b1 = GetBValue(spec_color);

  int gradient_mode = get_nowbar_spectrum_gradient_mode();
  COLORREF spec_color2 = get_spectrum_secondary_color();
  BYTE r2 = GetRValue(spec_color2), g2 = GetGValue(spec_color2), b2 = GetBValue(spec_color2);

  // Stereo default colors
  BYTE lr1 = 255, lg1 = 100, lb1 = 50;
  BYTE rr1 = 50, rg1 = 200, rb1 = 180;

  Gdiplus::SmoothingMode oldSmoothing = g.GetSmoothingMode();
  Gdiplus::PixelOffsetMode oldPixelOffset = g.GetPixelOffsetMode();
  g.SetSmoothingMode(Gdiplus::SmoothingModeNone);
  g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeNone);

  Gdiplus::GraphicsPath path;
  float bottom_f = (float)m_rect_spectrum_full.bottom;

  // Dominoes segmentation constants
  constexpr float domino_seg_h = 4.0f;
  constexpr float domino_gap_h = 2.0f;
  constexpr float domino_cell_h = domino_seg_h + domino_gap_h;

  // Helper to draw domino segments for a bar using a pre-created brush.
  // Batches all segment rectangles into a single GraphicsPath and fills once
  // to avoid per-segment FillRectangle overhead in the GDI+ fallback path.
  auto fill_domino_segments = [&](float x, float y, float height, Gdiplus::Brush& brush) {
    path.Reset();
    // Add full segments from bottom up, aligned to the bottom of the area
    float seg_bottom = bottom_f;
    while (seg_bottom - domino_seg_h > y) {
      float seg_top = seg_bottom - domino_seg_h;
      path.AddRectangle(Gdiplus::RectF(x, seg_top, bar_w, domino_seg_h));
      seg_bottom -= domino_cell_h;  // Move up by segment + gap
    }
    // Partial top segment (clip to bar top)
    if (seg_bottom > y) {
      float partial_h = seg_bottom - y;
      if (partial_h > 0.5f) {
        path.AddRectangle(Gdiplus::RectF(x, y, bar_w, partial_h));
      }
    }
    g.FillPath(&brush, &path);
  };

  // Lambda to draw one bar at a given x position
  auto draw_gdiplus_bar = [&](float x, float value, int bar_idx, int half_count, bool is_right) {
    float height = value * (float)area_h;
    if (height < 1.0f) return;
    float y = bottom_f - height;

    BYTE cr1, cg1, cb1;
    if (stereo && gradient_mode != 3) {
      if (gradient_mode == 2) {
        cr1 = is_right ? r2 : r1; cg1 = is_right ? g2 : g1; cb1 = is_right ? b2 : b1;
      } else {
        cr1 = is_right ? rr1 : lr1; cg1 = is_right ? rg1 : lg1; cb1 = is_right ? rb1 : lb1;
      }
    } else {
      cr1 = r1; cg1 = g1; cb1 = b1;
    }

    if (gradient_mode == 3) {
      float hue = (half_count > 1) ? (float)bar_idx / (float)(half_count - 1) * 300.0f : 0.0f;
      if (stereo && is_right) hue = 300.0f - hue;
      COLORREF freq_color = hsl_to_rgb(hue, 0.9f, 0.55f);
      Gdiplus::SolidBrush barBrush(Gdiplus::Color(alpha, GetRValue(freq_color), GetGValue(freq_color), GetBValue(freq_color)));
      if (is_dominoes) {
        fill_domino_segments(x, y, height, barBrush);
      } else {
        g.FillRectangle(&barBrush, x, y, bar_w, height);
      }
    } else if (gradient_mode == 2 && !stereo) {
      Gdiplus::LinearGradientBrush barBrush(
          Gdiplus::PointF(x, y), Gdiplus::PointF(x, bottom_f),
          Gdiplus::Color(alpha, r1, g1, b1),
          Gdiplus::Color(alpha, r2, g2, b2));
      if (is_dominoes) {
        fill_domino_segments(x, y, height, barBrush);
      } else {
        g.FillRectangle(&barBrush, x, y, bar_w, height);
      }
    } else {
      Gdiplus::SolidBrush barBrush(Gdiplus::Color(alpha, cr1, cg1, cb1));
      if (is_dominoes) {
        fill_domino_segments(x, y, height, barBrush);
      } else {
        g.FillRectangle(&barBrush, x, y, bar_w, height);
      }
    }
  };

  auto draw_gdiplus_peak = [&](float x, float peak_val, float bar_val, int bar_idx, int half_count, bool is_right) {
    if (is_dominoes) return;  // No peaks in Dominoes mode
    if (peak_val <= bar_val) return;
    float peak_y = bottom_f - peak_val * (float)area_h;
    BYTE pr, pg, pb;
    if (gradient_mode == 3) {
      float hue = (half_count > 1) ? (float)bar_idx / (float)(half_count - 1) * 300.0f : 0.0f;
      if (stereo && is_right) hue = 300.0f - hue;
      COLORREF freq_color = hsl_to_rgb(hue, 0.9f, 0.55f);
      pr = GetRValue(freq_color); pg = GetGValue(freq_color); pb = GetBValue(freq_color);
    } else if (stereo && gradient_mode != 2) {
      pr = is_right ? rr1 : lr1; pg = is_right ? rg1 : lg1; pb = is_right ? rb1 : lb1;
    } else if (stereo && gradient_mode == 2) {
      pr = is_right ? r2 : r1; pg = is_right ? g2 : g1; pb = is_right ? b2 : b1;
    } else {
      pr = r1; pg = g1; pb = b1;
    }
    Gdiplus::SolidBrush peakBrush(Gdiplus::Color(alpha, pr, pg, pb));
    g.FillRectangle(&peakBrush, x, peak_y, bar_w, 2.0f);
  };

  if (stereo) {
    int half = bar_count / 2;
    if (half <= 0) half = 1;
    float center_x = (float)(m_rect_spectrum_full.left + area_w / 2);

    for (int i = 0; i < half; i++) {
      if (i >= (int)m_spectrum_bars.size()) break;
      float x = center_x - (float)((i + 1) * unit_w - gap_w_i);
      draw_gdiplus_bar(x, m_spectrum_bars[i], i, half, false);
      if (i < (int)m_spectrum_peaks.size())
        draw_gdiplus_peak(x, m_spectrum_peaks[i], m_spectrum_bars[i], i, half, false);
    }

    for (int i = 0; i < half; i++) {
      if (i >= (int)m_spectrum_bars_right.size()) break;
      float x = center_x + (float)(i * unit_w);
      draw_gdiplus_bar(x, m_spectrum_bars_right[i], i, half, true);
      if (i < (int)m_spectrum_peaks_right.size())
        draw_gdiplus_peak(x, m_spectrum_peaks_right[i], m_spectrum_bars_right[i], i, half, true);
    }
  } else {
    int total_used = bar_count * unit_w - gap_w_i;
    int margin_left = (area_w - total_used) / 2;
    if (margin_left < 0) margin_left = 0;

    for (int i = 0; i < bar_count; i++) {
      if (i >= (int)m_spectrum_bars.size()) break;
      float x = (float)(m_rect_spectrum_full.left + margin_left + i * unit_w);
      draw_gdiplus_bar(x, m_spectrum_bars[i], i, bar_count, false);
      if (i < (int)m_spectrum_peaks.size())
        draw_gdiplus_peak(x, m_spectrum_peaks[i], m_spectrum_bars[i], i, bar_count, false);
    }
  }

  g.SetSmoothingMode(oldSmoothing);
  g.SetPixelOffsetMode(oldPixelOffset);
}

void ControlPanelCore::update_waveform_brushes() {
    COLORREF wave_color = get_nowbar_custom_waveform_color_enabled()
        ? get_nowbar_waveform_color() : m_theme_highlight;
    COLORREF unplayed_color = get_nowbar_custom_waveform_unplayed_enabled()
        ? get_nowbar_waveform_unplayed_color() : m_track_color;
    m_waveform_brush_accent = std::make_unique<Gdiplus::SolidBrush>(
        Gdiplus::Color(255, GetRValue(wave_color), GetGValue(wave_color), GetBValue(wave_color)));
    m_waveform_brush_dim = std::make_unique<Gdiplus::SolidBrush>(
        Gdiplus::Color(255, GetRValue(unplayed_color), GetGValue(unplayed_color), GetBValue(unplayed_color)));

    // Style 2: Two-tone layered brushes (outer peaks / outline and inner core)
    BYTE pr = GetRValue(wave_color);
    BYTE pg = GetGValue(wave_color);
    BYTE pb = GetBValue(wave_color);

    // Played Outer: saturated/deep primary accent
    BYTE pr_out = pr;
    BYTE pg_out = pg;
    BYTE pb_out = pb;

    // Played Inner: lighter tint core
    BYTE pr_in = static_cast<BYTE>(pr + (255 - pr) * 0.42f);
    BYTE pg_in = static_cast<BYTE>(pg + (255 - pg) * 0.42f);
    BYTE pb_in = static_cast<BYTE>(pb + (255 - pb) * 0.42f);

    m_waveform_brush_accent_outer = std::make_unique<Gdiplus::SolidBrush>(
        Gdiplus::Color(255, pr_out, pg_out, pb_out));
    m_waveform_brush_accent_inner = std::make_unique<Gdiplus::SolidBrush>(
        Gdiplus::Color(255, pr_in, pg_in, pb_in));

    // Unplayed Outer (dark slate / charcoal) & Inner (mid-gray)
    BYTE ur = GetRValue(unplayed_color);
    BYTE ug = GetGValue(unplayed_color);
    BYTE ub = GetBValue(unplayed_color);

    if (m_dark_mode) {
        // Dark theme: Outer is darker charcoal/slate, Inner is medium gray
        BYTE ur_out = static_cast<BYTE>(ur * 0.55f);
        BYTE ug_out = static_cast<BYTE>(ug * 0.55f);
        BYTE ub_out = static_cast<BYTE>(ub * 0.55f);
        BYTE ur_in = static_cast<BYTE>(std::min(255, (int)(ur * 0.85f + 30)));
        BYTE ug_in = static_cast<BYTE>(std::min(255, (int)(ug * 0.85f + 30)));
        BYTE ub_in = static_cast<BYTE>(std::min(255, (int)(ub * 0.85f + 30)));
        m_waveform_brush_dim_outer = std::make_unique<Gdiplus::SolidBrush>(
            Gdiplus::Color(255, ur_out, ug_out, ub_out));
        m_waveform_brush_dim_inner = std::make_unique<Gdiplus::SolidBrush>(
            Gdiplus::Color(255, ur_in, ug_in, ub_in));
    } else {
        // Light theme: Outer is darker edge, Inner is lighter fill
        BYTE ur_out = static_cast<BYTE>(ur * 0.70f);
        BYTE ug_out = static_cast<BYTE>(ug * 0.70f);
        BYTE ub_out = static_cast<BYTE>(ub * 0.70f);
        BYTE ur_in = static_cast<BYTE>(ur + (255 - ur) * 0.35f);
        BYTE ug_in = static_cast<BYTE>(ug + (255 - ug) * 0.35f);
        BYTE ub_in = static_cast<BYTE>(ub + (255 - ub) * 0.35f);
        m_waveform_brush_dim_outer = std::make_unique<Gdiplus::SolidBrush>(
            Gdiplus::Color(255, ur_out, ug_out, ub_out));
        m_waveform_brush_dim_inner = std::make_unique<Gdiplus::SolidBrush>(
            Gdiplus::Color(255, ur_in, ug_in, ub_in));
    }

    m_waveform_brushes_dirty = false;
}

void ControlPanelCore::draw_waveform_bar(Gdiplus::Graphics& g) {
  if (m_rect_waveform.right <= m_rect_waveform.left) return;

  int w = m_rect_waveform.right - m_rect_waveform.left;
  int h = m_rect_waveform.bottom - m_rect_waveform.top;
  bool is_pill = (get_nowbar_bar_style() == 0);

  // Progress calculation
  double progress;
  if (m_seeking) {
    progress = (m_state.track_length > 0)
                   ? (m_state.playback_time / m_state.track_length)
                   : 0.0;
  } else {
    progress = m_target_progress;
  }
  progress = std::max(0.0, std::min(1.0, progress));

  // Ensure cached brushes are up to date
  if (m_waveform_brushes_dirty || !m_waveform_brush_accent || !m_waveform_brush_dim ||
      !m_waveform_brush_accent_outer || !m_waveform_brush_accent_inner ||
      !m_waveform_brush_dim_outer || !m_waveform_brush_dim_inner) {
      update_waveform_brushes();
  }

  // Keep color values for placeholder rendering (not a hot path)
  COLORREF wave_color = get_nowbar_custom_waveform_color_enabled()
      ? get_nowbar_waveform_color() : m_theme_highlight;

  // Lock and read waveform data
  std::vector<float> peaks;
  bool valid;
  bool is_stream;
  {
    std::lock_guard<std::mutex> lock(m_waveform_mutex);
    peaks = m_waveform_peaks;
    valid = m_waveform_valid;
    is_stream = m_waveform_is_stream;
  }

  if (is_stream || peaks.empty()) {
    // Streams, or peaks cleared by a vis mode switch while reveal state is stale: nothing to draw
    m_waveform_animating = false;
    return;
  } else {
    // Advance reveal cursor toward decode count (ease-out)
    float decode_target = static_cast<float>(m_waveform_decode_count.load(std::memory_order_relaxed));
    if (decode_target > 0.0f && !m_waveform_reveal_active && m_waveform_reveal_pos < decode_target - 0.5f) {
      m_waveform_reveal_active = true;  // Activate on main thread when decode starts producing data
    }
    if (m_waveform_reveal_pos < decode_target) {
      m_waveform_reveal_pos += (decode_target - m_waveform_reveal_pos) * 0.15f;
      if (decode_target - m_waveform_reveal_pos < 0.5f) {
        m_waveform_reveal_pos = decode_target;
      }
    }

    bool still_animating = (m_waveform_reveal_pos < decode_target);

    // Draw waveform bars with reveal cutoff
    int wave_style = get_nowbar_waveform_style();
    int wave_w_setting = get_nowbar_waveform_width();

    int display_count;
    float bar_w_f, gap, bar_total_w;
    if (wave_style == 1) {
      // Style 2: Solid continuous envelope (0-gap, 1 column per pixel)
      display_count = w;
      if (display_count < 1) display_count = 1;
      bar_w_f = 1.0f;
      gap = 0.0f;
      bar_total_w = 1.0f;
    } else {
      // Style 1: SoundCloud bottom-aligned bars width & gap
      if (wave_w_setting == 0) {
        // Thin: 1px dark, 1px blank
        bar_w_f = 1.0f;
        gap = 1.0f;
      } else if (wave_w_setting == 1) {
        // Wide: 2px dark, 1px blank
        bar_w_f = 2.0f;
        gap = 1.0f;
      } else if (wave_w_setting == 2) {
        // Wide (4K): 3px dark, 2px blank
        bar_w_f = 3.0f;
        gap = 2.0f;
      } else {
        bar_w_f = 1.0f;
        gap = 1.0f;
      }
      bar_total_w = bar_w_f + gap;
      display_count = (int)((float)w / bar_total_w);
      if (display_count < 1) display_count = 1;
    }

    int num_segments = (int)peaks.size();
    if (num_segments < 1) num_segments = 1;

    // Map reveal_pos (in segment space 0-400) to display bar index
    float reveal_bar_limit = (m_waveform_reveal_pos / (float)WAVEFORM_SEGMENTS) * display_count;

    if (wave_style == 1) {
      // Style 2: Centered continuous mirrored waveform envelope with two-tone shading
      float center_y = m_rect_waveform.top + h * 0.5f;
      float max_half_h = (float)h * 0.5f;
      float min_outer_h = 0.75f * m_dpi_scale;
      float min_inner_h = 0.5f * m_dpi_scale;

      Gdiplus::SmoothingMode oldSmoothing = g.GetSmoothingMode();
      Gdiplus::PixelOffsetMode oldPixelOffset = g.GetPixelOffsetMode();
      g.SetSmoothingMode(Gdiplus::SmoothingModeNone);
      g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeNone);

      for (int i = 0; i < display_count; i++) {
        if ((float)i >= reveal_bar_limit) break;

        float src = (float)i / display_count * num_segments;
        int lo = (int)src;
        int hi = lo + 1;
        if (lo >= num_segments) lo = num_segments - 1;
        if (hi >= num_segments) hi = num_segments - 1;
        float frac = src - lo;
        float peak = peaks[lo] * (1.0f - frac) + peaks[hi] * frac;

        float outer_half_h = peak * max_half_h;
        if (outer_half_h < min_outer_h) outer_half_h = min_outer_h;

        float inner_half_h = outer_half_h * 0.55f;
        if (inner_half_h < min_inner_h) inner_half_h = min_inner_h;

        float bx = (float)(m_rect_waveform.left + i);
        float seg_progress = (float)(i + 0.5f) / display_count;
        bool played = (seg_progress <= (float)progress);

        auto* brush_outer = played ? m_waveform_brush_accent_outer.get() : m_waveform_brush_dim_outer.get();
        auto* brush_inner = played ? m_waveform_brush_accent_inner.get() : m_waveform_brush_dim_inner.get();

        // 1. Draw outer envelope (peaks / outline)
        g.FillRectangle(brush_outer, bx, center_y - outer_half_h, 1.0f, outer_half_h * 2.0f);
        // 2. Draw inner core (RMS body)
        g.FillRectangle(brush_inner, bx, center_y - inner_half_h, 1.0f, inner_half_h * 2.0f);
      }

      g.SetSmoothingMode(oldSmoothing);
      g.SetPixelOffsetMode(oldPixelOffset);
    } else {
      // Style 1: SoundCloud bottom-aligned discrete bars with gaps
      float min_bar_h = 1.0f * m_dpi_scale;

      for (int i = 0; i < display_count; i++) {
        if ((float)i >= reveal_bar_limit) break;

        float src = (float)i / display_count * num_segments;
        int lo = (int)src;
        int hi = lo + 1;
        if (lo >= num_segments) lo = num_segments - 1;
        if (hi >= num_segments) hi = num_segments - 1;
        float frac = src - lo;
        float peak = peaks[lo] * (1.0f - frac) + peaks[hi] * frac;

        float bar_h = peak * (float)h;
        if (bar_h < min_bar_h) bar_h = min_bar_h;

        float bx = (float)m_rect_waveform.left + (float)i * bar_total_w;
        float by = (float)m_rect_waveform.bottom - bar_h;

        float seg_progress = (float)(i + 0.5f) / display_count;
        bool played = (seg_progress <= (float)progress);

        g.FillRectangle(played ? m_waveform_brush_accent.get() : m_waveform_brush_dim.get(),
                        bx, by, bar_w_f, bar_h);
      }
    }

    // Keep animating if reveal hasn't caught up to decoded segments
    if (still_animating || m_waveform_reveal_active) {
      m_waveform_animating = true;
      request_animation();
      // Clear reveal_active once fully revealed
      if (m_waveform_reveal_pos >= (float)WAVEFORM_SEGMENTS) {
        m_waveform_reveal_active = false;
      }
    } else {
      m_waveform_animating = false;
    }
  }

}

void ControlPanelCore::draw_waveform_tooltip(Gdiplus::Graphics& g) {
  if (m_rect_waveform.right <= m_rect_waveform.left) return;
  if (!((m_hover_region == HitRegion::SeekBar || m_seeking) && m_state.track_length > 0)) return;

  int w = m_rect_waveform.right - m_rect_waveform.left;
  double progress;
  if (m_seeking) {
    progress = (m_state.track_length > 0)
                   ? (m_state.playback_time / m_state.track_length)
                   : 0.0;
  } else {
    progress = m_target_progress;
  }
  progress = std::max(0.0, std::min(1.0, progress));

  std::wstring timeStr = format_time(m_seeking ? m_state.playback_time : m_preview_time);
  Gdiplus::Font tooltipFont(L"Segoe UI", 10.0f * m_dpi_scale, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
  Gdiplus::RectF textBounds;
  g.MeasureString(timeStr.c_str(), -1, &tooltipFont, Gdiplus::PointF(0, 0), &textBounds);
  int padding_h = static_cast<int>(6 * m_dpi_scale);
  int padding_v = static_cast<int>(3 * m_dpi_scale);
  int tooltip_w = static_cast<int>(textBounds.Width) + padding_h * 2;
  int tooltip_h = static_cast<int>(textBounds.Height) + padding_v * 2;
  int progress_w = static_cast<int>(w * progress);
  int cursor_x = m_seeking ? (m_rect_waveform.left + progress_w) : m_seekbar_hover_x;
  int tooltip_x = cursor_x - tooltip_w / 2;
  int tooltip_y = m_rect_waveform.top - tooltip_h - static_cast<int>(6 * m_dpi_scale);
  tooltip_x = std::max((int)m_rect_waveform.left, std::min(tooltip_x, (int)m_rect_waveform.right - tooltip_w));
  Gdiplus::Color bgColor = m_dark_mode ? Gdiplus::Color(220, 60, 60, 60) : Gdiplus::Color(220, 40, 40, 40);
  Gdiplus::SolidBrush bgBrush(bgColor);
  int corner = static_cast<int>(4 * m_dpi_scale);
  Gdiplus::GraphicsPath tooltipPath;
  tooltipPath.AddArc(tooltip_x, tooltip_y, corner * 2, corner * 2, 180, 90);
  tooltipPath.AddArc(tooltip_x + tooltip_w - corner * 2, tooltip_y, corner * 2, corner * 2, 270, 90);
  tooltipPath.AddArc(tooltip_x + tooltip_w - corner * 2, tooltip_y + tooltip_h - corner * 2, corner * 2, corner * 2, 0, 90);
  tooltipPath.AddArc(tooltip_x, tooltip_y + tooltip_h - corner * 2, corner * 2, corner * 2, 90, 90);
  tooltipPath.CloseFigure();
  g.FillPath(&bgBrush, &tooltipPath);
  Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, 255, 255, 255));
  Gdiplus::StringFormat sf;
  sf.SetAlignment(Gdiplus::StringAlignmentCenter);
  sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
  Gdiplus::RectF textRect(static_cast<float>(tooltip_x), static_cast<float>(tooltip_y),
                          static_cast<float>(tooltip_w), static_cast<float>(tooltip_h));
  g.DrawString(timeStr.c_str(), -1, &tooltipFont, textRect, &sf, &textBrush);
}

void ControlPanelCore::draw_time_display_top_right(Gdiplus::Graphics& g) {
  if (!get_nowbar_playback_time_visible()) return;
  if (m_rect_time.right <= m_rect_time.left) return;

  std::wstring elapsed = format_time(m_state.playback_time);
  std::wstring total = is_live_stream(m_state) ? L"Live" : format_time(m_state.track_length);
  std::wstring display = elapsed + L" / " + total;

  int time_w = m_rect_time.right - m_rect_time.left;
  int time_h = m_rect_time.bottom - m_rect_time.top;

  int bg_style = get_nowbar_background_style();
  bool use_light_foreground = (bg_style == 1 && m_artwork_colors_valid) ||
                              (bg_style == 2 && m_blurred_artwork);
  Gdiplus::Color time_color = use_light_foreground
      ? Gdiplus::Color(255, 200, 200, 200) : m_text_secondary_color;
  COLORREF custom_time_color;
  if (get_nowbar_time_font_color(custom_time_color))
      time_color = Gdiplus::Color(255, GetRValue(custom_time_color), GetGValue(custom_time_color), GetBValue(custom_time_color));
  Gdiplus::SolidBrush timeBrush(time_color);

  Gdiplus::StringFormat sf(Gdiplus::StringFormat::GenericTypographic());
  sf.SetAlignment(Gdiplus::StringAlignmentFar);
  sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
  sf.SetFormatFlags(sf.GetFormatFlags() | Gdiplus::StringFormatFlagsNoClip);

  // Draw directly to the caller's Graphics — the DC already has an opaque
  // background (from draw_background or the spectrum bg cache restoration),
  // so ClearType sub-pixel rendering works correctly without fringing.
  // Previous versions cached to a transparent bitmap with grayscale AA,
  // which avoided ClearType fringing but produced noticeably fuzzier text.
  Gdiplus::RectF timeRect((float)m_rect_time.left, (float)m_rect_time.top,
                           (float)time_w, (float)time_h);
  g.DrawString(display.c_str(), -1, m_font_time.get(), timeRect, &sf, &timeBrush);
}

void ControlPanelCore::draw_volume(Gdiplus::Graphics &g) {
  bool volume_icon_vis = get_nowbar_volume_icon_visible();
  bool volume_bar_vis = get_nowbar_volume_bar_visible();
  bool volume_number_vis = get_nowbar_volume_number_enabled();

  if (!volume_icon_vis && !volume_bar_vis && !volume_number_vis)
    return;

  int w = m_rect_volume.right - m_rect_volume.left;
  int h = m_rect_volume.bottom - m_rect_volume.top;

  // Determine if we're using artwork-based background that needs light icons
  int bg_style = get_nowbar_background_style();
  bool use_light_foreground = (bg_style == 1 && m_artwork_colors_valid) ||
                              (bg_style == 2 && m_blurred_artwork);
  Gdiplus::Color volume_icon_color = use_light_foreground
      ? Gdiplus::Color(255, 200, 200, 200) : m_text_secondary_color;
  Gdiplus::Color vol_hover_color;
  if (use_light_foreground) {
      vol_hover_color = Gdiplus::Color(40, 255, 255, 255);
  } else if (get_nowbar_custom_hover_color_enabled()) {
      COLORREF hc = get_nowbar_hover_color();
      vol_hover_color = Gdiplus::Color(40, GetRValue(hc), GetGValue(hc), GetBValue(hc));
  } else {
      vol_hover_color = Gdiplus::Color(40, GetRValue(m_theme_selection), GetGValue(m_theme_selection), GetBValue(m_theme_selection));
  }

  // Determine volume level: 0=mute, 1=low, 2=full
  float bar_level = db_to_slider(m_state.volume_db); // 0.0 to 1.0
  int vol_level;
  if (m_state.volume_db <= -100.0f) {
    vol_level = 0; // mute
  } else {
    vol_level = (bar_level <= 0.5f) ? 1 : 2;
  }

  // Icon dimensions
  int icon_size = static_cast<int>(23 * m_dpi_scale * m_size_scale);
  int vol_gap = static_cast<int>(14 * m_dpi_scale * m_size_scale);
  int num_width = volume_number_vis
      ? ((m_volume_number_width > 0) ? m_volume_number_width : static_cast<int>(28 * m_dpi_scale))
      : 0;

  if (volume_icon_vis) {
    // Draw custom volume icon (scaled with panel height) - always grayed
    int icon_y = m_rect_volume.top + (h - icon_size) / 2;
    int icon_base_x = m_rect_volume.left;

    // Hover circle on volume icon (same pattern as MiniPlayer button)
    bool icon_hovered = (m_hover_region == HitRegion::VolumeIcon);
    if (icon_hovered && get_nowbar_hover_circles_enabled()) {
      Gdiplus::SolidBrush hoverBrush(vol_hover_color);
      // Use full rect height for a proper circle, centered on icon
      int circle_size = h;
      int icon_center_x = icon_base_x + icon_size / 2;
      int icon_center_y = m_rect_volume.top + h / 2;
      g.FillEllipse(&hoverBrush, icon_center_x - circle_size / 2,
                    icon_center_y - circle_size / 2, circle_size, circle_size);
    }

    // Enlarge icon when hovered (same 15% pattern as other buttons)
    float vol_icon_scale = icon_hovered ? HOVER_SCALE_FACTOR : 1.0f;
    int scaled_icon_size = static_cast<int>(icon_size * vol_icon_scale);
    int icon_x = icon_base_x + (icon_size - scaled_icon_size) / 2;
    int scaled_icon_y = icon_y + (icon_size - scaled_icon_size) / 2;
    draw_volume_icon(g, icon_x, scaled_icon_y, scaled_icon_size,
                     volume_icon_color, vol_level);
  }

  int bar_x = 0;
  int bar_w = 0;
  if (volume_bar_vis) {
    // Volume bar - use same thickness as seekbar
    bar_x = m_rect_volume.left + (volume_icon_vis ? (icon_size + vol_gap) : 0);
    bar_w = (m_rect_volume.right - (volume_number_vis ? (vol_gap + num_width) : 0)) - bar_x;
    if (bar_w < 10) bar_w = 10;
    int bar_h = static_cast<int>(m_metrics.seekbar_height * m_size_scale);
    int bar_y = m_rect_volume.top + (h - bar_h) / 2;
    int radius = bar_h / 2;
    bool is_pill = (get_nowbar_bar_style() == 0); // 0=Pill-shaped, 1=Rectangular

    // Background - theme-aware track color
    Gdiplus::Color trackColor;
    bool has_artwork_bg = (bg_style == 1 || bg_style == 2) && m_artwork_colors_valid;

    if (get_nowbar_custom_volume_track_enabled()) {
      // Custom color overrides everything
      COLORREF tc = get_nowbar_volume_track_color();
      trackColor = Gdiplus::Color(255, GetRValue(tc), GetGValue(tc), GetBValue(tc));
    } else if (has_artwork_bg) {
      // Artwork backgrounds: approximate the rendered background color
      int r = m_artwork_color_primary.GetR();
      int g = m_artwork_color_primary.GetG();
      int b = m_artwork_color_primary.GetB();
      BYTE ov = (bg_style == 1) ? (m_dark_mode ? 120 : 80)
                                 : (m_dark_mode ? 140 : 180);
      r = r * (255 - ov) / 255 + 15;
      g = g * (255 - ov) / 255 + 15;
      b = b * (255 - ov) / 255 + 15;
      trackColor = Gdiplus::Color(255, std::min(r, 255), std::min(g, 255), std::min(b, 255));
    } else {
      // Solid background: use theme-appropriate track color
      trackColor = Gdiplus::Color(255, GetRValue(m_track_color),
          GetGValue(m_track_color), GetBValue(m_track_color));
    }
    Gdiplus::SolidBrush trackBrush(trackColor);
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

    // Level - use perceptual mapping so slider reflects perceived loudness
    int level_w = static_cast<int>(bar_w * bar_level);

    if (level_w > 0) {
      // Get custom volume accent color from preferences
      COLORREF vol_accent = get_nowbar_custom_volume_accent_enabled()
          ? get_nowbar_volume_accent_color() : m_theme_highlight;
      Gdiplus::SolidBrush levelBrush(
          Gdiplus::Color(255, GetRValue(vol_accent), GetGValue(vol_accent), GetBValue(vol_accent)));
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

    // Volume handle dot (only on hover or drag) - uses highlight color
    if (m_hover_region == HitRegion::VolumeSlider || m_volume_dragging) {
      int handle_size = bar_h * 2;
      int handle_x = bar_x + level_w - handle_size / 2;
      int handle_y = m_rect_volume.top + (h - handle_size) / 2;
      COLORREF vol_handle_accent = get_nowbar_custom_volume_accent_enabled()
          ? get_nowbar_volume_accent_color() : m_theme_highlight;
      Gdiplus::SolidBrush handleBrush(Gdiplus::Color(255,
          GetRValue(vol_handle_accent), GetGValue(vol_handle_accent), GetBValue(vol_handle_accent)));
      g.FillEllipse(&handleBrush, handle_x, handle_y, handle_size, handle_size);
    }
  }

  // Volume number (1-100, or 0 on mute) displayed after the volume bar or volume icon
  if (volume_number_vis && m_font_time) {
    int vol_percent;
    if (m_state.volume_db <= -100.0f) {
      vol_percent = 0;
    } else {
      vol_percent = static_cast<int>(std::round(bar_level * 100.0f));
      if (vol_percent < 1 && m_state.volume_db > -100.0f) vol_percent = 1;
      if (vol_percent > 100) vol_percent = 100;
    }

    wchar_t num_str[16];
    swprintf_s(num_str, L"%d", vol_percent);

    Gdiplus::Color num_color = use_light_foreground
        ? Gdiplus::Color(255, 200, 200, 200) : m_text_secondary_color;
    COLORREF custom_time_color;
    if (get_nowbar_time_font_color(custom_time_color)) {
      num_color = Gdiplus::Color(255, GetRValue(custom_time_color), GetGValue(custom_time_color), GetBValue(custom_time_color));
    }
    Gdiplus::SolidBrush numBrush(num_color);

    float num_top = static_cast<float>(m_rect_volume.top + h / 2) - m_volume_number_glyph_y - m_volume_number_glyph_h / 2.0f;
    float num_height = m_volume_number_line_h;

    Gdiplus::StringFormat sf(Gdiplus::StringFormat::GenericTypographic());
    sf.SetAlignment(Gdiplus::StringAlignmentCenter);
    sf.SetLineAlignment(Gdiplus::StringAlignmentNear);
    sf.SetFormatFlags(sf.GetFormatFlags() | Gdiplus::StringFormatFlagsNoWrap | Gdiplus::StringFormatFlagsNoClip);

    int num_x;
    if (volume_bar_vis) {
      num_x = bar_x + bar_w + vol_gap;
    } else if (volume_icon_vis) {
      num_x = m_rect_volume.left + icon_size + vol_gap;
    } else {
      num_x = m_rect_volume.left;
    }
    Gdiplus::RectF numRect(static_cast<float>(num_x), num_top,
                           static_cast<float>(num_width), num_height);
    g.DrawString(num_str, -1, m_font_time.get(), numRect, &sf, &numBrush);
  }
}

HitRegion ControlPanelCore::hit_test(int x, int y) const {
  if (pt_in_rect(m_rect_play, x, y))
    return HitRegion::PlayButton;
  if (get_nowbar_stop_icon_visible() && pt_in_rect(m_rect_stop, x, y))
    return HitRegion::StopButton;
  if (get_nowbar_stop_after_current_icon_visible() && pt_in_rect(m_rect_stop_after_current, x, y))
    return HitRegion::StopAfterCurrentButton;
  if (pt_in_rect(m_rect_prev, x, y))
    return HitRegion::PrevButton;
  if (pt_in_rect(m_rect_next, x, y))
    return HitRegion::NextButton;
  if (get_nowbar_rating_visible() && pt_in_rect(m_rect_rating, x, y))
    return HitRegion::RatingArea;
  if (get_nowbar_mood_icon_visible() && pt_in_rect(m_rect_heart, x, y))
    return HitRegion::HeartButton;
  if (get_nowbar_shuffle_icon_visible() && pt_in_rect(m_rect_shuffle, x, y))
    return HitRegion::ShuffleButton;
  if (get_nowbar_repeat_icon_visible() && pt_in_rect(m_rect_repeat, x, y))
    return HitRegion::RepeatButton;
  if (get_nowbar_super_icon_visible() && pt_in_rect(m_rect_super, x, y))
    return HitRegion::SuperButton;
  // Mode 1: thin progress bar at top with expanded hit area (±8px vertical)
  if (m_rect_thin_progress.right > m_rect_thin_progress.left) {
    int expand = static_cast<int>(8 * m_dpi_scale);
    RECT expanded_thin = {m_rect_thin_progress.left, m_rect_thin_progress.top,
                          m_rect_thin_progress.right, m_rect_thin_progress.bottom + expand};
    if (pt_in_rect(expanded_thin, x, y))
      return HitRegion::ThinProgressBar;
  }
  if (pt_in_rect(m_rect_seekbar, x, y))
    return HitRegion::SeekBar;
  // Custom buttons #1-6 (hidden during active playback if auto-hide enabled or fading)
  if (m_cbutton_opacity > 0.5f) {
    if (get_nowbar_cbutton_enabled(0) && pt_in_rect(m_rect_cbutton1, x, y))
      return HitRegion::CButton1;
    if (get_nowbar_cbutton_enabled(1) && pt_in_rect(m_rect_cbutton2, x, y))
      return HitRegion::CButton2;
    if (get_nowbar_cbutton_enabled(2) && pt_in_rect(m_rect_cbutton3, x, y))
      return HitRegion::CButton3;
    if (get_nowbar_cbutton_enabled(3) && pt_in_rect(m_rect_cbutton4, x, y))
      return HitRegion::CButton4;
    if (get_nowbar_cbutton_enabled(4) && pt_in_rect(m_rect_cbutton5, x, y))
      return HitRegion::CButton5;
    if (get_nowbar_cbutton_enabled(5) && pt_in_rect(m_rect_cbutton6, x, y))
      return HitRegion::CButton6;
  }
  // Volume area - check both icon and slider
  if (get_nowbar_volume_icon_visible() || get_nowbar_volume_bar_visible() || get_nowbar_volume_number_enabled()) {
    if (pt_in_rect(m_rect_volume, x, y)) {
      if (get_nowbar_volume_icon_visible() && get_nowbar_volume_bar_visible()) {
        int icon_size = static_cast<int>(23 * m_dpi_scale * m_size_scale);
        int vol_gap = static_cast<int>(14 * m_dpi_scale * m_size_scale);
        if (x < m_rect_volume.left + icon_size + vol_gap / 2) {
          return HitRegion::VolumeIcon;
        } else {
          return HitRegion::VolumeSlider;
        }
      } else if (get_nowbar_volume_icon_visible()) {
        int icon_size = static_cast<int>(23 * m_dpi_scale * m_size_scale);
        int vol_gap = static_cast<int>(14 * m_dpi_scale * m_size_scale);
        if (!get_nowbar_volume_number_enabled() || x < m_rect_volume.left + icon_size + vol_gap / 2) {
          return HitRegion::VolumeIcon;
        }
      } else if (get_nowbar_volume_bar_visible()) {
        return HitRegion::VolumeSlider;
      }
    }
  }
  if (get_nowbar_miniplayer_icon_visible() &&
      pt_in_rect(m_rect_miniplayer, x, y))
    return HitRegion::MiniPlayerButton;
  if (get_nowbar_cover_artwork_visible() && pt_in_rect(m_rect_artwork, x, y))
    return HitRegion::Artwork;
  if (pt_in_rect(m_rect_track_info, x, y))
    return HitRegion::TrackInfo;
  return HitRegion::None;
}

static bool is_center_playback_region(HitRegion region) {
  switch (region) {
    case HitRegion::HeartButton:
    case HitRegion::RatingArea:
    case HitRegion::ShuffleButton:
    case HitRegion::PrevButton:
    case HitRegion::PlayButton:
    case HitRegion::NextButton:
    case HitRegion::StopButton:
    case HitRegion::StopAfterCurrentButton:
    case HitRegion::RepeatButton:
    case HitRegion::SuperButton:
      return true;
    default:
      return false;
  }
}

void ControlPanelCore::on_mouse_move(int x, int y) {
  HitRegion new_region = hit_test(x, y);

  if (m_seeking) {
    // Calculate seek position using appropriate rect
    RECT seek_rect = m_rect_seekbar;
    if (m_pressed_region == HitRegion::ThinProgressBar) {
      seek_rect = m_rect_thin_progress;
    }
    double pos = static_cast<double>(x - seek_rect.left) /
                 (seek_rect.right - seek_rect.left);
    pos = std::max(0.0, std::min(1.0, pos));
    // Preview position (don't seek yet)
    m_state.playback_time = pos * m_state.track_length;
    invalidate();
    return;
  }

  if (m_volume_dragging) {
    bool volume_icon_vis = get_nowbar_volume_icon_visible();
    bool volume_number_vis = get_nowbar_volume_number_enabled();
    int icon_size = static_cast<int>(23 * m_dpi_scale * m_size_scale);
    int vol_gap = static_cast<int>(14 * m_dpi_scale * m_size_scale);
    int num_width = volume_number_vis
        ? ((m_volume_number_width > 0) ? m_volume_number_width : static_cast<int>(28 * m_dpi_scale))
        : 0;

    int bar_x = m_rect_volume.left + (volume_icon_vis ? (icon_size + vol_gap) : 0);
    int bar_w = (m_rect_volume.right - (volume_number_vis ? (vol_gap + num_width) : 0)) - bar_x;
    if (bar_w < 1) bar_w = 1;
    double level = static_cast<double>(x - bar_x) / bar_w;
    level = std::max(0.0, std::min(1.0, level));
    float db = slider_to_db(static_cast<float>(level));
    playback_control::get()->set_volume(db);
    // Update local state for immediate visual feedback
    m_state.volume_db = db;
    m_volume_hover_x = x;
    invalidate();
    return;
  }

  if (new_region != m_hover_region) {
    // Track previous hover for fade-out animation
    HitRegion old_region = m_hover_region;
    m_prev_hover_region = m_hover_region;
    m_hover_change_time = std::chrono::steady_clock::now();
    m_hover_region = new_region;
    update_thin_progress_hover_state();

    // Only skip full repaint when transitioning strictly between center playback buttons
    // while spectrum animation is active. Any transition involving outside controls
    // (Custom buttons, Volume, Miniplayer, ThinProgressBar) requires a full repaint
    // to update their static cache in m_cache_dc.
    bool old_center = is_center_playback_region(old_region);
    bool new_center = is_center_playback_region(new_region) || (new_region == HitRegion::None);
    bool can_use_soft = old_center && new_center && (m_spectrum_animating || m_waveform_animating);

    if (can_use_soft) {
      invalidate_soft();
    } else {
      invalidate();
    }
  }

  // Track which rating star the cursor is over
  if (new_region == HitRegion::RatingArea) {
    int new_hover_star = 0;
    for (int i = 0; i < 5; i++) {
      if (pt_in_rect(m_rect_stars[i], x, y)) {
        new_hover_star = i + 1;
        break;
      }
    }
    // Also check gaps between stars - assign to nearest star
    if (new_hover_star == 0 && m_rect_rating.right > m_rect_rating.left) {
      float frac = static_cast<float>(x - m_rect_rating.left) / (m_rect_rating.right - m_rect_rating.left);
      new_hover_star = std::min(5, std::max(1, static_cast<int>(frac * 5.0f) + 1));
    }
    if (new_hover_star != m_rating_hover_star) {
      m_rating_hover_star = new_hover_star;
      // Line 3 mode: stars are in track info area (not redrawn by fast path)
      if (get_nowbar_rating_mode() == 2 || !m_spectrum_animating) {
        invalidate();
      } else {
        invalidate_soft();
      }
    }
  } else if (m_rating_hover_star != 0) {
    m_rating_hover_star = 0;
    if (get_nowbar_rating_mode() == 2 || !(m_spectrum_animating || m_waveform_animating)) {
      invalidate();
    } else {
      invalidate_soft();
    }
  }

  // Spectrum hover fade (Mode 1): dim spectrum when hovering over button row
  if (get_nowbar_visualization_mode() == 1) {
    bool hovering_button = pt_in_rect(m_rect_button_row, x, y);
    float target = hovering_button ? 0.3f : 1.0f;
    if (m_spectrum_hover_target != target) {
      m_spectrum_hover_target = target;
      m_spectrum_hover_last_time = std::chrono::steady_clock::now();
      request_animation();
      invalidate_soft();
    }
  }

  // Track thin progress bar hover position for tooltip + invalidate for enlarge.
  // Always use full invalidate — the tooltip and expanded bar extend outside the
  // fast path's cached background area.
  if (new_region == HitRegion::ThinProgressBar) {
    m_seekbar_hover_x = x;
    invalidate();
  }

  // Track seekbar hover position for tooltip
  if (new_region == HitRegion::SeekBar && m_state.track_length > 0) {
    int old_hover_x = m_seekbar_hover_x;
    m_seekbar_hover_x = x;
    // Calculate preview time at this position
    int bar_w = m_rect_seekbar.right - m_rect_seekbar.left;
    double pos = static_cast<double>(x - m_rect_seekbar.left) / bar_w;
    pos = std::max(0.0, std::min(1.0, pos));
    m_preview_time = pos * m_state.track_length;
    
    // Only invalidate if position changed, with frame rate limiting
    if (old_hover_x != m_seekbar_hover_x) {
      auto now = std::chrono::steady_clock::now();
      float frame_elapsed = std::chrono::duration<float, std::milli>(now - m_last_animation_frame).count();
      if (frame_elapsed >= MIN_ANIMATION_FRAME_MS) {
        m_last_animation_frame = now;
        // Use partial invalidation - only repaint the tooltip area above the seekbar
        // Tooltip is roughly 60 pixels wide and 25 pixels tall, positioned above seekbar
        int tooltip_h = static_cast<int>(25 * m_dpi_scale);
        int tooltip_w = static_cast<int>(60 * m_dpi_scale);
        int gap = static_cast<int>(6 * m_dpi_scale);
        // Invalidate area covering old and new tooltip positions
        int left_x = std::min(old_hover_x, x) - tooltip_w / 2 - 5;
        int right_x = std::max(old_hover_x, x) + tooltip_w / 2 + 5;
        RECT tooltip_rect = {
          std::max(0L, (LONG)left_x),
          m_rect_seekbar.top - tooltip_h - gap - 5,
          (LONG)right_x,
          m_rect_seekbar.top + 5
        };
        invalidate_rect(tooltip_rect);
      }
    }
  }

  // Track volume slider hover position for tooltip and dot
  if (new_region == HitRegion::VolumeSlider) {
    int old_vol_x = m_volume_hover_x;
    m_volume_hover_x = x;
    if (old_vol_x != x) {
      invalidate();
    }
  }

  // Update native tooltip for custom buttons
  if (m_tooltip_hwnd) {
    int new_tooltip_button = -1;
    switch (new_region) {
      case HitRegion::CButton1: new_tooltip_button = 0; break;
      case HitRegion::CButton2: new_tooltip_button = 1; break;
      case HitRegion::CButton3: new_tooltip_button = 2; break;
      case HitRegion::CButton4: new_tooltip_button = 3; break;
      case HitRegion::CButton5: new_tooltip_button = 4; break;
      case HitRegion::CButton6: new_tooltip_button = 5; break;
      default: break;
    }
    
    // Only update if hovering over an enabled custom button
    if (new_tooltip_button >= 0 && get_nowbar_cbutton_enabled(new_tooltip_button)) {
      if (new_tooltip_button != m_tooltip_button_index) {
        m_tooltip_button_index = new_tooltip_button;
        
        // Get the tooltip label
        pfc::string8 label = get_nowbar_cbutton_label(new_tooltip_button);
        std::wstring labelW = pfc::stringcvt::string_wide_from_utf8(label.c_str()).get_ptr();
        
        // Update tooltip text
        TOOLINFOW ti = {};
        ti.cbSize = sizeof(TOOLINFOW);
        ti.hwnd = m_hwnd;
        ti.uId = (UINT_PTR)m_hwnd;
        ti.lpszText = const_cast<LPWSTR>(labelW.c_str());
        SendMessage(m_tooltip_hwnd, TTM_UPDATETIPTEXTW, 0, (LPARAM)&ti);
        
        // Activate the tooltip
        SendMessage(m_tooltip_hwnd, TTM_ACTIVATE, TRUE, 0);
      }
    } else {
      // Not over a custom button - hide tooltip
      if (m_tooltip_button_index != -1) {
        m_tooltip_button_index = -1;
        SendMessage(m_tooltip_hwnd, TTM_ACTIVATE, FALSE, 0);
      }
    }
  }
}

void ControlPanelCore::on_mouse_leave() {
  m_rating_hover_star = 0;

  if (m_hover_region != HitRegion::None) {
    HitRegion old_region = m_hover_region;
    m_hover_region = HitRegion::None;
    update_thin_progress_hover_state();

    // If leaving a center playback button, fast path can clear it; otherwise need full repaint
    if (is_center_playback_region(old_region) && (m_spectrum_animating || m_waveform_animating)) {
      invalidate_soft();
    } else {
      invalidate();
    }
  } else {
    update_thin_progress_hover_state();
  }

  // Reset spectrum hover target when mouse leaves the panel
  if (m_spectrum_hover_target != 1.0f) {
    m_spectrum_hover_target = 1.0f;
    m_spectrum_hover_last_time = std::chrono::steady_clock::now();
    request_animation();
    invalidate_soft();
  }

  // Hide tooltip when mouse leaves the panel
  if (m_tooltip_hwnd && m_tooltip_button_index != -1) {
    m_tooltip_button_index = -1;
    SendMessage(m_tooltip_hwnd, TTM_ACTIVATE, FALSE, 0);
  }
}

void ControlPanelCore::on_lbutton_down(int x, int y) {
  m_pressed_region = hit_test(x, y);

  // 3D button press animation
  if (get_nowbar_3d_buttons_enabled()) {
    int btn_idx = -1;
    switch (m_pressed_region) {
      case HitRegion::CButton1: btn_idx = 0; break;
      case HitRegion::CButton2: btn_idx = 1; break;
      case HitRegion::CButton3: btn_idx = 2; break;
      case HitRegion::CButton4: btn_idx = 3; break;
      case HitRegion::CButton5: btn_idx = 4; break;
      case HitRegion::CButton6: btn_idx = 5; break;
      default: break;
    }
    if (btn_idx >= 0) {
      m_cbutton_pressed[btn_idx] = true;
      m_cbutton_press_time[btn_idx] = std::chrono::steady_clock::now();
      m_cbutton_animating = true;
      request_animation();
      invalidate();
    }
  }

  if (m_pressed_region == HitRegion::SeekBar) {
    m_seeking = true;
    on_mouse_move(x, y);
  } else if (m_pressed_region == HitRegion::ThinProgressBar) {
    m_seeking = true;
    update_thin_progress_hover_state();
    // Calculate seek position using thin progress bar width
    double pos = static_cast<double>(x - m_rect_thin_progress.left) /
                 (m_rect_thin_progress.right - m_rect_thin_progress.left);
    pos = std::max(0.0, std::min(1.0, pos));
    m_state.playback_time = pos * m_state.track_length;
    invalidate();
  } else if (m_pressed_region == HitRegion::VolumeSlider) {
    m_volume_dragging = true;
    on_mouse_move(x, y);
  }
}

void ControlPanelCore::on_lbutton_up(int x, int y) {
  HitRegion release_region = hit_test(x, y);

  // 3D button release animation
  if (get_nowbar_3d_buttons_enabled()) {
    for (int i = 0; i < 6; i++) {
      if (m_cbutton_pressed[i]) {
        m_cbutton_pressed[i] = false;
        m_cbutton_release_time[i] = std::chrono::steady_clock::now();
        m_cbutton_animating = true;
        request_animation();
      }
    }
    invalidate();
  }

  if (m_seeking) {
    m_seeking = false;
    update_thin_progress_hover_state();
    // Commit seek - use appropriate rect based on which region started the seek
    RECT seek_rect = m_rect_seekbar;
    if (m_pressed_region == HitRegion::ThinProgressBar) {
      seek_rect = m_rect_thin_progress;
    }
    double pos = static_cast<double>(x - seek_rect.left) /
                 (seek_rect.right - seek_rect.left);
    pos = std::max(0.0, std::min(1.0, pos));
    do_seek(pos * m_state.track_length);
  } else if (m_volume_dragging) {
    m_volume_dragging = false;
  } else if (release_region == m_pressed_region) {
    switch (release_region) {
    case HitRegion::PlayButton:
      do_play_pause();
      break;
    case HitRegion::StopButton:
      do_stop();
      break;
    case HitRegion::StopAfterCurrentButton:
      do_stop_after_current();
      break;
    case HitRegion::PrevButton:
      do_prev();
      break;
    case HitRegion::NextButton:
      do_next();
      break;
    case HitRegion::HeartButton:
      do_toggle_mood();
      break;
    case HitRegion::RatingArea: {
      // Determine which star was clicked
      int clicked_star = 0;
      for (int i = 0; i < 5; i++) {
        if (pt_in_rect(m_rect_stars[i], x, y)) {
          clicked_star = i + 1;
          break;
        }
      }
      // Fallback: use position within rating area
      if (clicked_star == 0 && m_rect_rating.right > m_rect_rating.left) {
        float frac = static_cast<float>(x - m_rect_rating.left) / (m_rect_rating.right - m_rect_rating.left);
        clicked_star = std::min(5, std::max(1, static_cast<int>(frac * 5.0f) + 1));
      }
      if (clicked_star > 0) {
        do_set_rating(clicked_star);
      }
      break;
    }
    case HitRegion::ShuffleButton:
      do_shuffle_toggle();
      break;
    case HitRegion::RepeatButton:
      do_repeat_cycle();
      break;
    case HitRegion::SuperButton:
      show_autoplaylist_menu();
      break;
    case HitRegion::MiniPlayerButton: {
      // Launch / reveal MiniPlayer via mainmenu command
      service_enum_t<mainmenu_commands> e;
      mainmenu_commands::ptr ptr;
      while (e.next(ptr)) {
        for (t_uint32 i = 0; i < ptr->get_command_count(); i++) {
          pfc::string8 name;
          ptr->get_name(i, name);
          if (pfc::string_find_first(name, "Launch MiniPlayer") !=
              pfc::infinite_size) {
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
    case HitRegion::CustomButton:  // Legacy - maps to button 0
    case HitRegion::CButton1:
    case HitRegion::CButton2:
    case HitRegion::CButton3:
    case HitRegion::CButton4:
    case HitRegion::CButton5:
    case HitRegion::CButton6: {
      // Determine which button was clicked - use m_pressed_region which equals release_region here
      int button_index = -1;
      switch (m_pressed_region) {
        case HitRegion::CustomButton:
        case HitRegion::CButton1: button_index = 0; break;
        case HitRegion::CButton2: button_index = 1; break;
        case HitRegion::CButton3: button_index = 2; break;
        case HitRegion::CButton4: button_index = 3; break;
        case HitRegion::CButton5: button_index = 4; break;
        case HitRegion::CButton6: button_index = 5; break;
        default: break;
      }
      
      if (button_index < 0) break;
      
      int action = get_nowbar_cbutton_action(button_index);
      pfc::string8 path = get_nowbar_cbutton_path(button_index);
      
      if (action == 1 && !path.is_empty()) {
        // Open URL with title formatting support - use selected track from playlist
        pfc::string8 evaluated_url;
        
        // Check if path contains title formatting (has % character)
        if (path.find_first('%') != pfc::infinite_size) {
          // Has title formatting - evaluate it using selected track
          metadb_handle_ptr track;
          bool has_track = false;
          
          // Get the focused/selected track from the active playlist
          auto pm = playlist_manager::get();
          t_size active_playlist = pm->get_active_playlist();
          if (active_playlist != pfc_infinite) {
            t_size focus = pm->playlist_get_focus_item(active_playlist);
            if (focus != pfc_infinite) {
              has_track = pm->playlist_get_item_handle(track, active_playlist, focus);
            }
          }
          
          service_ptr_t<titleformat_object> script;
          titleformat_compiler::get()->compile_safe(script, path);
          
          url_titleformat_text_filter url_filter;
          if (has_track && track.is_valid() && script.is_valid()) {
            track->format_title(nullptr, evaluated_url, script, &url_filter);
          } else if (script.is_valid()) {
            // Fallback to playing track if no selection
            auto pc = playback_control::get();
            pc->playback_format_title(nullptr, evaluated_url, script, &url_filter, playback_control::display_level_all);
          }
        } else {
          // No title formatting - use path directly as URL, escaping spaces
          for (t_size i = 0; i < path.get_length(); ++i) {
            char c = path[i];
            if (c == ' ') {
              evaluated_url.add_string("%20", 3);
            } else {
              evaluated_url.add_byte(c);
            }
          }
        }
        
        if (!evaluated_url.is_empty()) {
          pfc::stringcvt::string_wide_from_utf8 wideUrl(evaluated_url);
          ShellExecuteW(nullptr, L"open", wideUrl, nullptr, nullptr, SW_SHOWNORMAL);
        }
      } else if (action == 2 && !path.is_empty()) {
        // Run Executable with file path argument - use selected track from playlist
        pfc::string8 exe_path;
        
        // Get the focused/selected track from the active playlist first
        auto pm = playlist_manager::get();
        metadb_handle_ptr track;
        bool has_track = false;
        
        t_size active_playlist = pm->get_active_playlist();
        if (active_playlist != pfc_infinite) {
          t_size focus = pm->playlist_get_focus_item(active_playlist);
          if (focus != pfc_infinite) {
            has_track = pm->playlist_get_item_handle(track, active_playlist, focus);
          }
        }
        
        // Check if path contains title formatting (has % character)
        if (path.find_first('%') != pfc::infinite_size) {
          // Has title formatting - evaluate it using selected track
          service_ptr_t<titleformat_object> script;
          titleformat_compiler::get()->compile_safe(script, path);
          
          if (has_track && track.is_valid() && script.is_valid()) {
            track->format_title(nullptr, exe_path, script, nullptr);
          } else if (script.is_valid()) {
            // Fallback to playing track if no selection
            auto pc = playback_control::get();
            pc->playback_format_title(nullptr, exe_path, script, nullptr, playback_control::display_level_all);
          }
        } else {
          // No title formatting - use path directly as executable
          exe_path = path;
        }
        
        if (!exe_path.is_empty()) {
          
          if (has_track && track.is_valid()) {
            const char* file_path = track->get_path();
            if (file_path) {
              pfc::string8 physical_path;
              filesystem::g_get_display_path(file_path, physical_path);
              pfc::string8 quoted_path;
              quoted_path << "\"" << physical_path << "\"";
              
              pfc::stringcvt::string_wide_from_utf8 wideExe(exe_path);
              pfc::stringcvt::string_wide_from_utf8 wideArgs(quoted_path);
              ShellExecuteW(nullptr, L"open", wideExe, wideArgs, nullptr, SW_SHOWNORMAL);
            } else {
              pfc::stringcvt::string_wide_from_utf8 wideExe(exe_path);
              ShellExecuteW(nullptr, L"open", wideExe, nullptr, nullptr, SW_SHOWNORMAL);
            }
          } else {
            // No track selected - just run the executable without arguments
            pfc::stringcvt::string_wide_from_utf8 wideExe(exe_path);
            ShellExecuteW(nullptr, L"open", wideExe, nullptr, nullptr, SW_SHOWNORMAL);
          }
        }
      } else if (action == 3 && !path.is_empty()) {
        // Foobar2k Action
        execute_fb2k_action_by_path(path.c_str());
      } else if (action == 4) {
        // Open Folder - open the directory containing the currently playing track
        metadb_handle_ptr track;
        bool has_track = false;

        // Try the focused/selected track from the active playlist first
        auto pm = playlist_manager::get();
        t_size active_playlist = pm->get_active_playlist();
        if (active_playlist != pfc_infinite) {
          t_size focus = pm->playlist_get_focus_item(active_playlist);
          if (focus != pfc_infinite) {
            has_track = pm->playlist_get_item_handle(track, active_playlist, focus);
          }
        }

        // Fallback to the currently playing track
        if (!has_track || !track.is_valid()) {
          auto pc = playback_control::get();
          has_track = pc->get_now_playing(track);
        }

        if (has_track && track.is_valid()) {
          const char* file_path = track->get_path();
          if (file_path) {
            pfc::string8 display_path;
            filesystem::g_get_display_path(file_path, display_path);

            // Open folder and select the file (opens in new tab on Windows 11)
            pfc::stringcvt::string_wide_from_utf8 widePath(display_path);
            PIDLIST_ABSOLUTE pidl = nullptr;
            if (SUCCEEDED(SHParseDisplayName(widePath, nullptr, &pidl, 0, nullptr))) {
              SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
              CoTaskMemFree(pidl);
            }
          }
        }
      } else if (action == 5) {
        // Output Device Menu - show popup menu with available devices
        show_output_device_menu(button_index);
      }
      break;
    }
    default:
      break;
    }
  }

  m_pressed_region = HitRegion::None;
  invalidate();
}

void ControlPanelCore::on_mouse_wheel(int delta) {
  // Adjust volume with mouse wheel using position-based stepping
  // to match foobar2000's default volume control (scroll_step = 20/1000).
  auto pc = playback_control::get();
  // Step in slider position space (0.0 to 1.0), matching SDK's 20/1000
  constexpr float position_step = 20.0f / 1000.0f;
  float current_pos = db_to_slider(m_state.volume_db);
  float new_pos = current_pos + ((delta > 0) ? position_step : -position_step);
  new_pos = std::max(0.0f, std::min(1.0f, new_pos));
  float new_volume = slider_to_db(new_pos);
  pc->set_volume(new_volume);
  // Update local state for immediate visual feedback (same as drag handler)
  m_state.volume_db = new_volume;
  invalidate();
}

void ControlPanelCore::on_lbutton_dblclk(int x, int y) {
  HitRegion region = hit_test(x, y);
  if (region == HitRegion::Artwork) {
    // Only show picture viewer for local/embedded artwork
    // Online artwork from foo_artwork isn't available to foobar2000's viewer
    if (!m_artwork_is_online) {
      show_picture_viewer();
    }
  } else if (region == HitRegion::TrackInfo) {
    // Highlight the currently playing track in the playlist
    // First, clear focus to ensure on_item_focus_change fires even if already focused
    auto pm = playlist_manager::get();
    t_size playing_playlist = pm->get_playing_playlist();
    if (playing_playlist != pfc_infinite) {
      // Set focus to no item first (forces focus change event on subsequent call)
      pm->playlist_set_focus_item(playing_playlist, pfc_infinite);
    }
    // Now highlight - this will set focus and trigger the callback
    pm->highlight_playing_item();
  }
}


void ControlPanelCore::show_picture_viewer() {
  // Use foobar2000's built-in Picture Viewer (available since 1.6.2)
  metadb_handle_ptr track = get_display_track();
  if (!track.is_valid())
    return;

  auto viewer = fb2k::imageViewer::tryGet();
  if (viewer.is_valid()) {
    metadb_handle_list items;
    items.add_item(track);
    viewer->load_and_show(m_hwnd, items, album_art_ids::cover_front);
  }
}

void ControlPanelCore::create_autoplaylist(const char* name, const char* query, const char* sort) {
  try {
    auto pm = playlist_manager::get();
    
    // First, check if a playlist with this name already exists
    t_size playlist_count = pm->get_playlist_count();
    for (t_size i = 0; i < playlist_count; i++) {
      pfc::string8 playlist_name;
      pm->playlist_get_name(i, playlist_name);
      if (pfc::stricmp_ascii(playlist_name.c_str(), name) == 0) {
        // Playlist with this name exists - just switch to it
        pm->set_active_playlist(i);
        return;
      }
    }
    
    // Playlist doesn't exist - create new one
    t_size new_playlist = pm->create_playlist(name, strlen(name), pfc_infinite);
    
    // Set it as autoplaylist with the query
    autoplaylist_manager::get()->add_client_simple(query, sort, new_playlist, 0);
    
    // Switch to the new playlist
    pm->set_active_playlist(new_playlist);
  } catch (const std::exception& e) {
    pfc::string8 msg;
    msg << "foo_nowbar: Failed to create autoplaylist - " << e.what();
    console::print(msg.c_str());
  }
}

void ControlPanelCore::show_autoplaylist_menu() {
  // Toggle behavior: if menu was just closed (within 200ms), don't reopen
  auto now = std::chrono::steady_clock::now();
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      now - m_autoplaylist_menu_close_time).count();
  if (elapsed < 200) {
    return;  // Don't reopen the menu if it was just closed
  }
  
  // Menu item IDs
  enum MenuID {
    ID_NEVER_PLAYED = 1,
    ID_PLAYED_LAST_5_DAYS,
    ID_UNRATED,
    ID_RATED_3_TO_5,
    ID_RATED_4,
    ID_RATED_5,
    ID_LOVED_TRACKS,
    ID_RECENTLY_ADDED,
    ID_SAME_ARTIST,
    ID_SAME_TITLE,
    ID_INFINITE_PLAYBACK,
    // Playback Preview submenu IDs
    ID_PREVIEW_OFF,
    ID_PREVIEW_35_PERCENT,
    ID_PREVIEW_50_PERCENT,
    ID_PREVIEW_60_SECONDS,
    // Skip Low Rating submenu IDs
    ID_SKIP_LOW_RATING_OFF,
    ID_SKIP_LOW_RATING_1,
    ID_SKIP_LOW_RATING_2,
    ID_SKIP_LOW_RATING_3,
    // Selection Mode submenu IDs
    ID_SELECTION_MODE_PRIORITIZE_NOW_PLAYING,
    ID_SELECTION_MODE_FOLLOW_SELECTION,
    ID_SETTINGS
  };
  
  // Create popup menu
  HMENU menu = CreatePopupMenu();
  if (!menu) return;
  
  // Get currently selected track for dynamic menu items
  // Try to get the focused item in the active playlist
  auto pm = playlist_manager::get();
  metadb_handle_ptr track;
  bool has_track = false;
  
  t_size active_playlist = pm->get_active_playlist();
  if (active_playlist != pfc_infinite) {
    t_size focus = pm->playlist_get_focus_item(active_playlist);
    if (focus != pfc_infinite) {
      has_track = pm->playlist_get_item_handle(track, active_playlist, focus);
    }
  }
  
  pfc::string8 current_artist;
  pfc::string8 current_title;
  
  if (has_track && track.is_valid()) {
    metadb_info_container::ptr info_container = track->get_info_ref();
    if (info_container.is_valid()) {
      const file_info& info = info_container->info();
      const char* artist = info.meta_get("artist", 0);
      const char* title = info.meta_get("title", 0);
      if (artist) current_artist = artist;
      if (title) current_title = title;
    }
  }
  
  // Add menu items - Group 1: Play count related
  AppendMenuW(menu, MF_STRING, ID_NEVER_PLAYED, L"Tracks never played");
  AppendMenuW(menu, MF_STRING, ID_PLAYED_LAST_5_DAYS, L"Tracks played in last 5 days");
  
  // Separator
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  
  // Group 2: Rating related
  AppendMenuW(menu, MF_STRING, ID_UNRATED, L"Tracks unrated");
  AppendMenuW(menu, MF_STRING, ID_RATED_3_TO_5, L"Tracks rated 3 to 5");
  AppendMenuW(menu, MF_STRING, ID_RATED_4, L"Tracks rated 4");
  AppendMenuW(menu, MF_STRING, ID_RATED_5, L"Tracks rated 5");
  
  // Separator
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  
  // Group 3: Special
  AppendMenuW(menu, MF_STRING, ID_LOVED_TRACKS, L"Loved Tracks");
  AppendMenuW(menu, MF_STRING, ID_RECENTLY_ADDED, L"Recently added");
  
  // Separator
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  
  // Group 4: Dynamic (based on currently selected track)
  UINT artist_flags = MF_STRING | (current_artist.is_empty() ? MF_GRAYED : 0);
  UINT title_flags = MF_STRING | (current_title.is_empty() ? MF_GRAYED : 0);
  AppendMenuW(menu, artist_flags, ID_SAME_ARTIST, L"Same artist as currently selected");
  AppendMenuW(menu, title_flags, ID_SAME_TITLE, L"Same title as currently selected");

  // Separator
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

  // Group 5: Playback mode
  UINT infinite_flags = MF_STRING | (get_nowbar_infinite_playback_enabled() ? MF_CHECKED : 0);
  AppendMenuW(menu, infinite_flags, ID_INFINITE_PLAYBACK, L"Infinite playback");

  // Group 6: Playback Preview submenu
  HMENU preview_submenu = CreatePopupMenu();
  if (preview_submenu) {
    int current_preview_mode = get_nowbar_preview_mode();

    UINT off_flags = MF_STRING | (current_preview_mode == 0 ? MF_CHECKED : 0);
    UINT p35_flags = MF_STRING | (current_preview_mode == 1 ? MF_CHECKED : 0);
    UINT p50_flags = MF_STRING | (current_preview_mode == 2 ? MF_CHECKED : 0);
    UINT p60_flags = MF_STRING | (current_preview_mode == 3 ? MF_CHECKED : 0);

    AppendMenuW(preview_submenu, off_flags, ID_PREVIEW_OFF, L"Off");
    AppendMenuW(preview_submenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(preview_submenu, p35_flags, ID_PREVIEW_35_PERCENT, L"35% of track");
    AppendMenuW(preview_submenu, p50_flags, ID_PREVIEW_50_PERCENT, L"50% of track");
    AppendMenuW(preview_submenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(preview_submenu, p60_flags, ID_PREVIEW_60_SECONDS, L"60 seconds");

    AppendMenuW(menu, MF_POPUP, (UINT_PTR)preview_submenu, L"Playback Preview");
  }

  // Group 7: Skip Low Rating submenu
  HMENU skip_submenu = CreatePopupMenu();
  if (skip_submenu) {
    bool skip_enabled = get_nowbar_skip_low_rating_enabled();
    int skip_threshold = get_nowbar_skip_low_rating_threshold();

    UINT off_flags = MF_STRING | (!skip_enabled ? MF_CHECKED : 0);
    UINT s1_flags = MF_STRING | (skip_enabled && skip_threshold == 1 ? MF_CHECKED : 0);
    UINT s2_flags = MF_STRING | (skip_enabled && skip_threshold == 2 ? MF_CHECKED : 0);
    UINT s3_flags = MF_STRING | (skip_enabled && skip_threshold == 3 ? MF_CHECKED : 0);

    AppendMenuW(skip_submenu, off_flags, ID_SKIP_LOW_RATING_OFF, L"Off");
    AppendMenuW(skip_submenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(skip_submenu, s1_flags, ID_SKIP_LOW_RATING_1, L"Skip if rating = 1");
    AppendMenuW(skip_submenu, s2_flags, ID_SKIP_LOW_RATING_2, L"Skip if rating \x2264 2");
    AppendMenuW(skip_submenu, s3_flags, ID_SKIP_LOW_RATING_3, L"Skip if rating \x2264 3");

    AppendMenuW(menu, MF_POPUP, (UINT_PTR)skip_submenu, L"Skip Low Rating");
  }

  // Separator between Skip Low Rating and Selection Mode
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

  // Group 8: Selection Mode submenu
  HMENU selection_submenu = CreatePopupMenu();
  if (selection_submenu) {
    int sel_mode = get_nowbar_selection_mode();

    UINT pnp_flags = MF_STRING | (sel_mode == 0 ? MF_CHECKED : 0);
    UINT fs_flags = MF_STRING | (sel_mode == 1 ? MF_CHECKED : 0);

    AppendMenuW(selection_submenu, pnp_flags, ID_SELECTION_MODE_PRIORITIZE_NOW_PLAYING, L"Prioritize Now Playing Mode");
    AppendMenuW(selection_submenu, fs_flags, ID_SELECTION_MODE_FOLLOW_SELECTION, L"Follow Selection");

    AppendMenuW(menu, MF_POPUP, (UINT_PTR)selection_submenu, L"Selection Mode");
  }

  // Separator before Settings
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

  // Settings option - opens Now Bar preferences page
  AppendMenuW(menu, MF_STRING, ID_SETTINGS, L"Settings");

  // Get Super button position for menu placement
  POINT pt;
  pt.x = m_rect_super.left;
  pt.y = m_rect_super.bottom;
  ClientToScreen(m_hwnd, &pt);
  
  // Show menu and get selection
  int cmd = TrackPopupMenuEx(menu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD | TPM_NONOTIFY,
                             pt.x, pt.y, m_hwnd, nullptr);
  
  DestroyMenu(menu);
  
  // Record when menu closed for toggle behavior
  m_autoplaylist_menu_close_time = std::chrono::steady_clock::now();
  
  // Handle selection
  switch (cmd) {
    case ID_NEVER_PLAYED:
      create_autoplaylist("Tracks never played", 
                          "%play_count% MISSING",
                          "%album artist% | %album% | %discnumber% | %tracknumber%");
      break;
    case ID_PLAYED_LAST_5_DAYS:
      create_autoplaylist("Tracks played in last 5 days",
                          "%last_played% DURING LAST 5 DAYS",
                          "%last_played%");
      break;
    case ID_UNRATED:
      create_autoplaylist("Tracks unrated",
                          "%rating% MISSING",
                          "%album artist% | %album% | %discnumber% | %tracknumber%");
      break;
    case ID_RATED_3_TO_5:
      create_autoplaylist("Tracks rated 3 to 5",
                          "%rating% GREATER 2",
                          "%rating% | %album artist% | %album%");
      break;
    case ID_RATED_4:
      create_autoplaylist("Tracks rated 4",
                          "%rating% EQUAL 4",
                          "%album artist% | %album% | %discnumber% | %tracknumber%");
      break;
    case ID_RATED_5:
      create_autoplaylist("Tracks rated 5",
                          "%rating% EQUAL 5",
                          "%album artist% | %album% | %discnumber% | %tracknumber%");
      break;
    case ID_LOVED_TRACKS: {
      // Determine tag name based on mood tag mode setting
      // 0=FEEDBACK, 1=2003_LOVED, 2=LFM_LOVED, 3=SMP_LOVED, 4=MOOD
      int tag_mode = get_nowbar_mood_tag_mode();
      const char* tag_name;
      pfc::string8 query;
      pfc::string8 sort;

      switch (tag_mode) {
        case 0:
          tag_name = "FEEDBACK";
          break;
        case 1:
          tag_name = "2003_LOVED";
          break;
        case 2:
          tag_name = "LFM_LOVED";
          break;
        case 3:
          tag_name = "SMP_LOVED";
          break;
        case 4:
        default:
          tag_name = "MOOD";
          break;
      }

      // For FEEDBACK tag, check for value "1" (loved); for others, just check PRESENT
      if (tag_mode == 0) {
        query.set_string("%");
        query.add_string(tag_name);
        query.add_string("% EQUAL 1");
      } else {
        query.set_string("%");
        query.add_string(tag_name);
        query.add_string("% PRESENT");
      }

      sort.set_string("%");
      sort.add_string(tag_name);
      sort.add_string("% | %album artist% | %album%");

      create_autoplaylist("Loved Tracks", query.c_str(), sort.c_str());
      break;
    }
    case ID_RECENTLY_ADDED:
      create_autoplaylist("Recently added",
                          "%added% DURING LAST 2 WEEKS",
                          "%added%");
      break;
    case ID_SAME_ARTIST:
      if (!current_artist.is_empty()) {
        // Escape double quotes in artist name using UTF-8 safe replace
        pfc::string8 escaped_artist = current_artist.replace("\"", "\\\"");
        
        pfc::string8 query;
        query << "artist IS \"" << escaped_artist << "\"";
        
        pfc::string8 name;
        name << "Artist: " << current_artist;
        
        create_autoplaylist(name.c_str(), query.c_str(),
                            "%album% | %discnumber% | %tracknumber%");
      }
      break;
    case ID_SAME_TITLE:
      if (!current_title.is_empty()) {
        // Escape double quotes in title using UTF-8 safe replace
        pfc::string8 escaped_title = current_title.replace("\"", "\\\"");

        pfc::string8 query;
        query << "title IS \"" << escaped_title << "\"";

        pfc::string8 name;
        name << "Title: " << current_title;

        create_autoplaylist(name.c_str(), query.c_str(),
                            "%album artist% | %album%");
      }
      break;
    case ID_INFINITE_PLAYBACK:
      // Toggle infinite playback mode
      set_nowbar_infinite_playback_enabled(!get_nowbar_infinite_playback_enabled());
      break;
    case ID_PREVIEW_OFF:
      set_nowbar_preview_mode(0);
      break;
    case ID_PREVIEW_35_PERCENT:
      set_nowbar_preview_mode(1);
      break;
    case ID_PREVIEW_50_PERCENT:
      set_nowbar_preview_mode(2);
      break;
    case ID_PREVIEW_60_SECONDS:
      set_nowbar_preview_mode(3);
      break;
    case ID_SKIP_LOW_RATING_OFF:
      set_nowbar_skip_low_rating_enabled(false);
      break;
    case ID_SKIP_LOW_RATING_1:
      set_nowbar_skip_low_rating_enabled(true);
      set_nowbar_skip_low_rating_threshold(1);
      break;
    case ID_SKIP_LOW_RATING_2:
      set_nowbar_skip_low_rating_enabled(true);
      set_nowbar_skip_low_rating_threshold(2);
      break;
    case ID_SKIP_LOW_RATING_3:
      set_nowbar_skip_low_rating_enabled(true);
      set_nowbar_skip_low_rating_threshold(3);
      break;
    case ID_SELECTION_MODE_PRIORITIZE_NOW_PLAYING:
      set_nowbar_selection_mode(0);
      m_selected_track.release();
      notify_all_settings_changed();
      break;
    case ID_SELECTION_MODE_FOLLOW_SELECTION:
      set_nowbar_selection_mode(1);
      notify_all_settings_changed();
      break;
    case ID_SETTINGS:
      // Defer via one-shot timer so show_autoplaylist_menu() returns first,
      // allowing animation frames between menu close and preferences open
      SetTimer(m_hwnd, SHOW_PREFS_TIMER_ID, 1, nullptr);
      break;
    default:
      break;
  }
}

void ControlPanelCore::show_output_device_menu_popup(HWND hwnd, POINT pt) {
  if (!static_api_test_t<output_manager_v2>()) {
    return;
  }
  auto api = output_manager_v2::get();

  struct DeviceEntry {
    GUID output_id;
    GUID device_id;
    pfc::string8 name;
  };
  std::vector<DeviceEntry> devices;
  api->listDevices([&devices](const char* name, const GUID& output_id, const GUID& device_id) {
    devices.push_back({output_id, device_id, name});
  });

  outputCoreConfig_t config{};
  api->getCoreConfig(config);

  HMENU menu = CreatePopupMenu();
  if (!menu) return;

  if (devices.empty()) {
    AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, L"(No output devices available)");
  } else {
    for (size_t i = 0; i < devices.size(); ++i) {
      bool is_active = (devices[i].output_id == config.m_output && devices[i].device_id == config.m_device);
      UINT flags = MF_STRING | (is_active ? MF_CHECKED : 0);
      pfc::stringcvt::string_wide_from_utf8 wideName(devices[i].name);
      AppendMenuW(menu, flags, static_cast<UINT>(1000 + i), wideName.get_ptr());
    }
  }

  if (hwnd) {
    SetForegroundWindow(hwnd);
  }

  int cmd = TrackPopupMenuEx(menu, TPM_LEFTALIGN | TPM_BOTTOMALIGN | TPM_RETURNCMD | TPM_NONOTIFY,
                             pt.x, pt.y, hwnd ? hwnd : core_api::get_main_window(), nullptr);
  DestroyMenu(menu);

  if (cmd >= 1000 && cmd < 1000 + static_cast<int>(devices.size())) {
    size_t selected_idx = static_cast<size_t>(cmd - 1000);
    api->setCoreConfigDevice(devices[selected_idx].output_id, devices[selected_idx].device_id);
  }
}

void ControlPanelCore::show_output_device_menu(int button_index) {
  RECT* rects[6] = {&m_rect_cbutton1, &m_rect_cbutton2, &m_rect_cbutton3,
                    &m_rect_cbutton4, &m_rect_cbutton5, &m_rect_cbutton6};
  RECT btn_rect = (button_index >= 0 && button_index < 6) ? *rects[button_index] : RECT{};
  POINT pt = { btn_rect.left, btn_rect.top };
  ClientToScreen(m_hwnd, &pt);
  show_output_device_menu_popup(m_hwnd, pt);
}

void ControlPanelCore::do_play_pause() {
  playback_control::get()->play_or_pause();
}

void ControlPanelCore::do_prev() { playback_control::get()->previous(); }

void ControlPanelCore::do_next() { playback_control::get()->next(); }

void ControlPanelCore::do_stop() { playback_control::get()->stop(); }

void ControlPanelCore::do_stop_after_current() {
  auto pc = playback_control::get();
  pc->set_stop_after_current(!pc->get_stop_after_current());
  m_stop_after_current_active = pc->get_stop_after_current();
  invalidate();
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
  case 0:
    new_order = 1;
    break; // default -> repeat playlist
  case 1:
    new_order = 2;
    break; // repeat playlist -> repeat track
  case 2:
    new_order = 0;
    break; // repeat track -> default
  default:
    new_order = 0;
    break;
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

// Mood metadata filter for updating mood-related tags
class MoodMetadataFilter : public file_info_filter {
public:
  MoodMetadataFilter(bool set_mood, const char *tag_name, const char *value)
      : m_set_mood(set_mood), m_tag_name(tag_name), m_value(value) {}

  bool apply_filter(trackRef track, t_filestats stats,
                    file_info &info) override {
    (void)track;
    (void)stats;
    if (m_set_mood) {
      info.meta_set(m_tag_name.c_str(), m_value.c_str());
    } else {
      info.meta_remove_field(m_tag_name.c_str());
    }
    return true;
  }

private:
  bool m_set_mood;
  pfc::string8 m_tag_name;
  pfc::string8 m_value;
};

void ControlPanelCore::do_set_rating(int star) {
  // If clicking the same star that's already set, clear the rating
  int new_rating = (star == m_rating_value) ? 0 : star;

  metadb_handle_ptr track = get_rating_track();
  if (!track.is_valid()) return;

  // Build target command path for foo_playcount's context menu
  // e.g. "Playback Statistics/Rating/3" or "Playback Statistics/Rating/<not set>"
  pfc::string8 target_path("Playback Statistics/Rating/");
  if (new_rating >= 1 && new_rating <= 5) {
    char buf[2] = { (char)('0' + new_rating), 0 };
    target_path += buf;
  } else {
    target_path += "<not set>";
  }

  // Execute via context menu on the target track
  metadb_handle_list tracks;
  tracks.add_item(track);

  contextmenu_manager::ptr cm;
  contextmenu_manager::g_create(cm);
  cm->init_context(tracks, 0);

  contextmenu_node* root = cm->get_root();
  if (!root) return;

  // Recursive traversal matching the proven pattern in execute_fb2k_action_by_path
  std::function<bool(contextmenu_node*, pfc::string8)> search_and_execute;
  search_and_execute = [&](contextmenu_node* node, pfc::string8 parent_path) -> bool {
    if (!node) return false;
    t_size child_count = node->get_num_children();
    for (t_size i = 0; i < child_count; i++) {
      contextmenu_node* child = node->get_child(i);
      if (!child) continue;

      const char* child_name = child->get_name();
      if (!child_name) continue;
      if (child->get_type() == contextmenu_item_node::type_separator) continue;

      pfc::string8 full_path;
      if (!parent_path.is_empty()) {
        full_path << parent_path << "/";
      }
      full_path << child_name;

      if (child->get_type() == contextmenu_item_node::type_command) {
        if (stricmp_utf8(full_path.c_str(), target_path.c_str()) == 0) {
          child->execute();
          return true;
        }
      } else if (child->get_type() == contextmenu_item_node::type_group) {
        if (search_and_execute(child, full_path)) return true;
      }
    }
    return false;
  };

  if (search_and_execute(root, "")) {
    m_rating_value = new_rating;
    invalidate();
  }
}

void ControlPanelCore::do_toggle_mood() {
  metadb_handle_ptr track = get_display_track();
  if (!track.is_valid()) {
    return;
  }

  // Toggle mood state
  bool new_mood = !m_mood_active;

  // Determine tag name and value based on mood tag mode setting
  // 0=FEEDBACK (1/-1), 1=2003_LOVED (1), 2=LFM_LOVED (1), 3=SMP_LOVED (1), 4=MOOD (datetime)
  int tag_mode = get_nowbar_mood_tag_mode();
  const char* tag_name;
  pfc::string8 value;

  switch (tag_mode) {
    case 0: {  // FEEDBACK - "1" if loved, "-1" if hated (we only do loved via toggle)
      tag_name = "FEEDBACK";
      value = "1";
      break;
    }
    case 1: {  // 2003_LOVED - "1" if loved, remove if not
      tag_name = "2003_LOVED";
      value = "1";
      break;
    }
    case 2: {  // LFM_LOVED - "1" if loved, remove if not
      tag_name = "LFM_LOVED";
      value = "1";
      break;
    }
    case 3: {  // SMP_LOVED - "1" if loved, remove if not
      tag_name = "SMP_LOVED";
      value = "1";
      break;
    }
    case 4:  // MOOD - datetime string
    default: {
      tag_name = "MOOD";
      // Get current date/time for MOOD tag
      SYSTEMTIME st;
      GetLocalTime(&st);
      char datetime[64];
      snprintf(datetime, sizeof(datetime), "%04d-%02d-%02d %02d:%02d:%02d",
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
      value = datetime;
      break;
    }
  }

  // Create filter
  auto filter = fb2k::service_new<MoodMetadataFilter>(new_mood, tag_name, value.c_str());

  // Update metadata asynchronously
  metadb_handle_list tracks;
  tracks.add_item(track);
  metadb_io_v2::get()->update_info_async(
      tracks, filter, core_api::get_main_window(),
      metadb_io_v2::op_flag_partial_info_aware, nullptr);

  m_mood_active = new_mood;
  invalidate();
}

void ControlPanelCore::update_mood_state() {
  metadb_handle_ptr track = get_display_track();
  if (!track.is_valid()) {
    m_mood_active = false;
    return;
  }

  // Determine tag name based on mood tag mode setting
  // 0=FEEDBACK, 1=2003_LOVED, 2=LFM_LOVED, 3=SMP_LOVED, 4=MOOD
  int tag_mode = get_nowbar_mood_tag_mode();
  const char* tag_name;

  switch (tag_mode) {
    case 0: {
      tag_name = "FEEDBACK";
      break;
    }
    case 1: {
      tag_name = "2003_LOVED";
      break;
    }
    case 2: {
      tag_name = "LFM_LOVED";
      break;
    }
    case 3: {
      tag_name = "SMP_LOVED";
      break;
    }
    case 4:
    default: {
      tag_name = "MOOD";
      break;
    }
  }

  // Check if the configured tag exists and has a "positive" value
  metadb_info_container::ptr info_container = track->get_info_ref();
  if (info_container.is_valid()) {
    const file_info &info = info_container->info();
    const char *tag_value = info.meta_get(tag_name, 0);
    if (tag_value != nullptr && strlen(tag_value) > 0) {
      // For FEEDBACK tag, check if value is "1" (loved) vs "-1" (hated)
      // For other tags, any non-empty value means "loved"
      if (tag_mode == 0) {  // FEEDBACK mode
        m_mood_active = (strcmp(tag_value, "1") == 0);
      } else {
        m_mood_active = true;
      }
    } else {
      m_mood_active = false;
    }
  } else {
    m_mood_active = false;
  }
}

metadb_handle_ptr ControlPanelCore::get_selected_track() const {
  // 1. Try ui_selection_manager
  metadb_handle_list list;
  if (static_api_test_t<ui_selection_manager_v2>()) {
    ui_selection_manager_v2::get()->get_selection(list, ui_selection_manager_v2::flag_no_now_playing);
  } else if (static_api_test_t<ui_selection_manager>()) {
    ui_selection_manager::get()->get_selection(list);
  }
  if (list.get_count() > 0 && list[0].is_valid()) {
    return list[0];
  }

  // 2. Fall back to focused item in active playlist
  auto pm = playlist_manager::get();
  t_size active = pm->get_active_playlist();
  if (active != pfc_infinite) {
    t_size focus = pm->playlist_get_focus_item(active);
    if (focus != pfc_infinite) {
      metadb_handle_ptr track;
      if (pm->playlist_get_item_handle(track, active, focus) && track.is_valid()) {
        return track;
      }
    }
  }

  return metadb_handle_ptr();
}

metadb_handle_ptr ControlPanelCore::get_display_track() const {
  int mode = get_nowbar_selection_mode(); // 0 = Prioritize Now Playing Mode, 1 = Follow Selection
  auto pc = playback_control::get();
  bool is_playing = pc->is_playing() || pc->is_paused();

  if (mode == 0) {
    // Prioritize Now Playing Mode: always show now playing during playback, fall back to follow selection when stopped
    if (is_playing) {
      metadb_handle_ptr track;
      if (pc->get_now_playing(track) && track.is_valid()) {
        return track;
      }
      if (m_state.current_track.is_valid()) {
        return m_state.current_track;
      }
    }
    return m_selected_track;
  } else {
    // Follow Selection: always follow selected track regardless of playback state
    metadb_handle_ptr sel = get_selected_track();
    if (sel.is_valid()) {
      return sel;
    }
    // Fall back to now-playing if playing and no track is selected
    if (is_playing) {
      metadb_handle_ptr track;
      if (pc->get_now_playing(track) && track.is_valid()) {
        return track;
      }
      if (m_state.current_track.is_valid()) {
        return m_state.current_track;
      }
    }
    return metadb_handle_ptr();
  }
}

metadb_handle_ptr ControlPanelCore::get_rating_track() {
  return get_display_track();
}

void ControlPanelCore::on_selection_or_focus_changed() {
  if (core_api::is_initializing()) {
    return;
  }

  int mode = get_nowbar_selection_mode();
  auto pc = playback_control::get();
  bool is_playing = pc->is_playing() || pc->is_paused();

  // In Prioritize Now Playing mode while playback is active, selection changes do not affect display
  if (mode == 0 && is_playing) {
    return;
  }

  if (mode == 0) {
    // When playback is stopped in Prioritize Now Playing mode, only follow active user selection
    metadb_handle_list list;
    if (static_api_test_t<ui_selection_manager_v2>()) {
      ui_selection_manager_v2::get()->get_selection(list, ui_selection_manager_v2::flag_no_now_playing);
    } else if (static_api_test_t<ui_selection_manager>()) {
      ui_selection_manager::get()->get_selection(list);
    }
    if (list.get_count() > 0 && list[0].is_valid()) {
      m_selected_track = list[0];
    } else {
      m_selected_track.release();
    }
  }

  update_rating_state();
  update_mood_state();
  evaluate_title_formats();

  if (m_artwork_request_cb) {
    m_artwork_request_cb();
  }

  m_needs_full_repaint = true;
  invalidate();
}

void ControlPanelCore::update_rating_state() {
  metadb_handle_ptr track = get_display_track();
  if (!track.is_valid()) {
    m_rating_value = 0;
    return;
  }

  // Evaluate %rating% using cached titleformat (compiled once, reused)
  try {
    if (!m_titleformat_rating.is_valid()) {
      static_api_ptr_t<titleformat_compiler> compiler;
      compiler->compile(m_titleformat_rating, "%rating%");
    }
    if (m_titleformat_rating.is_valid()) {
      pfc::string8 rating_str;
      track->format_title(nullptr, rating_str, m_titleformat_rating, nullptr);
      if (!rating_str.is_empty()) {
        int rating = atoi(rating_str.c_str());
        m_rating_value = (rating >= 1 && rating <= 5) ? rating : 0;
      } else {
        m_rating_value = 0;
      }
    }
  } catch (...) {
    m_rating_value = 0;
  }
}

void ControlPanelCore::on_playback_state_changed(const PlaybackState &state) {
  bool was_playing = m_state.is_playing || m_state.is_paused;
  bool was_paused = m_state.is_paused;
  m_state = state;
  bool is_playing_now = m_state.is_playing && !m_state.is_paused;
  bool is_paused_now = m_state.is_paused;
  bool is_stopped = !m_state.is_playing && !m_state.is_paused;

  // Track-to-track transition (on_playback_starting fires before on_playback_new_track):
  // The playback state hasn't meaningfully changed (still playing), and on_track_changed()
  // will handle all updates with the new track's data.  Skip all work here to avoid a
  // wasted paint cycle with stale data and redundant vis stream / waveform setup.
  if (was_playing && is_playing_now && !was_paused) {
    return;
  }

  // If playback stopped completely, reset to initial state
  if (was_playing && is_stopped) {
    m_selected_track.release();

    // Reset progress bar
    m_animated_progress = 0.0;
    m_target_progress = 0.0;

    // Reset stop-after-current state (SDK clears it when playback stops)
    m_stop_after_current_active = false;

    // Reset spectrum hover state
    m_spectrum_hover_target = 1.0f;
    m_spectrum_hover_opacity = 1.0f;
    m_spectrum_hover_last_time = {};

    // Fall back to selection (or refresh display track)
    update_mood_state();
    update_rating_state();
    evaluate_title_formats();

    if (m_artwork_request_cb) {
      m_artwork_request_cb();
    } else {
      clear_artwork();
    }
  }

  // Reapply theme when transitioning between stopped and playing/paused
  // This handles Light theme + artwork background special case (Light when stopped, Dark when playing)
  bool was_stopped = !was_playing;
  if (was_stopped != is_stopped) {
    apply_theme();
    nowbar_notify_color_changed();
  }

  // Trigger fade animation if playback state changed
  if (get_nowbar_cbutton_autohide() && (was_playing && is_stopped) != (!was_playing && is_playing_now)) {
    m_cbutton_start_opacity = m_cbutton_opacity;  // Start from current opacity
    m_cbutton_target_opacity = is_playing_now ? 0.0f : 1.0f;
    m_cbutton_fade_start_time = std::chrono::steady_clock::now();
    m_cbutton_fade_active = true;
    if (get_nowbar_smooth_animations_enabled()) {
      m_cbutton_animating = true;
      request_animation();
    }
  }

  // Spectrum visualizer fade animation (mode 1 only)
  int vis_mode = get_nowbar_visualization_mode();
  if (vis_mode == 1) {
    if (is_playing_now) {
      create_vis_stream();
      m_spectrum_start_opacity = m_spectrum_opacity;
      m_spectrum_target_opacity = 1.0f;
      m_spectrum_fade_start_time = std::chrono::steady_clock::now();
      m_spectrum_fade_active = true;
      m_spectrum_animating = true;
    } else {
      m_spectrum_start_opacity = m_spectrum_opacity;
      m_spectrum_target_opacity = 0.0f;
      m_spectrum_fade_start_time = std::chrono::steady_clock::now();
      m_spectrum_fade_active = true;
      m_spectrum_animating = true;
    }
  }

  // Waveform: start computation on play if mode 2 and not yet computed; clear on stop
  if (vis_mode == 2) {
    if (is_playing_now && !m_waveform_valid && !m_waveform_computing.load()) {
      start_waveform_computation();
    } else if (is_stopped) {
      // Cancel any in-progress decode but keep the computed waveform visible
      cancel_waveform_computation();
    }
  }

  evaluate_title_formats();
  invalidate();
}

void ControlPanelCore::on_playback_time_changed(double time) {
  if (!m_seeking) {
    m_state.playback_time = time;

    // Update target progress (used directly by seekbar, no interpolation)
    if (m_state.track_length > 0) {
      m_target_progress = time / m_state.track_length;
    } else {
      m_target_progress = 0.0;
    }

    // Always use direct invalidate for playback time updates
    // Progress bar does not use smooth animation for performance reasons
    invalidate_progress();

    // Re-evaluate line formats if they reference dynamic fields (%playback_time*%, %bitrate*%),
    // and flag full repaint so the UI wrapper redraws track info into the offscreen cache.
    if (m_lines_have_dynamic_time) {
      evaluate_title_formats();
      m_needs_full_repaint = true;
      if (m_hwnd) InvalidateRect(m_hwnd, nullptr, FALSE);
    }
  }
}

void ControlPanelCore::on_volume_changed(float volume_db) {
  m_state.volume_db = volume_db;
  if (!m_volume_dragging) {
    invalidate();
  }
}

void ControlPanelCore::on_track_changed() {
  m_selected_track.release();

  // Sync local state from the manager.  on_playback_new_track() updates
  // current_track and track info in the manager but intentionally skips
  // notify_state_changed(), so the local m_state would still have the
  // stale copy from on_playback_starting().  Without this sync,
  // evaluate_title_formats() sees an invalid current_track on the first
  // track after restart and produces empty title/artist strings.
  if (PlaybackStateManager::is_available()) {
    m_state = PlaybackStateManager::get().get_state();
  }

  m_needs_full_repaint = true;
  m_bg_cache_valid = false;

  // Re-apply theme to ensure bg color is fresh (Custom theme depends on
  // playback state for artwork-based backgrounds)
  apply_theme();
  nowbar_notify_color_changed();

  // Update mood state for new track
  update_mood_state();

  // Update rating state for new track
  update_rating_state();

  // Update stop-after-current state (may have been toggled externally)
  if (get_nowbar_stop_after_current_icon_visible()) {
    m_stop_after_current_active = playback_control::get()->get_stop_after_current();
  }

  // Re-evaluate title formats for new track
  evaluate_title_formats();

  // Reset progress animation for new track
  m_animated_progress = 0.0;
  m_target_progress = 0.0;

  // Waveform: reset reveal and recompute if mode 2
  if (get_nowbar_visualization_mode() == 2) {
    m_waveform_reveal_pos = 0.0f;
    m_waveform_decode_count.store(0, std::memory_order_relaxed);
    m_waveform_reveal_active = false;
    {
      std::lock_guard<std::mutex> lock(m_waveform_mutex);
      m_waveform_valid = false;
      m_waveform_peaks.assign(WAVEFORM_SEGMENTS, 0.0f);
    }
    start_waveform_computation();
  }

  // Request artwork update from UI wrapper
  if (m_artwork_request_cb) {
    m_artwork_request_cb();
  }
  invalidate();
}

void ControlPanelCore::on_track_info_changed() {
  if (PlaybackStateManager::is_available()) {
    m_state = PlaybackStateManager::get().get_state();
  }

  m_needs_full_repaint = true;
  m_bg_cache_valid = false;

  // Re-apply theme to ensure colors are updated if customized
  apply_theme();
  nowbar_notify_color_changed();

  // Update mood and rating state (tags might have changed)
  update_mood_state();
  update_rating_state();

  // Re-evaluate title formats with the updated metadata
  evaluate_title_formats();

  // Request artwork update from UI wrapper if tags or cover art changed
  if (m_artwork_request_cb) {
    m_artwork_request_cb();
  }
  invalidate();
}

void ControlPanelCore::on_dynamic_info_changed() {
  evaluate_title_formats();
  m_needs_full_repaint = true;
  if (m_hwnd) InvalidateRect(m_hwnd, nullptr, FALSE);
}

void ControlPanelCore::set_artwork(album_art_data_ptr data) {
  m_needs_full_repaint = true;
  m_artwork_is_online = false;  // Local/embedded artwork
  if (!data.is_valid() || data->get_size() == 0) {
    clear_artwork();
    return;
  }

  // Create GDI+ bitmap from data
  IStream *stream = nullptr;
  HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, data->get_size());
  if (hMem) {
    void *pMem = GlobalLock(hMem);
    if (pMem) {
      memcpy(pMem, data->get_ptr(), data->get_size());
      GlobalUnlock(hMem);

      if (SUCCEEDED(CreateStreamOnHGlobal(hMem, TRUE, &stream))) {
        m_artwork_bitmap.reset(Gdiplus::Bitmap::FromStream(stream));
        stream->Release();
        m_artwork_track = get_display_track();

        // Create thumbnail first (downscales full-res and releases it),
        // then extract colors from the smaller thumbnail instead of full-res.
        create_artwork_thumbnail();
        extract_artwork_colors();

        // Trigger background transition BEFORE invalidating cache
        // Only requires m_prev_background to exist (it contains the rendered old state)
        // m_bg_cache_valid check removed: it just indicates cache freshness, but
        // m_prev_background already has the old background baked in from prior paints
        int bg_style = get_nowbar_background_style();
        if ((bg_style == 1 || bg_style == 2) && get_nowbar_smooth_animations_enabled()) {
          if (m_prev_background) {
            m_bg_transition_active = true;
            m_bg_transition_start_time = std::chrono::steady_clock::now();
          }
        }
        
        // Invalidate blurred artwork cache so it regenerates with new artwork
        m_blurred_artwork.reset();
        m_target_background.reset(); // Invalidate target for new artwork
        m_blurred_artwork_size = {0, 0};
        m_bg_cache_valid = false;  // Invalidate cache for new artwork
      }
    }
  }

  invalidate();
}

void ControlPanelCore::set_artwork_from_hbitmap(HBITMAP bitmap) {
  m_needs_full_repaint = true;
  m_artwork_is_online = true;  // Online artwork from foo_artwork
  if (!bitmap) {
    clear_artwork();
    return;
  }

  // Create GDI+ bitmap from HBITMAP
  m_artwork_bitmap.reset(Gdiplus::Bitmap::FromHBITMAP(bitmap, nullptr));

  if (m_artwork_bitmap && m_artwork_bitmap->GetLastStatus() == Gdiplus::Ok) {
    m_artwork_track = get_display_track();

    // Create thumbnail first (downscales full-res and releases it),
    // then extract colors from the smaller thumbnail instead of full-res.
    create_artwork_thumbnail();
    extract_artwork_colors();

    // Trigger background transition BEFORE invalidating cache
    // Only requires m_prev_background to exist (it contains the rendered old state)
    int bg_style = get_nowbar_background_style();
    if ((bg_style == 1 || bg_style == 2) && get_nowbar_smooth_animations_enabled()) {
      if (m_prev_background) {
        m_bg_transition_active = true;
        m_bg_transition_start_time = std::chrono::steady_clock::now();
      }
    }

    // Invalidate blurred artwork cache so it regenerates with new artwork
    m_blurred_artwork.reset();
    m_target_background.reset();
    m_blurred_artwork_size = {0, 0};
    m_bg_cache_valid = false;
  } else {
    m_artwork_is_online = false;
    m_artwork_track.release();
    m_artwork_bitmap.reset();
    m_artwork_thumbnail.reset();
  }

  // Re-evaluate title formats when new artwork is loaded to sync artist/title
  evaluate_title_formats();

  invalidate();
}

void ControlPanelCore::clear_artwork() {
  bool had_valid_colors = m_artwork_colors_valid;
  m_needs_full_repaint = true;
  m_artwork_is_online = false;
  m_artwork_track.release();
  m_artwork_bitmap.reset();
  m_artwork_thumbnail.reset();
  m_artwork_colors_valid = false;
  m_blurred_artwork.reset();
  m_target_background.reset();
  m_blurred_artwork_size = {0, 0};
  m_bg_cache_valid = false;  // Invalidate cache when artwork cleared
  if (had_valid_colors) {
    nowbar_notify_color_changed();
  }
  invalidate();
}

void ControlPanelCore::invalidate() {
  m_needs_full_repaint = true;
  if (m_hwnd) {
    InvalidateRect(m_hwnd, nullptr, FALSE);
  }
}

void ControlPanelCore::invalidate_soft() {
  // Invalidate the window without setting m_needs_full_repaint, so the
  // spectrum/waveform fast path remains active.
  if (m_hwnd) {
    InvalidateRect(m_hwnd, nullptr, FALSE);
  }
}

void ControlPanelCore::invalidate_rect(const RECT& rect) {
  m_needs_full_repaint = true;
  if (m_hwnd) {
    InvalidateRect(m_hwnd, &rect, FALSE);
  }
}

void ControlPanelCore::invalidate_progress() {
    // Partial invalidation for progress updates only.
    // Does NOT set m_needs_full_repaint, allowing fast paths in UI wrappers.
    if (!m_hwnd) return;

    int vis_mode = get_nowbar_visualization_mode();
    RECT dirty = {};

    if (vis_mode == 1) {
        // Spectrum mode: full panel invalidation because thin progress bar
        // overlays artwork and time display is updated. Setting m_needs_full_repaint
        // ensures a clean full repaint refreshes the progress bar without
        // alpha accumulation over the cover artwork.
        m_needs_full_repaint = true;
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    } else if (vis_mode == 2) {
        // Waveform mode: waveform area + time display
        UnionRect(&dirty, &m_rect_waveform, &m_rect_time);
    } else {
        // Normal seekbar mode: seekbar + time display
        UnionRect(&dirty, &m_rect_seekbar, &m_rect_time);
    }

    InvalidateRect(m_hwnd, &dirty, FALSE);
}

void ControlPanelCore::request_animation(const RECT* dirty) {
  // Throttled animation request - schedules frames at ~60 FPS
  // Uses SetTimer to ensure the animation loop continues even without mouse events
  if (!m_hwnd) return;

  // Track dirty region: if any caller requests full repaint, stay full
  if (dirty && m_animation_dirty_partial) {
    // Merge with existing partial rect
    UnionRect(&m_animation_dirty_rect, &m_animation_dirty_rect, dirty);
  } else if (dirty && !m_animation_requested) {
    // First partial request this cycle
    m_animation_dirty_rect = *dirty;
    m_animation_dirty_partial = true;
  } else if (!dirty) {
    // Full repaint requested
    m_animation_dirty_partial = false;
  }

  // Use 30 FPS when only spectrum is animating (no other animations active),
  // unless 60fps mode is enabled in preferences
  bool spectrum_only = m_spectrum_animating && !m_seekbar_animating &&
                       !m_hover_animating && !m_cbutton_animating &&
                       !m_bg_animating && !m_waveform_animating;
  bool use_30fps = spectrum_only && !get_nowbar_vis_60fps();
  float target_interval = use_30fps ? SPECTRUM_FRAME_INTERVAL_MS : TARGET_FRAME_INTERVAL_MS;
  UINT max_delay = use_30fps ? 34 : 17;

  auto now = std::chrono::steady_clock::now();
  float elapsed_ms = std::chrono::duration<float, std::milli>(now - m_last_invalidate_time).count();

  if (elapsed_ms >= target_interval) {
    // Enough time has passed, perform actual invalidation
    m_last_invalidate_time = now;
    m_animation_requested = false;

    // Kill any pending timer since we're painting now
    if (m_animation_timer_active) {
      DeleteTimerQueueTimer(nullptr, m_anim_tp_timer, nullptr);
      m_anim_tp_timer = nullptr;
      m_animation_timer_active = false;
    }

    const RECT* inv_rect = m_animation_dirty_partial ? &m_animation_dirty_rect : nullptr;
    InvalidateRect(m_hwnd, inv_rect, FALSE);
    m_animation_dirty_partial = false;
  } else if (!m_animation_timer_active) {
    // Not enough time has passed — schedule a thread-pool one-shot that posts
    // WM_NOWBAR_ANIMATE (normal priority) instead of WM_TIMER (lowest priority).
    UINT delay_ms = static_cast<UINT>(target_interval - elapsed_ms + 1);
    if (delay_ms < 1) delay_ms = 1;
    if (delay_ms > max_delay) delay_ms = max_delay;

    CreateTimerQueueTimer(&m_anim_tp_timer, nullptr,
        anim_tp_callback, reinterpret_cast<PVOID>(m_hwnd),
        delay_ms, 0, WT_EXECUTEINTIMERTHREAD);
    m_animation_timer_active = true;
    m_animation_requested = true;
  }
}

// Helper to convert SVG coordinates (viewBox 0 -960 960 960) to normalized 0-24
// space SVG Y is inverted: -960 = top (0 in our space), -160 = bottom (20 in
// our space)
static Gdiplus::PointF svgToNorm(float x, float y) {
  // viewBox: 0 -960 960 960 means x: 0-960, y: -960 to 0
  // Convert to 0-24 range
  float nx = x / 40.0f;         // 960/24 = 40
  float ny = (y + 960) / 40.0f; // shift y by 960 then scale
  return Gdiplus::PointF(nx, ny);
}

// Draw shuffle icon from Material Design SVG
// Path: M560-160v-80h104L537-367l57-57 126 126v-102h80v240H560Zm-344 0-56-56
// 504-504H560v-80h240v240h-80v-104L216-160Zm151-377L160-744l56-56 207 207-56
// 56Z
void ControlPanelCore::draw_shuffle_icon(Gdiplus::Graphics &g, const RECT &rect,
                                         const Gdiplus::Color &color) {
  float iconSize = static_cast<float>(std::min(rect.right - rect.left,
                                               rect.bottom - rect.top)) *
                   1.0f; // Full size
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
  // Part 1: Bottom-right arrow (M560-160v-80h104L537-367l57-57 126
  // 126v-102h80v240H560Z)
  path.StartFigure();
  path.AddLine(svgToNorm(560, -160), svgToNorm(560, -240)); // v-80
  path.AddLine(svgToNorm(560, -240), svgToNorm(664, -240)); // h104
  path.AddLine(svgToNorm(664, -240), svgToNorm(537, -367)); // L537-367
  path.AddLine(svgToNorm(537, -367), svgToNorm(594, -424)); // l57-57
  path.AddLine(svgToNorm(594, -424), svgToNorm(720, -298)); // 126 126
  path.AddLine(svgToNorm(720, -298), svgToNorm(720, -400)); // v-102
  path.AddLine(svgToNorm(720, -400), svgToNorm(800, -400)); // h80
  path.AddLine(svgToNorm(800, -400), svgToNorm(800, -160)); // v240
  path.AddLine(svgToNorm(800, -160), svgToNorm(560, -160)); // H560
  path.CloseFigure();

  // Part 2: Top-right arrow (m-344 0-56-56
  // 504-504H560v-80h240v240h-80v-104L216-160Z) Starting from 560-344=216,
  // -160+0=-160
  path.StartFigure();
  path.AddLine(svgToNorm(216, -160), svgToNorm(160, -216)); // -56-56
  path.AddLine(svgToNorm(160, -216), svgToNorm(664, -720)); // 504-504
  path.AddLine(svgToNorm(664, -720), svgToNorm(560, -720)); // H560
  path.AddLine(svgToNorm(560, -720), svgToNorm(560, -800)); // v-80
  path.AddLine(svgToNorm(560, -800), svgToNorm(800, -800)); // h240
  path.AddLine(svgToNorm(800, -800), svgToNorm(800, -560)); // v240
  path.AddLine(svgToNorm(800, -560), svgToNorm(720, -560)); // h-80
  path.AddLine(svgToNorm(720, -560), svgToNorm(720, -664)); // v-104
  path.AddLine(svgToNorm(720, -664), svgToNorm(216, -160)); // L216-160
  path.CloseFigure();

  // Part 3: Bottom-left small piece (m151-377L160-744l56-56 207 207-56 56Z)
  // Starting from 216+151=367, -160-377=-537
  path.StartFigure();
  path.AddLine(svgToNorm(367, -537), svgToNorm(160, -744)); // L160-744
  path.AddLine(svgToNorm(160, -744), svgToNorm(216, -800)); // l56-56
  path.AddLine(svgToNorm(216, -800), svgToNorm(423, -593)); // 207 207
  path.AddLine(svgToNorm(423, -593), svgToNorm(367, -537)); // -56 56
  path.CloseFigure();

  g.FillPath(&brush, &path);
  g.SetTransform(&oldMatrix);
}

// Draw repeat icon from Material Design SVG
// Path: M280-80 120-240l160-160 56 58-62 62h406v-160h80v240H274l62 62-56
// 58Zm-80-440v-240h486l-62-62 56-58 160 160-160 160-56-58 62-62H280v160h-80Z
void ControlPanelCore::draw_repeat_icon(Gdiplus::Graphics &g, const RECT &rect,
                                        const Gdiplus::Color &color,
                                        bool repeat_one) {
  float iconSize = static_cast<float>(std::min(rect.right - rect.left,
                                               rect.bottom - rect.top)) *
                   1.0f; // Full size
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

  // Part 1: Bottom arrow (M280-80 120-240l160-160 56 58-62
  // 62h406v-160h80v240H274l62 62-56 58Z)
  path.StartFigure();
  path.AddLine(svgToNorm(280, -80),
               svgToNorm(120, -240)); // 120-240 (relative: -160, -160)
  path.AddLine(svgToNorm(120, -240), svgToNorm(280, -400)); // l160-160
  path.AddLine(svgToNorm(280, -400), svgToNorm(336, -342)); // 56 58
  path.AddLine(svgToNorm(336, -342), svgToNorm(274, -280)); // -62 62
  path.AddLine(svgToNorm(274, -280), svgToNorm(680, -280)); // h406
  path.AddLine(svgToNorm(680, -280), svgToNorm(680, -440)); // v-160
  path.AddLine(svgToNorm(680, -440), svgToNorm(760, -440)); // h80
  path.AddLine(svgToNorm(760, -440), svgToNorm(760, -200)); // v240
  path.AddLine(svgToNorm(760, -200), svgToNorm(274, -200)); // H274
  path.AddLine(svgToNorm(274, -200), svgToNorm(336, -138)); // l62 62
  path.AddLine(svgToNorm(336, -138), svgToNorm(280, -80));  // -56 58
  path.CloseFigure();

  // Part 2: Top arrow (m-80-440v-240h486l-62-62 56-58 160 160-160 160-56-58
  // 62-62H280v160h-80Z) Starting from 280-80=200, -80-440=-520
  path.StartFigure();
  path.AddLine(svgToNorm(200, -520), svgToNorm(200, -760)); // v-240
  path.AddLine(svgToNorm(200, -760), svgToNorm(686, -760)); // h486
  path.AddLine(svgToNorm(686, -760), svgToNorm(624, -822)); // l-62-62
  path.AddLine(svgToNorm(624, -822), svgToNorm(680, -880)); // 56-58
  path.AddLine(svgToNorm(680, -880), svgToNorm(840, -720)); // 160 160
  path.AddLine(svgToNorm(840, -720), svgToNorm(680, -560)); // -160 160
  path.AddLine(svgToNorm(680, -560), svgToNorm(624, -618)); // -56-58
  path.AddLine(svgToNorm(624, -618), svgToNorm(686, -680)); // 62-62
  path.AddLine(svgToNorm(686, -680), svgToNorm(280, -680)); // H280
  path.AddLine(svgToNorm(280, -680), svgToNorm(280, -520)); // v160
  path.AddLine(svgToNorm(280, -520), svgToNorm(200, -520)); // h-80
  path.CloseFigure();

  g.FillPath(&brush, &path);

  // Draw "1" in center if repeat_one
  // SVG: M460-360v-180h-60v-60h120v240h-60Z
  if (repeat_one) {
    Gdiplus::GraphicsPath onePath;
    onePath.StartFigure();
    onePath.AddLine(svgToNorm(460, -360), svgToNorm(460, -540)); // v-180
    onePath.AddLine(svgToNorm(460, -540), svgToNorm(400, -540)); // h-60
    onePath.AddLine(svgToNorm(400, -540), svgToNorm(400, -600)); // v-60
    onePath.AddLine(svgToNorm(400, -600), svgToNorm(520, -600)); // h120
    onePath.AddLine(svgToNorm(520, -600), svgToNorm(520, -360)); // v240
    onePath.AddLine(svgToNorm(520, -360), svgToNorm(460, -360)); // h-60
    onePath.CloseFigure();
    g.FillPath(&brush, &onePath);
  }

  g.SetTransform(&oldMatrix);
}

// Draw previous track icon from Material Design SVG
// Path: M220-240v-480h80v480h-80Zm520 0L380-480l360-240v480Zm-80-240Zm0
// 90v-180l-136 90 136 90Z
void ControlPanelCore::draw_prev_icon(Gdiplus::Graphics &g, const RECT &rect,
                                      const Gdiplus::Color &color) {
  float iconSize = static_cast<float>(std::min(rect.right - rect.left,
                                               rect.bottom - rect.top)) *
                   1.0f; // Full size
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

  // Triangle: m520 0L380-480l360-240v480Z -> starting at 220+520=740,
  // -240+0=-240
  path.StartFigure();
  path.AddLine(svgToNorm(740, -240), svgToNorm(380, -480));
  path.AddLine(svgToNorm(380, -480), svgToNorm(740, -720));
  path.AddLine(svgToNorm(740, -720), svgToNorm(740, -240));
  path.CloseFigure();

  g.FillPath(&brush, &path);
  g.SetTransform(&oldMatrix);
}

// Draw next track icon from Material Design SVG
// Path: M660-240v-480h80v480h-80Zm-440 0v-480l360 240-360 240Zm80-240Zm0 90
// 136-90-136-90v180Z
void ControlPanelCore::draw_next_icon(Gdiplus::Graphics &g, const RECT &rect,
                                      const Gdiplus::Color &color) {
  float iconSize = static_cast<float>(std::min(rect.right - rect.left,
                                               rect.bottom - rect.top)) *
                   1.0f; // Full size
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
void ControlPanelCore::draw_heart_icon(Gdiplus::Graphics &g, const RECT &rect,
                                       const Gdiplus::Color &color) {
  float iconSize = static_cast<float>(std::min(rect.right - rect.left,
                                               rect.bottom - rect.top)) *
                   1.0f;
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
  path.AddBezier(svgToNorm(bottomX, bottomY), // Bottom point
                 svgToNorm(200, -300),        // Control 1
                 svgToNorm(80, -500),         // Control 2
                 svgToNorm(80, leftLobeY)     // Left side
  );
  path.AddBezier(svgToNorm(80, leftLobeY),
                 svgToNorm(80, -780),       // Top left control
                 svgToNorm(200, -854),      // Top control
                 svgToNorm(leftLobeX, -854) // Top of left lobe
  );
  path.AddBezier(svgToNorm(leftLobeX, -854),
                 svgToNorm(400, -854),       // Control toward center
                 svgToNorm(480, -800),       // Center top control
                 svgToNorm(topIndentX, -720) // Top indent
  );
  // Right side (mirror)
  path.AddBezier(svgToNorm(topIndentX, -720), svgToNorm(480, -800),
                 svgToNorm(560, -854),
                 svgToNorm(rightLobeX, -854) // Top of right lobe
  );
  path.AddBezier(svgToNorm(rightLobeX, -854), svgToNorm(760, -854),
                 svgToNorm(880, -780), svgToNorm(880, rightLobeY) // Right side
  );
  path.AddBezier(svgToNorm(880, rightLobeY), svgToNorm(880, -500),
                 svgToNorm(760, -300),
                 svgToNorm(bottomX, bottomY) // Back to bottom
  );
  path.CloseFigure();

  g.FillPath(&brush, &path);
  g.SetTransform(&oldMatrix);
}

void ControlPanelCore::draw_star_filled_icon(Gdiplus::Graphics& g, const RECT& rect,
                                              const Gdiplus::Color& color) {
  float iconSize = static_cast<float>(std::min(rect.right - rect.left,
                                               rect.bottom - rect.top));
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

  // Outer star path: M233-120 l65-281 L80-590 l288-25 112-265 112 265 288 25 -218 189 65 281 -247-149 -247 149Z
  path.StartFigure();
  path.AddLine(svgToNorm(233, -120), svgToNorm(298, -401));   // l65-281
  path.AddLine(svgToNorm(298, -401), svgToNorm(80, -590));    // L80-590
  path.AddLine(svgToNorm(80, -590), svgToNorm(368, -615));    // l288-25
  path.AddLine(svgToNorm(368, -615), svgToNorm(480, -880));   // l112-265
  path.AddLine(svgToNorm(480, -880), svgToNorm(592, -615));   // l112 265
  path.AddLine(svgToNorm(592, -615), svgToNorm(880, -590));   // l288 25
  path.AddLine(svgToNorm(880, -590), svgToNorm(662, -401));   // l-218 189
  path.AddLine(svgToNorm(662, -401), svgToNorm(727, -120));   // l65 281
  path.AddLine(svgToNorm(727, -120), svgToNorm(480, -269));   // l-247-149
  path.AddLine(svgToNorm(480, -269), svgToNorm(233, -120));   // l-247 149
  path.CloseFigure();

  g.FillPath(&brush, &path);
  g.SetTransform(&oldMatrix);
}


// Draw play icon from Material Design SVG (without circle - just the triangle)
// Triangle: m380-300 280-180-280-180v360Z
void ControlPanelCore::draw_play_icon(Gdiplus::Graphics &g, const RECT &rect,
                                      const Gdiplus::Color &color,
                                      bool with_circle) {
  float iconSize = static_cast<float>(std::min(rect.right - rect.left,
                                               rect.bottom - rect.top)) *
                   1.0f; // Full size
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
  path.AddLine(svgToNorm(380, -300), svgToNorm(660, -480)); // right point
  path.AddLine(svgToNorm(660, -480), svgToNorm(380, -660)); // top
  path.AddLine(svgToNorm(380, -660), svgToNorm(380, -300)); // back down
  path.CloseFigure();

  g.FillPath(&brush, &path);
  g.SetTransform(&oldMatrix);
}

// Draw pause icon from Material Design SVG (without circle - just the bars)
// Bars: M360-320h80v-320h-80v320Zm160 0h80v-320h-80v320Z
void ControlPanelCore::draw_pause_icon(Gdiplus::Graphics &g, const RECT &rect,
                                       const Gdiplus::Color &color,
                                       bool with_circle) {
  float iconSize = static_cast<float>(std::min(rect.right - rect.left,
                                               rect.bottom - rect.top)) *
                   1.0f; // Full size
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
// Path: M80-520v-80h144L52-772l56-56 172 172v-144h80v280H80Zm80 360q-33
// 0-56.5-23.5T80-240v-200h80v200h320v80H160Zm640-280v-280H440v-80h360q33
// 0 56.5 23.5T880-720v280h-80ZM560-160v-200h320v200H560Z
void ControlPanelCore::draw_miniplayer_icon(Gdiplus::Graphics &g,
                                            const RECT &rect,
                                            const Gdiplus::Color &color) {
  float iconSize = static_cast<float>(std::min(rect.right - rect.left,
                                               rect.bottom - rect.top)) *
                   0.9f; // 90% size
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

  // Bottom-left outline: m80 360 -> starting at 80+80=160, -520+360=-160 (but
  // using absolute from SVG) M160-160 q-33
  // 0-56.5-23.5T80-240v-200h80v200h320v80H160Z Simplified: rect at bottom-left
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

// Draw volume icon: filled speaker with circular sound waves on a 16-unit
// grid, matching foo_modernplaylist's tab speaker icon
// level: 0=mute, 1=low, 2=full
void ControlPanelCore::draw_volume_icon(Gdiplus::Graphics &g, int x, int y,
                                        int size, const Gdiplus::Color &color,
                                        int level) {
  float scale = static_cast<float>(size) / 16.0f;

  Gdiplus::SolidBrush brush(color);

  // Draw speaker body and cone (common to all)
  Gdiplus::PointF speaker[6];
  speaker[0] = Gdiplus::PointF(x + 1.5f * scale, y + 6 * scale);
  speaker[1] = Gdiplus::PointF(x + 3.5f * scale, y + 6 * scale);
  speaker[2] = Gdiplus::PointF(x + 7.5f * scale, y + 2 * scale);
  speaker[3] = Gdiplus::PointF(x + 7.5f * scale, y + 14 * scale);
  speaker[4] = Gdiplus::PointF(x + 3.5f * scale, y + 10 * scale);
  speaker[5] = Gdiplus::PointF(x + 1.5f * scale, y + 10 * scale);
  g.FillPolygon(&brush, speaker, 6);

  Gdiplus::Pen pen(color, 1.3f * scale);
  if (level == 0) {
    // Mute: draw X
    g.DrawLine(&pen, x + 9.5f * scale, y + 5.75f * scale, x + 14 * scale,
               y + 10.25f * scale);
    g.DrawLine(&pen, x + 14 * scale, y + 5.75f * scale, x + 9.5f * scale,
               y + 10.25f * scale);
  } else {
    // Waves are circles centered at (6.5, 8), radius 4 and 7, clipped to
    // +/-46.4 degrees (atan 1.05)
    g.DrawArc(&pen, x + 2.5f * scale, y + 4 * scale, 8 * scale, 8 * scale,
              -46.4f, 92.8f);
    if (level == 2) {
      g.DrawArc(&pen, x - 0.5f * scale, y + 1 * scale, 14 * scale,
                14 * scale, -46.4f, 92.8f);
    }
  }
}

// Draw custom button icon - 3x3 grid of circles from SVG
// Based on cbutton3_24dp.svg - uniform grid of equal-sized circles
void ControlPanelCore::draw_custom_icon(Gdiplus::Graphics &g, const RECT &rect,
                                        const Gdiplus::Color &color) {
  float iconSize = static_cast<float>(
      std::min(rect.right - rect.left, rect.bottom - rect.top));
  float cx = (rect.left + rect.right) / 2.0f;
  float cy = (rect.top + rect.bottom) / 2.0f;
  float scale = iconSize / 960.0f; // SVG viewBox is 960x960
  float offsetX =
      cx - 480.0f * scale; // Center horizontally (icon center X = 480)
  // Icon's visual center Y is at -400 (not viewBox center -480), adjust by 80
  // units
  float offsetY =
      cy - 560.0f * scale; // 560 = 960 + (-400), centers on actual icon

  Gdiplus::SolidBrush brush(color);

  // Helper lambda to draw a circle at SVG coordinates
  auto drawCircle = [&](float svgX, float svgY, float radius) {
    float x = offsetX + svgX * scale;
    float y = offsetY + (960.0f + svgY) * scale; // SVG uses negative Y
    float r = radius * scale;
    g.FillEllipse(&brush, x - r, y - r, r * 2, r * 2);
  };

  // 3x3 grid of equal-sized circles (radius 56.5 from SVG)
  const float radius = 56.5f;
  const float xPositions[] = {240.0f, 480.0f, 720.0f};
  const float yPositions[] = {-640.0f, -400.0f, -160.0f};

  for (float yPos : yPositions) {
    for (float xPos : xPositions) {
      drawCircle(xPos, yPos, radius);
    }
  }
}

// Draw link icon - chain link from link_24dp.svg
void ControlPanelCore::draw_link_icon(Gdiplus::Graphics &g, const RECT &rect,
                                       const Gdiplus::Color &color) {
  float iconSize = static_cast<float>(
      std::min(rect.right - rect.left, rect.bottom - rect.top));
  float cx = (rect.left + rect.right) / 2.0f;
  float cy = (rect.top + rect.bottom) / 2.0f;
  float scale = iconSize / 960.0f;
  float offsetX = cx - 480.0f * scale;
  float offsetY = cy - 480.0f * scale;

  Gdiplus::SolidBrush brush(color);
  Gdiplus::GraphicsPath path;
  
  // Transform SVG coords to GDI+
  auto svgPt = [&](float x, float y) -> Gdiplus::PointF {
    return Gdiplus::PointF(offsetX + x * scale, offsetY + (960.0f + y) * scale);
  };
  
  // Simplified link icon - two rounded rectangles connected by a bar
  // Left link part (from SVG path: H280, q-50 curves for rounded edges)
  float barY = -480.0f;  // Vertical center
  float barH = 80.0f;    // Bar height
  float endW = 160.0f;   // End cap width
  float barMid = 160.0f; // Middle bar width per side
  
  // Draw left rounded end
  path.AddArc(offsetX + 80 * scale, offsetY + (960 - 520) * scale, 
              160 * scale, 160 * scale, 90, 180);
  // Top bar to center
  path.AddLine(svgPt(160, -560), svgPt(320, -560));
  // Gap in middle
  path.StartFigure();
  // Continue from other side of gap
  path.AddLine(svgPt(640, -560), svgPt(800, -560));
  // Right rounded end
  path.AddArc(offsetX + 720 * scale, offsetY + (960 - 520) * scale,
              160 * scale, 160 * scale, 270, 180);
  // Bottom bar back
  path.AddLine(svgPt(800, -400), svgPt(640, -400));
  path.StartFigure();
  path.AddLine(svgPt(320, -400), svgPt(160, -400));
  
  // Draw middle connecting bar
  g.FillRectangle(&brush, offsetX + 320 * scale, offsetY + (960 - 520) * scale,
                  320 * scale, 80 * scale);
  
  Gdiplus::Pen pen(color, 80.0f * scale);
  pen.SetStartCap(Gdiplus::LineCapRound);
  pen.SetEndCap(Gdiplus::LineCapRound);
  g.DrawPath(&pen, &path);
}

// Draw circle icon - circle outline from circle_24dp.svg
void ControlPanelCore::draw_circle_icon(Gdiplus::Graphics &g, const RECT &rect,
                                         const Gdiplus::Color &color) {
  float iconSize = static_cast<float>(
      std::min(rect.right - rect.left, rect.bottom - rect.top));
  float cx = (rect.left + rect.right) / 2.0f;
  float cy = (rect.top + rect.bottom) / 2.0f;
  
  // Draw circle outline - 80% of icon size for the outer circle
  float outerRadius = iconSize * 0.42f;
  float strokeWidth = iconSize * 0.10f;  // 10% stroke width
  
  Gdiplus::Pen pen(color, strokeWidth);
  g.DrawEllipse(&pen, cx - outerRadius, cy - outerRadius, 
                outerRadius * 2, outerRadius * 2);
}

// Draw square icon - square outline from square_24dp.svg
void ControlPanelCore::draw_square_icon(Gdiplus::Graphics &g, const RECT &rect,
                                         const Gdiplus::Color &color) {
  float iconSize = static_cast<float>(
      std::min(rect.right - rect.left, rect.bottom - rect.top));
  float cx = (rect.left + rect.right) / 2.0f;
  float cy = (rect.top + rect.bottom) / 2.0f;
  
  // Draw square outline - 80% of icon size
  float halfSize = iconSize * 0.38f;
  float strokeWidth = iconSize * 0.10f;  // 10% stroke width
  float cornerRadius = iconSize * 0.08f;  // Slight corner rounding
  
  Gdiplus::Pen pen(color, strokeWidth);
  Gdiplus::GraphicsPath path;
  
  // Create rounded rectangle
  float x = cx - halfSize;
  float y = cy - halfSize;
  float w = halfSize * 2;
  float h = halfSize * 2;
  float r = cornerRadius;
  
  path.AddArc(x, y, r * 2, r * 2, 180, 90);
  path.AddArc(x + w - r * 2, y, r * 2, r * 2, 270, 90);
  path.AddArc(x + w - r * 2, y + h - r * 2, r * 2, r * 2, 0, 90);
  path.AddArc(x, y + h - r * 2, r * 2, r * 2, 90, 90);
  path.CloseFigure();
  
  g.DrawPath(&pen, &path);
}

// Draw numbered square icon - square outline with a number in the center
void ControlPanelCore::draw_numbered_square_icon(Gdiplus::Graphics &g, const RECT &rect,
                                                  const Gdiplus::Color &color, int number) {
  float iconSize = static_cast<float>(
      std::min(rect.right - rect.left, rect.bottom - rect.top));
  float cx = (rect.left + rect.right) / 2.0f;
  float cy = (rect.top + rect.bottom) / 2.0f;
  
  // Draw square outline - 80% of icon size
  float halfSize = iconSize * 0.42f;
  float strokeWidth = iconSize * 0.08f;  // 8% stroke width
  float cornerRadius = iconSize * 0.10f;  // Slight corner rounding
  
  Gdiplus::Pen pen(color, strokeWidth);
  Gdiplus::GraphicsPath path;
  
  // Create rounded rectangle
  float x = cx - halfSize;
  float y = cy - halfSize;
  float w = halfSize * 2;
  float h = halfSize * 2;
  float r = cornerRadius;
  
  path.AddArc(x, y, r * 2, r * 2, 180, 90);
  path.AddArc(x + w - r * 2, y, r * 2, r * 2, 270, 90);
  path.AddArc(x + w - r * 2, y + h - r * 2, r * 2, r * 2, 0, 90);
  path.AddArc(x, y + h - r * 2, r * 2, r * 2, 90, 90);
  path.CloseFigure();
  
  g.DrawPath(&pen, &path);
  
  // Draw number in center
  wchar_t numStr[4];
  swprintf_s(numStr, L"%d", number);
  
  Gdiplus::FontFamily fontFamily(L"Segoe UI");
  float fontSize = iconSize * 0.45f;  // Font size relative to icon size
  Gdiplus::Font font(&fontFamily, fontSize, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
  
  Gdiplus::SolidBrush brush(color);
  Gdiplus::StringFormat sf;
  sf.SetAlignment(Gdiplus::StringAlignmentCenter);
  sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
  
  Gdiplus::RectF textRect(x, y, w, h);
  g.DrawString(numStr, -1, &font, textRect, &sf, &brush);
}

// Draw alternate play icon - outline style from play_arrow_24dp.svg
// Path: M320-200v-560l440 280-440 280Zm80-280Zm0 134 210-134-210-134v268Z
void ControlPanelCore::draw_alternate_play_icon(Gdiplus::Graphics &g, const RECT &rect,
                                                 const Gdiplus::Color &color) {
  float iconSize = static_cast<float>(std::min(rect.right - rect.left,
                                               rect.bottom - rect.top));
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

  // Outer triangle (clockwise)
  path.StartFigure();
  path.AddLine(svgToNorm(320, -200), svgToNorm(320, -760));  // v-560
  path.AddLine(svgToNorm(320, -760), svgToNorm(760, -480));  // l440 280
  path.AddLine(svgToNorm(760, -480), svgToNorm(320, -200));  // -440 280
  path.CloseFigure();

  // Inner triangle cutout (counter-clockwise for hole)
  path.StartFigure();
  path.AddLine(svgToNorm(400, -346), svgToNorm(400, -614));  // Inner top
  path.AddLine(svgToNorm(400, -614), svgToNorm(610, -480));  // To right
  path.AddLine(svgToNorm(610, -480), svgToNorm(400, -346));  // Back down
  path.CloseFigure();

  path.SetFillMode(Gdiplus::FillModeWinding);
  g.FillPath(&brush, &path);
  g.SetTransform(&oldMatrix);
}

// Draw alternate pause icon - outline style from pause_24dp.svg
// Two outlined rectangles
void ControlPanelCore::draw_alternate_pause_icon(Gdiplus::Graphics &g, const RECT &rect,
                                                  const Gdiplus::Color &color) {
  float iconSize = static_cast<float>(std::min(rect.right - rect.left,
                                               rect.bottom - rect.top));
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
  
  // Left bar - outer rect then inner hole
  // Outer: 200-200 to 440-760
  path.StartFigure();
  path.AddLine(svgToNorm(200, -200), svgToNorm(200, -760));
  path.AddLine(svgToNorm(200, -760), svgToNorm(440, -760));
  path.AddLine(svgToNorm(440, -760), svgToNorm(440, -200));
  path.AddLine(svgToNorm(440, -200), svgToNorm(200, -200));
  path.CloseFigure();
  
  // Left bar inner hole
  path.StartFigure();
  path.AddLine(svgToNorm(280, -280), svgToNorm(360, -280));
  path.AddLine(svgToNorm(360, -280), svgToNorm(360, -680));
  path.AddLine(svgToNorm(360, -680), svgToNorm(280, -680));
  path.AddLine(svgToNorm(280, -680), svgToNorm(280, -280));
  path.CloseFigure();

  // Right bar - outer rect then inner hole
  // Outer: 520-200 to 760-760
  path.StartFigure();
  path.AddLine(svgToNorm(520, -200), svgToNorm(520, -760));
  path.AddLine(svgToNorm(520, -760), svgToNorm(760, -760));
  path.AddLine(svgToNorm(760, -760), svgToNorm(760, -200));
  path.AddLine(svgToNorm(760, -200), svgToNorm(520, -200));
  path.CloseFigure();

  // Right bar inner hole
  path.StartFigure();
  path.AddLine(svgToNorm(600, -280), svgToNorm(680, -280));
  path.AddLine(svgToNorm(680, -280), svgToNorm(680, -680));
  path.AddLine(svgToNorm(680, -680), svgToNorm(600, -680));
  path.AddLine(svgToNorm(600, -680), svgToNorm(600, -280));
  path.CloseFigure();

  path.SetFillMode(Gdiplus::FillModeWinding);
  g.FillPath(&brush, &path);
  g.SetTransform(&oldMatrix);
}

// Draw Style 3 play icon - solid filled triangle (no inner cutout)
void ControlPanelCore::draw_style3_play_icon(Gdiplus::Graphics &g, const RECT &rect,
                                              const Gdiplus::Color &color) {
  float iconSize = static_cast<float>(std::min(rect.right - rect.left,
                                               rect.bottom - rect.top));
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

  // Solid triangle (no inner cutout)
  path.StartFigure();
  path.AddLine(svgToNorm(320, -200), svgToNorm(320, -760));
  path.AddLine(svgToNorm(320, -760), svgToNorm(760, -480));
  path.AddLine(svgToNorm(760, -480), svgToNorm(320, -200));
  path.CloseFigure();

  path.SetFillMode(Gdiplus::FillModeWinding);
  g.FillPath(&brush, &path);
  g.SetTransform(&oldMatrix);
}

// Draw Style 3 pause icon - solid filled bars from Material Icons Regular U+E034
void ControlPanelCore::draw_style3_pause_icon(Gdiplus::Graphics &g, const RECT &rect,
                                               const Gdiplus::Color &color) {
  float iconSize = static_cast<float>(std::min(rect.right - rect.left,
                                               rect.bottom - rect.top));
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

  // Left bar - solid filled
  path.StartFigure();
  path.AddLine(svgToNorm(235, -182), svgToNorm(235, -778));
  path.AddLine(svgToNorm(235, -778), svgToNorm(405, -778));
  path.AddLine(svgToNorm(405, -778), svgToNorm(405, -182));
  path.AddLine(svgToNorm(405, -182), svgToNorm(235, -182));
  path.CloseFigure();

  // Right bar - solid filled
  path.StartFigure();
  path.AddLine(svgToNorm(577, -182), svgToNorm(577, -778));
  path.AddLine(svgToNorm(577, -778), svgToNorm(747, -778));
  path.AddLine(svgToNorm(747, -778), svgToNorm(747, -182));
  path.AddLine(svgToNorm(747, -182), svgToNorm(577, -182));
  path.CloseFigure();

  path.SetFillMode(Gdiplus::FillModeWinding);
  g.FillPath(&brush, &path);
  g.SetTransform(&oldMatrix);
}

void ControlPanelCore::update_title_formats() {
  auto compiler = titleformat_compiler::get();
  pfc::string8 line1_fmt = get_nowbar_line1_format();
  pfc::string8 line2_fmt = get_nowbar_line2_format();
  pfc::string8 line3_fmt = get_nowbar_line3_format();

  m_titleformat_line1.release();
  m_titleformat_line2.release();
  m_titleformat_line3.release();

  // Compile format strings (compile_safe_ex returns false on error but populates with fallback)
  compiler->compile_safe_ex(m_titleformat_line1, line1_fmt.c_str());
  compiler->compile_safe_ex(m_titleformat_line2, line2_fmt.c_str());
  if (line3_fmt.get_length() > 0)
    compiler->compile_safe_ex(m_titleformat_line3, line3_fmt.c_str());

  // Detect dynamic fields (%playback_time*%, %bitrate*%) so on_playback_time_changed
  // and on_dynamic_info_changed know whether to re-evaluate dynamically.
  // Case-insensitive substring match catches %playback_time%, %playback_time_seconds%,
  // %playback_time_remaining%, %playback_time_remaining_seconds%,
  // %bitrate%, %bitrate_dynamic%, %bitrate_peak%, %BITRATE%, etc.
  auto has_dynamic_fields = [](const char* s) -> bool {
    if (!s || !*s) return false;
    std::string lower(s);
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower.find("%playback_time") != std::string::npos ||
           lower.find("%bitrate") != std::string::npos;
  };
  m_lines_have_dynamic_time = has_dynamic_fields(line1_fmt.c_str()) ||
                              has_dynamic_fields(line2_fmt.c_str()) ||
                              has_dynamic_fields(line3_fmt.c_str());

  // Re-evaluate with current track
  evaluate_title_formats();
}

namespace {

// Titleformat hook that substitutes fixed-width worst-case values for dynamic
// fields (%bitrate*%, %playback_time*%) when measuring layout width, preventing
// the scaled seekbar/spectrum from stretching and shrinking as VBR bitrates
// fluctuate across digit boundaries (e.g. 900-1000 kbps).
class layout_width_titleformat_hook : public titleformat_hook {
public:
  explicit layout_width_titleformat_hook(t_int64 nominal_bitrate)
      : m_max_bitrate(std::max<t_int64>(nominal_bitrate, 9999)) {}

  bool process_field(titleformat_text_out* p_out, const char* p_name,
                     t_size p_name_length, bool& p_found_flag) override {
    if (pfc::stricmp_ascii_ex(p_name, p_name_length, "bitrate", SIZE_MAX) == 0 ||
        pfc::stricmp_ascii_ex(p_name, p_name_length, "bitrate_dynamic", SIZE_MAX) == 0 ||
        pfc::stricmp_ascii_ex(p_name, p_name_length, "bitrate_peak", SIZE_MAX) == 0) {
      p_out->write_int(titleformat_inputtypes::meta, m_max_bitrate);
      p_found_flag = true;
      return true;
    }
    if (pfc::stricmp_ascii_ex(p_name, p_name_length, "playback_time", SIZE_MAX) == 0 ||
        pfc::stricmp_ascii_ex(p_name, p_name_length, "playback_time_remaining", SIZE_MAX) == 0) {
      p_out->write(titleformat_inputtypes::meta, "00:00");
      p_found_flag = true;
      return true;
    }
    if (pfc::stricmp_ascii_ex(p_name, p_name_length, "playback_time_seconds", SIZE_MAX) == 0 ||
        pfc::stricmp_ascii_ex(p_name, p_name_length, "playback_time_remaining_seconds", SIZE_MAX) == 0) {
      p_out->write(titleformat_inputtypes::meta, "0000");
      p_found_flag = true;
      return true;
    }
    p_found_flag = false;
    return false;
  }

  bool process_function(titleformat_text_out*, const char*, t_size,
                        titleformat_hook_function_params*, bool&) override {
    return false;
  }

private:
  t_int64 m_max_bitrate;
};

} // namespace

void ControlPanelCore::evaluate_title_formats() {
  metadb_handle_ptr track = get_display_track();
  if (!track.is_valid()) {
    m_formatted_line1 = "";
    m_formatted_line2 = "";
    m_formatted_line3 = "";
    m_layout_line1 = "";
    m_layout_line2 = "";
    m_layout_line3 = "";
    return;
  }

  // Use playback_format_title when playing the target track - it merges dynamic stream
  // metadata (internet radio artist/title) with static file metadata.
  // Fall back to metadb_handle::format_title when not actively playing this track.
  auto pc = playback_control::get();
  bool is_playing = pc->is_playing() || pc->is_paused();
  bool use_playback = false;
  if (is_playing) {
    metadb_handle_ptr now_playing_track;
    if (pc->get_now_playing(now_playing_track) && now_playing_track == track) {
      use_playback = true;
    }
  }

  if (m_titleformat_line1.is_valid()) {
    if (use_playback) {
      pc->playback_format_title(nullptr, m_formatted_line1,
                                m_titleformat_line1, nullptr,
                                playback_control::display_level_all);
    } else {
      track->format_title(nullptr, m_formatted_line1,
                          m_titleformat_line1, nullptr);
    }
  } else {
    m_formatted_line1 = "";
  }

  if (m_titleformat_line2.is_valid()) {
    if (use_playback) {
      pc->playback_format_title(nullptr, m_formatted_line2,
                                m_titleformat_line2, nullptr,
                                playback_control::display_level_all);
    } else {
      track->format_title(nullptr, m_formatted_line2,
                          m_titleformat_line2, nullptr);
    }
  } else {
    m_formatted_line2 = "";
  }

  if (m_titleformat_line3.is_valid()) {
    if (use_playback) {
      pc->playback_format_title(nullptr, m_formatted_line3,
                                m_titleformat_line3, nullptr,
                                playback_control::display_level_all);
    } else {
      track->format_title(nullptr, m_formatted_line3,
                          m_titleformat_line3, nullptr);
    }
  } else {
    m_formatted_line3 = "";
  }

  // Fallback check for streams discovered via foo_artwork (e.g. ?azuracast_api / ?radioreg_api)
  const bool missing_native_pair =
      (m_formatted_line1.is_empty() || m_formatted_line1 == "?" ||
       m_formatted_line1.find_first("http://") == 0 || m_formatted_line1.find_first("https://") == 0) &&
      (m_formatted_line2.is_empty() || m_formatted_line2 == "?");
  if (use_playback && missing_native_pair) {
    static service_ptr_t<titleformat_object> tf_fa_title, tf_fa_artist;
    if (!tf_fa_title.is_valid()) {
      titleformat_compiler::get()->compile_safe(tf_fa_title, "%foo_artwork_title%");
    }
    if (!tf_fa_artist.is_valid()) {
      titleformat_compiler::get()->compile_safe(tf_fa_artist, "%foo_artwork_artist%");
    }
    pfc::string8 fa_title, fa_artist;
    pc->playback_format_title(nullptr, fa_title, tf_fa_title, nullptr, playback_control::display_level_all);
    pc->playback_format_title(nullptr, fa_artist, tf_fa_artist, nullptr, playback_control::display_level_all);

    // Apply discovered metadata as a pair, only when native display fields are absent.
    if (!fa_title.is_empty() && fa_title != "?" && !fa_artist.is_empty() && fa_artist != "?") {
      m_formatted_line1 = fa_title;
      m_formatted_line2 = fa_artist;
    }
  }

  // Compute stabilized layout strings: when dynamic fields (%bitrate*%, %playback_time*%)
  // are present, evaluate with a hook that substitutes fixed worst-case values (e.g. 4-digit
  // bitrate) so update_layout() reserves a constant width while draw_track_info() renders
  // the live m_formatted_line* values.
  if (m_lines_have_dynamic_time) {
    t_int64 nominal_bitrate = 0;
    metadb_info_container::ptr info_ref = track->get_info_ref();
    if (info_ref.is_valid()) {
      nominal_bitrate = info_ref->info().info_get_bitrate();
    }
    layout_width_titleformat_hook hook(nominal_bitrate);

    if (m_titleformat_line1.is_valid()) {
      if (use_playback) {
        pc->playback_format_title(&hook, m_layout_line1,
                                  m_titleformat_line1, nullptr,
                                  playback_control::display_level_all);
      } else {
        track->format_title(&hook, m_layout_line1,
                            m_titleformat_line1, nullptr);
      }
    } else {
      m_layout_line1 = "";
    }

    if (m_titleformat_line2.is_valid()) {
      if (use_playback) {
        pc->playback_format_title(&hook, m_layout_line2,
                                  m_titleformat_line2, nullptr,
                                  playback_control::display_level_all);
      } else {
        track->format_title(&hook, m_layout_line2,
                            m_titleformat_line2, nullptr);
      }
    } else {
      m_layout_line2 = "";
    }

    if (m_titleformat_line3.is_valid()) {
      if (use_playback) {
        pc->playback_format_title(&hook, m_layout_line3,
                                  m_titleformat_line3, nullptr,
                                  playback_control::display_level_all);
      } else {
        track->format_title(&hook, m_layout_line3,
                            m_titleformat_line3, nullptr);
      }
    } else {
      m_layout_line3 = "";
    }

    if (use_playback && missing_native_pair) {
      m_layout_line1 = m_formatted_line1;
      m_layout_line2 = m_formatted_line2;
    }
  } else {
    m_layout_line1 = m_formatted_line1;
    m_layout_line2 = m_formatted_line2;
    m_layout_line3 = m_formatted_line3;
  }
}

// Draw Super button icon - 3x3 grid of dots (from sbutton_24dp.svg)
void ControlPanelCore::draw_super_icon(Gdiplus::Graphics &g, const RECT &rect,
                                       const Gdiplus::Color &color) {
  float w = static_cast<float>(rect.right - rect.left);
  float h = static_cast<float>(rect.bottom - rect.top);
  float x = static_cast<float>(rect.left);
  float y = static_cast<float>(rect.top);

  // The SVG shows a 3x3 grid of circles in a 960x960 viewBox
  // Circle radius is approximately 56.5 (from the path d="T240-320q33...")
  // Grid positions: 240, 480, 720 in the original viewBox
  // Normalized: 0.25, 0.5, 0.75

  Gdiplus::SolidBrush brush(color);

  // Scale factors
  float grid_positions[] = {0.25f, 0.5f, 0.75f};
  float dot_radius = w * 0.09f;  // Approximate radius relative to icon size

  // Draw 3x3 grid of circles
  for (int row = 0; row < 3; row++) {
    for (int col = 0; col < 3; col++) {
      float cx = x + w * grid_positions[col];
      float cy = y + h * grid_positions[row];
      g.FillEllipse(&brush, cx - dot_radius, cy - dot_radius, 
                    dot_radius * 2.0f, dot_radius * 2.0f);
    }
  }
}

// Draw Stop icon - square (filled or outline) based on stop_24dp.svg
// SVG path: M320-640v320-320Zm-80 400v-480h480v480H240Zm80-80h320v-320H320v320Z
void ControlPanelCore::draw_stop_icon(Gdiplus::Graphics &g, const RECT &rect,
                                       const Gdiplus::Color &color, bool filled) {
  float w = static_cast<float>(rect.right - rect.left);
  float h = static_cast<float>(rect.bottom - rect.top);
  float x = static_cast<float>(rect.left);
  float y = static_cast<float>(rect.top);
  
  // The SVG shows a square icon in a 960x960 viewBox
  // Outer square: 240, 240 to 720, 720 (normalized: 0.25 to 0.75)
  // Inner square for outline: 320, 320 to 640, 640 (normalized: 0.333 to 0.667)
  
  Gdiplus::SolidBrush brush(color);
  
  // Calculate square dimensions (centered in icon rect)
  float outer_inset = w * 0.25f;  // 25% inset from each edge
  float inner_inset = w * 0.333f; // 33% inset for inner border
  
  if (filled) {
    // Draw filled square
    g.FillRectangle(&brush, x + outer_inset, y + outer_inset, 
                    w - outer_inset * 2.0f, h - outer_inset * 2.0f);
  } else {
    // Draw square outline (outer - inner creates border)
    float border = inner_inset - outer_inset;
    
    // Top edge
    g.FillRectangle(&brush, x + outer_inset, y + outer_inset, 
                    w - outer_inset * 2.0f, border);
    // Bottom edge
    g.FillRectangle(&brush, x + outer_inset, y + h - outer_inset - border, 
                    w - outer_inset * 2.0f, border);
    // Left edge
    g.FillRectangle(&brush, x + outer_inset, y + outer_inset + border, 
                    border, h - outer_inset * 2.0f - border * 2.0f);
    // Right edge
    g.FillRectangle(&brush, x + w - outer_inset - border, y + outer_inset + border, 
                    border, h - outer_inset * 2.0f - border * 2.0f);
  }
}

// Draw "Stop after current" icon from circle_24dp.svg
// SVG viewBox: 0 -960 960 960
// Circle outline: outer from (80,80) to (880,880), inner cutout creates ring
void ControlPanelCore::draw_stop_after_current_icon(Gdiplus::Graphics &g, const RECT &rect,
                                                     const Gdiplus::Color &color) {
  float w = static_cast<float>(rect.right - rect.left);
  float h = static_cast<float>(rect.bottom - rect.top);
  float x = static_cast<float>(rect.left);
  float y = static_cast<float>(rect.top);

  g.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);

  // Circle outline - SVG outer edge at 80/960 inset, ~80px border in 960 space
  // Pen stroke is centered on the path, so inset to the midpoint of the ring
  float outer_inset = w * 0.0833f;   // 80/960
  float stroke_width = w * 0.0833f;  // ~80px border in 960 space
  float mid_inset = outer_inset + stroke_width / 2.0f;
  float ellipse_size = w - mid_inset * 2.0f;

  Gdiplus::Pen pen(color, stroke_width);
  g.DrawEllipse(&pen, x + mid_inset, y + mid_inset, ellipse_size, ellipse_size);

  // Center filled stop square
  float sq_inset = w * 0.375f;
  float sq_size = w * 0.25f;  // 0.625 - 0.375
  Gdiplus::SolidBrush brush(color);
  g.FillRectangle(&brush, x + sq_inset, y + sq_inset, sq_size, sq_size);
}

// Draw radio icon from Material Design SVG for internet radio streams
// SVG path: M160-80q-33 0-56.5-23.5T80-160v-480q0-25 13.5-45t36.5-29l506-206 26 66-330 134h468q33 0 56.5 23.5T880-640v480q0 33-23.5 56.5T800-80H160Z
// Plus sub-shapes for display panel, dial circle, etc.
void ControlPanelCore::draw_radio_icon(Gdiplus::Graphics &g, const RECT &rect,
                                       const Gdiplus::Color &color) {
  float w = static_cast<float>(rect.right - rect.left);
  float h = static_cast<float>(rect.bottom - rect.top);
  float cx = (rect.left + rect.right) / 2.0f;
  float cy = (rect.top + rect.bottom) / 2.0f;

  // Scale icon to ~50% of the artwork rect, matching music note sizing
  float iconDim = std::min(w, h) * 0.5f;
  float scale = iconDim / 24.0f;

  Gdiplus::Matrix oldMatrix;
  g.GetTransform(&oldMatrix);

  Gdiplus::Matrix matrix;
  matrix.Translate(cx - 12 * scale, cy - 12 * scale);
  matrix.Scale(scale, scale);
  g.SetTransform(&matrix);

  Gdiplus::SolidBrush brush(color);
  Gdiplus::GraphicsPath path;
  Gdiplus::SmoothingMode oldSmoothing = g.GetSmoothingMode();
  g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

  // Radio body outline (simplified from SVG path)
  // The SVG viewBox is 0 -960 960 960, converted via svgToNorm()
  // Main body: rounded rect from (80,-160) to (880,-640) with corners
  // Antenna line going from top of body up-left

  // --- Antenna line ---
  // SVG: l506-206 26 66-330 134 => a diagonal line from top of radio
  // Simplified as a thick line from roughly (662,-234) up to (336,-366)
  // which maps to the antenna segment
  {
    Gdiplus::Pen pen(color, 1.5f);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    // Antenna: from about (330,-640) going up-left to (166,-706)
    // In SVG coords: the path goes l506-206 from (130,-234)
    // Simplified: draw from (636,-234) => svgToNorm to (330,-700)
    g.DrawLine(&pen, svgToNorm(330, -700), svgToNorm(636, -234));
  }

  // --- Radio body (rounded rectangle) ---
  // Body spans from x=160 to x=800, y=-160 to y=-640
  // With rounded corners (radius ~57 from the q-33 bezier)
  {
    float bx1 = svgToNorm(160, 0).X;
    float by1 = svgToNorm(0, -640).Y;
    float bx2 = svgToNorm(800, 0).X;
    float by2 = svgToNorm(0, -160).Y;
    float bw = bx2 - bx1;
    float bh = by2 - by1;
    float radius = 57.0f / 40.0f; // Corner radius in normalized coords

    Gdiplus::GraphicsPath bodyPath;
    // Top-left corner
    bodyPath.AddArc(bx1, by1, radius * 2, radius * 2, 180, 90);
    // Top edge
    bodyPath.AddLine(bx1 + radius, by1, bx2 - radius, by1);
    // Top-right corner
    bodyPath.AddArc(bx2 - radius * 2, by1, radius * 2, radius * 2, 270, 90);
    // Right edge
    bodyPath.AddLine(bx2, by1 + radius, bx2, by2 - radius);
    // Bottom-right corner
    bodyPath.AddArc(bx2 - radius * 2, by2 - radius * 2, radius * 2, radius * 2, 0, 90);
    // Bottom edge
    bodyPath.AddLine(bx2 - radius, by2, bx1 + radius, by2);
    // Bottom-left corner
    bodyPath.AddArc(bx1, by2 - radius * 2, radius * 2, radius * 2, 90, 90);
    bodyPath.CloseFigure();

    g.FillPath(&brush, &bodyPath);
  }

  // --- Display panel (cutout drawn as darker rect) ---
  // SVG sub-path: M160-520h480v-80h80v80h80v-120H160v120Z
  // This is the top section of the radio body, y=-520 to y=-640
  // We draw the lower section in body color and a display area on top
  // Display: from (160,-640) to (800,-520) with a notch
  {
    // Draw display panel as a slightly different shade (darken/lighten)
    int r = color.GetR(), gc = color.GetG(), b = color.GetB();
    // Make it slightly darker/lighter for contrast
    Gdiplus::Color displayColor(color.GetA(),
        (BYTE)std::max(0, r - 40),
        (BYTE)std::max(0, gc - 40),
        (BYTE)std::max(0, b - 40));
    Gdiplus::SolidBrush displayBrush(displayColor);

    // Display area: x=160..640, y=-640..-520 (the main screen area)
    Gdiplus::PointF tl = svgToNorm(160, -640);
    Gdiplus::PointF br = svgToNorm(640, -520);
    float dw = br.X - tl.X;
    float dh = br.Y - tl.Y;
    float dRadius = 57.0f / 40.0f;
    // Only round top-left corner since it's in the body corner
    g.FillRectangle(&displayBrush, tl.X, tl.Y + dRadius, dw, dh - dRadius);
    // Top portion with rounded top-left corner
    Gdiplus::GraphicsPath displayTop;
    displayTop.AddArc(tl.X, tl.Y, dRadius * 2, dRadius * 2, 180, 90);
    displayTop.AddLine(tl.X + dRadius, tl.Y, tl.X + dw, tl.Y);
    displayTop.AddLine(tl.X + dw, tl.Y, tl.X + dw, tl.Y + dRadius);
    displayTop.AddLine(tl.X + dw, tl.Y + dRadius, tl.X, tl.Y + dRadius);
    displayTop.CloseFigure();
    g.FillPath(&displayBrush, &displayTop);
  }

  // --- Tuning dial (circle) ---
  // SVG: M320-200 circle centered at (320,-320) with radius 71
  // Actually the sub-path describes: center at (320, -300) radius ~71
  // q42 0 71-29t29-71q0-42-29-71t-71-29q-42 0-71 29t-29 71q0 42 29 71t71 29
  {
    Gdiplus::PointF center = svgToNorm(320, -370);
    float dialRadius = 71.0f / 40.0f;
    // Draw as a lighter/brighter circle for contrast
    int r = color.GetR(), gc = color.GetG(), b = color.GetB();
    Gdiplus::Color dialColor(color.GetA(),
        (BYTE)std::min(255, r + 50),
        (BYTE)std::min(255, gc + 50),
        (BYTE)std::min(255, b + 50));
    Gdiplus::SolidBrush dialBrush(dialColor);
    g.FillEllipse(&dialBrush,
        center.X - dialRadius, center.Y - dialRadius,
        dialRadius * 2, dialRadius * 2);
  }

  g.SetSmoothingMode(oldSmoothing);
  g.SetTransform(&oldMatrix);
}


static pfc::string8 make_waveform_key(const playable_location& loc) {
  pfc::string8 key = loc.get_path();
  t_uint32 subsong = loc.get_subsong();
  if (subsong != 0) {
    key << ":" << subsong;
  }
  return key;
}

void ControlPanelCore::start_waveform_computation() {
  cancel_waveform_computation();

  // Guard: skip if no current track or stream (track_length <= 0)
  if (!m_state.current_track.is_valid()) {
    std::lock_guard<std::mutex> lock(m_waveform_mutex);
    m_waveform_is_stream = m_state.is_playing;
    m_waveform_valid = false;
    m_waveform_peaks.clear();
    return;
  }

  double track_length = m_state.track_length;
  if (track_length <= 0 && m_state.current_track.is_valid()) {
    track_length = m_state.current_track->get_length();
  }

  if (track_length <= 0) {
    std::lock_guard<std::mutex> lock(m_waveform_mutex);
    m_waveform_is_stream = (m_state.track_length <= 0 && m_state.is_playing);
    m_waveform_valid = false;
    m_waveform_peaks.clear();
    return;
  }

  // Get the playable location from the current track
  playable_location_impl location;
  pfc::string8 track_key;
  try {
    location = m_state.current_track->get_location();
    track_key = make_waveform_key(location);
  } catch (...) {
    return;
  }

  // Don't recompute if same track is already valid
  if (m_waveform_valid && m_waveform_track_path == track_key) return;

  // Check waveform cache before spawning a decoding thread
  {
    std::vector<float> cached_peaks;
    if (lookup_waveform_cache(track_key.c_str(), cached_peaks)) {
      {
        std::lock_guard<std::mutex> lock(m_waveform_mutex);
        m_waveform_peaks = std::move(cached_peaks);
        m_waveform_valid = true;
        m_waveform_is_stream = false;
        m_waveform_track_path = track_key;
      }
      // All segments available — reveal animation will sweep across
      m_waveform_decode_count.store(WAVEFORM_SEGMENTS, std::memory_order_relaxed);
      m_waveform_reveal_active = true;
      m_waveform_animating = true;
      request_animation();
      return;
    }
  }

  m_waveform_cancel = false;
  m_waveform_computing = true;

  // Pre-size peaks so the render loop can read partially-filled data
  {
    std::lock_guard<std::mutex> lock(m_waveform_mutex);
    m_waveform_peaks.assign(WAVEFORM_SEGMENTS, 0.0f);
    m_waveform_is_stream = false;
    m_waveform_track_path = track_key;
  }

  HWND hwnd = m_hwnd;

  m_waveform_thread = std::thread([this, track_key, location, hwnd, track_length]() {
    try {
      int num_segments = WAVEFORM_SEGMENTS;
      double segment_duration = track_length / num_segments;
      std::vector<float> peaks(num_segments, 0.0f);
      std::vector<double> rms_sums(num_segments, 0.0);
      std::vector<int> rms_counts(num_segments, 0);

      // Decode audio in a tight scope so the file handle is released
      // immediately after reading. This prevents blocking tag writers
      // that need write access to the same file (e.g., CUE sheets
      // where the underlying full-track file is both played and tagged).
      {
        service_ptr_t<input_decoder> decoder;
        input_entry::g_open_for_decoding(decoder, nullptr, location.get_path(), m_waveform_abort);
        decoder->initialize(location.get_subsong(), input_flag_simpledecode, m_waveform_abort);

        audio_chunk_impl_temporary chunk;
        int current_segment = 0;
        double segment_start = 0.0;

        while (current_segment < num_segments) {
          if (m_waveform_cancel.load()) return;

          bool got_data = false;
          try {
            got_data = decoder->run(chunk, m_waveform_abort);
          } catch (...) {
            break;
          }

          if (!got_data) break;

          // Process samples in this chunk
          const audio_sample* data = chunk.get_data();
          t_size samples = chunk.get_sample_count();
          int channels = chunk.get_channel_count();
          int sample_rate = chunk.get_sample_rate();

          if (!data || samples == 0 || channels == 0 || sample_rate == 0) continue;

          double chunk_duration = (double)samples / sample_rate;
          double chunk_end_time = segment_start + chunk_duration;

          // Accumulate squared sample values for RMS per segment
          for (t_size s = 0; s < samples; s++) {
            double sample_time = segment_start + (double)s / sample_rate;
            int seg = (int)(sample_time / segment_duration);
            if (seg >= num_segments) seg = num_segments - 1;
            if (seg < 0) seg = 0;

            // Average across channels, then accumulate squared value
            float sum_ch = 0.0f;
            for (int ch = 0; ch < channels; ch++) {
              sum_ch += std::abs(data[s * channels + ch]);
            }
            float val = sum_ch / channels;
            rms_sums[seg] += (double)(val * val);
            rms_counts[seg]++;

            // Track segment progress and report with running normalization
            if (seg > current_segment) {
              // Finalize RMS for completed segments
              for (int k = current_segment; k < seg; k++) {
                if (rms_counts[k] > 0) {
                  peaks[k] = std::sqrt((float)(rms_sums[k] / rms_counts[k]));
                }
              }
              current_segment = seg;

              // Publish normalized peaks periodically
              if ((seg % 20) == 0 && hwnd && ::IsWindow(hwnd)) {
                // Running max across all decoded segments
                float running_max = 0.0f;
                for (int k = 0; k < seg; k++) {
                  if (peaks[k] > running_max) running_max = peaks[k];
                }
                if (running_max > 0.0f) {
                  std::lock_guard<std::mutex> lock(m_waveform_mutex);
                  for (int k = 0; k < seg; k++) {
                    float norm = peaks[k] / running_max;
                    m_waveform_peaks[k] = std::pow(norm, 0.65f);
                  }
                }
                m_waveform_decode_count.store(seg, std::memory_order_relaxed);
                ::InvalidateRect(hwnd, nullptr, FALSE);
              }
            }
          }

          segment_start = chunk_end_time;
        }
      }
      // decoder is now destroyed — file handle released

      // Compute RMS for each segment
      for (int i = 0; i < num_segments; i++) {
        if (rms_counts[i] > 0) {
          peaks[i] = std::sqrt((float)(rms_sums[i] / rms_counts[i]));
        }
      }

      if (m_waveform_cancel.load()) return;

      // Normalize to 0.0-1.0 with gentle perceptual scaling
      float max_peak = 0.0f;
      for (float p : peaks) {
        if (p > max_peak) max_peak = p;
      }
      if (max_peak > 0.0f) {
        for (float& p : peaks) {
          p /= max_peak;
          // Gentle power curve to slightly expand quieter segments
          p = std::pow(p, 0.65f);
        }
      }

      // Persist to disk cache (file I/O only, no shared state)
      save_waveform_entry(track_key.c_str(), peaks);

      // Store final normalized peaks and update in-memory cache
      {
        std::lock_guard<std::mutex> lock(m_waveform_mutex);
        m_waveform_cache[std::string(track_key.c_str())] = peaks;
        m_waveform_peaks = std::move(peaks);
        m_waveform_valid = true;
      }
      m_waveform_computing = false;

      // All segments decoded — main thread will pick up via decode_count
      m_waveform_decode_count.store(WAVEFORM_SEGMENTS, std::memory_order_relaxed);

      // Trigger repaint on main thread via InvalidateRect (safe cross-thread).
      if (hwnd && ::IsWindow(hwnd)) {
        ::InvalidateRect(hwnd, nullptr, FALSE);
      }
    } catch (...) {
      m_waveform_computing = false;
    }
  });
}

void ControlPanelCore::cancel_waveform_computation() {
  m_waveform_cancel = true;
  m_waveform_abort.abort();
  if (m_waveform_thread.joinable()) {
    m_waveform_thread.join();
  }
  m_waveform_computing = false;
  m_waveform_cancel = false;
  m_waveform_abort.reset();
}

// Waveform cache magic and version
static constexpr char WAVECACHE_MAGIC[4] = {'N', 'W', 'W', 'C'};
static constexpr uint32_t WAVECACHE_VERSION = 1;

static pfc::string8 get_wavecache_path() {
  pfc::string8 dir = get_config_dir_path();
  dir << "\\wavecache.db";
  return dir;
}

void ControlPanelCore::load_waveform_cache() {
  if (m_waveform_cache_loaded) return;
  m_waveform_cache_loaded = true;

  pfc::string8 cache_path = get_wavecache_path();
  pfc::stringcvt::string_wide_from_utf8 wide_path(cache_path);
  std::ifstream file(wide_path.get_ptr(), std::ios::binary);
  if (!file.is_open()) return;

  // Read and validate header
  char magic[4];
  uint32_t version = 0;
  uint32_t entry_count = 0;

  file.read(magic, 4);
  if (!file || memcmp(magic, WAVECACHE_MAGIC, 4) != 0) return;

  file.read(reinterpret_cast<char*>(&version), 4);
  if (!file || version != WAVECACHE_VERSION) return;

  file.read(reinterpret_cast<char*>(&entry_count), 4);
  if (!file) return;

  // Read entries
  for (uint32_t i = 0; i < entry_count; i++) {
    uint32_t path_len = 0;
    file.read(reinterpret_cast<char*>(&path_len), 4);
    if (!file || path_len > 4096) return;  // Sanity limit

    std::string path(path_len, '\0');
    file.read(&path[0], path_len);
    if (!file) return;

    uint32_t peak_count = 0;
    file.read(reinterpret_cast<char*>(&peak_count), 4);
    if (!file || peak_count > 10000) return;  // Sanity limit

    std::vector<float> peaks(peak_count);
    file.read(reinterpret_cast<char*>(peaks.data()), peak_count * sizeof(float));
    if (!file) return;

    m_waveform_cache[path] = std::move(peaks);
  }
}

void ControlPanelCore::save_waveform_entry(const char* path, const std::vector<float>& peaks) {
  ensure_config_dir_exists();

  pfc::string8 cache_path = get_wavecache_path();
  pfc::stringcvt::string_wide_from_utf8 wide_path(cache_path);

  // Check if file exists
  bool file_exists = false;
  {
    std::ifstream test(wide_path.get_ptr(), std::ios::binary);
    file_exists = test.is_open();
  }

  std::fstream file(wide_path.get_ptr(),
                    std::ios::binary | std::ios::in | std::ios::out |
                    (file_exists ? std::ios::openmode(0) : std::ios::trunc));

  if (!file.is_open()) {
    // File doesn't exist yet or can't open with in|out — create fresh
    file.open(wide_path.get_ptr(), std::ios::binary | std::ios::out | std::ios::trunc);
    if (!file.is_open()) return;
    file_exists = false;
  }

  uint32_t new_count = 1;

  if (!file_exists) {
    // Write header
    file.write(WAVECACHE_MAGIC, 4);
    file.write(reinterpret_cast<const char*>(&WAVECACHE_VERSION), 4);
    file.write(reinterpret_cast<const char*>(&new_count), 4);
  } else {
    // Read current entry count
    uint32_t current_count = 0;
    file.seekg(8, std::ios::beg);
    file.read(reinterpret_cast<char*>(&current_count), 4);
    new_count = current_count + 1;

    // Update entry count in header
    file.seekp(8, std::ios::beg);
    file.write(reinterpret_cast<const char*>(&new_count), 4);

    // Seek to end for appending
    file.seekp(0, std::ios::end);
  }

  // Write entry
  uint32_t path_len = static_cast<uint32_t>(strlen(path));
  file.write(reinterpret_cast<const char*>(&path_len), 4);
  file.write(path, path_len);

  uint32_t peak_count = static_cast<uint32_t>(peaks.size());
  file.write(reinterpret_cast<const char*>(&peak_count), 4);
  file.write(reinterpret_cast<const char*>(peaks.data()), peak_count * sizeof(float));
}

bool ControlPanelCore::lookup_waveform_cache(const char* path, std::vector<float>& out_peaks) {
  load_waveform_cache();

  std::lock_guard<std::mutex> lock(m_waveform_mutex);
  auto it = m_waveform_cache.find(std::string(path));
  if (it != m_waveform_cache.end()) {
    out_peaks = it->second;
    return true;
  }
  return false;
}

// Command state polling for custom buttons with fb2k actions
void ControlPanelCore::start_command_state_timer() {
  if (!m_hwnd || m_command_state_timer_active) return;
  SetTimer(m_hwnd, COMMAND_STATE_TIMER_ID, COMMAND_STATE_POLL_INTERVAL_MS, nullptr);
  m_command_state_timer_active = true;
  
  // Do an initial poll immediately
  poll_custom_button_states();
}

void ControlPanelCore::stop_command_state_timer() {
  if (!m_hwnd || !m_command_state_timer_active) return;
  KillTimer(m_hwnd, COMMAND_STATE_TIMER_ID);
  m_command_state_timer_active = false;
}

void ControlPanelCore::do_show_preferences() {
  KillTimer(m_hwnd, SHOW_PREFS_TIMER_ID);
  ui_control::get()->show_preferences(guid_nowbar_preferences_page);
}

void ControlPanelCore::poll_custom_button_states() {
  bool needs_repaint = false;
  
  for (int i = 0; i < 6; i++) {
    // Only poll enabled buttons with fb2k action type (3)
    if (!get_nowbar_cbutton_enabled(i) || get_nowbar_cbutton_action(i) != 3) {
      if (m_cbutton_states[i].found || m_cbutton_states[i].checked || m_cbutton_states[i].disabled || m_cbutton_states[i].cache_valid) {
        m_cbutton_states[i] = {};
        needs_repaint = true;
      }
      continue;
    }
    
    pfc::string8 path = get_nowbar_cbutton_path(i);
    if (path.is_empty()) {
      if (m_cbutton_states[i].found || m_cbutton_states[i].checked || m_cbutton_states[i].disabled || m_cbutton_states[i].cache_valid) {
        m_cbutton_states[i] = {};
        needs_repaint = true;
      }
      continue;
    }

    if (m_cbutton_states[i].cache_valid) {
      // Fast path: use cached service reference, no full enumeration
      bool old_checked = m_cbutton_states[i].checked;
      bool old_disabled = m_cbutton_states[i].disabled;
      poll_fb2k_action_state(m_cbutton_states[i]);
      if (old_checked != m_cbutton_states[i].checked ||
          old_disabled != m_cbutton_states[i].disabled) {
        needs_repaint = true;
      }
    } else {
      // First poll or path changed: full lookup (caches reference for next time).
      // Skip context menu search — init_context() builds the full context menu
      // tree for all components, which can crash non-thread-safe components.
      CommandState new_state = get_fb2k_action_state_by_path(path.c_str(), true);
      if (new_state.found != m_cbutton_states[i].found ||
          new_state.checked != m_cbutton_states[i].checked ||
          new_state.disabled != m_cbutton_states[i].disabled) {
        needs_repaint = true;
      }
      m_cbutton_states[i] = new_state;
    }
  }

  // Also poll stop-after-current state (catches external toggles via menu/hotkeys)
  if (get_nowbar_stop_after_current_icon_visible()) {
    bool new_sac = playback_control::get()->get_stop_after_current();
    if (new_sac != m_stop_after_current_active) {
      m_stop_after_current_active = new_sac;
      needs_repaint = true;
    }
  }

  if (needs_repaint) {
    invalidate();
  }
}

} // namespace nowbar




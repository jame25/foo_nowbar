#pragma once
#include "pch.h"
#include "playback_state.h"

namespace nowbar {

// Layout constants (will be scaled by DPI)
struct LayoutMetrics {
    int panel_height = 80;
    int artwork_size = 128;
    int artwork_margin = 8;
    int button_size = 38;
    int play_button_size = 48;
    int seekbar_height = 9;
    int volume_width = 192;  // 20% wider than 160
    int spacing = 16;
    int text_height = 30;  // Increased to prevent text clipping
    
    void scale(float dpi_scale) {
        panel_height = static_cast<int>(panel_height * dpi_scale);
        artwork_size = static_cast<int>(artwork_size * dpi_scale);
        artwork_margin = static_cast<int>(artwork_margin * dpi_scale);
        button_size = static_cast<int>(button_size * dpi_scale);
        play_button_size = static_cast<int>(play_button_size * dpi_scale);
        seekbar_height = static_cast<int>(seekbar_height * dpi_scale);
        volume_width = static_cast<int>(volume_width * dpi_scale);
        spacing = static_cast<int>(spacing * dpi_scale);
        text_height = static_cast<int>(text_height * dpi_scale);
    }
};

// Button hit regions
enum class HitRegion {
    None,
    Artwork,
    TrackInfo,
    HeartButton,   // Mood/favorite toggle
    PrevButton,
    PlayButton,
    NextButton,
    ShuffleButton,
    RepeatButton,
    SuperButton,   // Super button (cosmetic)
    SeekBar,
    CustomButton,  // Legacy - kept for compatibility
    CButton1,      // Custom button #1
    CButton2,      // Custom button #2
    CButton3,      // Custom button #3
    CButton4,      // Custom button #4
    CButton5,      // Custom button #5
    CButton6,      // Custom button #6
    VolumeIcon,    // Click to mute/unmute
    VolumeSlider,  // Drag to adjust volume
    MiniPlayerButton
};

// Callback for requesting artwork update from UI wrapper
using ArtworkRequestCallback = std::function<void()>;

// The core control panel implementation (shared between DUI and CUI)
class ControlPanelCore : public IPlaybackStateCallback {
public:
    ControlPanelCore();
    ~ControlPanelCore();
    
    // Initialization
    void initialize(HWND hwnd);
    void set_dark_mode(bool dark);
    void update_dpi(float scale);
    void set_artwork_request_callback(ArtworkRequestCallback cb) { m_artwork_request_cb = cb; }
    
    // Static theme change notification
    static void register_instance(ControlPanelCore* instance);
    static void unregister_instance(ControlPanelCore* instance);
    static void notify_theme_changed();
    
    // Painting
    void paint(HDC hdc, const RECT& rect);
    
    // Mouse interaction
    HitRegion hit_test(int x, int y) const;
    void on_mouse_move(int x, int y);
    void on_mouse_leave();
    void on_lbutton_down(int x, int y);
    void on_lbutton_up(int x, int y);
    void on_lbutton_dblclk(int x, int y);
    void on_mouse_wheel(int delta);
    
    // IPlaybackStateCallback
    void on_playback_state_changed(const PlaybackState& state) override;
    void on_playback_time_changed(double time) override;
    void on_volume_changed(float volume_db) override;
    void on_track_changed() override;
    
    // Artwork
    void set_artwork(album_art_data_ptr data);
    void clear_artwork();
    
    // Get minimum size
    SIZE get_min_size() const;
    
    // MiniPlayer state (for icon color)
    void set_miniplayer_active(bool active);
    
    // Settings change notification
    void on_settings_changed();
    static void notify_all_settings_changed();
    
    // Custom color scheme support (for DUI color sync)
    using ColorQueryCallback = std::function<bool(COLORREF& bg, COLORREF& text, COLORREF& highlight)>;
    void set_color_query_callback(ColorQueryCallback callback);
    void apply_custom_colors();  // Called when theme mode is "Custom"
    
private:
    void update_layout(const RECT& rect);
    void invalidate();
    void update_fonts();
    void extract_artwork_colors();  // Extract dominant colors from artwork for dynamic background
    void create_blurred_artwork();  // Create blurred version of artwork for background
    
    // Drawing helpers
    void draw_background(Gdiplus::Graphics& g, const RECT& rect);
    void draw_artwork(Gdiplus::Graphics& g);
    void draw_track_info(Gdiplus::Graphics& g);
    void draw_playback_buttons(Gdiplus::Graphics& g);
    void draw_seekbar(Gdiplus::Graphics& g);
    void draw_volume(Gdiplus::Graphics& g);
    void draw_time_display(Gdiplus::Graphics& g);
    
    // Button rendering
    void draw_button(Gdiplus::Graphics& g, const RECT& rect, const wchar_t* icon, bool hovered, bool active);
    void draw_circular_button(Gdiplus::Graphics& g, const RECT& rect, const wchar_t* icon, bool hovered, bool pressed);
    
    // Custom icon drawing from SVG paths
    void draw_shuffle_icon(Gdiplus::Graphics& g, const RECT& rect, const Gdiplus::Color& color);
    void draw_repeat_icon(Gdiplus::Graphics& g, const RECT& rect, const Gdiplus::Color& color, bool repeat_one);
    void draw_play_icon(Gdiplus::Graphics& g, const RECT& rect, const Gdiplus::Color& color, bool with_circle);
    void draw_pause_icon(Gdiplus::Graphics& g, const RECT& rect, const Gdiplus::Color& color, bool with_circle);
    void draw_prev_icon(Gdiplus::Graphics& g, const RECT& rect, const Gdiplus::Color& color);
    void draw_next_icon(Gdiplus::Graphics& g, const RECT& rect, const Gdiplus::Color& color);
    void draw_heart_icon(Gdiplus::Graphics& g, const RECT& rect, const Gdiplus::Color& color);
    void draw_miniplayer_icon(Gdiplus::Graphics& g, const RECT& rect, const Gdiplus::Color& color);
    void draw_volume_icon(Gdiplus::Graphics& g, int x, int y, int size, const Gdiplus::Color& color, int level);  // level: 0=mute, 1=low, 2=full
    void draw_custom_icon(Gdiplus::Graphics& g, const RECT& rect, const Gdiplus::Color& color);  // Button #1 icon (dots grid)
    void draw_link_icon(Gdiplus::Graphics& g, const RECT& rect, const Gdiplus::Color& color);    // Button #2 icon (link chain)
    void draw_circle_icon(Gdiplus::Graphics& g, const RECT& rect, const Gdiplus::Color& color);  // Button #3 icon (circle outline)
    void draw_square_icon(Gdiplus::Graphics& g, const RECT& rect, const Gdiplus::Color& color);  // Button #4 icon (square outline)
    void draw_numbered_square_icon(Gdiplus::Graphics& g, const RECT& rect, const Gdiplus::Color& color, int number);  // Square with number
    void draw_alternate_play_icon(Gdiplus::Graphics& g, const RECT& rect, const Gdiplus::Color& color);   // Alternate play icon (outline)
    void draw_alternate_pause_icon(Gdiplus::Graphics& g, const RECT& rect, const Gdiplus::Color& color);  // Alternate pause icon (outline)
    void draw_stop_icon(Gdiplus::Graphics& g, const RECT& rect, const Gdiplus::Color& color, bool filled = false);  // Stop icon (square)
    void draw_super_icon(Gdiplus::Graphics& g, const RECT& rect, const Gdiplus::Color& color);  // Super button icon (3x3 grid of dots)
    
    // Playback control actions
    void do_play_pause();
    void do_prev();
    void do_next();
    void do_shuffle_toggle();
    void do_repeat_cycle();
    void do_seek(double position);
    void do_volume_change(float delta);
    void do_toggle_mood();
    void do_stop();
    void update_mood_state();
    void show_picture_viewer();
    void show_autoplaylist_menu();
    void create_autoplaylist(const char* name, const char* query, const char* sort = "");
    
    // Animation helpers
    float get_hover_opacity(HitRegion region);  // Get animated hover opacity for a region
    
    // Member variables
    HWND m_hwnd = nullptr;
    bool m_dark_mode = true;
    bool m_glass_effect_enabled = false;  // Windows 11 acrylic backdrop
    float m_dpi_scale = 1.0f;
    float m_size_scale = 1.0f;  // Scale factor for controls based on panel height
    LayoutMetrics m_metrics;
    
    // Current state
    PlaybackState m_state;
    
    // Hit regions
    RECT m_rect_artwork = {};
    RECT m_rect_track_info = {};
    RECT m_rect_heart = {};
    RECT m_rect_prev = {};
    RECT m_rect_play = {};
    RECT m_rect_next = {};
    RECT m_rect_shuffle = {};
    RECT m_rect_repeat = {};
    RECT m_rect_super = {};   // Super button
    RECT m_rect_seekbar = {};
    RECT m_rect_custom = {};   // Legacy - kept for compatibility
    RECT m_rect_cbutton1 = {}; // Custom button #1
    RECT m_rect_cbutton2 = {}; // Custom button #2
    RECT m_rect_cbutton3 = {}; // Custom button #3
    RECT m_rect_cbutton4 = {}; // Custom button #4
    RECT m_rect_cbutton5 = {}; // Custom button #5
    RECT m_rect_cbutton6 = {}; // Custom button #6
    RECT m_rect_volume = {};
    RECT m_rect_miniplayer = {};
    RECT m_rect_time = {};
    
    // Interaction state
    HitRegion m_hover_region = HitRegion::None;
    HitRegion m_pressed_region = HitRegion::None;
    bool m_seeking = false;
    bool m_volume_dragging = false;
    float m_prev_volume_db = 0.0f;  // Store previous volume for mute toggle
    bool m_miniplayer_active = false;  // MiniPlayer enabled state for icon color
    bool m_mood_active = false;  // MOOD tag state for heart icon color
    
    // Seekbar tooltip
    int m_seekbar_hover_x = 0;      // Current X position on seekbar (for tooltip)
    double m_preview_time = 0.0;    // Preview time at cursor position
    
    // Play button hover timer for stop icon
    std::chrono::steady_clock::time_point m_play_hover_start_time;
    bool m_play_hover_timer_active = false;
    bool m_show_stop_icon = false;
    
    // Hover enlarge animation for core controls
    static constexpr float HOVER_SCALE_FACTOR = 1.15f;  // 15% larger when hovered
    
    // Autoplaylist menu state for toggle behavior
    bool m_autoplaylist_menu_open = false;
    std::chrono::steady_clock::time_point m_autoplaylist_menu_close_time;
    
    // Custom button fade animation
    float m_cbutton_opacity = 1.0f;  // Current opacity (0.0 = hidden, 1.0 = visible)
    float m_cbutton_target_opacity = 1.0f;  // Target opacity for animation
    std::chrono::steady_clock::time_point m_cbutton_fade_start_time;
    bool m_cbutton_fade_active = false;  // Animation in progress
    static constexpr float CBUTTON_FADE_DURATION_MS = 300.0f;  // Fade duration in milliseconds
    
    // Smooth progress bar animation
    double m_animated_progress = 0.0;      // Current animated progress (0.0 - 1.0)
    double m_target_progress = 0.0;        // Target progress position
    std::chrono::steady_clock::time_point m_last_frame_time;
    static constexpr double PROGRESS_LERP_SPEED = 8.0;  // Interpolation speed multiplier
    
    // Button hover fade animation
    float m_hover_opacity[16] = {};  // Per-region hover opacity (0.0 - 1.0)
    HitRegion m_prev_hover_region = HitRegion::None;
    std::chrono::steady_clock::time_point m_hover_change_time;
    static constexpr float HOVER_FADE_DURATION_MS = 150.0f;  // Quick fade for responsiveness
    
    // Artwork
    std::unique_ptr<Gdiplus::Bitmap> m_artwork_bitmap;
    std::unique_ptr<Gdiplus::Bitmap> m_default_artwork;
    
    // Colors
    Gdiplus::Color m_bg_color;
    Gdiplus::Color m_text_color;
    Gdiplus::Color m_text_secondary_color;
    Gdiplus::Color m_accent_color;
    Gdiplus::Color m_button_hover_color;
    
    // Artwork-extracted colors for dynamic background
    Gdiplus::Color m_artwork_color_primary;
    Gdiplus::Color m_artwork_color_secondary;
    bool m_artwork_colors_valid = false;
    
    // Blurred artwork for background
    std::unique_ptr<Gdiplus::Bitmap> m_blurred_artwork;
    
    // Fonts
    std::unique_ptr<Gdiplus::Font> m_font_title;
    std::unique_ptr<Gdiplus::Font> m_font_artist;
    std::unique_ptr<Gdiplus::Font> m_font_time;
    
    // Artwork request callback
    ArtworkRequestCallback m_artwork_request_cb;
    
    // Color query callback (for DUI custom colors)
    ColorQueryCallback m_color_query_cb;
    
    // Title formatting
    titleformat_object::ptr m_titleformat_line1;
    titleformat_object::ptr m_titleformat_line2;
    pfc::string8 m_formatted_line1;
    pfc::string8 m_formatted_line2;
    
    // Methods for title formatting
    void update_title_formats();
    void evaluate_title_formats();
    
    // Custom button icons (PNG/ICO custom icons)
    std::unique_ptr<Gdiplus::Bitmap> m_cbutton_icons[6];  // Cached custom icons
    pfc::string8 m_cbutton_icon_paths[6];  // Cached paths to detect changes
    
    // Methods for custom icon loading
    void load_custom_icon(int button_index);
    void reload_all_custom_icons();
};

} // namespace nowbar

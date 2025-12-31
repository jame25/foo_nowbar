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
    SeekBar,
    CustomButton,  // Legacy - kept for compatibility
    CButton1,      // Custom button #1
    CButton2,      // Custom button #2
    CButton3,      // Custom button #3
    CButton4,      // Custom button #4
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
    
private:
    void update_layout(const RECT& rect);
    void invalidate();
    void update_fonts();
    
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
    
    // Playback control actions
    void do_play_pause();
    void do_prev();
    void do_next();
    void do_shuffle_toggle();
    void do_repeat_cycle();
    void do_seek(double position);
    void do_volume_change(float delta);
    void do_toggle_mood();
    void update_mood_state();
    void show_picture_viewer();
    
    // Member variables
    HWND m_hwnd = nullptr;
    bool m_dark_mode = true;
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
    RECT m_rect_seekbar = {};
    RECT m_rect_custom = {};   // Legacy - kept for compatibility
    RECT m_rect_cbutton1 = {}; // Custom button #1
    RECT m_rect_cbutton2 = {}; // Custom button #2
    RECT m_rect_cbutton3 = {}; // Custom button #3
    RECT m_rect_cbutton4 = {}; // Custom button #4
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
    
    // Artwork
    std::unique_ptr<Gdiplus::Bitmap> m_artwork_bitmap;
    std::unique_ptr<Gdiplus::Bitmap> m_default_artwork;
    
    // Colors
    Gdiplus::Color m_bg_color;
    Gdiplus::Color m_text_color;
    Gdiplus::Color m_text_secondary_color;
    Gdiplus::Color m_accent_color;
    Gdiplus::Color m_button_hover_color;
    
    // Fonts
    std::unique_ptr<Gdiplus::Font> m_font_title;
    std::unique_ptr<Gdiplus::Font> m_font_artist;
    std::unique_ptr<Gdiplus::Font> m_font_time;
    
    // Artwork request callback
    ArtworkRequestCallback m_artwork_request_cb;
};

} // namespace nowbar

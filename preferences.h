#pragma once

#include "pch.h"
#include "resource.h"

// Configuration access functions
int get_nowbar_theme_mode();  // 0=Auto, 1=Dark, 2=Light
bool get_nowbar_cover_margin();  // true=Yes (margin), false=No (edge-to-edge)
int get_nowbar_bar_style();  // 0=Pill-shaped, 1=Rectangular
bool get_nowbar_mood_icon_visible();  // true=Show, false=Hidden
bool get_nowbar_miniplayer_icon_visible();  // true=Show, false=Hidden
bool get_nowbar_hover_circles_enabled();  // true=Yes (show), false=No (hide)
bool get_nowbar_alternate_icons_enabled();  // true=Enabled, false=Disabled
bool get_nowbar_cbutton_autohide();  // true=Yes (auto-hide), false=No
bool get_nowbar_custom_button_visible();  // true=Show, false=Hidden
int get_nowbar_custom_button_action();  // 0=None, 1=Open URL, 2=Run Executable, 3=Foobar2k Action
pfc::string8 get_nowbar_custom_button_url();
pfc::string8 get_nowbar_custom_button_executable();
pfc::string8 get_nowbar_custom_button_fb2k_action();

// Multi-button configuration accessors (for Custom Button tab)
bool get_nowbar_cbutton_enabled(int button_index);  // 0-5, returns enabled state
int get_nowbar_cbutton_action(int button_index);    // 0-5, returns action 0-3
pfc::string8 get_nowbar_cbutton_path(int button_index);  // 0-5, returns path string
pfc::string8 get_nowbar_cbutton_icon_path(int button_index);  // 0-5, returns custom icon path (PNG/ICO)

// Display format configuration functions
pfc::string8 get_nowbar_line1_format();
pfc::string8 get_nowbar_line2_format();

// Execute a foobar2000 main menu command by path
bool execute_fb2k_action_by_path(const char* path);

// Get the effective background color for the current theme mode configuration
// Can be called early before core is fully initialized
COLORREF get_nowbar_initial_bg_color();

// Font configuration functions
bool get_nowbar_use_custom_fonts();
LOGFONT get_nowbar_artist_font();
LOGFONT get_nowbar_track_font();
void set_nowbar_artist_font(const LOGFONT& font);
void set_nowbar_track_font(const LOGFONT& font);
void reset_nowbar_fonts();
LOGFONT get_nowbar_default_font(bool is_artist);

// Preferences page instance - the actual dialog
class nowbar_preferences : public preferences_page_instance {
private:
    HWND m_hwnd;
    preferences_page_callback::ptr m_callback;
    bool m_has_changes;
    fb2k::CCoreDarkModeHooks m_darkMode;
    int m_current_tab;  // 0 = General, 1 = Fonts
    
public:
    nowbar_preferences(preferences_page_callback::ptr callback);
    
    // preferences_page_instance implementation
    HWND get_wnd() override;
    t_uint32 get_state() override;
    void apply() override;
    void reset() override;
    
    // Dialog procedure
    static INT_PTR CALLBACK ConfigProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
    
private:
    void on_changed();
    void apply_settings();
    void reset_settings();
    void update_font_displays();
    void select_artist_font();
    void select_track_font();
    pfc::string8 format_font_name(const LOGFONT& lf);
    
    // Tab control methods
    void init_tab_control();
    void switch_tab(int tab);
};

// Preferences page factory
class nowbar_preferences_page : public preferences_page_v3 {
public:
    const char* get_name() override;
    GUID get_guid() override;
    GUID get_parent_guid() override;
    preferences_page_instance::ptr instantiate(HWND parent, preferences_page_callback::ptr callback) override;
};

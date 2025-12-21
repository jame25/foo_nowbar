#pragma once

#include "pch.h"
#include "resource.h"

// Configuration access functions
int get_nowbar_theme_mode();  // 0=Auto, 1=Dark, 2=Light

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
    void reset_fonts_to_default();
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

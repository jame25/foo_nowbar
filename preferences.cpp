#include "pch.h"
#include "preferences.h"
#include "core/control_panel_core.h"
#include <uxtheme.h>
#include <commdlg.h>
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "comdlg32.lib")

// External declaration from component.cpp
extern HINSTANCE g_hInstance;

// Configuration variables - stored in foobar2000's config system
static cfg_int cfg_nowbar_theme_mode(
    GUID{0xABCDEF01, 0x1234, 0x5678, {0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67, 0x89}}, 
    0  // Default: Auto
);

static cfg_int cfg_nowbar_cover_margin(
    GUID{0xABCDEF05, 0x1234, 0x5678, {0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67, 0x8D}},
    1  // Default: Yes (margin enabled)
);

static cfg_int cfg_nowbar_bar_style(
    GUID{0xABCDEF06, 0x1234, 0x5678, {0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67, 0x8E}},
    0  // Default: Pill-shaped
);

static cfg_int cfg_nowbar_mood_icon_visible(
    GUID{0xABCDEF07, 0x1234, 0x5678, {0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67, 0x8F}},
    1  // Default: Show (visible)
);

static cfg_int cfg_nowbar_miniplayer_icon_visible(
    GUID{0xABCDEF08, 0x1234, 0x5678, {0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67, 0x90}},
    1  // Default: Show (visible)
);

// Font configuration
static cfg_struct_t<LOGFONT> cfg_nowbar_artist_font(
    GUID{0xABCDEF02, 0x1234, 0x5678, {0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67, 0x8A}},
    []() {
        LOGFONT lf = {};
        lf.lfHeight = -17;  // ~12.6pt at 96 DPI (40% larger than 9pt)
        lf.lfWeight = FW_NORMAL;
        lf.lfCharSet = DEFAULT_CHARSET;
        lf.lfOutPrecision = OUT_DEFAULT_PRECIS;
        lf.lfClipPrecision = CLIP_DEFAULT_PRECIS;
        lf.lfQuality = CLEARTYPE_QUALITY;
        lf.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
        wcscpy_s(lf.lfFaceName, L"Microsoft YaHei");
        return lf;
    }()
);

static cfg_struct_t<LOGFONT> cfg_nowbar_track_font(
    GUID{0xABCDEF03, 0x1234, 0x5678, {0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67, 0x8B}},
    []() {
        LOGFONT lf = {};
        lf.lfHeight = -21;  // ~15.4pt at 96 DPI (40% larger than 11pt)
        lf.lfWeight = FW_BOLD;
        lf.lfCharSet = DEFAULT_CHARSET;
        lf.lfOutPrecision = OUT_DEFAULT_PRECIS;
        lf.lfClipPrecision = CLIP_DEFAULT_PRECIS;
        lf.lfQuality = CLEARTYPE_QUALITY;
        lf.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
        wcscpy_s(lf.lfFaceName, L"Microsoft YaHei");
        return lf;
    }()
);

static cfg_int cfg_nowbar_use_custom_fonts(
    GUID{0xABCDEF04, 0x1234, 0x5678, {0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67, 0x8C}},
    0  // Default: use built-in fonts
);

// Configuration accessors
int get_nowbar_theme_mode() {
    int mode = cfg_nowbar_theme_mode;
    if (mode < 0) mode = 0;
    if (mode > 2) mode = 2;
    return mode;
}

bool get_nowbar_cover_margin() {
    return cfg_nowbar_cover_margin != 0;
}

int get_nowbar_bar_style() {
    int style = cfg_nowbar_bar_style;
    if (style < 0) style = 0;
    if (style > 1) style = 1;
    return style;
}

bool get_nowbar_mood_icon_visible() {
    return cfg_nowbar_mood_icon_visible != 0;
}

bool get_nowbar_miniplayer_icon_visible() {
    return cfg_nowbar_miniplayer_icon_visible != 0;
}

COLORREF get_nowbar_initial_bg_color() {
    int theme_mode = get_nowbar_theme_mode();
    bool dark;
    
    switch (theme_mode) {
        case 1:  // Force Dark
            dark = true;
            break;
        case 2:  // Force Light
            dark = false;
            break;
        default:  // Auto (0) - query foobar2000
            // Use tryGet() to safely check if ui_config_manager is available
            {
                auto api = ui_config_manager::tryGet();
                if (api.is_valid()) {
                    dark = api->is_dark_mode();
                } else {
                    // Default to dark if we can't determine (during early startup)
                    dark = true;
                }
            }
            break;
    }
    
    return dark ? RGB(24, 24, 24) : RGB(245, 245, 245);
}

bool get_nowbar_use_custom_fonts() {
    return cfg_nowbar_use_custom_fonts != 0;
}

LOGFONT get_nowbar_artist_font() {
    return cfg_nowbar_artist_font.get_value();
}

LOGFONT get_nowbar_track_font() {
    return cfg_nowbar_track_font.get_value();
}

void set_nowbar_artist_font(const LOGFONT& font) {
    cfg_nowbar_artist_font = font;
    cfg_nowbar_use_custom_fonts = 1;
}

void set_nowbar_track_font(const LOGFONT& font) {
    cfg_nowbar_track_font = font;
    cfg_nowbar_use_custom_fonts = 1;
}

void reset_nowbar_fonts() {
    cfg_nowbar_use_custom_fonts = 0;
    cfg_nowbar_artist_font = get_nowbar_default_font(true);
    cfg_nowbar_track_font = get_nowbar_default_font(false);
}

LOGFONT get_nowbar_default_font(bool is_artist) {
    LOGFONT lf = {};
    
    // Get DPI for proper scaling
    HDC hdc = GetDC(nullptr);
    int dpi = GetDeviceCaps(hdc, LOGPIXELSY);
    ReleaseDC(nullptr, hdc);
    
    // Artist: 12.6pt, Track: 15.4pt (40% larger than originals)
    int point_size = is_artist ? 13 : 15;
    lf.lfHeight = -MulDiv(point_size, dpi, 72);
    lf.lfWeight = is_artist ? FW_NORMAL : FW_BOLD;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfOutPrecision = OUT_TT_PRECIS;
    lf.lfClipPrecision = CLIP_DEFAULT_PRECIS;
    lf.lfQuality = CLEARTYPE_QUALITY;
    lf.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
    wcscpy_s(lf.lfFaceName, L"Microsoft YaHei");
    
    return lf;
}

// GUID for preferences page
static const GUID guid_nowbar_preferences_page = 
{ 0xABCDEF00, 0x1234, 0x5678, { 0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67, 0x88 } };

//=============================================================================
// nowbar_preferences - preferences page instance implementation
//=============================================================================

nowbar_preferences::nowbar_preferences(preferences_page_callback::ptr callback)
    : m_hwnd(nullptr), m_callback(callback), m_has_changes(false), m_current_tab(0) {
}

HWND nowbar_preferences::get_wnd() {
    return m_hwnd;
}

t_uint32 nowbar_preferences::get_state() {
    t_uint32 state = preferences_state::resettable | preferences_state::dark_mode_supported;
    if (m_has_changes) {
        state |= preferences_state::changed;
    }
    return state;
}

void nowbar_preferences::apply() {
    apply_settings();
    m_has_changes = false;
    m_callback->on_state_changed();
}

void nowbar_preferences::reset() {
    reset_settings();
    m_has_changes = false;
    m_callback->on_state_changed();
}

void nowbar_preferences::init_tab_control() {
    HWND hTab = GetDlgItem(m_hwnd, IDC_TAB_CONTROL);
    
    TCITEM tie = {};
    tie.mask = TCIF_TEXT;
    
    tie.pszText = (LPWSTR)L"General";
    TabCtrl_InsertItem(hTab, 0, &tie);
    
    tie.pszText = (LPWSTR)L"Fonts";
    TabCtrl_InsertItem(hTab, 1, &tie);
}

void nowbar_preferences::switch_tab(int tab) {
    m_current_tab = tab;
    
    // General tab controls
    BOOL show_general = (tab == 0) ? SW_SHOW : SW_HIDE;
    ShowWindow(GetDlgItem(m_hwnd, IDC_THEME_MODE_LABEL), show_general);
    ShowWindow(GetDlgItem(m_hwnd, IDC_THEME_MODE_COMBO), show_general);
    ShowWindow(GetDlgItem(m_hwnd, IDC_COVER_MARGIN_LABEL), show_general);
    ShowWindow(GetDlgItem(m_hwnd, IDC_COVER_MARGIN_COMBO), show_general);
    ShowWindow(GetDlgItem(m_hwnd, IDC_BAR_STYLE_LABEL), show_general);
    ShowWindow(GetDlgItem(m_hwnd, IDC_BAR_STYLE_COMBO), show_general);
    ShowWindow(GetDlgItem(m_hwnd, IDC_MOOD_ICON_LABEL), show_general);
    ShowWindow(GetDlgItem(m_hwnd, IDC_MOOD_ICON_COMBO), show_general);
    ShowWindow(GetDlgItem(m_hwnd, IDC_MINIPLAYER_ICON_LABEL), show_general);
    ShowWindow(GetDlgItem(m_hwnd, IDC_MINIPLAYER_ICON_COMBO), show_general);
    
    // Fonts tab controls
    BOOL show_fonts = (tab == 1) ? SW_SHOW : SW_HIDE;
    ShowWindow(GetDlgItem(m_hwnd, IDC_FONTS_GROUP), show_fonts);
    ShowWindow(GetDlgItem(m_hwnd, IDC_TRACK_FONT_LABEL), show_fonts);
    ShowWindow(GetDlgItem(m_hwnd, IDC_TRACK_FONT_DISPLAY), show_fonts);
    ShowWindow(GetDlgItem(m_hwnd, IDC_TRACK_FONT_SELECT), show_fonts);
    ShowWindow(GetDlgItem(m_hwnd, IDC_ARTIST_FONT_LABEL), show_fonts);
    ShowWindow(GetDlgItem(m_hwnd, IDC_ARTIST_FONT_DISPLAY), show_fonts);
    ShowWindow(GetDlgItem(m_hwnd, IDC_ARTIST_FONT_SELECT), show_fonts);
}

INT_PTR CALLBACK nowbar_preferences::ConfigProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    nowbar_preferences* p_this = nullptr;
    
    if (msg == WM_INITDIALOG) {
        p_this = reinterpret_cast<nowbar_preferences*>(lp);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, lp);
        p_this->m_hwnd = hwnd;
        
        // Initialize dark mode hooks
        p_this->m_darkMode.AddDialogWithControls(hwnd);
        
        // Enable tab page texture
        EnableThemeDialogTexture(hwnd, ETDT_ENABLETAB);
        
        // Initialize tab control
        p_this->init_tab_control();
        
        // Initialize theme mode combobox
        HWND hThemeCombo = GetDlgItem(hwnd, IDC_THEME_MODE_COMBO);
        SendMessage(hThemeCombo, CB_ADDSTRING, 0, (LPARAM)L"Auto");
        SendMessage(hThemeCombo, CB_ADDSTRING, 0, (LPARAM)L"Dark");
        SendMessage(hThemeCombo, CB_ADDSTRING, 0, (LPARAM)L"Light");
        SendMessage(hThemeCombo, CB_SETCURSEL, cfg_nowbar_theme_mode, 0);
        
        // Initialize cover margin combobox
        HWND hMarginCombo = GetDlgItem(hwnd, IDC_COVER_MARGIN_COMBO);
        SendMessage(hMarginCombo, CB_ADDSTRING, 0, (LPARAM)L"Yes");
        SendMessage(hMarginCombo, CB_ADDSTRING, 0, (LPARAM)L"No");
        SendMessage(hMarginCombo, CB_SETCURSEL, cfg_nowbar_cover_margin ? 0 : 1, 0);
        
        // Initialize bar style combobox
        HWND hBarStyleCombo = GetDlgItem(hwnd, IDC_BAR_STYLE_COMBO);
        SendMessage(hBarStyleCombo, CB_ADDSTRING, 0, (LPARAM)L"Pill-shaped");
        SendMessage(hBarStyleCombo, CB_ADDSTRING, 0, (LPARAM)L"Rectangular");
        SendMessage(hBarStyleCombo, CB_SETCURSEL, cfg_nowbar_bar_style, 0);
        
        // Initialize mood icon visibility combobox
        HWND hMoodIconCombo = GetDlgItem(hwnd, IDC_MOOD_ICON_COMBO);
        SendMessage(hMoodIconCombo, CB_ADDSTRING, 0, (LPARAM)L"Show");
        SendMessage(hMoodIconCombo, CB_ADDSTRING, 0, (LPARAM)L"Hidden");
        SendMessage(hMoodIconCombo, CB_SETCURSEL, cfg_nowbar_mood_icon_visible ? 0 : 1, 0);
        
        // Initialize miniplayer icon visibility combobox
        HWND hMiniplayerIconCombo = GetDlgItem(hwnd, IDC_MINIPLAYER_ICON_COMBO);
        SendMessage(hMiniplayerIconCombo, CB_ADDSTRING, 0, (LPARAM)L"Show");
        SendMessage(hMiniplayerIconCombo, CB_ADDSTRING, 0, (LPARAM)L"Hidden");
        SendMessage(hMiniplayerIconCombo, CB_SETCURSEL, cfg_nowbar_miniplayer_icon_visible ? 0 : 1, 0);
        
        // Initialize font displays
        p_this->update_font_displays();
        
        // Show initial tab
        p_this->switch_tab(0);
        
        p_this->m_has_changes = false;
    } else {
        p_this = reinterpret_cast<nowbar_preferences*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }
    
    if (p_this == nullptr) return FALSE;
    
    switch (msg) {
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_THEME_MODE_COMBO:
        case IDC_COVER_MARGIN_COMBO:
        case IDC_BAR_STYLE_COMBO:
        case IDC_MOOD_ICON_COMBO:
        case IDC_MINIPLAYER_ICON_COMBO:
            if (HIWORD(wp) == CBN_SELCHANGE) {
                p_this->on_changed();
            }
            break;
            
        case IDC_TRACK_FONT_SELECT:
            if (HIWORD(wp) == BN_CLICKED) {
                p_this->select_track_font();
            }
            break;
            
        case IDC_ARTIST_FONT_SELECT:
            if (HIWORD(wp) == BN_CLICKED) {
                p_this->select_artist_font();
            }
            break;
        }
        break;
        
    case WM_NOTIFY:
        {
            NMHDR* pnmhdr = reinterpret_cast<NMHDR*>(lp);
            if (pnmhdr->idFrom == IDC_TAB_CONTROL && pnmhdr->code == TCN_SELCHANGE) {
                int sel = TabCtrl_GetCurSel(pnmhdr->hwndFrom);
                p_this->switch_tab(sel);
            }
        }
        break;
        
    case WM_DESTROY:
        p_this->m_hwnd = nullptr;
        break;
    }
    
    return FALSE;
}

void nowbar_preferences::on_changed() {
    m_has_changes = true;
    m_callback->on_state_changed();
}

void nowbar_preferences::apply_settings() {
    if (m_hwnd) {
        // Save theme mode
        cfg_nowbar_theme_mode = (int)SendMessage(GetDlgItem(m_hwnd, IDC_THEME_MODE_COMBO), CB_GETCURSEL, 0, 0);
        
        // Save cover margin (0=Yes, 1=No in combobox -> config 1=Yes, 0=No)
        int marginSel = (int)SendMessage(GetDlgItem(m_hwnd, IDC_COVER_MARGIN_COMBO), CB_GETCURSEL, 0, 0);
        cfg_nowbar_cover_margin = (marginSel == 0) ? 1 : 0;
        
        // Save bar style (0=Pill-shaped, 1=Rectangular)
        cfg_nowbar_bar_style = (int)SendMessage(GetDlgItem(m_hwnd, IDC_BAR_STYLE_COMBO), CB_GETCURSEL, 0, 0);
        
        // Save mood icon visibility (0=Show, 1=Hidden in combobox -> config 1=Show, 0=Hidden)
        int moodIconSel = (int)SendMessage(GetDlgItem(m_hwnd, IDC_MOOD_ICON_COMBO), CB_GETCURSEL, 0, 0);
        cfg_nowbar_mood_icon_visible = (moodIconSel == 0) ? 1 : 0;
        
        // Save miniplayer icon visibility (0=Show, 1=Hidden in combobox -> config 1=Show, 0=Hidden)
        int miniplayerIconSel = (int)SendMessage(GetDlgItem(m_hwnd, IDC_MINIPLAYER_ICON_COMBO), CB_GETCURSEL, 0, 0);
        cfg_nowbar_miniplayer_icon_visible = (miniplayerIconSel == 0) ? 1 : 0;
        
        // Notify all registered instances to update
        nowbar::ControlPanelCore::notify_all_settings_changed();
    }
}

void nowbar_preferences::reset_settings() {
    if (m_hwnd) {
        if (m_current_tab == 0) {
            // Reset General tab settings
            cfg_nowbar_theme_mode = 0;  // Auto
            cfg_nowbar_cover_margin = 1;  // Yes (margin enabled)
            cfg_nowbar_bar_style = 0;  // Pill-shaped
            cfg_nowbar_mood_icon_visible = 1;  // Show (visible)
            cfg_nowbar_miniplayer_icon_visible = 1;  // Show (visible)
            
            // Update General tab UI
            SendMessage(GetDlgItem(m_hwnd, IDC_THEME_MODE_COMBO), CB_SETCURSEL, 0, 0);
            SendMessage(GetDlgItem(m_hwnd, IDC_COVER_MARGIN_COMBO), CB_SETCURSEL, 0, 0);
            SendMessage(GetDlgItem(m_hwnd, IDC_BAR_STYLE_COMBO), CB_SETCURSEL, 0, 0);
            SendMessage(GetDlgItem(m_hwnd, IDC_MOOD_ICON_COMBO), CB_SETCURSEL, 0, 0);
            SendMessage(GetDlgItem(m_hwnd, IDC_MINIPLAYER_ICON_COMBO), CB_SETCURSEL, 0, 0);
        } else if (m_current_tab == 1) {
            // Reset Fonts tab settings
            reset_nowbar_fonts();
            update_font_displays();
        }
        
        // Notify panels
        nowbar::ControlPanelCore::notify_all_settings_changed();
    }
}

void nowbar_preferences::update_font_displays() {
    if (!m_hwnd) return;
    
    // Track font
    if (get_nowbar_use_custom_fonts()) {
        LOGFONT lf = get_nowbar_track_font();
        pfc::string8 desc = format_font_name(lf);
        uSetDlgItemText(m_hwnd, IDC_TRACK_FONT_DISPLAY, desc);
    } else {
        uSetDlgItemText(m_hwnd, IDC_TRACK_FONT_DISPLAY, "Microsoft YaHei, 15pt, Bold (Default)");
    }
    
    // Artist font
    if (get_nowbar_use_custom_fonts()) {
        LOGFONT lf = get_nowbar_artist_font();
        pfc::string8 desc = format_font_name(lf);
        uSetDlgItemText(m_hwnd, IDC_ARTIST_FONT_DISPLAY, desc);
    } else {
        uSetDlgItemText(m_hwnd, IDC_ARTIST_FONT_DISPLAY, "Microsoft YaHei, 13pt, Regular (Default)");
    }
}

void nowbar_preferences::select_track_font() {
    LOGFONT lf = get_nowbar_use_custom_fonts() ? get_nowbar_track_font() : get_nowbar_default_font(false);
    
    CHOOSEFONT cf = {};
    cf.lStructSize = sizeof(cf);
    cf.hwndOwner = m_hwnd;
    cf.lpLogFont = &lf;
    cf.Flags = CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT | CF_EFFECTS;
    
    if (ChooseFont(&cf)) {
        set_nowbar_track_font(lf);
        update_font_displays();
        on_changed();
    }
}

void nowbar_preferences::select_artist_font() {
    LOGFONT lf = get_nowbar_use_custom_fonts() ? get_nowbar_artist_font() : get_nowbar_default_font(true);
    
    CHOOSEFONT cf = {};
    cf.lStructSize = sizeof(cf);
    cf.hwndOwner = m_hwnd;
    cf.lpLogFont = &lf;
    cf.Flags = CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT | CF_EFFECTS;
    
    if (ChooseFont(&cf)) {
        set_nowbar_artist_font(lf);
        update_font_displays();
        on_changed();
    }
}

pfc::string8 nowbar_preferences::format_font_name(const LOGFONT& lf) {
    pfc::string8 result;
    
    // Convert face name to UTF-8
    char face_name[64];
    WideCharToMultiByte(CP_UTF8, 0, lf.lfFaceName, -1, face_name, sizeof(face_name), nullptr, nullptr);
    
    // Calculate point size from height
    HDC hdc = GetDC(nullptr);
    int point_size = MulDiv(-lf.lfHeight, 72, GetDeviceCaps(hdc, LOGPIXELSY));
    ReleaseDC(nullptr, hdc);
    
    // Format string
    result << face_name << ", " << point_size << "pt";
    if (lf.lfWeight >= FW_BOLD) {
        result << ", Bold";
    }
    if (lf.lfItalic) {
        result << ", Italic";
    }
    
    return result;
}

//=============================================================================
// nowbar_preferences_page - preferences page factory
//=============================================================================

const char* nowbar_preferences_page::get_name() {
    return "Now Bar";
}

GUID nowbar_preferences_page::get_guid() {
    return guid_nowbar_preferences_page;
}

GUID nowbar_preferences_page::get_parent_guid() {
    return preferences_page::guid_tools;
}

preferences_page_instance::ptr nowbar_preferences_page::instantiate(HWND parent, preferences_page_callback::ptr callback) {
    auto instance = fb2k::service_new<nowbar_preferences>(callback);
    
    // Create dialog
    HWND hwnd = CreateDialogParam(
        g_hInstance,
        MAKEINTRESOURCE(IDD_NOWBAR_PREFERENCES),
        parent,
        nowbar_preferences::ConfigProc,
        reinterpret_cast<LPARAM>(instance.get_ptr())
    );
    
    return instance;
}

// Register preferences page
static preferences_page_factory_t<nowbar_preferences_page> g_nowbar_preferences_factory;

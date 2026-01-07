#include "pch.h"
#include "preferences.h"
#include "core/control_panel_core.h"
#include <uxtheme.h>
#include <commdlg.h>
#include <fstream>
#include <sstream>
#include <vector>
#include <map>
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

static cfg_int cfg_nowbar_stop_icon_visible(
    GUID{0xABCDEF51, 0x1234, 0x5678, {0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67, 0xE1}},
    0  // Default: Hidden
);

static cfg_int cfg_nowbar_miniplayer_icon_visible(
    GUID{0xABCDEF08, 0x1234, 0x5678, {0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67, 0x90}},
    1  // Default: Show (visible)
);

static cfg_int cfg_nowbar_custom_button_visible(
    GUID{0xABCDEF09, 0x1234, 0x5678, {0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67, 0x91}},
    1  // Default: Show (visible)
);

static cfg_int cfg_nowbar_hover_circles(
    GUID{0xABCDEF1C, 0x1234, 0x5678, {0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67, 0xAC}},
    1  // Default: Yes (show hover circles)
);

static cfg_int cfg_nowbar_alternate_icons(
    GUID{0xABCDEF1D, 0x1234, 0x5678, {0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67, 0xAD}},
    0  // Default: Disabled
);

static cfg_int cfg_nowbar_cbutton_autohide(
    GUID{0xABCDEF1E, 0x1234, 0x5678, {0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67, 0xAE}},
    1  // Default: Yes (auto-hide during playback)
);

static cfg_int cfg_nowbar_glass_effect(
    GUID{0xABCDEF1F, 0x1234, 0x5678, {0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67, 0xAF}},
    0  // Default: Disabled
);

static cfg_int cfg_nowbar_background_style(
    GUID{0xABCDEF50, 0x1234, 0x5678, {0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67, 0xE0}},
    0  // Default: Solid (0=Solid, 1=Artwork Colors, 2=Blurred Artwork)
);

static cfg_int cfg_nowbar_custom_button_action(
    GUID{0xABCDEF0A, 0x1234, 0x5678, {0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67, 0x92}},
    0  // Default: None (0=None, 1=Open URL, 2=Run Executable)
);

static cfg_string cfg_nowbar_custom_button_url(
    GUID{0xABCDEF0B, 0x1234, 0x5678, {0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67, 0x93}},
    ""  // Default: empty
);

static cfg_string cfg_nowbar_custom_button_executable(
    GUID{0xABCDEF0C, 0x1234, 0x5678, {0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67, 0x94}},
    ""  // Default: empty
);

static cfg_string cfg_nowbar_custom_button_fb2k_action(
    GUID{0xABCDEF0D, 0x1234, 0x5678, {0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67, 0x95}},
    ""  // Default: empty (e.g., "Playback/Matrix Now Playing/Announce current track to Matrix")
);

// Display format configuration
static cfg_string cfg_nowbar_line1_format(
    GUID{0xABCDEF30, 0x1234, 0x5678, {0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67, 0xC0}},
    "%title%"  // Default: title only
);

static cfg_string cfg_nowbar_line2_format(
    GUID{0xABCDEF31, 0x1234, 0x5678, {0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67, 0xC1}},
    "%artist%"  // Default: artist only
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

// Custom Button tab configuration (4 buttons)
static cfg_int cfg_cbutton1_enabled(
    GUID{0xABCDEF10, 0x1234, 0x5678, {0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67, 0xA0}},
    0  // Default: disabled
);
static cfg_int cfg_cbutton1_action(
    GUID{0xABCDEF11, 0x1234, 0x5678, {0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67, 0xA1}},
    0  // Default: None
);
static cfg_int cfg_cbutton2_enabled(
    GUID{0xABCDEF12, 0x1234, 0x5678, {0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67, 0xA2}},
    0  // Default: disabled
);
static cfg_int cfg_cbutton2_action(
    GUID{0xABCDEF13, 0x1234, 0x5678, {0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67, 0xA3}},
    0  // Default: None
);
static cfg_int cfg_cbutton3_enabled(
    GUID{0xABCDEF14, 0x1234, 0x5678, {0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67, 0xA4}},
    0  // Default: disabled
);
static cfg_int cfg_cbutton3_action(
    GUID{0xABCDEF15, 0x1234, 0x5678, {0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67, 0xA5}},
    0  // Default: None
);
static cfg_int cfg_cbutton4_enabled(
    GUID{0xABCDEF16, 0x1234, 0x5678, {0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67, 0xA6}},
    0  // Default: disabled
);
static cfg_int cfg_cbutton4_action(
    GUID{0xABCDEF17, 0x1234, 0x5678, {0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67, 0xA7}},
    0  // Default: None
);

static cfg_string cfg_cbutton1_path(
    GUID{0xABCDEF18, 0x1234, 0x5678, {0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67, 0xA8}},
    ""  // Default: empty
);
static cfg_string cfg_cbutton2_path(
    GUID{0xABCDEF19, 0x1234, 0x5678, {0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67, 0xA9}},
    ""  // Default: empty
);
static cfg_string cfg_cbutton3_path(
    GUID{0xABCDEF1A, 0x1234, 0x5678, {0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67, 0xAA}},
    ""  // Default: empty
);
static cfg_string cfg_cbutton4_path(
    GUID{0xABCDEF1B, 0x1234, 0x5678, {0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67, 0xAB}},
    ""  // Default: empty
);

// Custom Button 5 and 6 config
static cfg_int cfg_cbutton5_enabled(
    GUID{0xABCDEF20, 0x1234, 0x5678, {0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67, 0xB0}},
    0  // Default: disabled
);
static cfg_int cfg_cbutton5_action(
    GUID{0xABCDEF21, 0x1234, 0x5678, {0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67, 0xB1}},
    0  // Default: None
);
static cfg_string cfg_cbutton5_path(
    GUID{0xABCDEF22, 0x1234, 0x5678, {0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67, 0xB2}},
    ""  // Default: empty
);
static cfg_int cfg_cbutton6_enabled(
    GUID{0xABCDEF23, 0x1234, 0x5678, {0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67, 0xB3}},
    0  // Default: disabled
);
static cfg_int cfg_cbutton6_action(
    GUID{0xABCDEF24, 0x1234, 0x5678, {0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67, 0xB4}},
    0  // Default: None
);
static cfg_string cfg_cbutton6_path(
    GUID{0xABCDEF25, 0x1234, 0x5678, {0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67, 0xB5}},
    ""  // Default: empty
);

// Custom Button Icon paths (PNG/ICO files for custom icons)
static cfg_string cfg_cbutton1_icon(
    GUID{0xABCDEF40, 0x1234, 0x5678, {0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67, 0xD0}},
    ""  // Default: empty (use default numbered icon)
);
static cfg_string cfg_cbutton2_icon(
    GUID{0xABCDEF41, 0x1234, 0x5678, {0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67, 0xD1}},
    ""  // Default: empty
);
static cfg_string cfg_cbutton3_icon(
    GUID{0xABCDEF42, 0x1234, 0x5678, {0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67, 0xD2}},
    ""  // Default: empty
);
static cfg_string cfg_cbutton4_icon(
    GUID{0xABCDEF43, 0x1234, 0x5678, {0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67, 0xD3}},
    ""  // Default: empty
);
static cfg_string cfg_cbutton5_icon(
    GUID{0xABCDEF44, 0x1234, 0x5678, {0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67, 0xD4}},
    ""  // Default: empty
);
static cfg_string cfg_cbutton6_icon(
    GUID{0xABCDEF45, 0x1234, 0x5678, {0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67, 0xD5}},
    ""  // Default: empty
);

// Custom Button Tooltip labels (user-friendly names for tooltips)
static cfg_string cfg_cbutton1_label(
    GUID{0xABCDEF60, 0x1234, 0x5678, {0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67, 0xF0}},
    ""  // Default: empty (will show "Button #1")
);
static cfg_string cfg_cbutton2_label(
    GUID{0xABCDEF61, 0x1234, 0x5678, {0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67, 0xF1}},
    ""  // Default: empty (will show "Button #2")
);
static cfg_string cfg_cbutton3_label(
    GUID{0xABCDEF62, 0x1234, 0x5678, {0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67, 0xF2}},
    ""  // Default: empty (will show "Button #3")
);
static cfg_string cfg_cbutton4_label(
    GUID{0xABCDEF63, 0x1234, 0x5678, {0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67, 0xF3}},
    ""  // Default: empty (will show "Button #4")
);
static cfg_string cfg_cbutton5_label(
    GUID{0xABCDEF64, 0x1234, 0x5678, {0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67, 0xF4}},
    ""  // Default: empty (will show "Button #5")
);
static cfg_string cfg_cbutton6_label(
    GUID{0xABCDEF65, 0x1234, 0x5678, {0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67, 0xF5}},
    ""  // Default: empty (will show "Button #6")
);

// Profile configuration storage
static cfg_string cfg_cbutton_profiles(
    GUID{0xABCDEF70, 0x1234, 0x5678, {0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67, 0xF6}},
    ""  // Default: empty (will create "Default" profile on first use)
);
static cfg_string cfg_cbutton_current_profile(
    GUID{0xABCDEF71, 0x1234, 0x5678, {0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67, 0xF7}},
    "Default"  // Default profile name
);

//=============================================================================
// Profile Management - Simple JSON-like serialization
//=============================================================================

// Escape a string for JSON-like storage
static pfc::string8 escape_json_string(const char* str) {
    pfc::string8 result;
    while (*str) {
        if (*str == '"') result << "\\\"";
        else if (*str == '\\') result << "\\\\";
        else if (*str == '\n') result << "\\n";
        else if (*str == '\r') result << "\\r";
        else if (*str == '\t') result << "\\t";
        else result.add_byte(*str);
        str++;
    }
    return result;
}

// Unescape a JSON-like string
static pfc::string8 unescape_json_string(const char* str) {
    pfc::string8 result;
    while (*str) {
        if (*str == '\\' && *(str + 1)) {
            str++;
            if (*str == '"') result.add_byte('"');
            else if (*str == '\\') result.add_byte('\\');
            else if (*str == 'n') result.add_byte('\n');
            else if (*str == 'r') result.add_byte('\r');
            else if (*str == 't') result.add_byte('\t');
            else result.add_byte(*str);
        } else {
            result.add_byte(*str);
        }
        str++;
    }
    return result;
}

// Button configuration for a single button
struct ButtonConfig {
    bool enabled = false;
    int action = 0;
    pfc::string8 path;
    pfc::string8 icon;
    pfc::string8 label;
};

// Full profile with 6 buttons
struct CButtonProfile {
    pfc::string8 name;
    ButtonConfig buttons[6];
};

// Serialize a single profile to JSON format
static pfc::string8 serialize_profile(const CButtonProfile& profile) {
    pfc::string8 json;
    json << "{\"name\":\"" << escape_json_string(profile.name.c_str()) << "\",\"buttons\":[";
    for (int i = 0; i < 6; i++) {
        if (i > 0) json << ",";
        json << "{\"enabled\":" << (profile.buttons[i].enabled ? "true" : "false");
        json << ",\"action\":" << profile.buttons[i].action;
        json << ",\"path\":\"" << escape_json_string(profile.buttons[i].path.c_str()) << "\"";
        json << ",\"icon\":\"" << escape_json_string(profile.buttons[i].icon.c_str()) << "\"";
        json << ",\"label\":\"" << escape_json_string(profile.buttons[i].label.c_str()) << "\"}";
    }
    json << "]}";
    return json;
}

// Helper to skip whitespace
static const char* skip_ws(const char* p) {
    while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    return p;
}

// Helper to parse a JSON string value (assumes p points to opening quote)
static const char* parse_json_string(const char* p, pfc::string8& out) {
    out.reset();
    if (*p != '"') return p;
    p++;
    while (*p && *p != '"') {
        if (*p == '\\' && *(p + 1)) {
            p++;
            if (*p == '"') out.add_byte('"');
            else if (*p == '\\') out.add_byte('\\');
            else if (*p == 'n') out.add_byte('\n');
            else if (*p == 'r') out.add_byte('\r');
            else if (*p == 't') out.add_byte('\t');
            else out.add_byte(*p);
        } else {
            out.add_byte(*p);
        }
        p++;
    }
    if (*p == '"') p++;
    return p;
}

// Skip any JSON value (string, number, boolean, null, object, array)
static const char* skip_json_value(const char* p) {
    p = skip_ws(p);
    if (*p == '"') {
        // String - skip to closing quote
        p++;
        while (*p && *p != '"') {
            if (*p == '\\' && *(p + 1)) p++;  // Skip escaped char
            p++;
        }
        if (*p == '"') p++;
    } else if (*p == '{') {
        // Object - skip to matching }
        int depth = 1;
        p++;
        while (*p && depth > 0) {
            if (*p == '{') depth++;
            else if (*p == '}') depth--;
            else if (*p == '"') {
                p++;
                while (*p && *p != '"') {
                    if (*p == '\\' && *(p + 1)) p++;
                    p++;
                }
            }
            if (*p) p++;
        }
    } else if (*p == '[') {
        // Array - skip to matching ]
        int depth = 1;
        p++;
        while (*p && depth > 0) {
            if (*p == '[') depth++;
            else if (*p == ']') depth--;
            else if (*p == '"') {
                p++;
                while (*p && *p != '"') {
                    if (*p == '\\' && *(p + 1)) p++;
                    p++;
                }
            }
            if (*p) p++;
        }
    } else {
        // Number, boolean, or null - skip until delimiter
        while (*p && *p != ',' && *p != '}' && *p != ']' && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') {
            p++;
        }
    }
    return p;
}

// Parse a single profile from JSON
static const char* parse_profile(const char* p, CButtonProfile& profile) {
    p = skip_ws(p);
    if (*p != '{') return p;
    p++;
    
    while (*p && *p != '}') {
        p = skip_ws(p);
        if (*p == '"') {
            pfc::string8 key;
            p = parse_json_string(p, key);
            p = skip_ws(p);
            if (*p == ':') p++;
            p = skip_ws(p);
            
            if (key == "name") {
                p = parse_json_string(p, profile.name);
            } else if (key == "buttons") {
                if (*p == '[') {
                    p++;
                    int btn_idx = 0;
                    while (*p && *p != ']' && btn_idx < 6) {
                        p = skip_ws(p);
                        if (*p == '{') {
                            p++;
                            while (*p && *p != '}') {
                                p = skip_ws(p);
                                if (*p == '"') {
                                    pfc::string8 btn_key;
                                    p = parse_json_string(p, btn_key);
                                    p = skip_ws(p);
                                    if (*p == ':') p++;
                                    p = skip_ws(p);
                                    
                                    if (btn_key == "enabled") {
                                        if (strncmp(p, "true", 4) == 0) {
                                            profile.buttons[btn_idx].enabled = true;
                                            p += 4;
                                        } else if (strncmp(p, "false", 5) == 0) {
                                            profile.buttons[btn_idx].enabled = false;
                                            p += 5;
                                        } else {
                                            p = skip_json_value(p);  // Skip unexpected value
                                        }
                                    } else if (btn_key == "action") {
                                        profile.buttons[btn_idx].action = atoi(p);
                                        while (*p && ((*p >= '0' && *p <= '9') || *p == '-')) p++;
                                    } else if (btn_key == "path") {
                                        p = parse_json_string(p, profile.buttons[btn_idx].path);
                                    } else if (btn_key == "icon") {
                                        p = parse_json_string(p, profile.buttons[btn_idx].icon);
                                    } else if (btn_key == "label") {
                                        p = parse_json_string(p, profile.buttons[btn_idx].label);
                                    } else {
                                        // Unknown key - skip its value
                                        p = skip_json_value(p);
                                    }
                                } else if (*p && *p != '}') {
                                    // Unexpected character, skip it
                                    p++;
                                }
                                p = skip_ws(p);
                                if (*p == ',') p++;
                            }
                            if (*p == '}') p++;
                            btn_idx++;
                        } else if (*p && *p != ']') {
                            // Skip non-object elements in array
                            p = skip_json_value(p);
                        }
                        p = skip_ws(p);
                        if (*p == ',') p++;
                    }
                    // Skip any remaining buttons beyond 6
                    while (*p && *p != ']') {
                        p = skip_ws(p);
                        if (*p == '{' || *p == '[' || *p == '"') {
                            p = skip_json_value(p);
                        } else if (*p && *p != ']') {
                            p++;
                        }
                        p = skip_ws(p);
                        if (*p == ',') p++;
                    }
                    if (*p == ']') p++;
                } else {
                    p = skip_json_value(p);  // Skip non-array buttons value
                }
            } else {
                // Unknown key - skip its value
                p = skip_json_value(p);
            }
        } else if (*p && *p != '}') {
            // Unexpected character at top level, skip it
            p++;
        }
        p = skip_ws(p);
        if (*p == ',') p++;
    }
    if (*p == '}') p++;
    return p;
}


// Get all profiles from storage
static std::vector<CButtonProfile> get_all_profiles() {
    std::vector<CButtonProfile> profiles;
    pfc::string8 data = cfg_cbutton_profiles.get();
    
    if (data.is_empty()) {
        // Create default profile with current settings
        CButtonProfile def;
        def.name = "Default";
        for (int i = 0; i < 6; i++) {
            def.buttons[i].enabled = get_nowbar_cbutton_enabled(i);
            def.buttons[i].action = get_nowbar_cbutton_action(i);
            def.buttons[i].path = get_nowbar_cbutton_path(i);
            def.buttons[i].icon = get_nowbar_cbutton_icon_path(i);
            def.buttons[i].label = cfg_cbutton1_label.get();  // Will fix below
        }
        // Fix labels
        def.buttons[0].label = cfg_cbutton1_label.get();
        def.buttons[1].label = cfg_cbutton2_label.get();
        def.buttons[2].label = cfg_cbutton3_label.get();
        def.buttons[3].label = cfg_cbutton4_label.get();
        def.buttons[4].label = cfg_cbutton5_label.get();
        def.buttons[5].label = cfg_cbutton6_label.get();
        profiles.push_back(def);
        return profiles;
    }
    
    // Parse profiles array
    const char* p = data.c_str();
    p = skip_ws(p);
    if (*p == '[') {
        p++;
        while (*p && *p != ']') {
            p = skip_ws(p);
            if (*p == '{') {
                CButtonProfile profile;
                p = parse_profile(p, profile);
                if (!profile.name.is_empty()) {
                    profiles.push_back(profile);
                }
            }
            p = skip_ws(p);
            if (*p == ',') p++;
        }
    }
    
    if (profiles.empty()) {
        CButtonProfile def;
        def.name = "Default";
        profiles.push_back(def);
    }
    
    return profiles;
}

// Save all profiles to storage
static void save_all_profiles(const std::vector<CButtonProfile>& profiles) {
    pfc::string8 data;
    data << "[";
    for (size_t i = 0; i < profiles.size(); i++) {
        if (i > 0) data << ",";
        data << serialize_profile(profiles[i]);
    }
    data << "]";
    cfg_cbutton_profiles = data;
}

// Find a profile by name
static CButtonProfile* find_profile(std::vector<CButtonProfile>& profiles, const char* name) {
    for (auto& p : profiles) {
        if (stricmp_utf8(p.name.c_str(), name) == 0) {
            return &p;
        }
    }
    return nullptr;
}

// Get current profile name
static pfc::string8 get_current_profile_name() {
    return cfg_cbutton_current_profile.get();
}

// Set current profile name
static void set_current_profile_name(const char* name) {
    cfg_cbutton_current_profile = name;
}

// Static buffer for input dialog (shared across invocations)

static wchar_t s_input_dialog_buffer[256];
static POINT s_input_dialog_pos;

// Dialog proc for input dialog with positioning
static INT_PTR CALLBACK InputDialogProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_INITDIALOG: {
        SetDlgItemTextW(hDlg, 101, s_input_dialog_buffer);
        SendDlgItemMessage(hDlg, 101, EM_SETSEL, 0, -1);  // Select all
        
        // Position at cursor
        RECT dlgRect;
        GetWindowRect(hDlg, &dlgRect);
        int dlgWidth = dlgRect.right - dlgRect.left;
        int dlgHeight = dlgRect.bottom - dlgRect.top;
        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        int screenHeight = GetSystemMetrics(SM_CYSCREEN);
        int x = s_input_dialog_pos.x;
        int y = s_input_dialog_pos.y;
        if (x + dlgWidth > screenWidth) x = screenWidth - dlgWidth;
        if (y + dlgHeight > screenHeight) y = screenHeight - dlgHeight;
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        SetWindowPos(hDlg, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        
        SetFocus(GetDlgItem(hDlg, 101));
        return FALSE;  // Don't set default focus
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            GetDlgItemTextW(hDlg, 101, s_input_dialog_buffer, 256);
            EndDialog(hDlg, IDOK);
            return TRUE;
        } else if (LOWORD(wParam) == IDCANCEL) {
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    case WM_CLOSE:
        EndDialog(hDlg, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

// Show input dialog at cursor position
// Returns true if OK was pressed, false if cancelled
// Result is stored in out_result
static bool show_input_dialog(HWND hwndParent, const wchar_t* title, const wchar_t* prompt, 
                              const wchar_t* initial_value, pfc::string8& out_result) {
    // Copy initial value to buffer and get cursor position
    wcsncpy_s(s_input_dialog_buffer, initial_value, 255);
    GetCursorPos(&s_input_dialog_pos);
    
    // Build dialog template in memory
    BYTE dlg_buffer[1024] = {};
    DLGTEMPLATE* pTemplate = (DLGTEMPLATE*)dlg_buffer;
    pTemplate->style = DS_MODALFRAME | WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_SETFONT;
    pTemplate->dwExtendedStyle = 0;
    pTemplate->cdit = 4;  // 4 controls: static, edit, OK, Cancel
    pTemplate->x = 0;
    pTemplate->y = 0;
    pTemplate->cx = 200;
    pTemplate->cy = 70;
    
    WORD* pWord = (WORD*)(pTemplate + 1);
    *pWord++ = 0;  // No menu
    *pWord++ = 0;  // Default window class
    
    // Title
    size_t title_len = wcslen(title) + 1;
    memcpy(pWord, title, title_len * sizeof(wchar_t));
    pWord += title_len;
    
    // Font (required with DS_SETFONT)
    *pWord++ = 8;  // Font size
    const wchar_t* font = L"MS Shell Dlg";
    size_t font_len = wcslen(font) + 1;
    memcpy(pWord, font, font_len * sizeof(wchar_t));
    pWord += font_len;
    
    // Align to DWORD
    pWord = (WORD*)(((ULONG_PTR)pWord + 3) & ~3);
    
    // Static text control
    DLGITEMTEMPLATE* pItem = (DLGITEMTEMPLATE*)pWord;
    pItem->style = WS_VISIBLE | WS_CHILD | SS_LEFT;
    pItem->dwExtendedStyle = 0;
    pItem->x = 10; pItem->y = 10; pItem->cx = 180; pItem->cy = 10;
    pItem->id = 100;
    pWord = (WORD*)(pItem + 1);
    *pWord++ = 0xFFFF; *pWord++ = 0x0082;  // Static class
    size_t prompt_len = wcslen(prompt) + 1;
    memcpy(pWord, prompt, prompt_len * sizeof(wchar_t));
    pWord += prompt_len;
    *pWord++ = 0;  // No creation data
    pWord = (WORD*)(((ULONG_PTR)pWord + 3) & ~3);
    
    // Edit control
    pItem = (DLGITEMTEMPLATE*)pWord;
    pItem->style = WS_VISIBLE | WS_CHILD | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL;
    pItem->dwExtendedStyle = 0;
    pItem->x = 10; pItem->y = 22; pItem->cx = 180; pItem->cy = 14;
    pItem->id = 101;
    pWord = (WORD*)(pItem + 1);
    *pWord++ = 0xFFFF; *pWord++ = 0x0081;  // Edit class
    *pWord++ = 0;  // No text
    *pWord++ = 0;  // No creation data
    pWord = (WORD*)(((ULONG_PTR)pWord + 3) & ~3);
    
    // OK button
    pItem = (DLGITEMTEMPLATE*)pWord;
    pItem->style = WS_VISIBLE | WS_CHILD | WS_TABSTOP | BS_DEFPUSHBUTTON;
    pItem->dwExtendedStyle = 0;
    pItem->x = 55; pItem->y = 45; pItem->cx = 40; pItem->cy = 14;
    pItem->id = IDOK;
    pWord = (WORD*)(pItem + 1);
    *pWord++ = 0xFFFF; *pWord++ = 0x0080;  // Button class
    const wchar_t* ok_text = L"OK";
    memcpy(pWord, ok_text, 3 * sizeof(wchar_t));
    pWord += 3;
    *pWord++ = 0;  // No creation data
    pWord = (WORD*)(((ULONG_PTR)pWord + 3) & ~3);
    
    // Cancel button
    pItem = (DLGITEMTEMPLATE*)pWord;
    pItem->style = WS_VISIBLE | WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON;
    pItem->dwExtendedStyle = 0;
    pItem->x = 105; pItem->y = 45; pItem->cx = 40; pItem->cy = 14;
    pItem->id = IDCANCEL;
    pWord = (WORD*)(pItem + 1);
    *pWord++ = 0xFFFF; *pWord++ = 0x0080;  // Button class
    const wchar_t* cancel_text = L"Cancel";
    memcpy(pWord, cancel_text, 7 * sizeof(wchar_t));
    pWord += 7;
    *pWord++ = 0;  // No creation data
    
    INT_PTR dlgResult = DialogBoxIndirectW(g_hInstance, pTemplate, hwndParent, InputDialogProc);
    
    if (dlgResult == IDOK && wcslen(s_input_dialog_buffer) > 0) {
        pfc::stringcvt::string_utf8_from_wide utf8_result(s_input_dialog_buffer);
        out_result = utf8_result.get_ptr();
        return true;
    }
    return false;
}


// Load profile settings into the cfg_ variables

static void load_profile_to_config(const CButtonProfile& profile) {
    cfg_cbutton1_enabled = profile.buttons[0].enabled ? 1 : 0;
    cfg_cbutton2_enabled = profile.buttons[1].enabled ? 1 : 0;
    cfg_cbutton3_enabled = profile.buttons[2].enabled ? 1 : 0;
    cfg_cbutton4_enabled = profile.buttons[3].enabled ? 1 : 0;
    cfg_cbutton5_enabled = profile.buttons[4].enabled ? 1 : 0;
    cfg_cbutton6_enabled = profile.buttons[5].enabled ? 1 : 0;
    
    cfg_cbutton1_action = profile.buttons[0].action;
    cfg_cbutton2_action = profile.buttons[1].action;
    cfg_cbutton3_action = profile.buttons[2].action;
    cfg_cbutton4_action = profile.buttons[3].action;
    cfg_cbutton5_action = profile.buttons[4].action;
    cfg_cbutton6_action = profile.buttons[5].action;
    
    cfg_cbutton1_path = profile.buttons[0].path;
    cfg_cbutton2_path = profile.buttons[1].path;
    cfg_cbutton3_path = profile.buttons[2].path;
    cfg_cbutton4_path = profile.buttons[3].path;
    cfg_cbutton5_path = profile.buttons[4].path;
    cfg_cbutton6_path = profile.buttons[5].path;
    
    cfg_cbutton1_icon = profile.buttons[0].icon;
    cfg_cbutton2_icon = profile.buttons[1].icon;
    cfg_cbutton3_icon = profile.buttons[2].icon;
    cfg_cbutton4_icon = profile.buttons[3].icon;
    cfg_cbutton5_icon = profile.buttons[4].icon;
    cfg_cbutton6_icon = profile.buttons[5].icon;
    
    cfg_cbutton1_label = profile.buttons[0].label;
    cfg_cbutton2_label = profile.buttons[1].label;
    cfg_cbutton3_label = profile.buttons[2].label;
    cfg_cbutton4_label = profile.buttons[3].label;
    cfg_cbutton5_label = profile.buttons[4].label;
    cfg_cbutton6_label = profile.buttons[5].label;
}

// Save current config to a profile
static void save_config_to_profile(CButtonProfile& profile) {
    profile.buttons[0].enabled = cfg_cbutton1_enabled != 0;
    profile.buttons[1].enabled = cfg_cbutton2_enabled != 0;
    profile.buttons[2].enabled = cfg_cbutton3_enabled != 0;
    profile.buttons[3].enabled = cfg_cbutton4_enabled != 0;
    profile.buttons[4].enabled = cfg_cbutton5_enabled != 0;
    profile.buttons[5].enabled = cfg_cbutton6_enabled != 0;
    
    profile.buttons[0].action = cfg_cbutton1_action;
    profile.buttons[1].action = cfg_cbutton2_action;
    profile.buttons[2].action = cfg_cbutton3_action;
    profile.buttons[3].action = cfg_cbutton4_action;
    profile.buttons[4].action = cfg_cbutton5_action;
    profile.buttons[5].action = cfg_cbutton6_action;
    
    profile.buttons[0].path = cfg_cbutton1_path.get();
    profile.buttons[1].path = cfg_cbutton2_path.get();
    profile.buttons[2].path = cfg_cbutton3_path.get();
    profile.buttons[3].path = cfg_cbutton4_path.get();
    profile.buttons[4].path = cfg_cbutton5_path.get();
    profile.buttons[5].path = cfg_cbutton6_path.get();
    
    profile.buttons[0].icon = cfg_cbutton1_icon.get();
    profile.buttons[1].icon = cfg_cbutton2_icon.get();
    profile.buttons[2].icon = cfg_cbutton3_icon.get();
    profile.buttons[3].icon = cfg_cbutton4_icon.get();
    profile.buttons[4].icon = cfg_cbutton5_icon.get();
    profile.buttons[5].icon = cfg_cbutton6_icon.get();
    
    profile.buttons[0].label = cfg_cbutton1_label.get();
    profile.buttons[1].label = cfg_cbutton2_label.get();
    profile.buttons[2].label = cfg_cbutton3_label.get();
    profile.buttons[3].label = cfg_cbutton4_label.get();
    profile.buttons[4].label = cfg_cbutton5_label.get();
    profile.buttons[5].label = cfg_cbutton6_label.get();
}

// Export a profile to a file
static bool export_profile_to_file(const CButtonProfile& profile, const wchar_t* filepath) {
    std::ofstream file(filepath, std::ios::out | std::ios::binary);
    if (!file.is_open()) return false;
    
    pfc::string8 json;
    json << "{\n  \"name\": \"" << escape_json_string(profile.name.c_str()) << "\",\n";
    json << "  \"version\": 1,\n";
    json << "  \"buttons\": [\n";
    for (int i = 0; i < 6; i++) {
        json << "    {\n";
        json << "      \"enabled\": " << (profile.buttons[i].enabled ? "true" : "false") << ",\n";
        json << "      \"action\": " << profile.buttons[i].action << ",\n";
        json << "      \"path\": \"" << escape_json_string(profile.buttons[i].path.c_str()) << "\",\n";
        json << "      \"icon\": \"" << escape_json_string(profile.buttons[i].icon.c_str()) << "\",\n";
        json << "      \"label\": \"" << escape_json_string(profile.buttons[i].label.c_str()) << "\"\n";
        json << "    }" << (i < 5 ? "," : "") << "\n";
    }
    json << "  ]\n}\n";
    
    file.write(json.c_str(), json.length());
    file.close();
    return true;
}

// Import a profile from a file
static bool import_profile_from_file(const wchar_t* filepath, CButtonProfile& profile) {
    std::ifstream file(filepath, std::ios::in | std::ios::binary);
    if (!file.is_open()) return false;
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    file.close();
    
    std::string content = buffer.str();
    parse_profile(content.c_str(), profile);
    
    return !profile.name.is_empty();
}

// Configuration accessors

int get_nowbar_theme_mode() {
    int mode = cfg_nowbar_theme_mode;
    if (mode < 0) mode = 0;
    if (mode > 3) mode = 3;  // 0=Auto, 1=Dark, 2=Light, 3=Custom
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

bool get_nowbar_stop_icon_visible() {
    return cfg_nowbar_stop_icon_visible != 0;
}

bool get_nowbar_miniplayer_icon_visible() {
    return cfg_nowbar_miniplayer_icon_visible != 0;
}

bool get_nowbar_hover_circles_enabled() {
    return true;  // Hover circles always enabled (preference option removed)
}

bool get_nowbar_alternate_icons_enabled() {
    return cfg_nowbar_alternate_icons != 0;
}

bool get_nowbar_cbutton_autohide() {
    return cfg_nowbar_cbutton_autohide != 0;
}

bool get_nowbar_glass_effect_enabled() {
    return cfg_nowbar_glass_effect != 0;
}

int get_nowbar_background_style() {
    int style = cfg_nowbar_background_style;
    if (style < 0) style = 0;
    if (style > 2) style = 2;  // 0=Solid, 1=Artwork Colors, 2=Blurred Artwork
    return style;
}

bool get_nowbar_custom_button_visible() {
    return cfg_nowbar_custom_button_visible != 0;
}

int get_nowbar_custom_button_action() {
    int action = cfg_nowbar_custom_button_action;
    if (action < 0) action = 0;
    if (action > 3) action = 3;  // 0=None, 1=Open URL, 2=Run Executable, 3=Foobar2k Action
    return action;
}

pfc::string8 get_nowbar_custom_button_url() {
    return cfg_nowbar_custom_button_url.get();
}

pfc::string8 get_nowbar_custom_button_executable() {
    return cfg_nowbar_custom_button_executable.get();
}

pfc::string8 get_nowbar_custom_button_fb2k_action() {
    return cfg_nowbar_custom_button_fb2k_action.get();
}

bool get_nowbar_cbutton_enabled(int button_index) {
    switch (button_index) {
        case 0: return cfg_cbutton1_enabled != 0;
        case 1: return cfg_cbutton2_enabled != 0;
        case 2: return cfg_cbutton3_enabled != 0;
        case 3: return cfg_cbutton4_enabled != 0;
        case 4: return cfg_cbutton5_enabled != 0;
        case 5: return cfg_cbutton6_enabled != 0;
        default: return false;
    }
}

int get_nowbar_cbutton_action(int button_index) {
    int action = 0;
    switch (button_index) {
        case 0: action = cfg_cbutton1_action; break;
        case 1: action = cfg_cbutton2_action; break;
        case 2: action = cfg_cbutton3_action; break;
        case 3: action = cfg_cbutton4_action; break;
        case 4: action = cfg_cbutton5_action; break;
        case 5: action = cfg_cbutton6_action; break;
    }
    if (action < 0) action = 0;
    if (action > 3) action = 3;
    return action;
}

pfc::string8 get_nowbar_cbutton_path(int button_index) {
    switch (button_index) {
        case 0: return cfg_cbutton1_path.get();
        case 1: return cfg_cbutton2_path.get();
        case 2: return cfg_cbutton3_path.get();
        case 3: return cfg_cbutton4_path.get();
        case 4: return cfg_cbutton5_path.get();
        case 5: return cfg_cbutton6_path.get();
        default: return pfc::string8();
    }
}

pfc::string8 get_nowbar_cbutton_icon_path(int button_index) {
    switch (button_index) {
        case 0: return cfg_cbutton1_icon.get();
        case 1: return cfg_cbutton2_icon.get();
        case 2: return cfg_cbutton3_icon.get();
        case 3: return cfg_cbutton4_icon.get();
        case 4: return cfg_cbutton5_icon.get();
        case 5: return cfg_cbutton6_icon.get();
        default: return pfc::string8();
    }
}

pfc::string8 get_nowbar_cbutton_label(int button_index) {
    pfc::string8 label;
    switch (button_index) {
        case 0: label = cfg_cbutton1_label.get(); break;
        case 1: label = cfg_cbutton2_label.get(); break;
        case 2: label = cfg_cbutton3_label.get(); break;
        case 3: label = cfg_cbutton4_label.get(); break;
        case 4: label = cfg_cbutton5_label.get(); break;
        case 5: label = cfg_cbutton6_label.get(); break;
    }
    // Return default "Button #N" if label is empty
    if (label.is_empty()) {
        label << "Button #" << (button_index + 1);
    }
    return label;
}

pfc::string8 get_nowbar_line1_format() {
    return cfg_nowbar_line1_format.get();
}

pfc::string8 get_nowbar_line2_format() {
    return cfg_nowbar_line2_format.get();
}

// Execute a foobar2000 main menu command by path (e.g., "Playback/Matrix Now Playing/Announce current track to Matrix")
bool execute_fb2k_action_by_path(const char* path) {
    if (!path || !*path) return false;

    pfc::string8 target_path(path);

    // Enumerate all main menu commands and find one matching the path
    service_enum_t<mainmenu_commands> enumerator;
    service_ptr_t<mainmenu_commands> commands;

    while (enumerator.next(commands)) {
        service_ptr_t<mainmenu_commands_v2> commands_v2;
        commands->service_query_t(commands_v2);

        const auto command_count = commands->get_command_count();
        for (uint32_t command_index = 0; command_index < command_count; command_index++) {
            GUID command_id = commands->get_command(command_index);

            // Build the full path for this command
            pfc::string8 command_name;
            commands->get_name(command_index, command_name);

            // Get parent group names
            std::list<pfc::string8> name_parts;
            name_parts.push_back(command_name);

            GUID parent_id = commands->get_parent();
            while (parent_id != pfc::guid_null) {
                service_enum_t<mainmenu_group> group_enum;
                service_ptr_t<mainmenu_group> group;
                bool found = false;

                while (group_enum.next(group)) {
                    if (group->get_guid() == parent_id) {
                        service_ptr_t<mainmenu_group_popup> popup;
                        if (group->service_query_t(popup)) {
                            pfc::string8 group_name;
                            popup->get_display_string(group_name);
                            name_parts.push_front(group_name);
                        }
                        parent_id = group->get_parent();
                        found = true;
                        break;
                    }
                }
                if (!found) break;
            }

            // Build full path string
            pfc::string8 full_path;
            bool first = true;
            for (const auto& part : name_parts) {
                if (!first) full_path << "/";
                full_path << part;
                first = false;
            }

            // Check for match (case-insensitive)
            if (stricmp_utf8(full_path.c_str(), target_path.c_str()) == 0) {
                mainmenu_commands::g_execute(command_id);
                return true;
            }

            // For dynamic commands, check sub-items
            if (commands_v2.is_valid() && commands_v2->is_command_dynamic(command_index)) {
                mainmenu_node::ptr node = commands_v2->dynamic_instantiate(command_index);

                // Recursive lambda to search dynamic nodes
                std::function<bool(const mainmenu_node::ptr&, std::list<pfc::string8>)> search_node;
                search_node = [&](const mainmenu_node::ptr& n, std::list<pfc::string8> parts) -> bool {
                    if (!n.is_valid()) return false;

                    pfc::string8 display_name;
                    uint32_t flags;
                    n->get_display(display_name, flags);

                    switch (n->get_type()) {
                    case mainmenu_node::type_command: {
                        parts.push_back(display_name);

                        pfc::string8 node_path;
                        bool first_part = true;
                        for (const auto& p : parts) {
                            if (!first_part) node_path << "/";
                            node_path << p;
                            first_part = false;
                        }

                        if (stricmp_utf8(node_path.c_str(), target_path.c_str()) == 0) {
                            mainmenu_commands::g_execute_dynamic(command_id, n->get_guid());
                            return true;
                        }
                        return false;
                    }
                    case mainmenu_node::type_group: {
                        if (!display_name.is_empty()) {
                            parts.push_back(display_name);
                        }

                        for (size_t i = 0, count = n->get_children_count(); i < count; i++) {
                            if (search_node(n->get_child(i), parts)) {
                                return true;
                            }
                        }
                        return false;
                    }
                    default:
                        return false;
                    }
                };

                // Start search from root with parent path
                std::list<pfc::string8> base_parts;
                for (const auto& p : name_parts) {
                    if (&p != &name_parts.back()) {  // Exclude the command name itself
                        base_parts.push_back(p);
                    }
                }
                if (search_node(node, base_parts)) {
                    return true;
                }
            }
        }
    }

    // Main menu command not found - try context menu commands
    // Get selected tracks from the active playlist
    auto pm = playlist_manager::get();
    metadb_handle_list tracks;
    
    t_size active_playlist = pm->get_active_playlist();
    if (active_playlist != pfc_infinite) {
        // Get all selected items, or just the focused item if nothing selected
        bit_array_bittable selection(pm->playlist_get_item_count(active_playlist));
        pm->playlist_get_selection_mask(active_playlist, selection);
        
        bool has_selection = false;
        for (t_size i = 0; i < pm->playlist_get_item_count(active_playlist); i++) {
            if (selection.get(i)) {
                has_selection = true;
                break;
            }
        }
        
        if (has_selection) {
            pm->playlist_get_selected_items(active_playlist, tracks);
        } else {
            // No selection - use focused item
            t_size focus = pm->playlist_get_focus_item(active_playlist);
            if (focus != pfc_infinite) {
                metadb_handle_ptr track;
                if (pm->playlist_get_item_handle(track, active_playlist, focus)) {
                    tracks.add_item(track);
                }
            }
        }
    }
    
    if (tracks.get_count() == 0) {
        return false;  // No tracks to operate on
    }
    
    // Create context menu manager and search for the command
    contextmenu_manager::ptr cm;
    contextmenu_manager::g_create(cm);
    cm->init_context(tracks, contextmenu_manager::flag_show_shortcuts);
    
    // Recursive function to search context menu nodes
    std::function<bool(contextmenu_node*, pfc::string8)> search_context_node;
    search_context_node = [&](contextmenu_node* node, pfc::string8 parent_path) -> bool {
        if (!node) return false;
        
        t_size child_count = node->get_num_children();
        for (t_size i = 0; i < child_count; i++) {
            contextmenu_node* child = node->get_child(i);
            if (!child) continue;
            
            // Get node name
            const char* child_name = child->get_name();
            if (!child_name) continue;
            
            // Skip separators
            if (child->get_type() == contextmenu_item_node::type_separator) continue;
            
            // Build full path for this node
            pfc::string8 full_path;
            if (!parent_path.is_empty()) {
                full_path << parent_path << "/";
            }
            full_path << child_name;
            
            if (child->get_type() == contextmenu_item_node::type_command) {
                // Check if this matches our target path
                if (stricmp_utf8(full_path.c_str(), target_path.c_str()) == 0) {
                    child->execute();
                    return true;
                }
            } else if (child->get_type() == contextmenu_item_node::type_group) {
                // Recurse into submenu
                if (search_context_node(child, full_path)) {
                    return true;
                }
            }
        }
        return false;
    };
    
    // Start search from root
    contextmenu_node* root = cm->get_root();
    if (root && search_context_node(root, "")) {
        return true;
    }
    
    return false;
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
    
    tie.pszText = (LPWSTR)L"Custom Button";
    TabCtrl_InsertItem(hTab, 1, &tie);
    
    tie.pszText = (LPWSTR)L"Fonts";
    TabCtrl_InsertItem(hTab, 2, &tie);
}

void nowbar_preferences::switch_tab(int tab) {
    m_current_tab = tab;
    
    // General tab controls
    BOOL show_general = (tab == 0) ? SW_SHOW : SW_HIDE;
    // Display Format section
    ShowWindow(GetDlgItem(m_hwnd, IDC_DISPLAY_FORMAT_GROUP), show_general);
    ShowWindow(GetDlgItem(m_hwnd, IDC_LINE1_FORMAT_LABEL), show_general);
    ShowWindow(GetDlgItem(m_hwnd, IDC_LINE1_FORMAT_EDIT), show_general);
    ShowWindow(GetDlgItem(m_hwnd, IDC_LINE2_FORMAT_LABEL), show_general);
    ShowWindow(GetDlgItem(m_hwnd, IDC_LINE2_FORMAT_EDIT), show_general);
    // Appearance settings
    ShowWindow(GetDlgItem(m_hwnd, IDC_THEME_MODE_LABEL), show_general);
    ShowWindow(GetDlgItem(m_hwnd, IDC_THEME_MODE_COMBO), show_general);
    ShowWindow(GetDlgItem(m_hwnd, IDC_COVER_MARGIN_LABEL), show_general);
    ShowWindow(GetDlgItem(m_hwnd, IDC_COVER_MARGIN_COMBO), show_general);
    ShowWindow(GetDlgItem(m_hwnd, IDC_BAR_STYLE_LABEL), show_general);
    ShowWindow(GetDlgItem(m_hwnd, IDC_BAR_STYLE_COMBO), show_general);
    ShowWindow(GetDlgItem(m_hwnd, IDC_MOOD_ICON_LABEL), show_general);
    ShowWindow(GetDlgItem(m_hwnd, IDC_MOOD_ICON_COMBO), show_general);
    ShowWindow(GetDlgItem(m_hwnd, IDC_STOP_ICON_LABEL), show_general);
    ShowWindow(GetDlgItem(m_hwnd, IDC_STOP_ICON_COMBO), show_general);
    ShowWindow(GetDlgItem(m_hwnd, IDC_MINIPLAYER_ICON_LABEL), show_general);
    ShowWindow(GetDlgItem(m_hwnd, IDC_MINIPLAYER_ICON_COMBO), show_general);
    ShowWindow(GetDlgItem(m_hwnd, IDC_ALTERNATE_ICONS_LABEL), show_general);
    ShowWindow(GetDlgItem(m_hwnd, IDC_ALTERNATE_ICONS_COMBO), show_general);
    ShowWindow(GetDlgItem(m_hwnd, IDC_AUTOHIDE_CBUTTONS_LABEL), show_general);
    ShowWindow(GetDlgItem(m_hwnd, IDC_AUTOHIDE_CBUTTONS_COMBO), show_general);
    ShowWindow(GetDlgItem(m_hwnd, IDC_GLASS_EFFECT_LABEL), show_general);
    ShowWindow(GetDlgItem(m_hwnd, IDC_GLASS_EFFECT_COMBO), show_general);
    ShowWindow(GetDlgItem(m_hwnd, IDC_BACKGROUND_STYLE_LABEL), show_general);
    ShowWindow(GetDlgItem(m_hwnd, IDC_BACKGROUND_STYLE_COMBO), show_general);

    // Custom Button tab controls
    BOOL show_cbutton = (tab == 1) ? SW_SHOW : SW_HIDE;
    // Profile configuration controls
    ShowWindow(GetDlgItem(m_hwnd, IDC_PROFILE_NAME_LABEL), show_cbutton);
    ShowWindow(GetDlgItem(m_hwnd, IDC_PROFILE_COMBO), show_cbutton);
    ShowWindow(GetDlgItem(m_hwnd, IDC_PROFILE_MENU_BTN), show_cbutton);
    // Button config headers
    ShowWindow(GetDlgItem(m_hwnd, IDC_CBUTTON_ENABLE_LABEL), show_cbutton);
    ShowWindow(GetDlgItem(m_hwnd, IDC_CBUTTON_ACTION_LABEL), show_cbutton);
    ShowWindow(GetDlgItem(m_hwnd, IDC_CBUTTON_PATH_LABEL), show_cbutton);

    ShowWindow(GetDlgItem(m_hwnd, IDC_CBUTTON1_ENABLE), show_cbutton);
    ShowWindow(GetDlgItem(m_hwnd, IDC_CBUTTON1_ACTION), show_cbutton);
    ShowWindow(GetDlgItem(m_hwnd, IDC_CBUTTON1_PATH), show_cbutton);
    ShowWindow(GetDlgItem(m_hwnd, IDC_CBUTTON1_BROWSE), show_cbutton);
    ShowWindow(GetDlgItem(m_hwnd, IDC_CBUTTON2_ENABLE), show_cbutton);
    ShowWindow(GetDlgItem(m_hwnd, IDC_CBUTTON2_ACTION), show_cbutton);
    ShowWindow(GetDlgItem(m_hwnd, IDC_CBUTTON2_PATH), show_cbutton);
    ShowWindow(GetDlgItem(m_hwnd, IDC_CBUTTON2_BROWSE), show_cbutton);
    ShowWindow(GetDlgItem(m_hwnd, IDC_CBUTTON3_ENABLE), show_cbutton);
    ShowWindow(GetDlgItem(m_hwnd, IDC_CBUTTON3_ACTION), show_cbutton);
    ShowWindow(GetDlgItem(m_hwnd, IDC_CBUTTON3_PATH), show_cbutton);
    ShowWindow(GetDlgItem(m_hwnd, IDC_CBUTTON3_BROWSE), show_cbutton);
    ShowWindow(GetDlgItem(m_hwnd, IDC_CBUTTON4_ENABLE), show_cbutton);
    ShowWindow(GetDlgItem(m_hwnd, IDC_CBUTTON4_ACTION), show_cbutton);
    ShowWindow(GetDlgItem(m_hwnd, IDC_CBUTTON4_PATH), show_cbutton);
    ShowWindow(GetDlgItem(m_hwnd, IDC_CBUTTON4_BROWSE), show_cbutton);
    ShowWindow(GetDlgItem(m_hwnd, IDC_CBUTTON5_ENABLE), show_cbutton);
    ShowWindow(GetDlgItem(m_hwnd, IDC_CBUTTON5_ACTION), show_cbutton);
    ShowWindow(GetDlgItem(m_hwnd, IDC_CBUTTON5_PATH), show_cbutton);
    ShowWindow(GetDlgItem(m_hwnd, IDC_CBUTTON5_BROWSE), show_cbutton);
    ShowWindow(GetDlgItem(m_hwnd, IDC_CBUTTON6_ENABLE), show_cbutton);
    ShowWindow(GetDlgItem(m_hwnd, IDC_CBUTTON6_ACTION), show_cbutton);
    ShowWindow(GetDlgItem(m_hwnd, IDC_CBUTTON6_PATH), show_cbutton);
    ShowWindow(GetDlgItem(m_hwnd, IDC_CBUTTON6_BROWSE), show_cbutton);
    
    // Custom Button icon controls
    ShowWindow(GetDlgItem(m_hwnd, IDC_CBUTTON_ICON_LABEL), show_cbutton);
    ShowWindow(GetDlgItem(m_hwnd, IDC_CBUTTON1_ICON), show_cbutton);
    ShowWindow(GetDlgItem(m_hwnd, IDC_CBUTTON1_ICON_BROWSE), show_cbutton);
    ShowWindow(GetDlgItem(m_hwnd, IDC_CBUTTON1_ICON_CLEAR), show_cbutton);
    ShowWindow(GetDlgItem(m_hwnd, IDC_CBUTTON2_ICON), show_cbutton);
    ShowWindow(GetDlgItem(m_hwnd, IDC_CBUTTON2_ICON_BROWSE), show_cbutton);
    ShowWindow(GetDlgItem(m_hwnd, IDC_CBUTTON2_ICON_CLEAR), show_cbutton);
    ShowWindow(GetDlgItem(m_hwnd, IDC_CBUTTON3_ICON), show_cbutton);
    ShowWindow(GetDlgItem(m_hwnd, IDC_CBUTTON3_ICON_BROWSE), show_cbutton);
    ShowWindow(GetDlgItem(m_hwnd, IDC_CBUTTON3_ICON_CLEAR), show_cbutton);
    ShowWindow(GetDlgItem(m_hwnd, IDC_CBUTTON4_ICON), show_cbutton);
    ShowWindow(GetDlgItem(m_hwnd, IDC_CBUTTON4_ICON_BROWSE), show_cbutton);
    ShowWindow(GetDlgItem(m_hwnd, IDC_CBUTTON4_ICON_CLEAR), show_cbutton);
    ShowWindow(GetDlgItem(m_hwnd, IDC_CBUTTON5_ICON), show_cbutton);
    ShowWindow(GetDlgItem(m_hwnd, IDC_CBUTTON5_ICON_BROWSE), show_cbutton);
    ShowWindow(GetDlgItem(m_hwnd, IDC_CBUTTON5_ICON_CLEAR), show_cbutton);
    ShowWindow(GetDlgItem(m_hwnd, IDC_CBUTTON6_ICON), show_cbutton);
    ShowWindow(GetDlgItem(m_hwnd, IDC_CBUTTON6_ICON_BROWSE), show_cbutton);
    ShowWindow(GetDlgItem(m_hwnd, IDC_CBUTTON6_ICON_CLEAR), show_cbutton);
    // Icon row number labels
    ShowWindow(GetDlgItem(m_hwnd, IDC_CBUTTON1_ICON_LABEL), show_cbutton);
    ShowWindow(GetDlgItem(m_hwnd, IDC_CBUTTON2_ICON_LABEL), show_cbutton);
    ShowWindow(GetDlgItem(m_hwnd, IDC_CBUTTON3_ICON_LABEL), show_cbutton);
    ShowWindow(GetDlgItem(m_hwnd, IDC_CBUTTON4_ICON_LABEL), show_cbutton);
    ShowWindow(GetDlgItem(m_hwnd, IDC_CBUTTON5_ICON_LABEL), show_cbutton);
    ShowWindow(GetDlgItem(m_hwnd, IDC_CBUTTON6_ICON_LABEL), show_cbutton);
    // Tooltip label controls
    ShowWindow(GetDlgItem(m_hwnd, IDC_CBUTTON_LABEL_LABEL), show_cbutton);
    ShowWindow(GetDlgItem(m_hwnd, IDC_CBUTTON1_LABEL), show_cbutton);
    ShowWindow(GetDlgItem(m_hwnd, IDC_CBUTTON2_LABEL), show_cbutton);
    ShowWindow(GetDlgItem(m_hwnd, IDC_CBUTTON3_LABEL), show_cbutton);
    ShowWindow(GetDlgItem(m_hwnd, IDC_CBUTTON4_LABEL), show_cbutton);
    ShowWindow(GetDlgItem(m_hwnd, IDC_CBUTTON5_LABEL), show_cbutton);
    ShowWindow(GetDlgItem(m_hwnd, IDC_CBUTTON6_LABEL), show_cbutton);
    
    // Fonts tab controls
    BOOL show_fonts = (tab == 2) ? SW_SHOW : SW_HIDE;
    ShowWindow(GetDlgItem(m_hwnd, IDC_FONTS_GROUP), show_fonts);
    ShowWindow(GetDlgItem(m_hwnd, IDC_TRACK_FONT_LABEL), show_fonts);
    ShowWindow(GetDlgItem(m_hwnd, IDC_TRACK_FONT_DISPLAY), show_fonts);
    ShowWindow(GetDlgItem(m_hwnd, IDC_TRACK_FONT_SELECT), show_fonts);
    ShowWindow(GetDlgItem(m_hwnd, IDC_ARTIST_FONT_LABEL), show_fonts);
    ShowWindow(GetDlgItem(m_hwnd, IDC_ARTIST_FONT_DISPLAY), show_fonts);
    ShowWindow(GetDlgItem(m_hwnd, IDC_ARTIST_FONT_SELECT), show_fonts);
}

// Helper to update path control states based on action selection
static void update_cbutton_path_state(HWND hwnd, int action_id, int path_id, int browse_id) {
    int action = (int)SendMessage(GetDlgItem(hwnd, action_id), CB_GETCURSEL, 0, 0);
    HWND hPath = GetDlgItem(hwnd, path_id);
    HWND hBrowse = GetDlgItem(hwnd, browse_id);
    
    // 0=None, 1=Open URL, 2=Run Executable, 3=Foobar2k Action
    switch (action) {
        case 0:  // None - disable both
            EnableWindow(hPath, FALSE);
            EnableWindow(hBrowse, FALSE);
            break;
        case 1:  // Open URL - enable path edit, hide browse
            EnableWindow(hPath, TRUE);
            EnableWindow(hBrowse, FALSE);
            break;
        case 2:  // Run Executable - enable path edit, enable browse
            EnableWindow(hPath, TRUE);
            EnableWindow(hBrowse, TRUE);
            break;
        case 3:  // Foobar2k Action - enable path edit, hide browse
            EnableWindow(hPath, TRUE);
            EnableWindow(hBrowse, FALSE);
            break;
    }
}

static void update_all_cbutton_path_states(HWND hwnd) {
    update_cbutton_path_state(hwnd, IDC_CBUTTON1_ACTION, IDC_CBUTTON1_PATH, IDC_CBUTTON1_BROWSE);
    update_cbutton_path_state(hwnd, IDC_CBUTTON2_ACTION, IDC_CBUTTON2_PATH, IDC_CBUTTON2_BROWSE);
    update_cbutton_path_state(hwnd, IDC_CBUTTON3_ACTION, IDC_CBUTTON3_PATH, IDC_CBUTTON3_BROWSE);
    update_cbutton_path_state(hwnd, IDC_CBUTTON4_ACTION, IDC_CBUTTON4_PATH, IDC_CBUTTON4_BROWSE);
    update_cbutton_path_state(hwnd, IDC_CBUTTON5_ACTION, IDC_CBUTTON5_PATH, IDC_CBUTTON5_BROWSE);
    update_cbutton_path_state(hwnd, IDC_CBUTTON6_ACTION, IDC_CBUTTON6_PATH, IDC_CBUTTON6_BROWSE);
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
        
        // Initialize display format edit controls
        uSetDlgItemText(hwnd, IDC_LINE1_FORMAT_EDIT, cfg_nowbar_line1_format);
        uSetDlgItemText(hwnd, IDC_LINE2_FORMAT_EDIT, cfg_nowbar_line2_format);
        
        // Initialize theme mode combobox
        HWND hThemeCombo = GetDlgItem(hwnd, IDC_THEME_MODE_COMBO);
        SendMessage(hThemeCombo, CB_ADDSTRING, 0, (LPARAM)L"Auto");
        SendMessage(hThemeCombo, CB_ADDSTRING, 0, (LPARAM)L"Dark");
        SendMessage(hThemeCombo, CB_ADDSTRING, 0, (LPARAM)L"Light");
        SendMessage(hThemeCombo, CB_ADDSTRING, 0, (LPARAM)L"Custom");
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
        
        // Initialize stop icon visibility combobox
        HWND hStopIconCombo = GetDlgItem(hwnd, IDC_STOP_ICON_COMBO);
        SendMessage(hStopIconCombo, CB_ADDSTRING, 0, (LPARAM)L"Show");
        SendMessage(hStopIconCombo, CB_ADDSTRING, 0, (LPARAM)L"Hidden");
        SendMessage(hStopIconCombo, CB_SETCURSEL, cfg_nowbar_stop_icon_visible ? 0 : 1, 0);
        
        // Initialize miniplayer icon visibility combobox
        HWND hMiniplayerIconCombo = GetDlgItem(hwnd, IDC_MINIPLAYER_ICON_COMBO);
        SendMessage(hMiniplayerIconCombo, CB_ADDSTRING, 0, (LPARAM)L"Show");
        SendMessage(hMiniplayerIconCombo, CB_ADDSTRING, 0, (LPARAM)L"Hidden");
        SendMessage(hMiniplayerIconCombo, CB_SETCURSEL, cfg_nowbar_miniplayer_icon_visible ? 0 : 1, 0);

        // Initialize alternate icons combobox
        HWND hAlternateIconsCombo = GetDlgItem(hwnd, IDC_ALTERNATE_ICONS_COMBO);
        SendMessage(hAlternateIconsCombo, CB_ADDSTRING, 0, (LPARAM)L"Enabled");
        SendMessage(hAlternateIconsCombo, CB_ADDSTRING, 0, (LPARAM)L"Disabled");
        SendMessage(hAlternateIconsCombo, CB_SETCURSEL, cfg_nowbar_alternate_icons ? 0 : 1, 0);

        // Initialize auto-hide C-buttons combobox
        HWND hAutohideCbuttonsCombo = GetDlgItem(hwnd, IDC_AUTOHIDE_CBUTTONS_COMBO);
        SendMessage(hAutohideCbuttonsCombo, CB_ADDSTRING, 0, (LPARAM)L"Yes");
        SendMessage(hAutohideCbuttonsCombo, CB_ADDSTRING, 0, (LPARAM)L"No");
        SendMessage(hAutohideCbuttonsCombo, CB_SETCURSEL, cfg_nowbar_cbutton_autohide ? 0 : 1, 0);

        // Initialize glass effect combobox
        HWND hGlassEffectCombo = GetDlgItem(hwnd, IDC_GLASS_EFFECT_COMBO);
        SendMessage(hGlassEffectCombo, CB_ADDSTRING, 0, (LPARAM)L"Disabled");
        SendMessage(hGlassEffectCombo, CB_ADDSTRING, 0, (LPARAM)L"Enabled");
        SendMessage(hGlassEffectCombo, CB_SETCURSEL, cfg_nowbar_glass_effect ? 1 : 0, 0);

        // Initialize background style combobox
        HWND hBgStyleCombo = GetDlgItem(hwnd, IDC_BACKGROUND_STYLE_COMBO);
        SendMessage(hBgStyleCombo, CB_ADDSTRING, 0, (LPARAM)L"Solid");
        SendMessage(hBgStyleCombo, CB_ADDSTRING, 0, (LPARAM)L"Artwork Colors");
        SendMessage(hBgStyleCombo, CB_ADDSTRING, 0, (LPARAM)L"Blurred Artwork");
        SendMessage(hBgStyleCombo, CB_SETCURSEL, cfg_nowbar_background_style, 0);

        // Initialize font displays
        p_this->update_font_displays();

        // Initialize Profile combobox for Custom Button tab
        {
            HWND hProfileCombo = GetDlgItem(hwnd, IDC_PROFILE_COMBO);
            auto profiles = get_all_profiles();
            pfc::string8 current_name = get_current_profile_name();
            int sel_index = 0;
            for (size_t i = 0; i < profiles.size(); i++) {
                pfc::stringcvt::string_wide_from_utf8 wide_name(profiles[i].name.c_str());
                SendMessage(hProfileCombo, CB_ADDSTRING, 0, (LPARAM)wide_name.get_ptr());
                if (stricmp_utf8(profiles[i].name.c_str(), current_name.c_str()) == 0) {
                    sel_index = (int)i;
                }
            }
            SendMessage(hProfileCombo, CB_SETCURSEL, sel_index, 0);
        }

        // Initialize Custom Button tab controls
        // Populate action comboboxes with choices
        int cbutton_action_combos[] = {IDC_CBUTTON1_ACTION, IDC_CBUTTON2_ACTION, IDC_CBUTTON3_ACTION, IDC_CBUTTON4_ACTION, IDC_CBUTTON5_ACTION, IDC_CBUTTON6_ACTION};

        for (int id : cbutton_action_combos) {
            HWND hCombo = GetDlgItem(hwnd, id);
            SendMessage(hCombo, CB_ADDSTRING, 0, (LPARAM)L"None");
            SendMessage(hCombo, CB_ADDSTRING, 0, (LPARAM)L"Open URL");
            SendMessage(hCombo, CB_ADDSTRING, 0, (LPARAM)L"Run Executable");
            SendMessage(hCombo, CB_ADDSTRING, 0, (LPARAM)L"Foobar2k Action");
        }
        
        // Set checkbox and combobox states from config
        CheckDlgButton(hwnd, IDC_CBUTTON1_ENABLE, cfg_cbutton1_enabled ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hwnd, IDC_CBUTTON2_ENABLE, cfg_cbutton2_enabled ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hwnd, IDC_CBUTTON3_ENABLE, cfg_cbutton3_enabled ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hwnd, IDC_CBUTTON4_ENABLE, cfg_cbutton4_enabled ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hwnd, IDC_CBUTTON5_ENABLE, cfg_cbutton5_enabled ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hwnd, IDC_CBUTTON6_ENABLE, cfg_cbutton6_enabled ? BST_CHECKED : BST_UNCHECKED);
        SendMessage(GetDlgItem(hwnd, IDC_CBUTTON1_ACTION), CB_SETCURSEL, cfg_cbutton1_action, 0);
        SendMessage(GetDlgItem(hwnd, IDC_CBUTTON2_ACTION), CB_SETCURSEL, cfg_cbutton2_action, 0);
        SendMessage(GetDlgItem(hwnd, IDC_CBUTTON3_ACTION), CB_SETCURSEL, cfg_cbutton3_action, 0);
        SendMessage(GetDlgItem(hwnd, IDC_CBUTTON4_ACTION), CB_SETCURSEL, cfg_cbutton4_action, 0);
        SendMessage(GetDlgItem(hwnd, IDC_CBUTTON5_ACTION), CB_SETCURSEL, cfg_cbutton5_action, 0);
        SendMessage(GetDlgItem(hwnd, IDC_CBUTTON6_ACTION), CB_SETCURSEL, cfg_cbutton6_action, 0);
        
        // Initialize path edit boxes
        uSetDlgItemText(hwnd, IDC_CBUTTON1_PATH, cfg_cbutton1_path);
        uSetDlgItemText(hwnd, IDC_CBUTTON2_PATH, cfg_cbutton2_path);
        uSetDlgItemText(hwnd, IDC_CBUTTON3_PATH, cfg_cbutton3_path);
        uSetDlgItemText(hwnd, IDC_CBUTTON4_PATH, cfg_cbutton4_path);
        uSetDlgItemText(hwnd, IDC_CBUTTON5_PATH, cfg_cbutton5_path);
        uSetDlgItemText(hwnd, IDC_CBUTTON6_PATH, cfg_cbutton6_path);
        
        // Initialize icon path edit boxes
        uSetDlgItemText(hwnd, IDC_CBUTTON1_ICON, cfg_cbutton1_icon);
        uSetDlgItemText(hwnd, IDC_CBUTTON2_ICON, cfg_cbutton2_icon);
        uSetDlgItemText(hwnd, IDC_CBUTTON3_ICON, cfg_cbutton3_icon);
        uSetDlgItemText(hwnd, IDC_CBUTTON4_ICON, cfg_cbutton4_icon);
        uSetDlgItemText(hwnd, IDC_CBUTTON5_ICON, cfg_cbutton5_icon);
        uSetDlgItemText(hwnd, IDC_CBUTTON6_ICON, cfg_cbutton6_icon);
        
        // Initialize tooltip label edit boxes
        uSetDlgItemText(hwnd, IDC_CBUTTON1_LABEL, cfg_cbutton1_label);
        uSetDlgItemText(hwnd, IDC_CBUTTON2_LABEL, cfg_cbutton2_label);
        uSetDlgItemText(hwnd, IDC_CBUTTON3_LABEL, cfg_cbutton3_label);
        uSetDlgItemText(hwnd, IDC_CBUTTON4_LABEL, cfg_cbutton4_label);
        uSetDlgItemText(hwnd, IDC_CBUTTON5_LABEL, cfg_cbutton5_label);
        uSetDlgItemText(hwnd, IDC_CBUTTON6_LABEL, cfg_cbutton6_label);
        
        // Update path control states based on action selection
        update_all_cbutton_path_states(hwnd);
        
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
        case IDC_STOP_ICON_COMBO:
        case IDC_MINIPLAYER_ICON_COMBO:
        case IDC_ALTERNATE_ICONS_COMBO:
        case IDC_AUTOHIDE_CBUTTONS_COMBO:
        case IDC_GLASS_EFFECT_COMBO:
        case IDC_BACKGROUND_STYLE_COMBO:
            if (HIWORD(wp) == CBN_SELCHANGE) {
                p_this->on_changed();
            }
            break;

        case IDC_LINE1_FORMAT_EDIT:
        case IDC_LINE2_FORMAT_EDIT:
            if (HIWORD(wp) == EN_CHANGE) {
                p_this->on_changed();
            }
            break;

        case IDC_PROFILE_COMBO:
            if (HIWORD(wp) == CBN_SELCHANGE) {

                // Save current profile before switching
                auto profiles = get_all_profiles();
                pfc::string8 current_name = get_current_profile_name();
                CButtonProfile* current_profile = find_profile(profiles, current_name.c_str());
                if (current_profile) {
                    save_config_to_profile(*current_profile);
                    save_all_profiles(profiles);
                }
                
                // Load newly selected profile
                int sel = (int)SendMessage(GetDlgItem(hwnd, IDC_PROFILE_COMBO), CB_GETCURSEL, 0, 0);
                if (sel >= 0 && sel < (int)profiles.size()) {
                    load_profile_to_config(profiles[sel]);
                    set_current_profile_name(profiles[sel].name.c_str());
                    
                    // Update UI controls with new profile settings
                    CheckDlgButton(hwnd, IDC_CBUTTON1_ENABLE, cfg_cbutton1_enabled ? BST_CHECKED : BST_UNCHECKED);
                    CheckDlgButton(hwnd, IDC_CBUTTON2_ENABLE, cfg_cbutton2_enabled ? BST_CHECKED : BST_UNCHECKED);
                    CheckDlgButton(hwnd, IDC_CBUTTON3_ENABLE, cfg_cbutton3_enabled ? BST_CHECKED : BST_UNCHECKED);
                    CheckDlgButton(hwnd, IDC_CBUTTON4_ENABLE, cfg_cbutton4_enabled ? BST_CHECKED : BST_UNCHECKED);
                    CheckDlgButton(hwnd, IDC_CBUTTON5_ENABLE, cfg_cbutton5_enabled ? BST_CHECKED : BST_UNCHECKED);
                    CheckDlgButton(hwnd, IDC_CBUTTON6_ENABLE, cfg_cbutton6_enabled ? BST_CHECKED : BST_UNCHECKED);
                    SendMessage(GetDlgItem(hwnd, IDC_CBUTTON1_ACTION), CB_SETCURSEL, cfg_cbutton1_action, 0);
                    SendMessage(GetDlgItem(hwnd, IDC_CBUTTON2_ACTION), CB_SETCURSEL, cfg_cbutton2_action, 0);
                    SendMessage(GetDlgItem(hwnd, IDC_CBUTTON3_ACTION), CB_SETCURSEL, cfg_cbutton3_action, 0);
                    SendMessage(GetDlgItem(hwnd, IDC_CBUTTON4_ACTION), CB_SETCURSEL, cfg_cbutton4_action, 0);
                    SendMessage(GetDlgItem(hwnd, IDC_CBUTTON5_ACTION), CB_SETCURSEL, cfg_cbutton5_action, 0);
                    SendMessage(GetDlgItem(hwnd, IDC_CBUTTON6_ACTION), CB_SETCURSEL, cfg_cbutton6_action, 0);
                    uSetDlgItemText(hwnd, IDC_CBUTTON1_PATH, cfg_cbutton1_path);
                    uSetDlgItemText(hwnd, IDC_CBUTTON2_PATH, cfg_cbutton2_path);
                    uSetDlgItemText(hwnd, IDC_CBUTTON3_PATH, cfg_cbutton3_path);
                    uSetDlgItemText(hwnd, IDC_CBUTTON4_PATH, cfg_cbutton4_path);
                    uSetDlgItemText(hwnd, IDC_CBUTTON5_PATH, cfg_cbutton5_path);
                    uSetDlgItemText(hwnd, IDC_CBUTTON6_PATH, cfg_cbutton6_path);
                    uSetDlgItemText(hwnd, IDC_CBUTTON1_ICON, cfg_cbutton1_icon);
                    uSetDlgItemText(hwnd, IDC_CBUTTON2_ICON, cfg_cbutton2_icon);
                    uSetDlgItemText(hwnd, IDC_CBUTTON3_ICON, cfg_cbutton3_icon);
                    uSetDlgItemText(hwnd, IDC_CBUTTON4_ICON, cfg_cbutton4_icon);
                    uSetDlgItemText(hwnd, IDC_CBUTTON5_ICON, cfg_cbutton5_icon);
                    uSetDlgItemText(hwnd, IDC_CBUTTON6_ICON, cfg_cbutton6_icon);
                    uSetDlgItemText(hwnd, IDC_CBUTTON1_LABEL, cfg_cbutton1_label);
                    uSetDlgItemText(hwnd, IDC_CBUTTON2_LABEL, cfg_cbutton2_label);
                    uSetDlgItemText(hwnd, IDC_CBUTTON3_LABEL, cfg_cbutton3_label);
                    uSetDlgItemText(hwnd, IDC_CBUTTON4_LABEL, cfg_cbutton4_label);
                    uSetDlgItemText(hwnd, IDC_CBUTTON5_LABEL, cfg_cbutton5_label);
                    uSetDlgItemText(hwnd, IDC_CBUTTON6_LABEL, cfg_cbutton6_label);
                    update_all_cbutton_path_states(hwnd);
                    p_this->on_changed();
                }
            }
            break;




        case IDC_PROFILE_MENU_BTN:
            if (HIWORD(wp) == BN_CLICKED) {
                // Get profiles first to check count for menu state
                auto profiles = get_all_profiles();
                pfc::string8 current_name = get_current_profile_name();
                
                // Create and show popup menu
                HMENU hMenu = CreatePopupMenu();
                AppendMenu(hMenu, MF_STRING, 1, L"New");
                AppendMenu(hMenu, MF_STRING, 2, L"Rename");
                // Gray out Delete if only one profile exists
                AppendMenu(hMenu, (profiles.size() <= 1) ? (MF_STRING | MF_GRAYED) : MF_STRING, 3, L"Delete");
                AppendMenu(hMenu, MF_SEPARATOR, 0, nullptr);
                AppendMenu(hMenu, MF_STRING, 4, L"Export...");
                AppendMenu(hMenu, MF_STRING, 5, L"Import...");
                
                // Get button position for menu placement
                RECT rc;
                GetWindowRect(GetDlgItem(hwnd, IDC_PROFILE_MENU_BTN), &rc);
                
                int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_LEFTBUTTON, rc.left, rc.bottom, 0, hwnd, nullptr);
                DestroyMenu(hMenu);
                
                switch (cmd) {
                case 1: { // New - show input dialog for profile name
                    // Generate a suggested unique name
                    pfc::string8 base_name = "Profile ";
                    int num = (int)profiles.size() + 1;
                    pfc::string8 suggested_name;
                    suggested_name << base_name << num;
                    while (find_profile(profiles, suggested_name.c_str())) {
                        num++;
                        suggested_name.reset();
                        suggested_name << base_name << num;
                    }
                    
                    // Show input dialog
                    pfc::stringcvt::string_wide_from_utf8 suggested_wide(suggested_name.c_str());
                    pfc::string8 new_name;
                    if (show_input_dialog(hwnd, L"New Profile", L"Enter profile name:", suggested_wide.get_ptr(), new_name)) {
                        // Check if name already exists
                        if (find_profile(profiles, new_name.c_str())) {
                            MessageBoxW(hwnd, L"A profile with this name already exists.", L"Error", MB_OK | MB_ICONWARNING);
                            break;
                        }
                        
                        // Save current settings to current profile first
                        CButtonProfile* current_profile = find_profile(profiles, current_name.c_str());
                        if (current_profile) {
                            save_config_to_profile(*current_profile);
                        }
                        
                        // Create new profile with current settings
                        CButtonProfile new_profile;
                        new_profile.name = new_name;
                        save_config_to_profile(new_profile);
                        profiles.push_back(new_profile);
                        save_all_profiles(profiles);
                        set_current_profile_name(new_name.c_str());
                        
                        // Update combobox
                        HWND hCombo = GetDlgItem(hwnd, IDC_PROFILE_COMBO);
                        pfc::stringcvt::string_wide_from_utf8 wide_name(new_name.c_str());
                        int new_idx = (int)SendMessage(hCombo, CB_ADDSTRING, 0, (LPARAM)wide_name.get_ptr());
                        SendMessage(hCombo, CB_SETCURSEL, new_idx, 0);
                        p_this->on_changed();
                    }
                    break;
                }

                case 2: { // Rename - show input dialog at cursor position
                    CButtonProfile* prof = find_profile(profiles, current_name.c_str());
                    if (prof) {
                        pfc::stringcvt::string_wide_from_utf8 current_wide(current_name.c_str());
                        pfc::string8 new_name;
                        if (show_input_dialog(hwnd, L"Rename Profile", L"Enter new profile name:", current_wide.get_ptr(), new_name)) {
                            // Check if name already exists (and is different from current)
                            if (stricmp_utf8(new_name.c_str(), current_name.c_str()) != 0 &&
                                find_profile(profiles, new_name.c_str())) {
                                MessageBoxW(hwnd, L"A profile with this name already exists.", L"Error", MB_OK | MB_ICONWARNING);
                            } else if (!new_name.is_empty()) {
                                prof->name = new_name;
                                save_all_profiles(profiles);
                                set_current_profile_name(new_name.c_str());
                                
                                // Rebuild combobox
                                HWND hCombo = GetDlgItem(hwnd, IDC_PROFILE_COMBO);
                                SendMessage(hCombo, CB_RESETCONTENT, 0, 0);
                                for (size_t i = 0; i < profiles.size(); i++) {
                                    pfc::stringcvt::string_wide_from_utf8 wide_name(profiles[i].name.c_str());
                                    SendMessage(hCombo, CB_ADDSTRING, 0, (LPARAM)wide_name.get_ptr());
                                    if (stricmp_utf8(profiles[i].name.c_str(), new_name.c_str()) == 0) {
                                        SendMessage(hCombo, CB_SETCURSEL, i, 0);
                                    }
                                }
                                // Update edit text for CBS_DROPDOWN
                                pfc::stringcvt::string_wide_from_utf8 new_wide(new_name.c_str());
                                SetWindowTextW(hCombo, new_wide.get_ptr());
                                p_this->on_changed();
                            }
                        }
                    }
                    break;
                }




                case 3: { // Delete
                    // Show confirmation dialog
                    pfc::stringcvt::string_wide_from_utf8 current_wide(current_name.c_str());
                    std::wstring confirm_msg = L"Are you sure you want to delete the profile \"";
                    confirm_msg += current_wide.get_ptr();
                    confirm_msg += L"\"?";
                    
                    if (MessageBoxW(hwnd, confirm_msg.c_str(), L"Delete Profile", MB_YESNO | MB_ICONQUESTION) != IDYES) {
                        break;  // User cancelled
                    }
                    
                    // Find and remove current profile

                    for (auto it = profiles.begin(); it != profiles.end(); ++it) {
                        if (stricmp_utf8(it->name.c_str(), current_name.c_str()) == 0) {
                            profiles.erase(it);
                            break;
                        }
                    }
                    save_all_profiles(profiles);
                    
                    // Switch to first remaining profile
                    if (!profiles.empty()) {
                        load_profile_to_config(profiles[0]);
                        set_current_profile_name(profiles[0].name.c_str());
                    }
                    
                    // Rebuild combobox
                    HWND hCombo = GetDlgItem(hwnd, IDC_PROFILE_COMBO);
                    SendMessage(hCombo, CB_RESETCONTENT, 0, 0);
                    for (size_t i = 0; i < profiles.size(); i++) {
                        pfc::stringcvt::string_wide_from_utf8 wide_name(profiles[i].name.c_str());
                        SendMessage(hCombo, CB_ADDSTRING, 0, (LPARAM)wide_name.get_ptr());
                    }
                    SendMessage(hCombo, CB_SETCURSEL, 0, 0);
                    
                    // Update UI with new profile settings
                    CheckDlgButton(hwnd, IDC_CBUTTON1_ENABLE, cfg_cbutton1_enabled ? BST_CHECKED : BST_UNCHECKED);
                    CheckDlgButton(hwnd, IDC_CBUTTON2_ENABLE, cfg_cbutton2_enabled ? BST_CHECKED : BST_UNCHECKED);
                    CheckDlgButton(hwnd, IDC_CBUTTON3_ENABLE, cfg_cbutton3_enabled ? BST_CHECKED : BST_UNCHECKED);
                    CheckDlgButton(hwnd, IDC_CBUTTON4_ENABLE, cfg_cbutton4_enabled ? BST_CHECKED : BST_UNCHECKED);
                    CheckDlgButton(hwnd, IDC_CBUTTON5_ENABLE, cfg_cbutton5_enabled ? BST_CHECKED : BST_UNCHECKED);
                    CheckDlgButton(hwnd, IDC_CBUTTON6_ENABLE, cfg_cbutton6_enabled ? BST_CHECKED : BST_UNCHECKED);
                    SendMessage(GetDlgItem(hwnd, IDC_CBUTTON1_ACTION), CB_SETCURSEL, cfg_cbutton1_action, 0);
                    SendMessage(GetDlgItem(hwnd, IDC_CBUTTON2_ACTION), CB_SETCURSEL, cfg_cbutton2_action, 0);
                    SendMessage(GetDlgItem(hwnd, IDC_CBUTTON3_ACTION), CB_SETCURSEL, cfg_cbutton3_action, 0);
                    SendMessage(GetDlgItem(hwnd, IDC_CBUTTON4_ACTION), CB_SETCURSEL, cfg_cbutton4_action, 0);
                    SendMessage(GetDlgItem(hwnd, IDC_CBUTTON5_ACTION), CB_SETCURSEL, cfg_cbutton5_action, 0);
                    SendMessage(GetDlgItem(hwnd, IDC_CBUTTON6_ACTION), CB_SETCURSEL, cfg_cbutton6_action, 0);
                    uSetDlgItemText(hwnd, IDC_CBUTTON1_PATH, cfg_cbutton1_path);
                    uSetDlgItemText(hwnd, IDC_CBUTTON2_PATH, cfg_cbutton2_path);
                    uSetDlgItemText(hwnd, IDC_CBUTTON3_PATH, cfg_cbutton3_path);
                    uSetDlgItemText(hwnd, IDC_CBUTTON4_PATH, cfg_cbutton4_path);
                    uSetDlgItemText(hwnd, IDC_CBUTTON5_PATH, cfg_cbutton5_path);
                    uSetDlgItemText(hwnd, IDC_CBUTTON6_PATH, cfg_cbutton6_path);
                    uSetDlgItemText(hwnd, IDC_CBUTTON1_ICON, cfg_cbutton1_icon);
                    uSetDlgItemText(hwnd, IDC_CBUTTON2_ICON, cfg_cbutton2_icon);
                    uSetDlgItemText(hwnd, IDC_CBUTTON3_ICON, cfg_cbutton3_icon);
                    uSetDlgItemText(hwnd, IDC_CBUTTON4_ICON, cfg_cbutton4_icon);
                    uSetDlgItemText(hwnd, IDC_CBUTTON5_ICON, cfg_cbutton5_icon);
                    uSetDlgItemText(hwnd, IDC_CBUTTON6_ICON, cfg_cbutton6_icon);
                    uSetDlgItemText(hwnd, IDC_CBUTTON1_LABEL, cfg_cbutton1_label);
                    uSetDlgItemText(hwnd, IDC_CBUTTON2_LABEL, cfg_cbutton2_label);
                    uSetDlgItemText(hwnd, IDC_CBUTTON3_LABEL, cfg_cbutton3_label);
                    uSetDlgItemText(hwnd, IDC_CBUTTON4_LABEL, cfg_cbutton4_label);
                    uSetDlgItemText(hwnd, IDC_CBUTTON5_LABEL, cfg_cbutton5_label);
                    uSetDlgItemText(hwnd, IDC_CBUTTON6_LABEL, cfg_cbutton6_label);
                    update_all_cbutton_path_states(hwnd);
                    p_this->on_changed();
                    break;
                }

                case 4: { // Export
                    CButtonProfile* prof = find_profile(profiles, current_name.c_str());
                    if (prof) {
                        wchar_t filename[MAX_PATH] = L"";
                        pfc::stringcvt::string_wide_from_utf8 default_name(current_name.c_str());
                        wcsncpy_s(filename, default_name.get_ptr(), MAX_PATH - 1);
                        wcscat_s(filename, L".json");
                        
                        OPENFILENAMEW ofn = {};
                        ofn.lStructSize = sizeof(ofn);
                        ofn.hwndOwner = hwnd;
                        ofn.lpstrFilter = L"JSON Files (*.json)\0*.json\0All Files (*.*)\0*.*\0";
                        ofn.lpstrFile = filename;
                        ofn.nMaxFile = MAX_PATH;
                        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
                        ofn.lpstrDefExt = L"json";
                        
                        if (GetSaveFileNameW(&ofn)) {
                            if (export_profile_to_file(*prof, filename)) {
                                MessageBoxW(hwnd, L"Profile exported successfully.", L"Export", MB_OK | MB_ICONINFORMATION);
                            } else {
                                MessageBoxW(hwnd, L"Failed to export profile.", L"Error", MB_OK | MB_ICONERROR);
                            }
                        }
                    }
                    break;
                }
                case 5: { // Import
                    wchar_t filename[MAX_PATH] = L"";
                    OPENFILENAMEW ofn = {};
                    ofn.lStructSize = sizeof(ofn);
                    ofn.hwndOwner = hwnd;
                    ofn.lpstrFilter = L"JSON Files (*.json)\0*.json\0All Files (*.*)\0*.*\0";
                    ofn.lpstrFile = filename;
                    ofn.nMaxFile = MAX_PATH;
                    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
                    
                    if (GetOpenFileNameW(&ofn)) {
                        CButtonProfile imported;
                        if (import_profile_from_file(filename, imported)) {
                            // Check for name collision
                            pfc::string8 import_name = imported.name;
                            int suffix = 1;
                            while (find_profile(profiles, import_name.c_str())) {
                                import_name.reset();
                                import_name << imported.name << " (" << suffix << ")";
                                suffix++;
                            }
                            imported.name = import_name;
                            
                            // Save current profile first
                            CButtonProfile* current_profile = find_profile(profiles, current_name.c_str());
                            if (current_profile) {
                                save_config_to_profile(*current_profile);
                            }
                            
                            // Add imported profile
                            profiles.push_back(imported);
                            save_all_profiles(profiles);
                            
                            // Load imported profile
                            load_profile_to_config(imported);
                            set_current_profile_name(imported.name.c_str());
                            
                            // Rebuild combobox
                            HWND hCombo = GetDlgItem(hwnd, IDC_PROFILE_COMBO);
                            SendMessage(hCombo, CB_RESETCONTENT, 0, 0);
                            int new_sel = 0;
                            for (size_t i = 0; i < profiles.size(); i++) {
                                pfc::stringcvt::string_wide_from_utf8 wide_name(profiles[i].name.c_str());
                                SendMessage(hCombo, CB_ADDSTRING, 0, (LPARAM)wide_name.get_ptr());
                                if (stricmp_utf8(profiles[i].name.c_str(), imported.name.c_str()) == 0) {
                                    new_sel = (int)i;
                                }
                            }
                            SendMessage(hCombo, CB_SETCURSEL, new_sel, 0);
                            
                            // Update UI
                            CheckDlgButton(hwnd, IDC_CBUTTON1_ENABLE, cfg_cbutton1_enabled ? BST_CHECKED : BST_UNCHECKED);
                            CheckDlgButton(hwnd, IDC_CBUTTON2_ENABLE, cfg_cbutton2_enabled ? BST_CHECKED : BST_UNCHECKED);
                            CheckDlgButton(hwnd, IDC_CBUTTON3_ENABLE, cfg_cbutton3_enabled ? BST_CHECKED : BST_UNCHECKED);
                            CheckDlgButton(hwnd, IDC_CBUTTON4_ENABLE, cfg_cbutton4_enabled ? BST_CHECKED : BST_UNCHECKED);
                            CheckDlgButton(hwnd, IDC_CBUTTON5_ENABLE, cfg_cbutton5_enabled ? BST_CHECKED : BST_UNCHECKED);
                            CheckDlgButton(hwnd, IDC_CBUTTON6_ENABLE, cfg_cbutton6_enabled ? BST_CHECKED : BST_UNCHECKED);
                            SendMessage(GetDlgItem(hwnd, IDC_CBUTTON1_ACTION), CB_SETCURSEL, cfg_cbutton1_action, 0);
                            SendMessage(GetDlgItem(hwnd, IDC_CBUTTON2_ACTION), CB_SETCURSEL, cfg_cbutton2_action, 0);
                            SendMessage(GetDlgItem(hwnd, IDC_CBUTTON3_ACTION), CB_SETCURSEL, cfg_cbutton3_action, 0);
                            SendMessage(GetDlgItem(hwnd, IDC_CBUTTON4_ACTION), CB_SETCURSEL, cfg_cbutton4_action, 0);
                            SendMessage(GetDlgItem(hwnd, IDC_CBUTTON5_ACTION), CB_SETCURSEL, cfg_cbutton5_action, 0);
                            SendMessage(GetDlgItem(hwnd, IDC_CBUTTON6_ACTION), CB_SETCURSEL, cfg_cbutton6_action, 0);
                            uSetDlgItemText(hwnd, IDC_CBUTTON1_PATH, cfg_cbutton1_path);
                            uSetDlgItemText(hwnd, IDC_CBUTTON2_PATH, cfg_cbutton2_path);
                            uSetDlgItemText(hwnd, IDC_CBUTTON3_PATH, cfg_cbutton3_path);
                            uSetDlgItemText(hwnd, IDC_CBUTTON4_PATH, cfg_cbutton4_path);
                            uSetDlgItemText(hwnd, IDC_CBUTTON5_PATH, cfg_cbutton5_path);
                            uSetDlgItemText(hwnd, IDC_CBUTTON6_PATH, cfg_cbutton6_path);
                            uSetDlgItemText(hwnd, IDC_CBUTTON1_ICON, cfg_cbutton1_icon);
                            uSetDlgItemText(hwnd, IDC_CBUTTON2_ICON, cfg_cbutton2_icon);
                            uSetDlgItemText(hwnd, IDC_CBUTTON3_ICON, cfg_cbutton3_icon);
                            uSetDlgItemText(hwnd, IDC_CBUTTON4_ICON, cfg_cbutton4_icon);
                            uSetDlgItemText(hwnd, IDC_CBUTTON5_ICON, cfg_cbutton5_icon);
                            uSetDlgItemText(hwnd, IDC_CBUTTON6_ICON, cfg_cbutton6_icon);
                            uSetDlgItemText(hwnd, IDC_CBUTTON1_LABEL, cfg_cbutton1_label);
                            uSetDlgItemText(hwnd, IDC_CBUTTON2_LABEL, cfg_cbutton2_label);
                            uSetDlgItemText(hwnd, IDC_CBUTTON3_LABEL, cfg_cbutton3_label);
                            uSetDlgItemText(hwnd, IDC_CBUTTON4_LABEL, cfg_cbutton4_label);
                            uSetDlgItemText(hwnd, IDC_CBUTTON5_LABEL, cfg_cbutton5_label);
                            uSetDlgItemText(hwnd, IDC_CBUTTON6_LABEL, cfg_cbutton6_label);
                            update_all_cbutton_path_states(hwnd);
                            p_this->on_changed();
                            
                            MessageBoxW(hwnd, L"Profile imported successfully.", L"Import", MB_OK | MB_ICONINFORMATION);
                        } else {
                            MessageBoxW(hwnd, L"Failed to import profile. Invalid file format.", L"Error", MB_OK | MB_ICONERROR);
                        }
                    }
                    break;
                }
                }
            }
            break;

        case IDC_CBUTTON1_ACTION:
        case IDC_CBUTTON2_ACTION:
        case IDC_CBUTTON3_ACTION:
        case IDC_CBUTTON4_ACTION:
        case IDC_CBUTTON5_ACTION:
        case IDC_CBUTTON6_ACTION:
            if (HIWORD(wp) == CBN_SELCHANGE) {
                p_this->on_changed();
                // Update path control states when action changes
                update_all_cbutton_path_states(hwnd);
            }
            break;

        case IDC_CBUTTON1_ENABLE:
        case IDC_CBUTTON2_ENABLE:
        case IDC_CBUTTON3_ENABLE:
        case IDC_CBUTTON4_ENABLE:
        case IDC_CBUTTON5_ENABLE:
        case IDC_CBUTTON6_ENABLE:
            if (HIWORD(wp) == BN_CLICKED) {
                p_this->on_changed();
            }
            break;

        case IDC_CBUTTON1_PATH:
        case IDC_CBUTTON2_PATH:
        case IDC_CBUTTON3_PATH:
        case IDC_CBUTTON4_PATH:
        case IDC_CBUTTON5_PATH:
        case IDC_CBUTTON6_PATH:
            if (HIWORD(wp) == EN_CHANGE) {
                p_this->on_changed();
            }
            break;

        case IDC_CBUTTON1_BROWSE:
        case IDC_CBUTTON2_BROWSE:
        case IDC_CBUTTON3_BROWSE:
        case IDC_CBUTTON4_BROWSE:
        case IDC_CBUTTON5_BROWSE:
        case IDC_CBUTTON6_BROWSE:
            if (HIWORD(wp) == BN_CLICKED) {
                int path_id = 0;
                switch (LOWORD(wp)) {
                    case IDC_CBUTTON1_BROWSE: path_id = IDC_CBUTTON1_PATH; break;
                    case IDC_CBUTTON2_BROWSE: path_id = IDC_CBUTTON2_PATH; break;
                    case IDC_CBUTTON3_BROWSE: path_id = IDC_CBUTTON3_PATH; break;
                    case IDC_CBUTTON4_BROWSE: path_id = IDC_CBUTTON4_PATH; break;
                    case IDC_CBUTTON5_BROWSE: path_id = IDC_CBUTTON5_PATH; break;
                    case IDC_CBUTTON6_BROWSE: path_id = IDC_CBUTTON6_PATH; break;
                }
                wchar_t filename[MAX_PATH] = L"";
                OPENFILENAMEW ofn = {};
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = hwnd;
                ofn.lpstrFilter = L"Executable Files (*.exe)\0*.exe\0All Files (*.*)\0*.*\0";
                ofn.lpstrFile = filename;
                ofn.nMaxFile = MAX_PATH;
                ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
                if (GetOpenFileNameW(&ofn)) {
                    SetDlgItemTextW(hwnd, path_id, filename);
                    p_this->on_changed();
                }
            }
            break;

        // Icon path edit changes
        case IDC_CBUTTON1_ICON:
        case IDC_CBUTTON2_ICON:
        case IDC_CBUTTON3_ICON:
        case IDC_CBUTTON4_ICON:
        case IDC_CBUTTON5_ICON:
        case IDC_CBUTTON6_ICON:
            if (HIWORD(wp) == EN_CHANGE) {
                p_this->on_changed();
            }
            break;

        // Tooltip label edit changes
        case IDC_CBUTTON1_LABEL:
        case IDC_CBUTTON2_LABEL:
        case IDC_CBUTTON3_LABEL:
        case IDC_CBUTTON4_LABEL:
        case IDC_CBUTTON5_LABEL:
        case IDC_CBUTTON6_LABEL:
            if (HIWORD(wp) == EN_CHANGE) {
                p_this->on_changed();
            }
            break;

        // Icon browse buttons
        case IDC_CBUTTON1_ICON_BROWSE:
        case IDC_CBUTTON2_ICON_BROWSE:
        case IDC_CBUTTON3_ICON_BROWSE:
        case IDC_CBUTTON4_ICON_BROWSE:
        case IDC_CBUTTON5_ICON_BROWSE:
        case IDC_CBUTTON6_ICON_BROWSE:
            if (HIWORD(wp) == BN_CLICKED) {
                int icon_id = 0;
                switch (LOWORD(wp)) {
                    case IDC_CBUTTON1_ICON_BROWSE: icon_id = IDC_CBUTTON1_ICON; break;
                    case IDC_CBUTTON2_ICON_BROWSE: icon_id = IDC_CBUTTON2_ICON; break;
                    case IDC_CBUTTON3_ICON_BROWSE: icon_id = IDC_CBUTTON3_ICON; break;
                    case IDC_CBUTTON4_ICON_BROWSE: icon_id = IDC_CBUTTON4_ICON; break;
                    case IDC_CBUTTON5_ICON_BROWSE: icon_id = IDC_CBUTTON5_ICON; break;
                    case IDC_CBUTTON6_ICON_BROWSE: icon_id = IDC_CBUTTON6_ICON; break;
                }
                wchar_t filename[MAX_PATH] = L"";
                OPENFILENAMEW ofn = {};
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = hwnd;
                ofn.lpstrFilter = L"Image Files (*.png;*.ico)\0*.png;*.ico\0PNG Files (*.png)\0*.png\0ICO Files (*.ico)\0*.ico\0All Files (*.*)\0*.*\0";
                ofn.lpstrFile = filename;
                ofn.nMaxFile = MAX_PATH;
                ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
                if (GetOpenFileNameW(&ofn)) {
                    SetDlgItemTextW(hwnd, icon_id, filename);
                    p_this->on_changed();
                }
            }
            break;

        // Icon clear buttons
        case IDC_CBUTTON1_ICON_CLEAR:
        case IDC_CBUTTON2_ICON_CLEAR:
        case IDC_CBUTTON3_ICON_CLEAR:
        case IDC_CBUTTON4_ICON_CLEAR:
        case IDC_CBUTTON5_ICON_CLEAR:
        case IDC_CBUTTON6_ICON_CLEAR:
            if (HIWORD(wp) == BN_CLICKED) {
                int icon_id = 0;
                switch (LOWORD(wp)) {
                    case IDC_CBUTTON1_ICON_CLEAR: icon_id = IDC_CBUTTON1_ICON; break;
                    case IDC_CBUTTON2_ICON_CLEAR: icon_id = IDC_CBUTTON2_ICON; break;
                    case IDC_CBUTTON3_ICON_CLEAR: icon_id = IDC_CBUTTON3_ICON; break;
                    case IDC_CBUTTON4_ICON_CLEAR: icon_id = IDC_CBUTTON4_ICON; break;
                    case IDC_CBUTTON5_ICON_CLEAR: icon_id = IDC_CBUTTON5_ICON; break;
                    case IDC_CBUTTON6_ICON_CLEAR: icon_id = IDC_CBUTTON6_ICON; break;
                }
                SetDlgItemTextW(hwnd, icon_id, L"");
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
        // Save display format strings
        pfc::string8 format_str;
        uGetDlgItemText(m_hwnd, IDC_LINE1_FORMAT_EDIT, format_str);
        cfg_nowbar_line1_format = format_str;
        uGetDlgItemText(m_hwnd, IDC_LINE2_FORMAT_EDIT, format_str);
        cfg_nowbar_line2_format = format_str;
        
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
        
        // Save stop icon visibility (0=Show, 1=Hidden in combobox -> config 1=Show, 0=Hidden)
        int stopIconSel = (int)SendMessage(GetDlgItem(m_hwnd, IDC_STOP_ICON_COMBO), CB_GETCURSEL, 0, 0);
        cfg_nowbar_stop_icon_visible = (stopIconSel == 0) ? 1 : 0;
        
        // Save miniplayer icon visibility (0=Show, 1=Hidden in combobox -> config 1=Show, 0=Hidden)
        int miniplayerIconSel = (int)SendMessage(GetDlgItem(m_hwnd, IDC_MINIPLAYER_ICON_COMBO), CB_GETCURSEL, 0, 0);
        cfg_nowbar_miniplayer_icon_visible = (miniplayerIconSel == 0) ? 1 : 0;

        // Save alternate icons setting (0=Enabled, 1=Disabled in combobox -> config 1=Enabled, 0=Disabled)
        int alternateIconsSel = (int)SendMessage(GetDlgItem(m_hwnd, IDC_ALTERNATE_ICONS_COMBO), CB_GETCURSEL, 0, 0);
        cfg_nowbar_alternate_icons = (alternateIconsSel == 0) ? 1 : 0;

        // Save auto-hide C-buttons setting (0=Yes, 1=No in combobox -> config 1=Yes, 0=No)
        int autohideCbuttonsSel = (int)SendMessage(GetDlgItem(m_hwnd, IDC_AUTOHIDE_CBUTTONS_COMBO), CB_GETCURSEL, 0, 0);
        cfg_nowbar_cbutton_autohide = (autohideCbuttonsSel == 0) ? 1 : 0;

        // Save glass effect setting (0=Disabled, 1=Enabled in combobox -> config 0=Disabled, 1=Enabled)
        int glassEffectSel = (int)SendMessage(GetDlgItem(m_hwnd, IDC_GLASS_EFFECT_COMBO), CB_GETCURSEL, 0, 0);
        cfg_nowbar_glass_effect = (glassEffectSel == 1) ? 1 : 0;

        // Save background style setting
        int bgStyleSel = (int)SendMessage(GetDlgItem(m_hwnd, IDC_BACKGROUND_STYLE_COMBO), CB_GETCURSEL, 0, 0);
        cfg_nowbar_background_style = bgStyleSel;

        // Save Custom Button tab settings
        cfg_cbutton1_enabled = IsDlgButtonChecked(m_hwnd, IDC_CBUTTON1_ENABLE) == BST_CHECKED ? 1 : 0;
        cfg_cbutton2_enabled = IsDlgButtonChecked(m_hwnd, IDC_CBUTTON2_ENABLE) == BST_CHECKED ? 1 : 0;
        cfg_cbutton3_enabled = IsDlgButtonChecked(m_hwnd, IDC_CBUTTON3_ENABLE) == BST_CHECKED ? 1 : 0;
        cfg_cbutton4_enabled = IsDlgButtonChecked(m_hwnd, IDC_CBUTTON4_ENABLE) == BST_CHECKED ? 1 : 0;
        cfg_cbutton5_enabled = IsDlgButtonChecked(m_hwnd, IDC_CBUTTON5_ENABLE) == BST_CHECKED ? 1 : 0;
        cfg_cbutton6_enabled = IsDlgButtonChecked(m_hwnd, IDC_CBUTTON6_ENABLE) == BST_CHECKED ? 1 : 0;
        cfg_cbutton1_action = (int)SendMessage(GetDlgItem(m_hwnd, IDC_CBUTTON1_ACTION), CB_GETCURSEL, 0, 0);
        cfg_cbutton2_action = (int)SendMessage(GetDlgItem(m_hwnd, IDC_CBUTTON2_ACTION), CB_GETCURSEL, 0, 0);
        cfg_cbutton3_action = (int)SendMessage(GetDlgItem(m_hwnd, IDC_CBUTTON3_ACTION), CB_GETCURSEL, 0, 0);
        cfg_cbutton4_action = (int)SendMessage(GetDlgItem(m_hwnd, IDC_CBUTTON4_ACTION), CB_GETCURSEL, 0, 0);
        cfg_cbutton5_action = (int)SendMessage(GetDlgItem(m_hwnd, IDC_CBUTTON5_ACTION), CB_GETCURSEL, 0, 0);
        cfg_cbutton6_action = (int)SendMessage(GetDlgItem(m_hwnd, IDC_CBUTTON6_ACTION), CB_GETCURSEL, 0, 0);
        
        // Save Custom Button paths
        pfc::string8 path;
        uGetDlgItemText(m_hwnd, IDC_CBUTTON1_PATH, path);
        cfg_cbutton1_path = path;
        uGetDlgItemText(m_hwnd, IDC_CBUTTON2_PATH, path);
        cfg_cbutton2_path = path;
        uGetDlgItemText(m_hwnd, IDC_CBUTTON3_PATH, path);
        cfg_cbutton3_path = path;
        uGetDlgItemText(m_hwnd, IDC_CBUTTON4_PATH, path);
        cfg_cbutton4_path = path;
        uGetDlgItemText(m_hwnd, IDC_CBUTTON5_PATH, path);
        cfg_cbutton5_path = path;
        uGetDlgItemText(m_hwnd, IDC_CBUTTON6_PATH, path);
        cfg_cbutton6_path = path;
        
        // Save Custom Button icon paths
        pfc::string8 icon_path;
        uGetDlgItemText(m_hwnd, IDC_CBUTTON1_ICON, icon_path);
        cfg_cbutton1_icon = icon_path;
        uGetDlgItemText(m_hwnd, IDC_CBUTTON2_ICON, icon_path);
        cfg_cbutton2_icon = icon_path;
        uGetDlgItemText(m_hwnd, IDC_CBUTTON3_ICON, icon_path);
        cfg_cbutton3_icon = icon_path;
        uGetDlgItemText(m_hwnd, IDC_CBUTTON4_ICON, icon_path);
        cfg_cbutton4_icon = icon_path;
        uGetDlgItemText(m_hwnd, IDC_CBUTTON5_ICON, icon_path);
        cfg_cbutton5_icon = icon_path;
        uGetDlgItemText(m_hwnd, IDC_CBUTTON6_ICON, icon_path);
        cfg_cbutton6_icon = icon_path;
        
        // Save Custom Button tooltip labels
        pfc::string8 label;
        uGetDlgItemText(m_hwnd, IDC_CBUTTON1_LABEL, label);
        cfg_cbutton1_label = label;
        uGetDlgItemText(m_hwnd, IDC_CBUTTON2_LABEL, label);
        cfg_cbutton2_label = label;
        uGetDlgItemText(m_hwnd, IDC_CBUTTON3_LABEL, label);
        cfg_cbutton3_label = label;
        uGetDlgItemText(m_hwnd, IDC_CBUTTON4_LABEL, label);
        cfg_cbutton4_label = label;
        uGetDlgItemText(m_hwnd, IDC_CBUTTON5_LABEL, label);
        cfg_cbutton5_label = label;
        uGetDlgItemText(m_hwnd, IDC_CBUTTON6_LABEL, label);
        cfg_cbutton6_label = label;

        // Save current settings to the current profile
        {
            auto profiles = get_all_profiles();
            pfc::string8 current_name = get_current_profile_name();
            CButtonProfile* current_profile = find_profile(profiles, current_name.c_str());
            if (current_profile) {
                save_config_to_profile(*current_profile);
                save_all_profiles(profiles);
            }
        }

        // Notify all registered instances to update
        nowbar::ControlPanelCore::notify_all_settings_changed();
    }
}

void nowbar_preferences::reset_settings() {
    if (m_hwnd) {
        if (m_current_tab == 0) {
            // Reset General tab settings
            cfg_nowbar_line1_format = "%title%";  // Default format
            cfg_nowbar_line2_format = "%artist%";  // Default format
            cfg_nowbar_theme_mode = 0;  // Auto
            cfg_nowbar_cover_margin = 1;  // Yes (margin enabled)
            cfg_nowbar_bar_style = 0;  // Pill-shaped
            cfg_nowbar_mood_icon_visible = 1;  // Show (visible)
            cfg_nowbar_miniplayer_icon_visible = 1;  // Show (visible)
            cfg_nowbar_hover_circles = 1;  // Yes (show hover circles)
            cfg_nowbar_alternate_icons = 0;  // Disabled
            cfg_nowbar_cbutton_autohide = 1;  // Yes (default)

            // Update General tab UI
            uSetDlgItemText(m_hwnd, IDC_LINE1_FORMAT_EDIT, "%title%");
            uSetDlgItemText(m_hwnd, IDC_LINE2_FORMAT_EDIT, "%artist%");
            SendMessage(GetDlgItem(m_hwnd, IDC_THEME_MODE_COMBO), CB_SETCURSEL, 0, 0);
            SendMessage(GetDlgItem(m_hwnd, IDC_COVER_MARGIN_COMBO), CB_SETCURSEL, 0, 0);
            SendMessage(GetDlgItem(m_hwnd, IDC_BAR_STYLE_COMBO), CB_SETCURSEL, 0, 0);
            SendMessage(GetDlgItem(m_hwnd, IDC_MOOD_ICON_COMBO), CB_SETCURSEL, 0, 0);
            SendMessage(GetDlgItem(m_hwnd, IDC_STOP_ICON_COMBO), CB_SETCURSEL, 1, 0);  // Default: Hidden
            SendMessage(GetDlgItem(m_hwnd, IDC_MINIPLAYER_ICON_COMBO), CB_SETCURSEL, 0, 0);
            SendMessage(GetDlgItem(m_hwnd, IDC_ALTERNATE_ICONS_COMBO), CB_SETCURSEL, 1, 0);  // Default: Disabled
            SendMessage(GetDlgItem(m_hwnd, IDC_AUTOHIDE_CBUTTONS_COMBO), CB_SETCURSEL, 0, 0);  // Default: Yes
            SendMessage(GetDlgItem(m_hwnd, IDC_GLASS_EFFECT_COMBO), CB_SETCURSEL, 0, 0);  // Default: Disabled
            SendMessage(GetDlgItem(m_hwnd, IDC_BACKGROUND_STYLE_COMBO), CB_SETCURSEL, 0, 0);  // Default: Solid
        } else if (m_current_tab == 1) {
            // Reset Custom Button tab settings
            cfg_cbutton1_enabled = 0;
            cfg_cbutton2_enabled = 0;
            cfg_cbutton3_enabled = 0;
            cfg_cbutton4_enabled = 0;
            cfg_cbutton5_enabled = 0;
            cfg_cbutton6_enabled = 0;
            cfg_cbutton1_action = 0;
            cfg_cbutton2_action = 0;
            cfg_cbutton3_action = 0;
            cfg_cbutton4_action = 0;
            cfg_cbutton5_action = 0;
            cfg_cbutton6_action = 0;
            cfg_cbutton1_path = "";
            cfg_cbutton2_path = "";
            cfg_cbutton3_path = "";
            cfg_cbutton4_path = "";
            cfg_cbutton5_path = "";
            cfg_cbutton6_path = "";
            
            // Update UI
            CheckDlgButton(m_hwnd, IDC_CBUTTON1_ENABLE, BST_UNCHECKED);
            CheckDlgButton(m_hwnd, IDC_CBUTTON2_ENABLE, BST_UNCHECKED);
            CheckDlgButton(m_hwnd, IDC_CBUTTON3_ENABLE, BST_UNCHECKED);
            CheckDlgButton(m_hwnd, IDC_CBUTTON4_ENABLE, BST_UNCHECKED);
            CheckDlgButton(m_hwnd, IDC_CBUTTON5_ENABLE, BST_UNCHECKED);
            CheckDlgButton(m_hwnd, IDC_CBUTTON6_ENABLE, BST_UNCHECKED);
            SendMessage(GetDlgItem(m_hwnd, IDC_CBUTTON1_ACTION), CB_SETCURSEL, 0, 0);
            SendMessage(GetDlgItem(m_hwnd, IDC_CBUTTON2_ACTION), CB_SETCURSEL, 0, 0);
            SendMessage(GetDlgItem(m_hwnd, IDC_CBUTTON3_ACTION), CB_SETCURSEL, 0, 0);
            SendMessage(GetDlgItem(m_hwnd, IDC_CBUTTON4_ACTION), CB_SETCURSEL, 0, 0);
            SendMessage(GetDlgItem(m_hwnd, IDC_CBUTTON5_ACTION), CB_SETCURSEL, 0, 0);
            SendMessage(GetDlgItem(m_hwnd, IDC_CBUTTON6_ACTION), CB_SETCURSEL, 0, 0);
            SetDlgItemTextW(m_hwnd, IDC_CBUTTON1_PATH, L"");
            SetDlgItemTextW(m_hwnd, IDC_CBUTTON2_PATH, L"");
            SetDlgItemTextW(m_hwnd, IDC_CBUTTON3_PATH, L"");
            SetDlgItemTextW(m_hwnd, IDC_CBUTTON4_PATH, L"");
            SetDlgItemTextW(m_hwnd, IDC_CBUTTON5_PATH, L"");
            SetDlgItemTextW(m_hwnd, IDC_CBUTTON6_PATH, L"");
            update_all_cbutton_path_states(m_hwnd);
        } else if (m_current_tab == 2) {
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

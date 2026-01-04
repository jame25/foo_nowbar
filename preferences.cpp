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
    GUID{0xABCDEF30, 0x1234, 0x5678, {0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67, 0xC0}},
    ""  // Default: empty (use default numbered icon)
);
static cfg_string cfg_cbutton2_icon(
    GUID{0xABCDEF31, 0x1234, 0x5678, {0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67, 0xC1}},
    ""  // Default: empty
);
static cfg_string cfg_cbutton3_icon(
    GUID{0xABCDEF32, 0x1234, 0x5678, {0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67, 0xC2}},
    ""  // Default: empty
);
static cfg_string cfg_cbutton4_icon(
    GUID{0xABCDEF33, 0x1234, 0x5678, {0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67, 0xC3}},
    ""  // Default: empty
);
static cfg_string cfg_cbutton5_icon(
    GUID{0xABCDEF34, 0x1234, 0x5678, {0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67, 0xC4}},
    ""  // Default: empty
);
static cfg_string cfg_cbutton6_icon(
    GUID{0xABCDEF35, 0x1234, 0x5678, {0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67, 0xC5}},
    ""  // Default: empty
);

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

bool get_nowbar_miniplayer_icon_visible() {
    return cfg_nowbar_miniplayer_icon_visible != 0;
}

bool get_nowbar_hover_circles_enabled() {
    return cfg_nowbar_hover_circles != 0;
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
    ShowWindow(GetDlgItem(m_hwnd, IDC_MINIPLAYER_ICON_LABEL), show_general);
    ShowWindow(GetDlgItem(m_hwnd, IDC_MINIPLAYER_ICON_COMBO), show_general);
    ShowWindow(GetDlgItem(m_hwnd, IDC_HOVER_CIRCLES_LABEL), show_general);
    ShowWindow(GetDlgItem(m_hwnd, IDC_HOVER_CIRCLES_COMBO), show_general);
    ShowWindow(GetDlgItem(m_hwnd, IDC_ALTERNATE_ICONS_LABEL), show_general);
    ShowWindow(GetDlgItem(m_hwnd, IDC_ALTERNATE_ICONS_COMBO), show_general);
    ShowWindow(GetDlgItem(m_hwnd, IDC_AUTOHIDE_CBUTTONS_LABEL), show_general);
    ShowWindow(GetDlgItem(m_hwnd, IDC_AUTOHIDE_CBUTTONS_COMBO), show_general);
    ShowWindow(GetDlgItem(m_hwnd, IDC_GLASS_EFFECT_LABEL), show_general);
    ShowWindow(GetDlgItem(m_hwnd, IDC_GLASS_EFFECT_COMBO), show_general);

    // Custom Button tab controls
    BOOL show_cbutton = (tab == 1) ? SW_SHOW : SW_HIDE;
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
        
        // Initialize miniplayer icon visibility combobox
        HWND hMiniplayerIconCombo = GetDlgItem(hwnd, IDC_MINIPLAYER_ICON_COMBO);
        SendMessage(hMiniplayerIconCombo, CB_ADDSTRING, 0, (LPARAM)L"Show");
        SendMessage(hMiniplayerIconCombo, CB_ADDSTRING, 0, (LPARAM)L"Hidden");
        SendMessage(hMiniplayerIconCombo, CB_SETCURSEL, cfg_nowbar_miniplayer_icon_visible ? 0 : 1, 0);

        // Initialize hover circles combobox
        HWND hHoverCirclesCombo = GetDlgItem(hwnd, IDC_HOVER_CIRCLES_COMBO);
        SendMessage(hHoverCirclesCombo, CB_ADDSTRING, 0, (LPARAM)L"Yes");
        SendMessage(hHoverCirclesCombo, CB_ADDSTRING, 0, (LPARAM)L"No");
        SendMessage(hHoverCirclesCombo, CB_SETCURSEL, cfg_nowbar_hover_circles ? 0 : 1, 0);

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

        // Initialize font displays
        p_this->update_font_displays();

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
        case IDC_MINIPLAYER_ICON_COMBO:
        case IDC_HOVER_CIRCLES_COMBO:
        case IDC_ALTERNATE_ICONS_COMBO:
        case IDC_AUTOHIDE_CBUTTONS_COMBO:
        case IDC_GLASS_EFFECT_COMBO:
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
        
        // Save miniplayer icon visibility (0=Show, 1=Hidden in combobox -> config 1=Show, 0=Hidden)
        int miniplayerIconSel = (int)SendMessage(GetDlgItem(m_hwnd, IDC_MINIPLAYER_ICON_COMBO), CB_GETCURSEL, 0, 0);
        cfg_nowbar_miniplayer_icon_visible = (miniplayerIconSel == 0) ? 1 : 0;

        int hoverCirclesSel = (int)SendMessage(GetDlgItem(m_hwnd, IDC_HOVER_CIRCLES_COMBO), CB_GETCURSEL, 0, 0);
        cfg_nowbar_hover_circles = (hoverCirclesSel == 0) ? 1 : 0;

        // Save alternate icons setting (0=Enabled, 1=Disabled in combobox -> config 1=Enabled, 0=Disabled)
        int alternateIconsSel = (int)SendMessage(GetDlgItem(m_hwnd, IDC_ALTERNATE_ICONS_COMBO), CB_GETCURSEL, 0, 0);
        cfg_nowbar_alternate_icons = (alternateIconsSel == 0) ? 1 : 0;

        // Save auto-hide C-buttons setting (0=Yes, 1=No in combobox -> config 1=Yes, 0=No)
        int autohideCbuttonsSel = (int)SendMessage(GetDlgItem(m_hwnd, IDC_AUTOHIDE_CBUTTONS_COMBO), CB_GETCURSEL, 0, 0);
        cfg_nowbar_cbutton_autohide = (autohideCbuttonsSel == 0) ? 1 : 0;

        // Save glass effect setting (0=Disabled, 1=Enabled in combobox -> config 0=Disabled, 1=Enabled)
        int glassEffectSel = (int)SendMessage(GetDlgItem(m_hwnd, IDC_GLASS_EFFECT_COMBO), CB_GETCURSEL, 0, 0);
        cfg_nowbar_glass_effect = (glassEffectSel == 1) ? 1 : 0;

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
            SendMessage(GetDlgItem(m_hwnd, IDC_MINIPLAYER_ICON_COMBO), CB_SETCURSEL, 0, 0);
            SendMessage(GetDlgItem(m_hwnd, IDC_HOVER_CIRCLES_COMBO), CB_SETCURSEL, 0, 0);
            SendMessage(GetDlgItem(m_hwnd, IDC_ALTERNATE_ICONS_COMBO), CB_SETCURSEL, 1, 0);  // Default: Disabled
            SendMessage(GetDlgItem(m_hwnd, IDC_AUTOHIDE_CBUTTONS_COMBO), CB_SETCURSEL, 0, 0);  // Default: Yes
            SendMessage(GetDlgItem(m_hwnd, IDC_GLASS_EFFECT_COMBO), CB_SETCURSEL, 0, 0);  // Default: Disabled
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

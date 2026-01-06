#include "pch.h"
#include "preferences.h"
#include <shellapi.h>

// GUIDs for the Now Bar mainmenu group and commands
// {D6A5E8F0-1234-5678-ABCD-000000000001} - Now Bar menu group
static const GUID guid_nowbar_menu_group = 
    { 0xD6A5E8F0, 0x1234, 0x5678, { 0xAB, 0xCD, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 } };

// Command GUIDs for Custom Buttons 1-6
static const GUID guid_cbutton_commands[] = {
    { 0xD6A5E8F1, 0x1234, 0x5678, { 0xAB, 0xCD, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 } },
    { 0xD6A5E8F1, 0x1234, 0x5678, { 0xAB, 0xCD, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02 } },
    { 0xD6A5E8F1, 0x1234, 0x5678, { 0xAB, 0xCD, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03 } },
    { 0xD6A5E8F1, 0x1234, 0x5678, { 0xAB, 0xCD, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04 } },
    { 0xD6A5E8F1, 0x1234, 0x5678, { 0xAB, 0xCD, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05 } },
    { 0xD6A5E8F1, 0x1234, 0x5678, { 0xAB, 0xCD, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06 } }
};

// Execute custom button action (shared implementation)
static void execute_cbutton_action(int button_index) {
    if (button_index < 0 || button_index >= 6) return;
    if (!get_nowbar_cbutton_enabled(button_index)) return;
    
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
            
            if (has_track && track.is_valid() && script.is_valid()) {
                track->format_title(nullptr, evaluated_url, script, nullptr);
            } else if (script.is_valid()) {
                // Fallback to playing track if no selection
                auto pc = playback_control::get();
                pc->playback_format_title(nullptr, evaluated_url, script, nullptr, playback_control::display_level_all);
            }
        } else {
            // No title formatting - use path directly as URL
            evaluated_url = path;
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
    }
}

// Register the Now Bar menu group under Playback
static mainmenu_group_popup_factory g_nowbar_menu_group(
    guid_nowbar_menu_group,
    mainmenu_groups::playback,
    mainmenu_commands::sort_priority_base + 1000,  // Low priority to appear near bottom
    "Now Bar"
);

// Main menu commands implementation
class nowbar_mainmenu_commands : public mainmenu_commands {
public:
    t_uint32 get_command_count() override {
        return 6;  // Custom buttons 1-6
    }
    
    GUID get_command(t_uint32 p_index) override {
        if (p_index < 6) {
            return guid_cbutton_commands[p_index];
        }
        return pfc::guid_null;
    }
    
    void get_name(t_uint32 p_index, pfc::string_base& p_out) override {
        if (p_index < 6) {
            p_out.reset();
            p_out << "Custom Button " << (p_index + 1);
        }
    }
    
    bool get_description(t_uint32 p_index, pfc::string_base& p_out) override {
        if (p_index < 6) {
            p_out.reset();
            if (get_nowbar_cbutton_enabled(p_index)) {
                int action = get_nowbar_cbutton_action(p_index);
                pfc::string8 path = get_nowbar_cbutton_path(p_index);
                
                switch (action) {
                    case 1:
                        p_out << "Open URL: " << (path.is_empty() ? "(not configured)" : path.c_str());
                        break;
                    case 2:
                        p_out << "Run program: " << (path.is_empty() ? "(not configured)" : path.c_str());
                        break;
                    case 3:
                        p_out << "foobar2000 action: " << (path.is_empty() ? "(not configured)" : path.c_str());
                        break;
                    default:
                        p_out << "No action configured";
                        break;
                }
            } else {
                p_out << "Button disabled";
            }
            return true;
        }
        return false;
    }
    
    GUID get_parent() override {
        return guid_nowbar_menu_group;
    }
    
    t_uint32 get_sort_priority() override {
        return mainmenu_commands::sort_priority_base;
    }
    
    bool get_display(t_uint32 p_index, pfc::string_base& p_text, t_uint32& p_flags) override {
        if (p_index >= 6) return false;
        
        get_name(p_index, p_text);
        
        // Gray out disabled or unconfigured buttons
        p_flags = 0;
        if (!get_nowbar_cbutton_enabled(p_index)) {
            p_flags |= flag_disabled;
        } else {
            int action = get_nowbar_cbutton_action(p_index);
            if (action == 0) {
                p_flags |= flag_disabled;  // No action
            }
        }
        
        return true;
    }
    
    void execute(t_uint32 p_index, service_ptr ctx) override {
        (void)ctx;
        if (p_index < 6) {
            execute_cbutton_action(p_index);
        }
    }
};

// Register the mainmenu commands
static mainmenu_commands_factory_t<nowbar_mainmenu_commands> g_nowbar_mainmenu_commands;

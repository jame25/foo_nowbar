#pragma once
#include "pch.h"
#include "core/control_panel_core.h"

namespace nowbar {

// DUI element wrapper - plain Win32 window without WTL
class ControlPanelDUI : public ui_element_instance {
public:
    // Static metadata for ui_element_impl
    static GUID g_get_guid() { return g_dui_element_guid; }
    static GUID g_get_subclass() { return ui_element_subclass_utility; }
    static void g_get_name(pfc::string_base& out) { out = FOO_NOWBAR_DISPLAY_NAME; }
    static const char* g_get_description() { return FOO_NOWBAR_DESCRIPTION; }
    static ui_element_config::ptr g_get_default_configuration();
    
    // Constructor
    ControlPanelDUI(ui_element_config::ptr config, ui_element_instance_callback::ptr callback);
    ~ControlPanelDUI();
    
    // ui_element_instance
    HWND get_wnd() override { return m_hwnd; }
    void set_configuration(ui_element_config::ptr data) override;
    ui_element_config::ptr get_configuration() override;
    GUID get_guid() override { return g_get_guid(); }
    GUID get_subclass() override { return g_get_subclass(); }
    ui_element_min_max_info get_min_max_info() override;
    
    void initialize_window(HWND parent);

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT handle_message(UINT msg, WPARAM wp, LPARAM lp);
    
    void update_artwork();
    
    static const wchar_t* get_class_name();
    static bool register_class();
    
    HWND m_hwnd = nullptr;
    ui_element_config::ptr m_config;
    ui_element_instance_callback::ptr m_callback;
    std::unique_ptr<ControlPanelCore> m_core;
    bool m_tracking_mouse = false;
    bool m_glass_effect_active = false;  // Tracks if acrylic backdrop is currently applied
};

} // namespace nowbar

#pragma once
#include "pch.h"
#include "core/control_panel_core.h"

namespace nowbar {

// CUI panel wrapper using container_uie_window_v3
class ControlPanelCUI : public uie::container_uie_window_v3 {
public:
    // uie::extension_base
    const GUID& get_extension_guid() const override {
        return g_cui_panel_guid;
    }
    
    void get_name(pfc::string_base& out) const override {
        out = FOO_NOWBAR_DISPLAY_NAME;
    }
    
    void get_category(pfc::string_base& out) const override {
        out = "Panels";
    }
    
    bool get_description(pfc::string_base& out) const override {
        out = FOO_NOWBAR_DESCRIPTION;
        return true;
    }
    
    // uie::window
    unsigned get_type() const override {
        return uie::type_panel;
    }

    // container_uie_window_v3
    uie::container_window_v3_config get_window_config() override {
        return uie::container_window_v3_config(L"foo_nowbar_cui_panel", false, CS_DBLCLKS);
    }

    LRESULT on_message(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) override;

private:
    std::unique_ptr<ControlPanelCore> m_core;
    bool m_tracking_mouse = false;
    bool m_glass_effect_active = false;  // Tracks if acrylic backdrop is currently applied
    
    // CUI colour change callback for Custom theme mode
    class ColourCallback : public cui::colours::common_callback {
    public:
        ColourCallback(ControlPanelCUI* owner) : m_owner(owner) {}
        
        void on_colour_changed(uint32_t changed_items_mask) const override {
            if (m_owner && m_owner->m_core) {
                m_owner->m_core->on_settings_changed();
            }
        }
        
        void on_bool_changed(uint32_t changed_items_mask) const override {
            // Dark mode change - also triggers on_settings_changed
            if ((changed_items_mask & cui::colours::bool_flag_dark_mode_enabled) && m_owner && m_owner->m_core) {
                m_owner->m_core->on_settings_changed();
            }
        }
        
    private:
        ControlPanelCUI* m_owner;
    };
    
    std::unique_ptr<ColourCallback> m_colour_callback;
    cui::colours::manager::ptr m_colour_manager;
    
    void initialize_core(HWND wnd);
    void update_artwork();

};

// Factory registration
extern uie::window_factory<ControlPanelCUI> g_cui_factory;

} // namespace nowbar

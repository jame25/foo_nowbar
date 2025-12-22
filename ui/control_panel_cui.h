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
    
    void initialize_core(HWND wnd);
    void update_artwork();
};

// Factory registration
extern uie::window_factory<ControlPanelCUI> g_cui_factory;

} // namespace nowbar

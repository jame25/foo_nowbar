#include "pch.h"
#include "control_panel_dui.h"
#include "../preferences.h"

namespace nowbar {

// Window class name
const wchar_t* ControlPanelDUI::get_class_name() {
    return L"foo_nowbar_dui_element";
}

bool ControlPanelDUI::register_class() {
    static bool registered = false;
    if (registered) return true;
    
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = core_api::get_my_instance();
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(24, 24, 24));  // Dark background to prevent white flash
    wc.lpszClassName = get_class_name();
    
    registered = (RegisterClassExW(&wc) != 0);
    return registered;
}

ui_element_config::ptr ControlPanelDUI::g_get_default_configuration() {
    return ui_element_config::g_create_empty(g_get_guid());
}

ControlPanelDUI::ControlPanelDUI(ui_element_config::ptr config, ui_element_instance_callback::ptr callback)
    : m_config(config)
    , m_callback(callback)
{
}

ControlPanelDUI::~ControlPanelDUI() {
    if (m_hwnd) {
        SetWindowLongPtr(m_hwnd, GWLP_USERDATA, 0);
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
}

void ControlPanelDUI::initialize_window(HWND parent) {
    if (!register_class()) return;
    
    m_hwnd = CreateWindowExW(
        0,
        get_class_name(),
        L"",
        WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | WS_VISIBLE,
        0, 0, 100, 100,
        parent,
        nullptr,
        core_api::get_my_instance(),
        this
    );
}

void ControlPanelDUI::set_configuration(ui_element_config::ptr data) {
    m_config = data;
}

ui_element_config::ptr ControlPanelDUI::get_configuration() {
    return m_config;
}

ui_element_min_max_info ControlPanelDUI::get_min_max_info() {
    ui_element_min_max_info info;
    
    // Minimum height: 0.55 inches, scaled by DPI
    // At 96 DPI: 0.55 * 96 = 53 pixels
    int dpi = 96;
    if (m_hwnd) {
        HDC hdc = GetDC(m_hwnd);
        if (hdc) {
            dpi = GetDeviceCaps(hdc, LOGPIXELSY);
            ReleaseDC(m_hwnd, hdc);
        }
    }

    info.m_min_height = static_cast<t_uint32>(0.55 * dpi);
    info.m_min_width = 200;  // Reasonable minimum width
    
    return info;
}

void ControlPanelDUI::update_artwork() {
    if (!m_core) return;
    
    auto pc = playback_control::get();
    metadb_handle_ptr track;
    if (pc->get_now_playing(track) && track.is_valid()) {
        auto art_manager = album_art_manager_v3::get();
        try {
            auto extractor = art_manager->open(
                pfc::list_single_ref_t<metadb_handle_ptr>(track),
                pfc::list_single_ref_t<GUID>(album_art_ids::cover_front),
                fb2k::noAbort
            );
            
            if (extractor.is_valid()) {
                album_art_data_ptr data;
                if (extractor->query(album_art_ids::cover_front, data, fb2k::noAbort)) {
                    m_core->set_artwork(data);
                    return;
                }
            }
        } catch (...) {}
    }
    
    m_core->clear_artwork();
}

LRESULT CALLBACK ControlPanelDUI::WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    ControlPanelDUI* self = nullptr;
    
    if (msg == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCT*>(lp);
        self = static_cast<ControlPanelDUI*>(cs->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->m_hwnd = hwnd;
    } else {
        self = reinterpret_cast<ControlPanelDUI*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }
    
    if (self) {
        return self->handle_message(msg, wp, lp);
    }
    
    return DefWindowProc(hwnd, msg, wp, lp);
}

LRESULT ControlPanelDUI::handle_message(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        m_core = std::make_unique<ControlPanelCore>();
        m_core->initialize(m_hwnd);
        
        // Set artwork request callback
        m_core->set_artwork_request_callback([this]() {
            update_artwork();
        });
        
        // Note: initialize() already calls on_settings_changed() which respects theme mode preferences
        update_artwork();
        return 0;
        
    case WM_DESTROY:
        m_core.reset();
        return 0;
        
    case WM_SIZE:
        if (m_core) {
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
        return 0;
        
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(m_hwnd, &ps);
        
        RECT rect;
        GetClientRect(m_hwnd, &rect);
        
        // Double buffering
        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBitmap = CreateCompatibleBitmap(hdc, rect.right, rect.bottom);
        HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);
        
        if (m_core) {
            m_core->paint(memDC, rect);
        }
        
        BitBlt(hdc, 0, 0, rect.right, rect.bottom, memDC, 0, 0, SRCCOPY);
        
        SelectObject(memDC, oldBitmap);
        DeleteObject(memBitmap);
        DeleteDC(memDC);
        
        EndPaint(m_hwnd, &ps);
        return 0;
    }
        
    case WM_ERASEBKGND: {
        // Paint background based on current theme setting to prevent flash
        HDC hdc = (HDC)wp;
        RECT rc;
        GetClientRect(m_hwnd, &rc);
        COLORREF bg_color = get_nowbar_initial_bg_color();
        HBRUSH brush = CreateSolidBrush(bg_color);
        FillRect(hdc, &rc, brush);
        DeleteObject(brush);
        return 1;
    }
        
    case WM_MOUSEMOVE:
        if (!m_tracking_mouse) {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, m_hwnd, 0 };
            TrackMouseEvent(&tme);
            m_tracking_mouse = true;
        }
        if (m_core) {
            m_core->on_mouse_move(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        }
        return 0;
        
    case WM_MOUSELEAVE:
        m_tracking_mouse = false;
        if (m_core) {
            m_core->on_mouse_leave();
        }
        return 0;
        
    case WM_LBUTTONDOWN:
        SetCapture(m_hwnd);
        if (m_core) {
            m_core->on_lbutton_down(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        }
        return 0;
        
    case WM_LBUTTONUP:
        ReleaseCapture();
        if (m_core) {
            m_core->on_lbutton_up(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        }
        return 0;
        
    case WM_LBUTTONDBLCLK:
        if (m_core) {
            m_core->on_lbutton_dblclk(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        }
        return 0;
        
    case WM_MOUSEWHEEL:
        if (m_core) {
            m_core->on_mouse_wheel(GET_WHEEL_DELTA_WPARAM(wp));
        }
        return 0;
    }
    
    return DefWindowProc(m_hwnd, msg, wp, lp);
}

// DUI factory - simple implementation without WTL
class ControlPanelDUIElement : public ui_element {
public:
    GUID get_guid() override { return ControlPanelDUI::g_get_guid(); }
    GUID get_subclass() override { return ControlPanelDUI::g_get_subclass(); }
    void get_name(pfc::string_base& out) override { ControlPanelDUI::g_get_name(out); }
    
    ui_element_instance::ptr instantiate(HWND parent, ui_element_config::ptr cfg, ui_element_instance_callback::ptr callback) override {
        PFC_ASSERT(cfg->get_guid() == get_guid());
        service_ptr_t<ControlPanelDUI> item = new service_impl_t<ControlPanelDUI>(cfg, callback);
        item->initialize_window(parent);
        return item;
    }
    
    ui_element_config::ptr get_default_configuration() override {
        return ControlPanelDUI::g_get_default_configuration();
    }
    
    ui_element_children_enumerator_ptr enumerate_children(ui_element_config::ptr cfg) override {
        return nullptr;
    }
    
    bool get_description(pfc::string_base& out) override {
        out = ControlPanelDUI::g_get_description();
        return true;
    }
};

static service_factory_single_t<ControlPanelDUIElement> g_dui_factory;

} // namespace nowbar

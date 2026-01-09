#pragma once

namespace uih {
namespace window_styles {
constexpr const uint32_t style_child_default = WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
constexpr const uint32_t ex_style_child_default = WS_EX_CONTROLPARENT;

constexpr const uint32_t style_popup_default = WS_SYSMENU | WS_POPUP | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_CAPTION;
constexpr const uint32_t ex_style_popup_default = WS_EX_DLGMODALFRAME;
} // namespace window_styles

/** \brief Simple class used to hold window positions */
struct WindowPosition {
    int32_t x = 0;
    int32_t y = 0;
    int32_t cx = 0;
    int32_t cy = 0;

    WindowPosition() = default;

    WindowPosition(int32_t x_, int32_t y_, int32_t cx_, int32_t cy_) : x{x_}, y{y_}, cx{cx_}, cy{cy_} {}

    WindowPosition(RECT rc) { from_rect(rc); }

    WindowPosition(HWND wnd_relative, int32_t cx_, int32_t cy_)
    {
        RECT rc;
        GetClientRect(wnd_relative, &rc);
        MapWindowPoints(wnd_relative, HWND_DESKTOP, reinterpret_cast<LPPOINT>(&rc), 2);
        x = rc.left;
        y = rc.top;
        cx = cx_;
        cy = cy_;
    }
    void from_rect(RECT rc)
    {
        x = rc.left;
        y = rc.top;
        cx = rc.right - rc.left;
        cy = rc.bottom - rc.top;
    }
    RECT to_rect() const { return RECT{x, y, x + cx, y + cy}; }
    void convert_from_dialog_units_to_pixels(HWND wnd_dialog)
    {
        auto rc = to_rect();
        MapDialogRect(wnd_dialog, &rc);
        from_rect(rc);
    }
};

struct ContainerWindowConfig {
    bool transparent_background{false};
    uint32_t window_styles{window_styles::style_child_default};
    uint32_t window_ex_styles{window_styles::ex_style_child_default};
    uint32_t class_styles{NULL};
    LPWSTR class_cursor{IDC_ARROW};
    HBRUSH class_background{nullptr};
    const wchar_t* window_title{L""};
    uint32_t class_extra_wnd_bytes{0};
    const wchar_t* class_name{nullptr};

    ContainerWindowConfig(const wchar_t* class_name_) : class_name(class_name_) {}
};

class ContainerWindow {
public:
    ContainerWindow(const ContainerWindowConfig& config,
        std::function<LRESULT(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)> on_message = nullptr,
        std::function<void(size_t cx, size_t cy)> on_size = nullptr)
        : m_config{config}
        , m_on_message{std::move(on_message)}
        , m_on_size{std::move(on_size)}
    {
    }

    ContainerWindow(const ContainerWindow& p_source) = delete;

    void on_size() const;
    HWND create(
        HWND wnd_parent, WindowPosition window_position, LPVOID create_param = nullptr, bool use_dialog_units = false);
    void destroy();
    HWND get_wnd() const { return m_wnd; }

private:
    static LRESULT WINAPI s_on_message(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) noexcept;

    LRESULT on_message(HWND wnd, UINT msg, WPARAM wp, LPARAM lp);
    void register_class();
    void deregister_class();

    static std::unordered_map<std::wstring, size_t> s_window_count;

    HWND m_wnd{nullptr};
    ContainerWindowConfig m_config;
    std::function<LRESULT(HWND, UINT, WPARAM, LPARAM)> m_on_message;
    std::function<void(size_t, size_t)> m_on_size;
};

} // namespace uih

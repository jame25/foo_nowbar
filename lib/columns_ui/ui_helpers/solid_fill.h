#pragma once

namespace uih {

class FillWindow {
public:
    enum class Mode {
        SolidFill,
        ThemeBackgroundFill,
        ThemeTextColourFill,
    };

    void set_fill_colour(COLORREF value)
    {
        m_mode = Mode::SolidFill;
        m_fill_colour = value;
        if (m_container_window->get_wnd())
            redraw();
    }

    void set_fill_themed(Mode mode, const WCHAR* pclass, int part, int state, bool is_dark, COLORREF fallback)
    {
        m_mode = mode;
        m_theme_state = state;
        m_theme_part = part;
        m_fill_colour = fallback;
        m_theme_class = pclass;
        m_theme.reset();
        m_is_dark = is_dark;

        if (m_container_window->get_wnd()) {
            if (IsThemeActive() && IsAppThemed())
                m_theme.reset(OpenThemeData(m_container_window->get_wnd(), m_theme_class.c_str()));
            redraw();
        }
    }

    FillWindow()
    {
        auto window_config = uih::ContainerWindowConfig{L"columns_ui_fill"};
        window_config.window_styles = WS_CHILD | WS_CLIPCHILDREN | WS_VISIBLE;
        window_config.window_ex_styles = WS_EX_CONTROLPARENT | WS_EX_STATICEDGE;
        m_container_window = std::make_unique<uih::ContainerWindow>(
            window_config, [this](auto&&... args) { return on_message(std::forward<decltype(args)>(args)...); });
    }

    template <typename... Ts>
    HWND create(Ts&&... vals)
    {
        return m_container_window->create(std::forward<Ts>(vals)...);
    }

    void destroy() const { m_container_window->destroy(); }

    HWND get_wnd() const { return m_container_window->get_wnd(); }

private:
    void redraw() const
    {
        RedrawWindow(m_container_window->get_wnd(), nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
    }

    LRESULT on_message(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        switch (msg) {
        case WM_CREATE:
            if (m_mode == Mode::ThemeBackgroundFill || m_mode == Mode::ThemeTextColourFill)
                m_theme.reset(IsThemeActive() && IsAppThemed() ? OpenThemeData(wnd, m_theme_class.c_str()) : nullptr);
            break;
        case WM_DESTROY:
            m_theme.reset();
            break;
        case WM_THEMECHANGED:
            m_theme.reset();
            if (m_mode == Mode::ThemeBackgroundFill || m_mode == Mode::ThemeTextColourFill)
                m_theme.reset(IsThemeActive() && IsAppThemed() ? OpenThemeData(wnd, m_theme_class.c_str()) : nullptr);
            redraw();
            break;
        case WM_ENABLE:
            redraw();
            break;
        case WM_ERASEBKGND:
            return FALSE;
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            const auto dc = wil::BeginPaint(wnd, &ps);

            RECT client_rect{};
            GetClientRect(wnd, &client_rect);

            if (!IsWindowEnabled(wnd)) {
                FillRect(dc.get(), &ps.rcPaint, GetSysColorBrush(COLOR_BTNFACE));
                break;
            }

            if (m_mode == Mode::SolidFill || !m_theme
                || !IsThemePartDefined(m_theme.get(), m_theme_part, m_theme_state)) {
                const wil::unique_hbrush brush(CreateSolidBrush(m_fill_colour));
                FillRect(dc.get(), &ps.rcPaint, brush.get());
            } else if (m_mode == Mode::ThemeBackgroundFill) {
                if (IsThemeBackgroundPartiallyTransparent(m_theme.get(), m_theme_part, m_theme_state))
                    DrawThemeParentBackground(wnd, dc.get(), &client_rect);

                RECT background_rect = client_rect;

                if (m_is_dark) {
                    InflateRect(&background_rect, 1, 1);
                }

                if (FAILED(DrawThemeBackground(
                        m_theme.get(), dc.get(), m_theme_part, m_theme_state, &background_rect, &client_rect))) {
                    const wil::unique_hbrush brush(CreateSolidBrush(m_fill_colour));
                    FillRect(dc.get(), &ps.rcPaint, brush.get());
                }
            } else if (m_mode == Mode::ThemeTextColourFill) {
                COLORREF fill_colour = m_fill_colour;
                GetThemeColor(m_theme.get(), m_theme_part, m_theme_state, TMT_TEXTCOLOR, &fill_colour);
                const wil::unique_hbrush brush(CreateSolidBrush(fill_colour));
                FillRect(dc.get(), &ps.rcPaint, brush.get());
            }

            return 0;
        }
        }
        return DefWindowProc(wnd, msg, wp, lp);
    }

    Mode m_mode{Mode::SolidFill};
    std::wstring m_theme_class;
    COLORREF m_fill_colour = NULL;
    wil::unique_htheme m_theme;
    int m_theme_part = NULL;
    int m_theme_state = NULL;
    int m_theme_colour_index = NULL;
    std::unique_ptr<uih::ContainerWindow> m_container_window;
    bool m_is_dark{};
};

class TranslucentFillWindow {
public:
    void set_fill_colour(COLORREF value)
    {
        m_fill_colour = value;
        if (m_container_window->get_wnd())
            redraw();
    }

    TranslucentFillWindow()
    {
        auto window_config = uih::ContainerWindowConfig{L"columns_ui_transparent_fill"};
        window_config.window_styles = WS_POPUP | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
        window_config.window_ex_styles = WS_EX_LAYERED;
        m_container_window = std::make_unique<uih::ContainerWindow>(
            window_config, [this](auto&&... args) { return on_message(std::forward<decltype(args)>(args)...); });
    }

    template <typename... Ts>
    HWND create(Ts&&... vals)
    {
        return m_container_window->create(std::forward<Ts>(vals)...);
    }
    void destroy() { m_container_window->destroy(); }
    HWND get_wnd() const { return m_container_window->get_wnd(); }

private:
    void redraw() { RedrawWindow(m_container_window->get_wnd(), nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW); }
    LRESULT on_message(HWND wnd, UINT msg, WPARAM wp, LPARAM lp);

    COLORREF m_fill_colour{NULL};
    std::unique_ptr<uih::ContainerWindow> m_container_window;
};
} // namespace uih

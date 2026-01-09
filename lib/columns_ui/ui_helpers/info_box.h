#pragma once

#include <utility>

namespace uih {

enum class InfoBoxType {
    Neutral,
    Information,
    Warning,
    Error,
};

enum class InfoBoxModalType {
    YesNo,
    OK,
};

class InfoBox : public std::enable_shared_from_this<InfoBox> {
public:
    InfoBox(std::function<std::optional<INT_PTR>(HWND, UINT, WPARAM, LPARAM)> on_before_message, bool no_wrap)
        : m_no_wrap(no_wrap)
        , m_on_before_message(std::move(on_before_message))
    {
    }

    static void s_open_modeless(HWND wnd_parent, const char* title, const char* text, InfoBoxType type,
        std::function<std::optional<INT_PTR>(HWND, UINT, WPARAM, LPARAM)> on_before_message = nullptr,
        bool no_wrap = false);

    static INT_PTR s_open_modal(HWND wnd_parent, const char* title, const char* text, InfoBoxType type,
        InfoBoxModalType modal_type,
        std::function<std::optional<INT_PTR>(HWND, UINT, WPARAM, LPARAM)> on_before_message = nullptr,
        bool no_wrap = false);

private:
    static constexpr int default_width_dip = 470;

    static int get_large_padding() { return scale_dpi_value(11); }
    static int get_small_padding() { return scale_dpi_value(7); }
    static int get_button_width() { return scale_dpi_value(75); }

    INT_PTR create(HWND wnd_parent, const char* title, const char* text, InfoBoxType type,
        std::optional<InfoBoxModalType> modal_type = {});

    INT_PTR on_message(HWND wnd, UINT msg, WPARAM wp, LPARAM lp);

    int calc_height() const;
    int calc_width() const;

    int get_text_height() const { return Edit_GetLineCount(m_wnd_edit) * get_font_height(m_font.get()); }

    int get_icon_height() const
    {
        if (!m_wnd_static)
            return 0;

        RECT rc_icon{};
        GetWindowRect(m_wnd_static, &rc_icon);
        return wil::rect_height(rc_icon);
    }

    HWND m_wnd{};
    HWND m_wnd_parent{};
    HWND m_wnd_edit{};
    HWND m_wnd_ok_button{};
    HWND m_wnd_cancel_button{};
    HWND m_wnd_static{};
    InfoBoxType m_type{InfoBoxType::Information};
    std::optional<InfoBoxModalType> m_modal_type;
    int m_button_height{};
    bool m_no_wrap{};
    wil::unique_hfont m_font;
    std::string m_message;
    wil::unique_hicon m_icon;
    std::function<std::optional<INT_PTR>(HWND, UINT, WPARAM, LPARAM)> m_on_before_message;
};

} // namespace uih

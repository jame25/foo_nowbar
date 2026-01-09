#pragma once

namespace uih {
extern const RECT rect_null;

struct KeyboardLParam {
    /** Specifies the repeat count. The value is the number of times the keystroke is repeated as a result of the user's
     * holding down the key. */
    LPARAM repeat_count : 16;
    /** Specifies the scan code. The value depends on the OEM. */
    LPARAM scan_code : 8;
    /** Specifies whether the key is an extended key, such as a function key or a key on the numeric keypad. The value
     * is 1 if the key is an extended key; otherwise, it is 0. */
    LPARAM extended_key : 1;
    /** Reserved. */
    LPARAM reserved : 4;
    /** Specifies the context code. The value is 1 if the ALT key is down; otherwise, it is 0.*/
    LPARAM context_code : 1;
    /** Specifies the previous key state. The value is 1 if the key is down before the message is sent; it is 0 if the
     * key is up.*/
    LPARAM previous_key_state : 1;
    /** Specifies the transition state. The value is 0 if the key is being pressed and 1 if it is being released. */
    LPARAM transition_code : 1;
};

inline KeyboardLParam& GetKeyboardLParam(LPARAM& lp)
{
    return reinterpret_cast<KeyboardLParam&>(lp);
}

bool are_keyboard_cues_enabled();
bool is_high_contrast_active();

BOOL set_process_dpi_aware();

// This is cached as system-DPI changes take effect at next log on.
// Per monitor-DPI changes can occur more frequently for per monitor-DPI aware applications.
// At present, though, per-monitor DPI support is not declared by the foobar2000.exe manifest.
// See https://msdn.microsoft.com/en-gb/library/windows/desktop/dn469266%28v=vs.85%29.aspx
SIZE get_system_dpi_cached();
int scale_dpi_value(int value, unsigned original_dpi = USER_DEFAULT_SCREEN_DPI);

int get_pointer_height();

void list_view_set_explorer_theme(HWND wnd);
int list_view_insert_column_text(HWND wnd_lv, int index, const TCHAR* text, int cx);
LRESULT list_view_insert_item_text(HWND wnd_lv, int item, int subitem, const TCHAR* text, bool b_set = false,
    LPARAM lp = 0, int image_index = I_IMAGENONE);
LRESULT list_view_insert_item_text(HWND wnd_lv, int item, int subitem, const char* text, bool b_set = false,
    LPARAM lp = 0, int image_index = I_IMAGENONE);

void tree_view_set_explorer_theme(HWND wnd, bool b_reduce_indent = false);
HTREEITEM tree_view_insert_item_simple(HWND wnd_tree, const char* sz_text, LPARAM data, DWORD state = TVIS_EXPANDED,
    HTREEITEM ti_parent = TVI_ROOT, HTREEITEM ti_after = TVI_LAST, bool b_image = false, UINT image = NULL,
    UINT integral_height = 1);
HTREEITEM tree_view_insert_item_simple(HWND wnd_tree, const WCHAR* sz_text, LPARAM data, DWORD state = TVIS_EXPANDED,
    HTREEITEM ti_parent = TVI_ROOT, HTREEITEM ti_after = TVI_LAST, bool b_image = false, UINT image = NULL,
    UINT integral_height = 1);
size_t tree_view_get_child_index(HWND wnd_tv, HTREEITEM ti);
void tree_view_collapse_other_nodes(HWND wnd, HTREEITEM ti);

int combo_box_add_string_data(HWND wnd, const TCHAR* str, LPARAM data);
int combo_box_find_item_by_data(HWND wnd, size_t id);

void rebar_show_all_bands(HWND wnd);

BOOL shell_notify_icon(DWORD action, HWND wnd, UINT id, UINT version, UINT callbackmsg, HICON icon, const char* tip);
BOOL shell_notify_icon_ex(DWORD action, HWND wnd, UINT id, UINT callbackmsg, HICON icon, const char* tip,
    const char* balloon_title, const char* balloon_msg);

BOOL tooltip_add_tool(HWND wnd, const LPTOOLINFO pti);
BOOL tooltip_update_tip_text(HWND wnd, const LPTOOLINFO pti);

BOOL header_set_item_text(HWND wnd, int n, const wchar_t* text);
BOOL header_set_item_width(HWND wnd, int n, UINT cx);

void format_date(uint64_t time, std::basic_string<TCHAR>& str, bool b_convert_to_local = false);

void handle_modern_background_paint(HWND wnd, HWND wnd_button,
    HBRUSH top_background_brush = GetSysColorBrush(COLOR_WINDOW),
    HBRUSH bottom_background_brush = GetSysColorBrush(COLOR_3DFACE));

void send_message_to_direct_children(HWND wnd_parent, UINT msg, WPARAM wp, LPARAM lp);
HWND handle_tab_down(HWND wnd);

/**
 * \brief Gets window rect of a window using the co-ordinate space of another window
 * \param wnd           Window to retrieve the window rect of
 * \param wnd_parent    Window whose co-ordinate space should be used
 * \return              Window rect of wnd in wnd_parent co-ordinate space
 */
RECT get_relative_rect(HWND wnd, HWND wnd_parent);
std::wstring get_window_text(HWND wnd);
void get_window_text(HWND wnd, pfc::string_base& out);
void set_window_font(HWND wnd, HFONT font, bool redraw = true);
HFONT get_window_font(HWND wnd);

HFONT create_icon_font();
int get_font_height(HFONT font);
int get_dc_font_height(HDC dc);

std::optional<std::vector<uint8_t>> get_clipboard_data(CLIPFORMAT format);

bool set_clipboard_text(const char* text, HWND wnd = nullptr);

template <class Element = uint8_t>
bool set_clipboard_data(CLIPFORMAT format, std::span<Element> data, HWND wnd = nullptr)
{
    if (!OpenClipboard(nullptr))
        return false;

    auto succeeded = false;
    const HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, data.size_bytes());
    if (mem) {
        const auto buffer = GlobalLock(mem);

        if (buffer) {
            memcpy(buffer, data.data(), data.size_bytes());
            GlobalUnlock(mem);
            if (EmptyClipboard())
                succeeded = SetClipboardData(format, mem) != nullptr;
        }
    }
    if (!succeeded && mem)
        GlobalFree(mem);
    CloseClipboard();
    return succeeded;
}

template <typename TInteger>
class IntegerAndDpi {
public:
    using Type = IntegerAndDpi<TInteger>;

    TInteger value;
    uint32_t dpi;

    operator TInteger() const { return get_scaled_value(); }
    Type& operator=(TInteger value)
    {
        set(value);
        return *this;
    }

    void set(TInteger _value, uint32_t _dpi = get_system_dpi_cached().cx)
    {
        value = _value;
        dpi = _dpi;
    }
    TInteger get_scaled_value() const { return scale_dpi_value(value, dpi); }

    IntegerAndDpi(TInteger _value = NULL, uint32_t _dpi = USER_DEFAULT_SCREEN_DPI) : value(_value), dpi(_dpi) {}
};

void enhance_edit_control(HWND wnd);
void enhance_edit_control(HWND wnd, int id);

} // namespace uih

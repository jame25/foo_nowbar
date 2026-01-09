#include "stdafx.h"

using namespace uih::literals::spx;
using namespace std::literals;

namespace uih {

const RECT rect_null = {0, 0, 0, 0};

void list_view_set_explorer_theme(HWND wnd)
{
    if (mmh::is_windows_vista_or_newer()) {
        ListView_SetExtendedListViewStyleEx(wnd, 0x00010000 | LVS_EX_FULLROWSELECT, 0x00010000 | LVS_EX_FULLROWSELECT);
        if (mmh::is_windows_7_or_newer()) {
            SetWindowTheme(wnd, L"ItemsView", nullptr);
            // SetWindowTheme(ListView_GetHeader (wnd), L"ItemsView", NULL);
        } else {
            SetWindowTheme(wnd, L"Explorer", nullptr);
        }
    }
}

bool are_keyboard_cues_enabled()
{
    BOOL a = true;
    SystemParametersInfo(SPI_GETKEYBOARDCUES, 0, &a, 0);
    return a != 0;
}

bool is_high_contrast_active()
{
    HIGHCONTRAST hc{};
    hc.cbSize = sizeof(HIGHCONTRAST);
    SystemParametersInfo(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0);
    return (hc.dwFlags & HCF_HIGHCONTRASTON) != 0;
}

void tree_view_set_explorer_theme(HWND wnd, bool b_reduce_indent)
{
    if (mmh::is_windows_vista_or_newer()) {
        UINT_PTR stylesex = TVS_EX_DOUBLEBUFFER | TVS_EX_AUTOHSCROLL;
        UINT_PTR styles = NULL;

        SendMessage(wnd, TVM_SETEXTENDEDSTYLE, stylesex, stylesex);
        SetWindowTheme(wnd, L"Explorer", nullptr);
        SetWindowLongPtr(
            wnd, GWL_STYLE, (GetWindowLongPtr(wnd, GWL_STYLE) & ~(TVS_HASLINES /*|TVS_NOHSCROLL*/)) | styles);
        if (b_reduce_indent)
            TreeView_SetIndent(wnd, 0xa);
    }
}

int list_view_insert_column_text(HWND wnd_lv, int index, const TCHAR* text, int cx)
{
    LVCOLUMN lvc;
    memset(&lvc, 0, sizeof(LVCOLUMN));
    lvc.mask = LVCF_TEXT | LVCF_WIDTH;

    lvc.pszText = const_cast<TCHAR*>(text);
    lvc.cx = cx;
    return ListView_InsertColumn(wnd_lv, index, &lvc);
}

LRESULT list_view_insert_item_text(
    HWND wnd_lv, int item, int subitem, const TCHAR* text, bool b_set, LPARAM lp, int image_index)
{
    LVITEM lvi;
    memset(&lvi, 0, sizeof(LVITEM));
    lvi.mask = LVIF_TEXT | (b_set ? 0 : LVIF_PARAM);
    lvi.iItem = item;
    lvi.iSubItem = subitem;
    lvi.pszText = const_cast<TCHAR*>(text);
    lvi.lParam = lp;
    if (image_index != I_IMAGENONE) {
        lvi.mask |= LVIF_IMAGE;
        lvi.iImage = image_index;
    }
    return b_set ? ListView_SetItem(wnd_lv, &lvi) : ListView_InsertItem(wnd_lv, &lvi);
}

LRESULT list_view_insert_item_text(
    HWND wnd_lv, int item, int subitem, const char* text, bool b_set, LPARAM lp, int image_index)
{
    pfc::stringcvt::string_os_from_utf8 wide(text);
    return list_view_insert_item_text(
        wnd_lv, item, subitem, const_cast<TCHAR*>(wide.get_ptr()), b_set, lp, image_index);
}
HTREEITEM tree_view_insert_item_simple(HWND wnd_tree, const char* sz_text, LPARAM data, DWORD state,
    HTREEITEM ti_parent, HTREEITEM ti_after, bool b_image, UINT image, UINT integral_height)
{
    return tree_view_insert_item_simple(wnd_tree, pfc::stringcvt::string_os_from_utf8(sz_text), data, state, ti_parent,
        ti_after, b_image, image, integral_height);
}

HTREEITEM tree_view_insert_item_simple(HWND wnd_tree, const WCHAR* sz_text, LPARAM data, DWORD state,
    HTREEITEM ti_parent, HTREEITEM ti_after, bool b_image, UINT image, UINT integral_height)
{
    TVINSERTSTRUCT is;
    memset(&is, 0, sizeof(is));
    is.hParent = ti_parent;
    is.hInsertAfter = ti_after;
    is.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_STATE | (b_image ? TVIF_IMAGE | TVIF_SELECTEDIMAGE : NULL)
        | (integral_height > 1 ? TVIF_INTEGRAL : NULL);
    is.item.pszText = const_cast<WCHAR*>(sz_text);
    is.item.state = state;
    is.item.stateMask = state;
    is.item.lParam = data;
    is.item.iImage = image;
    is.item.iSelectedImage = image;
    is.itemex.iIntegral = integral_height;
    return TreeView_InsertItem(wnd_tree, &is);
}

size_t tree_view_get_child_index(HWND wnd_tv, HTREEITEM ti)
{
    HTREEITEM item = ti;
    unsigned n = 0;
    while ((item = TreeView_GetPrevSibling(wnd_tv, item)))
        n++;
    return n;
}

void tree_view_collapse_other_nodes(HWND wnd, HTREEITEM ti)
{
    auto child = ti;
    do {
        auto sibling = child;
        while ((sibling = TreeView_GetPrevSibling(wnd, sibling))) {
            TreeView_Expand(wnd, sibling, TVE_COLLAPSE);
        }
        sibling = child;
        while ((sibling = TreeView_GetNextSibling(wnd, sibling))) {
            TreeView_Expand(wnd, sibling, TVE_COLLAPSE);
        }
    } while ((child = TreeView_GetParent(wnd, child)));
}

BOOL FileTimeToLocalFileTime2(__in CONST FILETIME* lpFileTime, __out LPFILETIME lpLocalFileTime)
{
    SYSTEMTIME stUTC;
    SYSTEMTIME stLocal;
    memset(&stUTC, 0, sizeof(stUTC));
    memset(&stLocal, 0, sizeof(stLocal));

    FileTimeToSystemTime(lpFileTime, &stUTC);
    SystemTimeToTzSpecificLocalTime(nullptr, &stUTC, &stLocal);
    return SystemTimeToFileTime(&stLocal, lpLocalFileTime);
}
FILETIME filetimestamp_to_FileTime(uint64_t time)
{
    FILETIME ret;
    ret.dwLowDateTime = (DWORD)(time & 0xFFFFFFFF);
    ret.dwHighDateTime = (DWORD)(time >> 32);
    return ret;
}
void format_date(uint64_t time, std::basic_string<TCHAR>& str, bool b_convert_to_local)
{
    FILETIME ft1 = filetimestamp_to_FileTime(time);
    FILETIME ft2 = ft1;
    if (b_convert_to_local)
        FileTimeToLocalFileTime2(&ft1, &ft2);
    SYSTEMTIME st;
    FileTimeToSystemTime(&ft2, &st);
    int size = GetDateFormat(LOCALE_USER_DEFAULT, 0, &st, nullptr, nullptr, NULL);
    int size2 = GetTimeFormat(LOCALE_USER_DEFAULT, 0, &st, nullptr, nullptr, NULL);
    pfc::array_t<TCHAR> buf, buf2;
    buf.set_size(size);
    buf2.set_size(size2);
    GetDateFormat(LOCALE_USER_DEFAULT, 0, &st, nullptr, buf.get_ptr(), size);
    GetTimeFormat(LOCALE_USER_DEFAULT, 0, &st, nullptr, buf2.get_ptr(), size2);
    str = _T("");
    str += buf.get_ptr();
    str += _T(" ");
    str += buf2.get_ptr();
}
BOOL set_process_dpi_aware()
{
    using SETPROCESSDPIAWAREPROC = BOOL(WINAPI*)();
    HINSTANCE hinstDll = LoadLibrary(_T("user32.dll"));

    if (hinstDll) {
        auto pSetProcessDPIAware = (SETPROCESSDPIAWAREPROC)GetProcAddress(hinstDll, "SetProcessDPIAware");

        if (pSetProcessDPIAware) {
            return pSetProcessDPIAware();
        }
    }

    return FALSE;
}

SIZE get_system_dpi()
{
    HDC dc = GetDC(nullptr);
    SIZE ret = {GetDeviceCaps(dc, LOGPIXELSX), GetDeviceCaps(dc, LOGPIXELSY)};
    ReleaseDC(nullptr, dc);
    return ret;
}

SIZE get_system_dpi_cached()
{
    static const SIZE size = get_system_dpi();
    return size;
}

int scale_dpi_value(int value, unsigned original_dpi)
{
    return MulDiv(value, get_system_dpi_cached().cx, original_dpi);
}

int get_pointer_height()
{
    int win_10_pointer_size{};
    if (SystemParametersInfo(0x2028, 0, &win_10_pointer_size, 0) && win_10_pointer_size > 0)
        return win_10_pointer_size;

    const auto cursor
        = static_cast<HCURSOR>(LoadImage(nullptr, IDC_ARROW, IMAGE_CURSOR, 0, 0, LR_DEFAULTSIZE | LR_SHARED));

    ICONINFO icon_info{};
    if (!GetIconInfo(cursor, &icon_info))
        return 0;

    const wil::unique_hbitmap _colour_bitmap(icon_info.hbmColor);
    const wil::unique_hbitmap _mask_bitmap(icon_info.hbmMask);

    if (!icon_info.hbmMask)
        return 0;

    BITMAP bitmap_info{};
    if (!GetObject(icon_info.hbmMask, sizeof(BITMAP), &bitmap_info))
        return 0;

    const auto bitmap_height = gsl::narrow<int>(icon_info.hbmColor ? bitmap_info.bmHeight : bitmap_info.bmHeight / 2);
    const auto bitmap_width = gsl::narrow<int>(bitmap_info.bmWidth);

    const wil::unique_hdc_window desktop_dc(GetDC(nullptr));
    const wil::unique_hdc memory_dc(CreateCompatibleDC(desktop_dc.get()));
    auto _ = wil::SelectObject(memory_dc.get(), icon_info.hbmMask);

    for (auto y : ranges::views::iota(0, bitmap_height) | ranges::views::reverse) {
        const auto any_pixel_set = ranges::any_of(ranges::views::iota(0, bitmap_width), [&memory_dc, y](auto x) {
            const auto colour = GetPixel(memory_dc.get(), x, y);
            return colour != 0xFFFFFF;
        });

        if (any_pixel_set)
            return y + 1 - gsl::narrow<int>(icon_info.yHotspot);
    }

    return 0;
}

BOOL run_action(DWORD action, NOTIFYICONDATA* data)
{
    if (Shell_NotifyIcon(action, data))
        return TRUE;
    if (action == NIM_MODIFY) {
        if (Shell_NotifyIcon(NIM_ADD, data))
            return TRUE;
    }
    return FALSE;
}

BOOL shell_notify_icon(DWORD action, HWND wnd, UINT id, UINT version, UINT callbackmsg, HICON icon, const char* tip)
{
    NOTIFYICONDATA nid;
    memset(&nid, 0, sizeof(nid));
    nid.cbSize = NOTIFYICONDATA_V2_SIZE;
    nid.hWnd = wnd;
    nid.uID = id;
    nid.uFlags = 0;
    if (action & NIM_SETVERSION)
        nid.uVersion = version;
    if (callbackmsg) {
        nid.uFlags |= NIF_MESSAGE;
        nid.uCallbackMessage = callbackmsg;
    }
    if (icon) {
        nid.uFlags |= NIF_ICON;
        nid.hIcon = icon;
    }
    if (tip) {
        nid.uFlags |= NIF_TIP;
        _tcsncpy_s(nid.szTip, pfc::stringcvt::string_os_from_utf8(tip), tabsize(nid.szTip) - 1);
    }

    return run_action(action, &nid);
}

BOOL shell_notify_icon_ex(DWORD action, HWND wnd, UINT id, UINT callbackmsg, HICON icon, const char* tip,
    const char* balloon_title, const char* balloon_msg)
{
    NOTIFYICONDATA nid;
    memset(&nid, 0, sizeof(nid));
    nid.cbSize = NOTIFYICONDATA_V2_SIZE;
    nid.hWnd = wnd;
    nid.uID = id;
    if (callbackmsg) {
        nid.uFlags |= NIF_MESSAGE;
        nid.uCallbackMessage = callbackmsg;
    }
    if (icon) {
        nid.uFlags |= NIF_ICON;
        nid.hIcon = icon;
    }
    if (tip) {
        nid.uFlags |= NIF_TIP;
        _tcsncpy_s(nid.szTip, pfc::stringcvt::string_os_from_utf8(tip), tabsize(nid.szTip) - 1);
    }

    nid.dwInfoFlags = NIIF_INFO | NIIF_NOSOUND;
    // if (balloon_title || balloon_msg)
    {
        nid.uFlags |= NIF_INFO;
        if (balloon_title)
            _tcsncpy_s(
                nid.szInfoTitle, pfc::stringcvt::string_os_from_utf8(balloon_title), tabsize(nid.szInfoTitle) - 1);
        if (balloon_msg)
            _tcsncpy_s(nid.szInfo, pfc::stringcvt::string_os_from_utf8(balloon_msg), tabsize(nid.szInfo) - 1);
    }
    return run_action(action, reinterpret_cast<NOTIFYICONDATA*>(&nid));
}
int combo_box_add_string_data(HWND wnd, const TCHAR* str, LPARAM data)
{
    int index = ComboBox_AddString(wnd, str);
    ComboBox_SetItemData(wnd, index, data);
    return index;
}

int combo_box_find_item_by_data(HWND wnd, size_t id)
{
    int count = ComboBox_GetCount(wnd);
    for (int i = 0; i < count; i++)
        if (ComboBox_GetItemData(wnd, i) == id)
            return i;
    return -1;
}

void rebar_show_all_bands(HWND wnd)
{
    const UINT count = static_cast<UINT>(SendMessage(wnd, RB_GETBANDCOUNT, 0, 0));
    unsigned n;
    for (n = 0; n < count; n++) {
        SendMessage(wnd, RB_SHOWBAND, n, TRUE);
    }
}

void handle_modern_background_paint(
    HWND wnd, HWND wnd_button, HBRUSH top_background_brush, HBRUSH bottom_background_brush)
{
    const auto dpi = dpi::get_dpi_for_window(wnd);
    const auto padding_height = dpi::scale_value(dpi, 12);

    PAINTSTRUCT ps{};
    const auto paint_dc = wil::BeginPaint(wnd, &ps);
    const BufferedDC buffered_dc(paint_dc.get(), ps.rcPaint);

    RECT rc_client{};
    GetClientRect(wnd, &rc_client);
    RECT rc_fill{rc_client};

    if (wnd_button) {
        RECT rc_button{};
        GetWindowRect(wnd_button, &rc_button);
        MapWindowPoints(HWND_DESKTOP, wnd, reinterpret_cast<LPPOINT>(&rc_button), 2);
        rc_fill.bottom = rc_button.top - padding_height;
    }

    FillRect(buffered_dc.get(), &rc_fill, top_background_brush);

    if (wnd_button) {
        rc_fill.top = rc_fill.bottom;
        rc_fill.bottom = rc_client.bottom;
        FillRect(buffered_dc.get(), &rc_fill, bottom_background_brush);
    }
}

void send_message_to_direct_children(HWND wnd_parent, UINT msg, WPARAM wp, LPARAM lp)
{
    HWND wnd = GetWindow(wnd_parent, GW_CHILD);
    if (!wnd)
        return;

    do {
        SendMessage(wnd, msg, wp, lp);
    } while ((wnd = GetWindow(wnd, GW_HWNDNEXT)));
}

RECT get_relative_rect(HWND wnd, HWND wnd_parent)
{
    RECT rc{0};
    GetWindowRect(wnd, &rc);
    MapWindowPoints(HWND_DESKTOP, wnd_parent, reinterpret_cast<LPPOINT>(&rc), 2);
    return rc;
}

std::wstring get_window_text(HWND wnd)
{
    const auto buffer_size = GetWindowTextLength(wnd) + 1;

    if (buffer_size <= 0)
        return {};

    std::wstring text;
    text.resize(buffer_size);

    const auto chars_written = GetWindowText(wnd, text.data(), buffer_size);

    if (chars_written <= 0)
        return {};

    text.resize(chars_written);
    return text;
}

void get_window_text(HWND wnd, pfc::string_base& out)
{
    auto text = get_window_text(wnd);
    const auto utf8_size = pfc::stringcvt::estimate_wide_to_utf8(text.data(), text.size());
    auto utf8_buffer = pfc::string_buffer(out, utf8_size);
    pfc::stringcvt::convert_wide_to_utf8(utf8_buffer, utf8_size, text.data(), text.size());
}

void set_window_font(HWND wnd, HFONT font, bool redraw)
{
    SendMessage(wnd, WM_SETFONT, reinterpret_cast<WPARAM>(font), MAKELPARAM(redraw, 0));
}

HFONT get_window_font(HWND wnd)
{
    return reinterpret_cast<HFONT>(SendMessage(wnd, WM_GETFONT, 0, 0));
}

HFONT create_icon_font()
{
    LOGFONT lf;
    memset(&lf, 0, sizeof(LOGFONT));
    SystemParametersInfo(SPI_GETICONTITLELOGFONT, 0, &lf, 0);

    return CreateFontIndirectW(&lf);
}

int get_font_height(HFONT font)
{
    HDC dc{GetDC(nullptr)};
    HFONT font_old = SelectFont(dc, font);
    const auto font_height = get_dc_font_height(dc);
    SelectFont(dc, font_old);
    ReleaseDC(nullptr, dc);
    return font_height;
}

int get_dc_font_height(HDC dc)
{
    TEXTMETRIC tm{};
    GetTextMetrics(dc, &tm);
    // Default mapping mode is MM_TEXT, so we don't map the units
    return tm.tmHeight > 1 ? tm.tmHeight : 1;
}

std::optional<std::vector<uint8_t>> get_clipboard_data(CLIPFORMAT format)
{
    if (!OpenClipboard(nullptr))
        return {};

    auto _ = gsl::finally([] { CloseClipboard(); });

    const auto global_mem = GetClipboardData(format);

    if (!global_mem)
        return {};

    const uint8_t* data = reinterpret_cast<uint8_t*>(GlobalLock(global_mem));

    if (!data)
        return {};

    const auto size = GlobalSize(global_mem);
    std::vector<uint8_t> read_data(data, data + size);

    GlobalUnlock(global_mem);

    return {std::move(read_data)};
}

BOOL tooltip_add_tool(HWND wnd, const LPTOOLINFO pti)
{
    return static_cast<BOOL>(SendMessage(wnd, TTM_ADDTOOL, 0, reinterpret_cast<LPARAM>(pti)));
}

BOOL tooltip_update_tip_text(HWND wnd, const LPTOOLINFO pti)
{
    return static_cast<BOOL>(SendMessage(wnd, TTM_UPDATETIPTEXT, 0, reinterpret_cast<LPARAM>(pti)));
}

BOOL header_set_item_text(HWND wnd, int n, const wchar_t* text)
{
    HDITEM hdi;
    memset(&hdi, 0, sizeof(hdi));
    hdi.mask = HDI_TEXT;
    hdi.cchTextMax = NULL;
    hdi.pszText = const_cast<wchar_t*>(text);
    return Header_SetItem(wnd, n, &hdi);
}

BOOL header_set_item_width(HWND wnd, int n, UINT cx)
{
    HDITEM hdi;
    memset(&hdi, 0, sizeof(hdi));
    hdi.mask = HDI_WIDTH;
    hdi.cxy = cx;
    return Header_SetItem(wnd, n, &hdi);
}

HWND handle_tab_down(HWND wnd)
{
    HWND wnd_focused{nullptr};
    HWND wnd_temp{GetAncestor(wnd, GA_ROOT)};

    if (wnd_temp) {
        HWND wnd_next = GetNextDlgTabItem(wnd_temp, wnd, (GetKeyState(VK_SHIFT) & KF_UP) ? TRUE : FALSE);
        if (wnd_next && wnd_next != wnd) {
            const auto flags = SendMessage(wnd_next, WM_GETDLGCODE, 0, 0);
            if (flags & DLGC_HASSETSEL)
                SendMessage(wnd_next, EM_SETSEL, 0, -1);
            SetFocus(wnd_next);

            wnd_focused = wnd_next;
        }
    }
    return wnd_focused;
}

bool set_clipboard_text(const char* text, HWND wnd)
{
    const pfc::stringcvt::string_wide_from_utf8 text_utf16(text);
    return set_clipboard_data<const wchar_t>(CF_UNICODETEXT, {text_utf16.get_ptr(), text_utf16.length() + 1}, wnd);
}

void enhance_edit_control(HWND wnd)
{
    auto send_backspace = [wnd] { SendMessage(wnd, WM_CHAR, 8, 0); };

    auto delete_text = [wnd, send_backspace](DWORD start, DWORD end) {
        Edit_SetSel(wnd, start, end);
        send_backspace();
    };

    subclass_window(
        wnd, [send_backspace, delete_text](auto _, HWND wnd, UINT msg, WPARAM wp, LPARAM lp) -> std::optional<LRESULT> {
            switch (msg) {
            case WM_KEYDOWN:
                // If Autocomplete is enabled, it processes Ctrl+Backspace here
                if (wp == VK_BACK && (GetKeyState(VK_CONTROL) & 0x8000))
                    return 0;
                break;
            case WM_CHAR:
                switch (wp) {
                // Ctrl+A
                // Note: The Edit control on Windows 10 1809 and newer also processes Ctrl-A in WM_CHAR
                case 1: {
                    if (!(HIWORD(lp) & KF_REPEAT) && (GetKeyState(VK_CONTROL) & 0x8000)) {
                        Edit_SetSel(wnd, 0, -1);
                    }
                    return 0;
                }
                // Ctrl+Backspace
                case 0x7f: {
                    const auto styles = GetWindowLongPtr(wnd, GWL_STYLE);

                    if (styles & (WS_DISABLED | ES_READONLY))
                        return 0;

                    DWORD selection_start{};
                    DWORD selection_end{};
                    SendMessage(wnd, EM_GETSEL, reinterpret_cast<WPARAM>(&selection_start),
                        reinterpret_cast<LPARAM>(&selection_end));

                    if (selection_start != selection_end) {
                        send_backspace();
                        return 0;
                    }

                    if (selection_start == 0)
                        return 0;

                    const auto text_length = GetWindowTextLength(wnd);

                    if (text_length < 0 || selection_start > gsl::narrow<DWORD>(std::numeric_limits<int>::max())
                        || gsl::narrow<int>(selection_start) > text_length)
                        return 0;

                    const auto cursor_pos = gsl::narrow<int>(selection_start);

                    std::vector<wchar_t> buffer(gsl::narrow<size_t>(text_length + 1));

                    GetWindowText(wnd, buffer.data(), gsl::narrow<int>(buffer.size()));

                    const auto space_chars = L" "s;
                    const auto line_break_chars = L"\n"s;
                    const auto fragment_break_chars = space_chars + line_break_chars;

                    auto start_of_fragment = cursor_pos;

                    while (start_of_fragment > 0
                        && space_chars.find_first_of(buffer[start_of_fragment - 1]) != std::wstring_view::npos)
                        --start_of_fragment;

                    while (start_of_fragment > 0
                        && fragment_break_chars.find_first_of(buffer[start_of_fragment - 1]) == std::wstring_view::npos)
                        --start_of_fragment;

                    auto end_of_fragment = cursor_pos;

                    while (end_of_fragment < text_length
                        && fragment_break_chars.find_first_of(buffer[end_of_fragment]) == std::wstring_view::npos)
                        ++end_of_fragment;

                    SCRIPT_STRING_ANALYSIS ssa{};
                    const auto hr = ScriptStringAnalyse(nullptr, buffer.data() + start_of_fragment,
                        end_of_fragment - start_of_fragment, 0, -1, SSA_LINK | SSA_BREAK, 0, nullptr, nullptr, nullptr,
                        nullptr, nullptr, &ssa);

                    if (FAILED(hr)) {
                        delete_text(start_of_fragment, cursor_pos);
                        return 0;
                    }

                    auto _ = gsl::finally([&ssa] {
                        if (FAILED(ScriptStringFree(&ssa)))
                            OutputDebugString(L"Warning: ScriptStringFree() failed");
                    });

                    const auto log_attr = ScriptString_pLogAttr(ssa);

                    if (!log_attr) {
                        delete_text(start_of_fragment, cursor_pos);
                        return 0;
                    }

                    auto delete_from_pos = cursor_pos - start_of_fragment;

                    if (delete_from_pos > 0 && log_attr[delete_from_pos].fWordStop)
                        --delete_from_pos;

                    while (!log_attr[delete_from_pos].fWordStop && delete_from_pos > 0)
                        --delete_from_pos;

                    delete_text(start_of_fragment + delete_from_pos, cursor_pos);
                    return 0;
                }
                }
            }
            return {};
        });
}

void enhance_edit_control(HWND wnd, int id)
{
    enhance_edit_control(GetDlgItem(wnd, id));
}

} // namespace uih

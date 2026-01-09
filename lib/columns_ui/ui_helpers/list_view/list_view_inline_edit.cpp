#include "stdafx.h"

#include "list_view.h"

namespace uih {

using namespace literals::spx;

void ListView::set_inline_edit_window_theme() const
{
    if (!m_wnd_inline_edit)
        return;

    SetWindowTheme(m_wnd_inline_edit, m_use_dark_mode ? L"DarkMode_Explorer" : nullptr, nullptr);
}

void ListView::set_inline_edit_rect() const
{
    if (!m_wnd_inline_edit)
        return;

    RECT window_rect{};
    GetWindowRect(m_wnd_inline_edit, &window_rect);

    const auto font_height = get_font_height(m_items_font.get());

    // Edit_SetRect makes the rectangle passed to it slightly smaller to
    // accomodate the control's border
    // So, we have to work out what that adjustment is, and make our rectangle
    // slightly larger to compensate for it
    RECT rc_client{};
    GetClientRect(m_wnd_inline_edit, &rc_client);
    Edit_SetRectNoPaint(m_wnd_inline_edit, &rc_client);

    RECT rc_client_adjusted{};
    Edit_GetRect(m_wnd_inline_edit, &rc_client_adjusted);

    RECT rc{};
    rc.left = 3_spx + 1_spx - rc_client_adjusted.left + rc_client.left;
    rc.top = (RECT_CY(window_rect) - font_height) / 2;
    rc.right = rc_client.right - 3_spx + rc_client.right - rc_client_adjusted.right - 1_spx;
    rc.bottom = rc.top + font_height;

    Edit_SetRect(m_wnd_inline_edit, &rc);
}

void ListView::activate_inline_editing(size_t column_start)
{
    size_t count = m_columns.size();
    if (count) {
        size_t focus = get_focus_item();
        if (focus != pfc_infinite) {
            size_t i;
            size_t pcount = m_items.size();
            pfc::bit_array_bittable sel(pcount);
            get_selection_state(sel);

            pfc::list_t<size_t> indices;
            indices.prealloc(32);
            for (i = 0; i < pcount; i++)
                if (sel[i])
                    indices.add_item(i);

            if (column_start > count)
                column_start = 0;

            size_t column;
            for (column = column_start; column < count; column++) {
                if (notify_before_create_inline_edit(indices, column, false)) {
                    create_inline_edit(indices, column);
                    break;
                }
            }
        }
    }
}

void ListView::activate_inline_editing(const pfc::list_base_const_t<size_t>& indices, size_t column)
{
    size_t count = m_columns.size();
    if (column < count) {
        if (notify_before_create_inline_edit(indices, column, false)) {
            create_inline_edit(indices, column);
        }
    }
}
void ListView::activate_inline_editing(size_t index, size_t column)
{
    activate_inline_editing(pfc::list_single_ref_t<size_t>(index), column);
}

LRESULT WINAPI ListView::s_on_inline_edit_message(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) noexcept
{
    ListView* p_this;
    LRESULT rv;

    p_this = reinterpret_cast<ListView*>(GetWindowLongPtr(wnd, GWLP_USERDATA));

    rv = p_this ? p_this->on_inline_edit_message(wnd, msg, wp, lp) : DefWindowProc(wnd, msg, wp, lp);
    ;

    return rv;
}

LRESULT ListView::on_inline_edit_message(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_KILLFOCUS:
        if (!m_inline_edit_prevent_kill) {
            save_inline_edit();
            PostMessage(get_wnd(), MSG_KILL_INLINE_EDIT, 0, 0);
        }
        invalidate_all();
        break;
    case WM_SETFOCUS:
        break;
    case WM_GETDLGCODE:
        return CallWindowProc(m_proc_inline_edit, wnd, msg, wp, lp) | DLGC_WANTALLKEYS;
    case WM_KEYDOWN:
        switch (wp) {
        case VK_TAB: {
            size_t count = m_columns.size();
            size_t indices_count = m_inline_edit_indices.get_count();
            assert(indices_count);
            if (count && indices_count) {
                bool back = (GetKeyState(VK_SHIFT) & KF_UP) != 0;
                if (back) {
                    size_t column = m_inline_edit_column;
                    size_t row = m_inline_edit_indices[0];
                    pfc::string8_fast_aggressive temp;
                    bool found = false;

                    if (indices_count == 1) {
                        do {
                            pfc::list_single_ref_t<size_t> indices(row);
                            while (column > 0
                                && !((found = notify_before_create_inline_edit(indices, --column, false)))) {}
                            if (!found)
                                column = count;
                        } while (!found && row > 0 && --row >= 0);

                        if (found) {
                            pfc::list_single_ref_t<size_t> indices(row);
                            create_inline_edit(indices, column);
                        }
                    } else {
                        while (column > 0
                            && !((found = notify_before_create_inline_edit(m_inline_edit_indices, --column, false)))) {}

                        if (found) {
                            create_inline_edit(m_inline_edit_indices, column);
                        }
                    }
                } else {
                    size_t column = m_inline_edit_column + 1;
                    size_t row = m_inline_edit_indices[0];
                    size_t row_count = m_items.size();
                    pfc::string8_fast_aggressive temp;
                    bool found = false;

                    if (indices_count == 1) {
                        do {
                            pfc::list_single_ref_t<size_t> indices(row);
                            while (column < count
                                && !((found = notify_before_create_inline_edit(indices, column, false)))) {
                                column++;
                            }
                            if (!found)
                                column = 0;
                        } while (!found && ++row < row_count);

                        if (found) {
                            pfc::list_single_ref_t<size_t> indices(row);
                            create_inline_edit(indices, column);
                        }

                    } else {
                        while (column < count
                            && !((found = notify_before_create_inline_edit(m_inline_edit_indices, column, false)))) {
                            column++;
                        }

                        if (found) {
                            create_inline_edit(m_inline_edit_indices, column);
                        }
                    }
                }
            }
        }
            return 0;
        case VK_ESCAPE:
            m_inline_edit_save = false;
            exit_inline_edit();
            return 0;
        case VK_RETURN:
            if ((GetKeyState(VK_CONTROL) & KF_UP) == 0)
                exit_inline_edit();
            // else
            //    return CallWindowProc(m_proc_original_inline_edit,wnd,msg,wp,lp); //cheat
            return 0;
        }
        break;
    }
    return CallWindowProc(m_proc_inline_edit, wnd, msg, wp, lp);
}

void ListView::create_inline_edit(const pfc::list_base_const_t<size_t>& indices, size_t column)
{
    size_t indices_count = indices.get_count();
    if (!indices_count)
        return;
    if (!(column < m_columns.size()) || m_selecting) {
        // console::print("internal error - edit column index out of range");
        return;
    }

    {
        size_t item_count = m_items.size();
        for (size_t j = 0; j < indices_count; j++) {
            if (indices[j] >= item_count)
                return;
        }
    }

    const auto indices_spread = indices[indices_count - 1] - indices[0] + 1;
    const auto items_top = get_item_position(indices[0]);
    const auto items_bottom = get_item_position_bottom(indices[indices_count - 1]);
    const int indices_total_height = std::min(items_bottom - items_top, MAXLONG);

    if (m_timer_inline_edit) {
        KillTimer(get_wnd(), EDIT_TIMER_ID);
        m_timer_inline_edit = false;
    }

    const auto start_visible = is_fully_visible(indices[0]);
    const auto end_visible = is_fully_visible(indices[indices_count - 1]);

    if (!start_visible || !end_visible) {
        const auto item_area_height = get_item_area_height();
        int new_scroll_position{};

        if (items_bottom - items_top > item_area_height) {
            const auto mid_point = indices[0] + indices_spread / 2;
            new_scroll_position
                = get_item_position(mid_point) + (get_item_height(mid_point) - get_item_area_height()) / 2;
        } else if (get_item_position(indices[0]) > m_scroll_position) {
            const auto last_index = indices[indices_count - 1];
            new_scroll_position = get_item_position_bottom(last_index) - get_item_area_height();
        } else {
            new_scroll_position = get_item_position(indices[0]);
        }

        scroll(new_scroll_position);
    }

    int x;
    {
        x = get_total_indentation();

        unsigned n;
        size_t count = m_columns.size();
        for (n = 0; n < count && n < column; n++) {
            x += m_columns[n].m_display_size;
        }
    }

    RECT rc_playlist;
    GetClientRect(get_wnd(), &rc_playlist);
    const auto rc_items = get_items_rect();

    int font_height = uih::get_font_height(m_items_font.get());
    int header_height = rc_items.top;

    int cx = m_columns[column].m_display_size;
    int cy = std::min(std::max(indices_total_height, font_height), gsl::narrow<int>(rc_items.bottom - rc_items.top));
    int y = get_item_position(indices[0]) - m_scroll_position + header_height;
    if (cy > indices_total_height)
        y -= (cy - indices_total_height) / 2;
    if (y < header_height)
        y = header_height;

    if (!m_autosize
        && ((x - m_horizontal_scroll_position < 0) || x + cx - m_horizontal_scroll_position > rc_items.right)) {
        if (x - m_horizontal_scroll_position < 0) {
            scroll(x, true);
        } else if (x + cx - m_horizontal_scroll_position > rc_items.right) {
            const int x_right = x + cx - rc_items.right;
            scroll(cx > rc_items.right ? x : x_right, true);
        }
    }

    x -= m_horizontal_scroll_position;

    if (m_wnd_inline_edit) {
        save_inline_edit();

        m_inline_edit_prevent_kill = true;
        DestroyWindow(m_wnd_inline_edit);
        m_wnd_inline_edit = nullptr;
        m_inline_edit_autocomplete.reset();
        m_inline_edit_prevent_kill = false;
    }

    pfc::string8 text;
    size_t flags = 0;
    wil::com_ptr<IUnknown> autocomplete_entries;
    if (!notify_create_inline_edit(indices, column, text, flags, autocomplete_entries)) {
        m_inline_edit_save = false;
        exit_inline_edit();
        return;
    }

    if (!m_wnd_inline_edit) {
        m_inline_edit_save = true;
        m_wnd_inline_edit = CreateWindowEx(0, WC_EDIT, pfc::stringcvt::string_os_from_utf8(text).get_ptr(),
            WS_CHILD | WS_CLIPSIBLINGS | ES_LEFT | ES_AUTOHSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | WS_BORDER
                | WS_CLIPCHILDREN | ((flags & inline_edit_uppercase) ? ES_UPPERCASE : 0),
            x, y, cx, cy, get_wnd(), HMENU(IDC_INLINEEDIT), wil::GetModuleInstanceHandle(), nullptr);

        m_proc_original_inline_edit = reinterpret_cast<WNDPROC>(GetWindowLongPtr(m_wnd_inline_edit, GWLP_WNDPROC));

        if (autocomplete_entries) {
            m_inline_edit_autocomplete = wil::CoCreateInstanceNoThrow<IAutoComplete>(CLSID_AutoComplete);

            if (m_inline_edit_autocomplete) {
                if (autocomplete_entries)
                    m_inline_edit_autocomplete->Init(m_wnd_inline_edit, autocomplete_entries.get(), nullptr, nullptr);

                const auto pA2 = m_inline_edit_autocomplete.try_query<IAutoComplete2>();
                const auto pAutoCompleteDropDown = m_inline_edit_autocomplete.try_query<IAutoCompleteDropDown>();
                if (pA2) {
                    pA2->SetOptions(ACO_AUTOSUGGEST | ACO_UPDOWNKEYDROPSLIST);
                }
            }
        }

        SetWindowLongPtr(m_wnd_inline_edit, GWLP_USERDATA, reinterpret_cast<LPARAM>(this));
        m_proc_inline_edit = reinterpret_cast<WNDPROC>(
            SetWindowLongPtr(m_wnd_inline_edit, GWLP_WNDPROC, reinterpret_cast<LPARAM>(s_on_inline_edit_message)));

        enhance_edit_control(m_wnd_inline_edit);

        SetWindowPos(m_wnd_inline_edit, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);

        set_inline_edit_window_theme();
        SendMessage(m_wnd_inline_edit, WM_SETFONT, reinterpret_cast<WPARAM>(m_items_font.get()), MAKELONG(TRUE, 0));

        m_inline_edit_initial_text.reset();
        get_window_text(m_wnd_inline_edit, m_inline_edit_initial_text);
    }

    if (!m_inline_edit_mouse_hook && m_wnd_inline_edit) {
        m_inline_edit_mouse_hook
            = register_message_hook(MessageHookType::type_mouse, [this](int code, WPARAM wp, LPARAM lp) -> bool {
                  const auto is_click
                      = wp == WM_LBUTTONDOWN || wp == WM_RBUTTONDOWN || wp == WM_MBUTTONDOWN || wp == WM_XBUTTONDOWN;
                  const auto is_nc_click = wp == WM_NCLBUTTONDOWN || wp == WM_NCRBUTTONDOWN || wp == WM_NCMBUTTONDOWN
                      || wp == WM_NCXBUTTONDOWN;
                  const auto lpmhs = reinterpret_cast<LPMOUSEHOOKSTRUCT>(lp);

                  if (code == HC_ACTION && (is_click || is_nc_click) && m_wnd_inline_edit
                      && lpmhs->hwnd != m_wnd_inline_edit) {
                      const auto is_autocomplete_window
                          = [is_nc_click, wnd_inline_edit = m_wnd_inline_edit, wnd = lpmhs->hwnd] {
                                if (is_nc_click || wnd == wnd_inline_edit || wnd == nullptr)
                                    return false;

                                const auto parent = GetAncestor(wnd, GA_PARENT);

                                if (parent == nullptr)
                                    return false;

                                std::array<wchar_t, 128> class_name{};
                                GetClassName(parent, class_name.data(), gsl::narrow<int>(class_name.size()));
                                return wcsncmp(L"Auto-Suggest Dropdown", class_name.data(), class_name.size()) == 0;
                            }();

                      if (!is_autocomplete_window)
                          PostMessage(get_wnd(), MSG_KILL_INLINE_EDIT, TRUE, 0);
                  }

                  return false;
              });
    }

    set_inline_edit_rect();

    SendMessage(m_wnd_inline_edit, EM_SETSEL, 0, -1);
    ShowWindow(m_wnd_inline_edit, SW_SHOWNORMAL);
    SetFocus(m_wnd_inline_edit);

    if (&indices != &m_inline_edit_indices)
        m_inline_edit_indices = indices;
    m_inline_edit_column = column;
}

void ListView::save_inline_edit()
{
    if (m_inline_edit_save && !m_inline_edit_saving) {
        m_inline_edit_saving = true;

        pfc::string8 text;
        uih::get_window_text(m_wnd_inline_edit, text);

        if (strcmp(text, m_inline_edit_initial_text) != 0)
            notify_save_inline_edit(text.get_ptr());

        m_inline_edit_saving = false;
    }
    m_inline_edit_save = true;
}

void ListView::exit_inline_edit()
{
    m_inline_edit_mouse_hook.reset();
    m_inline_edit_autocomplete.reset();
    if (m_wnd_inline_edit) {
        DestroyWindow(m_wnd_inline_edit);
        m_wnd_inline_edit = nullptr;
    }

    if (m_timer_inline_edit) {
        KillTimer(get_wnd(), EDIT_TIMER_ID);
        m_timer_inline_edit = false;
    }
    m_inline_edit_initial_text.reset();
    m_inline_edit_indices.remove_all();
    m_edit_background_brush.reset();
    notify_exit_inline_edit();
}

} // namespace uih

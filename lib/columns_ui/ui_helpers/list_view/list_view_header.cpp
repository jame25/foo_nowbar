#include "stdafx.h"

#include "list_view.h"
#include "../direct_write_text_out.h"

namespace uih {

using namespace literals::spx;

void ListView::destroy_header()
{
    if (m_wnd_header) {
        DestroyWindow(m_wnd_header);
        m_wnd_header = nullptr;
        m_header_font.reset();
    }
}

void ListView::create_header()
{
    // if (m_show_header)
    {
        if (!m_wnd_header) {
            m_header_font.reset(m_header_log_font ? CreateFontIndirect(&*m_header_log_font) : nullptr);
            m_wnd_header = CreateWindowEx(0, WC_HEADER, _T("NGLVH"),
                WS_CHILD | (0) | /*(m_autosize ? 0x0800  : NULL) |*/ HDS_HOTTRACK
                    | (m_allow_header_rearrange ? HDS_DRAGDROP : NULL) | HDS_HORZ | HDS_FULLDRAG
                    | (m_sorting_enabled ? HDS_BUTTONS : 0) | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
                0, 0, 0, 0, get_wnd(), HMENU(IDC_HEADER), wil::GetModuleInstanceHandle(), nullptr);

            set_header_window_theme();
            SetWindowFont(m_wnd_header, m_header_font.get(), FALSE);

            if (m_initialised) {
                build_header();
                ShowWindow(m_wnd_header, SW_SHOWNORMAL);
            }
        }
    }
}

void ListView::set_header_window_theme() const
{
    SetWindowTheme(m_wnd_header, m_use_dark_mode ? L"DarkMode_ItemsView" : nullptr, nullptr);
}

void ListView::set_show_header(bool new_value)
{
    if (new_value == m_show_header)
        return;

    m_show_header = new_value;

    if (!m_initialised)
        return;

    if (m_show_header)
        create_header();
    else
        destroy_header();

    invalidate_all();
    on_size();
}

void ListView::reposition_header()
{
    RECT rc;
    GetClientRect(get_wnd(), &rc);
    int cx = RECT_CX(rc);
    RECT rc_header;
    get_header_rect(&rc_header);
    SetWindowPos(m_wnd_header, nullptr, -m_horizontal_scroll_position, 0, cx + m_horizontal_scroll_position,
        RECT_CY(rc_header), SWP_NOZORDER);
}

void ListView::build_header()
{
    if (m_wnd_header) {
        pfc::vartoggle_t<bool> toggle(m_ignore_column_size_change_notification, true);

        size_t header_count = Header_GetItemCount(m_wnd_header);
        for (; header_count; header_count--)
            Header_DeleteItem(m_wnd_header, header_count - 1);

        HDITEM hdi{};
        hdi.mask = HDI_FORMAT | HDI_WIDTH;

        int insertion_index = 0;

        if (const auto indentation = get_total_indentation(); indentation > 0) {
            hdi.fmt = HDF_LEFT;
            hdi.cxy = indentation;
            Header_InsertItem(m_wnd_header, insertion_index++, &hdi);
            m_have_indent_column = true;
        } else
            m_have_indent_column = false;

        for (auto&& [column_index, column] : ranges::views::enumerate(m_columns)) {
            if (column.m_alignment == ALIGN_CENTRE)
                hdi.fmt |= HDF_CENTER;
            else if (column.m_alignment == ALIGN_RIGHT)
                hdi.fmt |= HDF_RIGHT;
            else
                hdi.fmt = HDF_LEFT;

            hdi.cxy = column.m_display_size;

            if (m_sort_column_index && *m_sort_column_index == column_index && m_show_sort_indicators) {
                hdi.fmt |= (m_sort_direction ? HDF_SORTDOWN : HDF_SORTUP);
            }

            if (mmh::is_wine()) {
                const auto text = mmh::to_utf16(column.m_title.c_str());
                hdi.mask |= HDI_TEXT;
                hdi.fmt |= HDF_STRING;
                hdi.pszText = const_cast<LPWSTR>(text.c_str());

                Header_InsertItem(m_wnd_header, insertion_index++, &hdi);
            } else {
                Header_InsertItem(m_wnd_header, insertion_index++, &hdi);
            }
        }
    }
}

std::optional<LRESULT> ListView::on_wm_notify_header(LPNMHDR lpnm)
{
    switch (lpnm->code) {
    case NM_CUSTOMDRAW: {
        const auto lpcd = reinterpret_cast<LPNMCUSTOMDRAW>(lpnm);
        switch (lpcd->dwDrawStage) {
        case CDDS_PREPAINT:
            return CDRF_NOTIFYITEMDRAW;
        case CDDS_ITEMPREPAINT:
            return CDRF_NOTIFYPOSTPAINT;
        case CDDS_ITEMPOSTPAINT: {
            if (mmh::is_wine())
                return CDRF_DODEFAULT;

            auto cr{GetTextColor(lpcd->hdc)};

            if (m_header_theme)
                GetThemeColor(m_header_theme.get(), HP_HEADERITEM, 0, TMT_TEXTCOLOR, &cr);

            const auto column_index = get_real_column_index(lpcd->dwItemSpec);

            if (m_header_text_format && column_index) {
                const auto& column = m_columns[*column_index];

                direct_write::text_out_columns_and_colours(*m_header_text_format, get_wnd(), lpcd->hdc,
                    mmh::to_string_view(column.m_title), 0, 4_spx, lpcd->rc, cr,
                    {.align = column.m_alignment, .enable_colour_codes = false, .enable_tab_columns = false});
            }

            return CDRF_DODEFAULT;
        }
        default:
            return {};
        }
    }
    case HDN_BEGINTRACKA:
    case HDN_BEGINTRACKW: {
        const auto lpnmh = reinterpret_cast<LPNMHEADERW>(lpnm);
        if (m_autosize && (!get_show_group_info_area() || lpnmh->iItem)) {
            return TRUE;
        }
    } break;
    case HDN_DIVIDERDBLCLICK: {
        if (m_autosize || !m_items_text_format)
            break;

        const auto lpnmh = reinterpret_cast<LPNMHEADERW>(lpnm);

        if (lpnmh->iItem < (m_have_indent_column ? 1 : 0))
            break;

        const size_t internal_column_index = gsl::narrow<size_t>(lpnmh->iItem - (m_have_indent_column ? 1 : 0));

        if (internal_column_index >= m_columns.size())
            break;

        int max_width = 0;

        for (const auto item_index : std::ranges::views::iota(size_t{}, get_item_count())) {
            const char* str = get_item_text(item_index, internal_column_index);
            max_width = std::max(max_width, m_items_text_format->measure_text_width(mmh::to_utf16(str)));
        }
        max_width += 3_spx + 2 * 1_spx;

        m_columns[internal_column_index].m_size = max_width;
        m_columns[internal_column_index].m_display_size = max_width;
        update_header();
        invalidate_all();
        notify_on_column_size_change(internal_column_index, m_columns[internal_column_index].m_size);
        update_scroll_info();
        break;
    }
    case HDN_ITEMCHANGING: {
        if (!m_ignore_column_size_change_notification) {
            auto lpnmh = (LPNMHEADER)lpnm;
            if (lpnmh->pitem->mask & HDI_WIDTH) {
                if (m_have_indent_column && lpnmh->iItem == 0) {
                    int min_indent = get_item_indentation();
                    if (get_show_group_info_area())
                        min_indent += get_indentation_step();
                    if (lpnmh->pitem->cxy < min_indent) {
                        return TRUE;
                    }
                }
            }
        }
    } break;
    case HDN_ITEMCHANGED: {
        if (!m_ignore_column_size_change_notification) {
            auto lpnmh = (LPNMHEADER)lpnm;
            if (lpnmh->pitem->mask & HDI_WIDTH) {
                if (lpnmh->iItem != -1) {
                    if (m_have_indent_column && lpnmh->iItem == 0) {
                        int new_size = lpnmh->pitem->cxy - get_item_indentation() - get_indentation_step();
                        if (new_size >= 0 && new_size != get_group_info_area_width()) {
                            notify_on_group_info_area_size_change(new_size);
                        }
                    } else if (!m_autosize) {
                        size_t realIndex = lpnmh->iItem;
                        if (m_have_indent_column)
                            realIndex--;
                        if (realIndex < m_columns.size() && m_columns[realIndex].m_display_size != lpnmh->pitem->cxy) {
                            m_columns[realIndex].m_size = lpnmh->pitem->cxy;
                            m_columns[realIndex].m_display_size = lpnmh->pitem->cxy;
                            invalidate_all();
                            notify_on_column_size_change(realIndex, m_columns[realIndex].m_size);
                            on_size();
                        }
                    }
                }
            }
        }
    } break;
    case HDN_ITEMCLICK: {
        const auto lpnmh = reinterpret_cast<LPNMHEADERW>(lpnm);
        if (lpnmh->iItem != -1 && (!m_have_indent_column || lpnmh->iItem)) {
            size_t internal_index = lpnmh->iItem;

            if (m_have_indent_column)
                internal_index--;

            if (internal_index < m_columns.size()) {
                const bool is_descending
                    = (m_sort_column_index && *m_sort_column_index == internal_index ? !m_sort_direction : false);
                sort_by_column(internal_index, is_descending);
            }
        }
        break;
    }
    case HDN_ENDDRAG: {
        auto lpnmh = (LPNMHEADER)lpnm;
        if (lpnmh->iButton == 0) {
            if (lpnmh->pitem && (lpnmh->pitem->mask & HDI_ORDER)) {
                int from = lpnmh->iItem;
                int to = lpnmh->pitem->iOrder;
                if (m_have_indent_column) {
                    from--;
                    to--;
                }
                if (to >= 0 && from >= 0 && from != to) {
                    notify_on_header_rearrange(from, to);
                }
            }
        }
    }
        return TRUE;
    }
    return {};
}

void ListView::get_header_rect(LPRECT rc) const
{
    if (m_wnd_header)
        *rc = uih::get_relative_rect(m_wnd_header, get_wnd());
    else {
        GetClientRect(get_wnd(), rc);
        rc->bottom = rc->top;
    }
}

int ListView::get_header_height() const
{
    unsigned ret = 0;
    if (m_wnd_header) {
        RECT rc;
        get_header_rect(&rc);
        ret = RECT_CY(rc);
    }
    return ret;
}

int ListView::calculate_header_height()
{
    unsigned rv = 0;
    if (m_wnd_header) {
        auto font = (HFONT)SendMessage(m_wnd_header, WM_GETFONT, 0, 0);
        rv = uih::get_font_height(font) + m_vertical_item_padding + scale_dpi_value(2);
    }
    return rv;
}

void ListView::update_header()
{
    if (m_wnd_header) {
        pfc::vartoggle_t<bool> toggle(m_ignore_column_size_change_notification, true);
        auto count = gsl::narrow<int>(m_columns.size());
        int j = 0;
        if (m_have_indent_column) {
            uih::header_set_item_width(m_wnd_header, j, get_total_indentation());
            j++;
        }
        for (int i = 0; i < count; i++) {
            uih::header_set_item_width(m_wnd_header, i + j, m_columns[i].m_display_size);
        }
        // SendMessage(m_wnd_header, WM_SETREDRAW, TRUE, NULL);
        // RedrawWindow(m_wnd_header, NULL, NULL, RDW_INVALIDATE|(b_update?RDW_UPDATENOW:0));
    }
}

} // namespace uih

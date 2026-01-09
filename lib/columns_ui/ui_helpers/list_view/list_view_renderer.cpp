#include "stdafx.h"

#include "list_view.h"
#include "../direct_write_text_out.h"

using namespace std::string_view_literals;
using namespace uih::literals::spx;

namespace uih {

constexpr int _level_spacing_size = 3;

int ListView::get_item_indentation() const
{
    const auto rc = get_items_rect();
    int ret = rc.left;
    if (m_group_count)
        ret += get_indentation_step() * gsl::narrow<int>(m_group_count);
    return ret;
}

int ListView::get_indentation_step() const
{
    if (!m_group_level_indentation_enabled)
        return 0;

    if (m_group_level_indentation_amount)
        return *m_group_level_indentation_amount;

    return get_default_indentation_step();
}

int ListView::get_default_indentation_step() const
{
    if (!m_items_text_format)
        return 0;

    if (!m_space_width)
        m_space_width = m_items_text_format->measure_text_width(L" "sv);

    return *m_space_width * _level_spacing_size;
}

void ListView::render_items(HDC dc, const RECT& rc_update, int cx)
{
    wil::com_ptr<IDWriteBitmapRenderTarget> bitmap_render_target;
    try {
        bitmap_render_target = m_direct_write_context->create_bitmap_render_target(dc, 0, 0);
    }
    CATCH_LOG()

    ColourData colours = render_get_colour_data();
    const lv::RendererContext context
        = {colours, m_use_dark_mode, m_is_high_contrast_active, get_wnd(), dc, m_list_view_theme.get(),
            m_items_view_theme.get(), m_items_text_format, m_group_text_format, bitmap_render_target};

    size_t highlight_index = get_highlight_item();
    size_t index_focus = get_focus_item();
    HWND wnd_focus = GetFocus();
    const bool should_hide_focus
        = (SendMessage(get_wnd(), WM_QUERYUISTATE, NULL, NULL) & UISF_HIDEFOCUS) != 0 && !m_always_show_focus;
    bool b_window_focused = (wnd_focus == get_wnd()) || IsChild(get_wnd(), wnd_focus);

    m_renderer->render_background(context, &rc_update);
    const auto rc_items = get_items_rect();

    if (rc_update.bottom <= rc_update.top || rc_update.bottom < rc_items.top)
        return;

    size_t i;
    size_t count = m_items.size();
    const auto indentation_step = m_group_count > 0 ? get_indentation_step() : 0;
    const int item_preindentation = indentation_step * gsl::narrow<int>(m_group_count) + rc_items.left;
    const int item_indentation = item_preindentation + get_group_info_area_total_width();
    cx = get_columns_display_width() + item_indentation;

    bool b_show_group_info_area = get_show_group_info_area();

    i = gsl::narrow<size_t>(
        get_item_at_or_before((rc_update.top > rc_items.top ? rc_update.top - rc_items.top : 0) + m_scroll_position));
    size_t i_start = i;
    size_t i_end = gsl::narrow<size_t>(get_item_at_or_after(
        (rc_update.bottom > rc_items.top + 1 ? rc_update.bottom - rc_items.top - 1 : 0) + m_scroll_position));
    for (; i <= i_end && i < count; i++) {
        size_t item_group_start = NULL;
        size_t item_group_count = NULL;
        get_item_group(i, m_group_count ? m_group_count - 1 : 0, item_group_start, item_group_count);

        size_t j;
        size_t countj = m_items[i]->m_groups.size();
        for (j = 0; j < countj; j++) {
            if (!i || m_items[i]->m_groups[j] != m_items[i - 1]->m_groups[j]) {
                t_group_ptr p_group = m_items[i]->m_groups[j];
                int y = get_item_position(i) - m_scroll_position - m_group_height * gsl::narrow<int>(countj - j)
                    + gsl::narrow_cast<int>(rc_items.top);
                int x = -m_horizontal_scroll_position + rc_items.left;
                // y += counter*m_item_height;
                RECT rc = {x, y, x + cx, y + m_group_height};

                if (rc.top >= rc_update.bottom) {
                    // OffsetWindowOrgEx(dc, -m_horizontal_scroll_position, 0, NULL);
                    break; // CRUDE
                }
                m_renderer->render_group(context, i, j, p_group->m_text.get_ptr(), indentation_step, j, rc);
            }
        }

        if (b_show_group_info_area && (i == i_start || i == item_group_start)) {
            int height = (std::max)(m_item_height * gsl::narrow<int>(item_group_count), get_group_info_area_height());
            int gx = 0 - m_horizontal_scroll_position + item_preindentation;
            int gy = get_item_position(item_group_start) - m_scroll_position + rc_items.top;
            int gcx = get_group_info_area_width();
            RECT rc_group_info
                = {gx, gy, gx + gcx, get_item_position(item_group_start) + height - m_scroll_position + rc_items.top};
            if (rc_group_info.top >= rc_update.bottom)
                break;
            m_renderer->render_group_info(context, item_group_start, rc_group_info);
        }

        bool b_selected = get_item_selected(i) || i == m_highlight_selected_item_index;

        t_item_ptr item = m_items[i];

        RECT rc = {0 - m_horizontal_scroll_position + item_indentation,
            get_item_position(i) - m_scroll_position + rc_items.top, cx - m_horizontal_scroll_position,
            get_item_position(i) + get_item_height(i) - m_scroll_position + rc_items.top};
        // rc.left += cx_space*level_spacing_size*countj;
        if (rc.top >= rc_update.bottom) {
            // OffsetWindowOrgEx(dc, -m_horizontal_scroll_position, 0, NULL);
            break; // CRUDE
        }
        if (rc.bottom > rc_update.top) {
            const auto show_item_focus = index_focus == i && (b_window_focused || m_always_show_focus);
            std::vector<lv::RendererSubItem> sub_items;

            for (size_t column_index{}; column_index < m_columns.size(); ++column_index) {
                const auto text = get_item_text(i, column_index);
                auto& column = m_columns[column_index];

                sub_items.emplace_back(lv::RendererSubItem{text, column.m_display_size, column.m_alignment});
            }

            m_renderer->render_item(context, i, sub_items, 0 /*item_indentation*/, b_selected, b_window_focused,
                (m_highlight_item_index == i) || (highlight_index == i), should_hide_focus, show_item_focus, rc);
        }
    }
    /*if (m_search_editbox)
    {
        RECT rc_search;
        get_search_box_rect(&rc_search);
        rc_search.right = rc_search.left;
        rc_search.left = 0;
        if (rc_update.bottom >= rc_search.top)
        {
            FillRect(dc, &rc_search, GetSysColorBrush(COLOR_BTNFACE));
            //render_background(dc, &rc_search);
            uih::text_out_colours_tab(dc, m_search_label.get_ptr(), m_search_label.get_length(), 0, 2, &rc_search,
    false, GetSysColor(COLOR_WINDOWTEXT), false, false, false, uih::ALIGN_LEFT, NULL);
        }
    }*/
    if (m_insert_mark_index != pfc_infinite && (m_insert_mark_index <= count)) {
        RECT rc_line;
        RECT rc_dummy;
        rc_line.left = item_indentation;
        rc_line.right = cx;
        int y_pos = 0;
        if (count) {
            if (m_insert_mark_index == count)
                y_pos
                    = get_item_position(count - 1) + get_item_height(count - 1) - m_scroll_position + rc_items.top - 1;
            else
                y_pos = get_item_position(m_insert_mark_index) - m_scroll_position + rc_items.top - 1;
        }
        const auto line_height = MulDiv(3, get_system_dpi_cached().cx, USER_DEFAULT_SCREEN_DPI * 2);
        rc_line.top = y_pos;
        rc_line.bottom = y_pos + line_height;
        if (IntersectRect(&rc_dummy, &rc_line, &rc_update)) {
            wil::unique_hbrush brush(CreateSolidBrush(colours.m_text));
            FillRect(dc, &rc_line, brush.get());
        }
    }
    // OffsetWindowOrgEx(dc, -m_horizontal_scroll_position, 0, NULL);
}

void ListView::render_get_colour_data(ColourData& p_out)
{
    p_out.m_themed = true;
    p_out.m_use_custom_active_item_frame = false;
    p_out.m_text = GetSysColor(COLOR_WINDOWTEXT);
    p_out.m_selection_text = GetSysColor(COLOR_HIGHLIGHTTEXT);
    p_out.m_background = GetSysColor(COLOR_WINDOW);
    p_out.m_selection_background = GetSysColor(COLOR_HIGHLIGHT);
    p_out.m_inactive_selection_text = GetSysColor(COLOR_BTNTEXT);
    p_out.m_inactive_selection_background = GetSysColor(COLOR_BTNFACE);
    p_out.m_active_item_frame = GetSysColor(COLOR_WINDOWFRAME);
    p_out.m_group_text = get_group_text_colour_default();
    p_out.m_group_background = p_out.m_background;
}

void lv::DefaultRenderer::render_group_line(RendererContext context, const RECT* rc)
{
    if (context.list_view_theme && IsThemePartDefined(context.list_view_theme, LVP_GROUPHEADERLINE, NULL)
        && SUCCEEDED(
            DrawThemeBackground(context.list_view_theme, context.dc, LVP_GROUPHEADERLINE, LVGH_OPEN, rc, nullptr))) {
    } else {
        COLORREF cr = context.colours.m_group_text; // get_group_text_colour_default();
        wil::unique_hpen pen(CreatePen(PS_SOLID, uih::scale_dpi_value(1), cr));
        HPEN pen_old = SelectPen(context.dc, pen.get());
        MoveToEx(context.dc, rc->left, rc->top, nullptr);
        LineTo(context.dc, rc->right, rc->top);
        SelectPen(context.dc, pen_old);
    }
}

void lv::DefaultRenderer::render_group_background(RendererContext context, const RECT* rc)
{
    FillRect(context.dc, rc, wil::unique_hbrush(CreateSolidBrush(context.colours.m_group_background)).get());
}

COLORREF ListView::get_group_text_colour_default()
{
    COLORREF cr = NULL;
    if (!(m_list_view_theme && IsThemePartDefined(m_list_view_theme.get(), LVP_GROUPHEADER, NULL)
            && SUCCEEDED(
                GetThemeColor(m_list_view_theme.get(), LVP_GROUPHEADER, LVGH_OPEN, TMT_HEADING1TEXTCOLOR, &cr))))
        cr = GetSysColor(COLOR_WINDOWTEXT);
    return cr;
}

bool ListView::get_group_text_colour_default(COLORREF& cr)
{
    cr = NULL;
    return m_list_view_theme && IsThemePartDefined(m_list_view_theme.get(), LVP_GROUPHEADER, NULL)
        && SUCCEEDED(GetThemeColor(m_list_view_theme.get(), LVP_GROUPHEADER, LVGH_OPEN, TMT_HEADING1TEXTCOLOR, &cr));
}

void lv::DefaultRenderer::render_group(RendererContext context, size_t item_index, size_t group_index,
    std::string_view text, int indentation, size_t level, RECT rc)
{
    if (!(context.group_text_format && context.bitmap_render_target))
        return;

    COLORREF cr = context.colours.m_group_text;

    render_group_background(context, &rc);

    const auto x_offset = 1_spx + indentation * gsl::narrow<int>(level);
    const auto border = 3_spx;
    const auto text_width = direct_write::text_out_columns_and_colours(*context.group_text_format, context.wnd,
        context.dc, text, x_offset, border, rc, cr,
        {.bitmap_render_target = context.bitmap_render_target, .enable_tab_columns = false});

    const auto line_height = 1_spx;
    const auto line_top = rc.top + wil::rect_height(rc) / 2 - line_height / 2;
    RECT rc_line = {
        rc.left + x_offset + border * 2 + text_width + 3_spx,
        line_top,
        rc.right - 4_spx,
        line_top + line_height,
    };

    if (rc_line.right > rc_line.left) {
        render_group_line(context, &rc_line);
    }
}

void lv::DefaultRenderer::render_item(RendererContext context, size_t index, std::vector<RendererSubItem> sub_items,
    int indentation, bool b_selected, bool b_window_focused, bool b_highlight, bool should_hide_focus, bool b_focused,
    RECT rc)
{
    int theme_state = NULL;
    if (b_selected) {
        if (b_highlight || (b_focused && b_window_focused))
            theme_state = LISS_HOTSELECTED;
        else if (b_window_focused)
            theme_state = LISS_SELECTED;
        else
            theme_state = LISS_SELECTEDNOTFOCUS;
    } else if (b_highlight) {
        theme_state = LISS_HOT;
    }

    // NB Third param of IsThemePartDefined "must be 0". But this works.
    bool b_themed = context.list_view_theme && context.colours.m_themed
        && IsThemePartDefined(context.list_view_theme, LVP_LISTITEM, theme_state);

    COLORREF cr_text = RGB(255, 0, 0);
    if (b_themed && theme_state) {
        if (FAILED(GetThemeColor(context.list_view_theme, LVP_LISTITEM, LISS_SELECTED, TMT_TEXTCOLOR, &cr_text)))
            cr_text = GetThemeSysColor(context.list_view_theme, b_selected ? COLOR_BTNTEXT : COLOR_WINDOWTEXT);

        if (IsThemeBackgroundPartiallyTransparent(context.list_view_theme, LVP_LISTITEM, theme_state))
            DrawThemeParentBackground(context.wnd, context.dc, &rc);

        RECT rc_background{rc};
        if (context.use_dark_mode)
            // This is inexplicable, but it needs to be done to get the same appearance as Windows Explorer
            InflateRect(&rc_background, 1, 1);
        DrawThemeBackground(context.list_view_theme, context.dc, LVP_LISTITEM, theme_state, &rc_background, &rc);
    } else {
        cr_text = b_selected
            ? (b_window_focused ? context.colours.m_selection_text : context.colours.m_inactive_selection_text)
            : context.colours.m_text;
        FillRect(context.dc, &rc,
            wil::unique_hbrush(
                CreateSolidBrush(b_selected ? (b_window_focused ? context.colours.m_selection_background
                                                                : context.colours.m_inactive_selection_background)
                                            : context.colours.m_background))
                .get());
    }

    RECT rc_subitem = rc;

    for (size_t column_index{0}; column_index < sub_items.size(); ++column_index) {
        auto& sub_item = sub_items[column_index];
        rc_subitem.right = rc_subitem.left + sub_item.width;

        if (context.item_text_format && context.bitmap_render_target)
            direct_write::text_out_columns_and_colours(*context.item_text_format, context.wnd, context.dc,
                sub_item.text, 1_spx + (column_index == 0 ? indentation : 0), 3_spx, rc_subitem, cr_text,
                {.bitmap_render_target = context.bitmap_render_target,
                    .is_selected = b_selected,
                    .align = sub_item.alignment,
                    .enable_tab_columns = m_enable_item_tab_columns});

        rc_subitem.left = rc_subitem.right;
    }

    if (b_focused) {
        render_focus_rect(context, should_hide_focus, rc);
    }
}

void lv::DefaultRenderer::render_focus_rect(RendererContext context, bool should_hide_focus, RECT rc) const
{
    const auto use_themed_rect = context.colours.m_themed && !context.colours.m_use_custom_active_item_frame
        && context.items_view_theme
        && IsThemePartDefined(
            context.items_view_theme, theming::items_view_part_focus_rect, theming::items_view_state_focus_rect_normal);

    if (use_themed_rect) {
        auto _ = gsl::finally([&context, result = SaveDC(context.dc)] {
            if (result)
                RestoreDC(context.dc, -1);
        });

        RECT content_rc{};
        const auto hr = GetThemeBackgroundContentRect(context.items_view_theme, context.dc,
            theming::items_view_part_focus_rect, theming::items_view_state_focus_rect_normal, &rc, &content_rc);

        if (SUCCEEDED(hr)) {
            ExcludeClipRect(context.dc, content_rc.left, content_rc.top, content_rc.right, content_rc.bottom);
        }

        DrawThemeBackground(context.items_view_theme, context.dc, theming::items_view_part_focus_rect,
            theming::items_view_state_focus_rect_normal, &rc, nullptr);

        return;
    }

    if (context.colours.m_themed && context.list_view_theme
        && IsThemePartDefined(context.list_view_theme, LVP_LISTITEM, LISS_SELECTED)) {
        MARGINS margins{};

        const auto hr = GetThemeMargins(
            context.list_view_theme, context.dc, LVP_LISTITEM, LISS_SELECTED, TMT_CONTENTMARGINS, nullptr, &margins);
        if (SUCCEEDED(hr)) {
            rc.left += margins.cxLeftWidth;
            rc.top += margins.cxRightWidth;
            rc.bottom -= margins.cyBottomHeight;
            rc.right -= margins.cxRightWidth;
        }
    }

    if (context.colours.m_use_custom_active_item_frame || (context.use_dark_mode && context.colours.m_themed)) {
        const auto colour
            = context.colours.m_use_custom_active_item_frame ? context.colours.m_active_item_frame : RGB(119, 119, 119);
        draw_rect_outline(context.dc, rc, colour, scale_dpi_value(1));
    } else if (!should_hide_focus) {
        // We only obey should_hide_focus for traditional dotted focus rectangles, similar to how
        // Windows behaves
        DrawFocusRect(context.dc, &rc);
    }
}

void lv::DefaultRenderer::render_background(RendererContext context, const RECT* rc)
{
    FillRect(context.dc, rc, wil::unique_hbrush(CreateSolidBrush(context.colours.m_background)).get());
}

bool ListView::is_item_clipped(size_t index, size_t column)
{
    if (!m_items_text_format)
        return false;

    const pfc::string8 text = get_item_text(index, column);
    const auto render_text = uih::remove_colour_codes({text.c_str(), text.get_length()});
    const auto text_width = m_items_text_format->measure_text_width(mmh::to_utf16(render_text));
    const auto col_width = m_columns[column].m_display_size;
    return text_width + 3_spx * 2 + 1_spx > col_width;
}

} // namespace uih

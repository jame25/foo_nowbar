#include "stdafx.h"

#include "list_view.h"

namespace uih {

lv::SavedScrollPosition ListView::save_scroll_position() const
{
    // Work out where the scroll position is proportionally in the item at the
    // scroll position, or between the previous and next items if the scroll
    // position is between items

    const auto previous_item_index = get_item_at_or_before(m_scroll_position);
    const auto next_item_index = get_item_at_or_after(m_scroll_position);
    const auto next_item_bottom = get_item_position_bottom(next_item_index);
    const auto previous_item_top = get_item_position(previous_item_index);

    // If next_item_bottom == previous_item_bottom == 0, there are probably no items
    const auto proportional_position = next_item_bottom != previous_item_top
        ? static_cast<double>(m_scroll_position - previous_item_top)
            / static_cast<double>(next_item_bottom - previous_item_top)
        : 0.0;

    return {previous_item_index, next_item_index, proportional_position};
}

void ListView::restore_scroll_position(const lv::SavedScrollPosition& position)
{
    const auto new_next_item_bottom = get_item_position_bottom(position.next_item_index);
    const auto new_previous_item_top = get_item_position(position.previous_item_index);
    const auto new_position
        = position.proportional_position * static_cast<double>(new_next_item_bottom - new_previous_item_top)
        + new_previous_item_top;
    const auto new_position_rounded = gsl::narrow<int>(std::lround(new_position));

    update_scroll_info(true, true, true, new_position_rounded);
}

void ListView::ensure_visible(size_t index, EnsureVisibleMode mode)
{
    if (m_ensure_visible_suspended || index > m_items.size())
        return;

    const auto item_visibility = get_item_visibility(index);

    const auto item_area_height = get_item_area_height();
    const auto item_height = get_item_height(index);
    const auto item_start_position = get_item_position(index);
    const auto item_end_position = get_item_position_bottom(index);

    switch (mode) {
    case EnsureVisibleMode::PreferCentringItem:
        switch (item_visibility) {
        case ItemVisibility::ObscuredAbove:
            scroll(item_start_position);
            break;
        case ItemVisibility::ObscuredBelow:
            scroll(item_end_position - item_area_height);
            break;
        case ItemVisibility::FullyVisible:
            break;
        default:
            scroll(item_start_position - (item_area_height - item_height) / 2);
        }
        break;
    case EnsureVisibleMode::PreferMinimalScrolling:
        switch (item_visibility) {
        case ItemVisibility::ObscuredAbove:
        case ItemVisibility::AboveViewport:
            scroll(item_start_position);
            break;
        case ItemVisibility::ObscuredBelow:
        case ItemVisibility::BelowViewport:
            scroll(item_end_position - item_area_height);
            break;
        case ItemVisibility::FullyVisible:
            break;
        }
    }
}

void ListView::scroll(int position, bool b_horizontal, bool suppress_scroll_window)
{
    const INT scroll_bar_type = b_horizontal ? SB_HORZ : SB_VERT;
    int& scroll_position = b_horizontal ? m_horizontal_scroll_position : m_scroll_position;
    const int original_scroll_position = scroll_position;

    SCROLLINFO scroll_info{};
    scroll_info.cbSize = sizeof(SCROLLINFO);
    scroll_info.fMask = SIF_POS;
    scroll_info.nPos = position;

    if (scroll_position == scroll_info.nPos)
        return;

    destroy_tooltip();
    scroll_position = SetScrollInfo(get_wnd(), scroll_bar_type, &scroll_info, true);

    const auto playlist = get_items_rect();
    int dx = 0;
    int dy = 0;
    (b_horizontal ? dx : dy) = original_scroll_position - scroll_position;

    if (b_horizontal)
        reposition_header();

    if (suppress_scroll_window) {
        invalidate_all();
    } else {
        RECT rc_invalidated{};
        const int rgn_type
            = ScrollWindowEx(get_wnd(), dx, dy, &playlist, &playlist, nullptr, &rc_invalidated, SW_INVALIDATE);

        if (dx != 0 || rgn_type == COMPLEXREGION
            || (rgn_type == SIMPLEREGION && !EqualRect(&rc_invalidated, &playlist)))
            RedrawWindow(get_wnd(), nullptr, nullptr, RDW_UPDATENOW | RDW_ALLCHILDREN);
    }
}

void ListView::scroll_from_scroll_bar(short scroll_bar_command, bool b_horizontal)
{
    const int scroll_bar_type = b_horizontal ? SB_HORZ : SB_VERT;
    int& scroll_position = b_horizontal ? m_horizontal_scroll_position : m_scroll_position;

    SCROLLINFO si{};
    si.cbSize = sizeof(SCROLLINFO);
    si.fMask = SIF_ALL;
    GetScrollInfo(get_wnd(), scroll_bar_type, &si);

    int pos{};
    if (scroll_bar_command == SB_LINEDOWN)
        pos = (std::min)(scroll_position + m_item_height, si.nMax);
    else if (scroll_bar_command == SB_LINEUP)
        pos = (std::max)(scroll_position - m_item_height, si.nMin);
    else if (scroll_bar_command == SB_PAGEUP)
        pos = scroll_position - si.nPage;
    else if (scroll_bar_command == SB_PAGEDOWN)
        pos = scroll_position + si.nPage;
    else if (scroll_bar_command == SB_THUMBTRACK)
        pos = si.nTrackPos;
    else if (scroll_bar_command == SB_THUMBPOSITION)
        pos = si.nTrackPos;
    else if (scroll_bar_command == SB_BOTTOM)
        pos = si.nMax;
    else if (scroll_bar_command == SB_TOP)
        pos = si.nMin;
    else // SB_ENDSCROLL
        return;

    scroll(pos, b_horizontal);
}

void ListView::update_vertical_scroll_info(bool redraw, std::optional<int> new_vertical_position)
{
    const auto rc = get_items_rect();
    const auto old_scroll_position = m_scroll_position;

    SCROLLINFO scroll{};
    scroll.cbSize = sizeof(SCROLLINFO);
    scroll.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    scroll.nMin = 0;
    const auto count = m_items.size();
    scroll.nMax = count ? get_item_group_bottom(count - 1) : 0;
    scroll.nPage = RECT_CY(rc);
    scroll.nPos = new_vertical_position.value_or(m_scroll_position);

    m_scroll_position = SetScrollInfo(get_wnd(), SB_VERT, &scroll, redraw);

    if (new_vertical_position || m_scroll_position != old_scroll_position)
        invalidate_all();
}

void ListView::update_horizontal_scroll_info(bool redraw)
{
    auto rc = get_items_rect();

    const auto old_scroll_position = m_horizontal_scroll_position;
    const auto cx = get_columns_display_width() + get_total_indentation();

    SCROLLINFO horizontal_si{};
    horizontal_si.cbSize = sizeof(SCROLLINFO);
    horizontal_si.fMask = SIF_RANGE | SIF_PAGE;
    horizontal_si.nMin = 0;
    horizontal_si.nMax = m_autosize ? 0 : (cx ? cx - 1 : 0);
    horizontal_si.nPage = m_autosize ? 0 : RECT_CX(rc);
    bool b_old_show = (GetWindowLongPtr(get_wnd(), GWL_STYLE) & WS_HSCROLL) != 0;
    ;
    m_horizontal_scroll_position = SetScrollInfo(get_wnd(), SB_HORZ, &horizontal_si, redraw);

    bool b_show = (GetWindowLongPtr(get_wnd(), GWL_STYLE) & WS_HSCROLL) != 0;

    if (m_horizontal_scroll_position != old_scroll_position) {
        invalidate_all();
        reposition_header();
    }

    if (b_old_show != b_show) {
        rc = get_items_rect();
        SCROLLINFO vertical_si{};
        vertical_si.cbSize = sizeof(SCROLLINFO);
        vertical_si.fMask = SIF_PAGE;
        vertical_si.nPage = RECT_CY(rc);

        m_scroll_position = SetScrollInfo(get_wnd(), SB_VERT, &vertical_si, redraw);
    }
}

void ListView::update_scroll_info(
    bool b_vertical, bool b_horizontal, bool redraw, std::optional<int> new_vertical_position)
{
    // Scroll bar updates can cause WM_SIZE (if one of the scroll bars was
    // shown or hidden), leading to recursive scroll bar updates and
    // misbehaviour
    if (m_scroll_bar_update_in_progress)
        return;

    m_scroll_bar_update_in_progress = true;
    auto _ = gsl::finally([this] { m_scroll_bar_update_in_progress = false; });

    // Note the dependency between the two scroll bars – showing one can cause the other to be required

    if (b_vertical)
        update_vertical_scroll_info(redraw, new_vertical_position);

    if (b_horizontal)
        update_horizontal_scroll_info(redraw);
}

unsigned ListView::calculate_scroll_timer_speed() const
{
    const auto num_visible_items = get_item_area_height() / get_item_height();
    const auto divisor = (std::max)(num_visible_items - 5, 1);
    return std::clamp(25 * 15 / divisor, 10, 100);
}

void ListView::create_timer_scroll_up()
{
    if (!m_timer_scroll_up) {
        SetTimer(get_wnd(), TIMER_SCROLL_UP, calculate_scroll_timer_speed(), nullptr);
        m_timer_scroll_up = true;
    }
}

void ListView::create_timer_scroll_down()
{
    if (!m_timer_scroll_down) {
        SetTimer(get_wnd(), TIMER_SCROLL_DOWN, calculate_scroll_timer_speed(), nullptr);
        m_timer_scroll_down = true;
    }
}
void ListView::destroy_timer_scroll_up()
{
    if (m_timer_scroll_up) {
        KillTimer(get_wnd(), TIMER_SCROLL_UP);
        m_timer_scroll_up = false;
    }
}
void ListView::destroy_timer_scroll_down()
{
    if (m_timer_scroll_down) {
        KillTimer(get_wnd(), TIMER_SCROLL_DOWN);
        m_timer_scroll_down = false;
    }
}
} // namespace uih

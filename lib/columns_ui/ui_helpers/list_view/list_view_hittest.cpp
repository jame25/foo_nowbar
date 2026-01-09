#include "stdafx.h"

#include "list_view.h"

namespace uih {

void ListView::hit_test_ex(POINT pt_client, ListView::HitTestResult& result)
{
    const RECT rc_item_area = get_items_rect();
    const int item_area_height = RECT_CY(rc_item_area);

    result.column = pfc_infinite;
    const int first_column_left = -m_horizontal_scroll_position + get_total_indentation();
    const size_t column_count = m_columns.size();
    int last_column_right = first_column_left;

    for (size_t column_index{0}; column_index < column_count; column_index++) {
        const int left = last_column_right;
        const int right = last_column_right + m_columns[column_index].m_display_size;

        if (pt_client.x >= left && pt_client.x < right)
            result.column = column_index;

        last_column_right = right;
    }

    if (pt_client.y < rc_item_area.top) {
        result.index = get_first_viewable_item();
        result.insertion_index = result.index;
        result.category = HitTestCategory::AboveViewport;
        return;
    }

    if (pt_client.y >= rc_item_area.bottom) {
        result.index = get_last_viewable_item();
        result.insertion_index = result.index;
        result.category = HitTestCategory::BelowViewport;
        return;
    }

    const int header_height = rc_item_area.top;
    const int y_position = pt_client.y - header_height + m_scroll_position;
    const auto vertical_hit_test_result = vertical_hit_test(y_position);

    if (vertical_hit_test_result.position_category == VerticalPositionCategory::OnItem) {
        const int item_top = get_item_position(vertical_hit_test_result.item_leftmost);
        const int item_bottom = get_item_position_bottom(vertical_hit_test_result.item_leftmost);
        const int item_height = get_item_height(result.index);

        result.index = vertical_hit_test_result.item_leftmost;
        result.insertion_index = result.index;

        if (pt_client.x < first_column_left)
            result.category = HitTestCategory::LeftOfItem;
        else if (pt_client.x >= last_column_right)
            result.category = HitTestCategory::RightOfItem;
        else if (item_top < m_scroll_position)
            result.category = HitTestCategory::OnItemObscuredAbove;
        else if (item_bottom > m_scroll_position + item_area_height)
            result.category = HitTestCategory::OnItemObscuredBelow;
        else
            result.category = HitTestCategory::OnUnobscuredItem;

        if (item_height >= 2 && y_position - item_top >= item_height / 2)
            result.insertion_index++;

        return;
    }

    if (vertical_hit_test_result.position_category == VerticalPositionCategory::OnGroupHeader) {
        assert(m_group_count > 0);

        const int item_top = get_item_position(vertical_hit_test_result.item_leftmost);

        result.category = HitTestCategory::OnGroupHeader;
        result.index = vertical_hit_test_result.item_leftmost;
        result.insertion_index = result.index;
        result.group_level = m_group_count - (item_top - 1 - y_position) / m_group_height - 1;

        assert(result.group_level < m_group_count);

        if (pt_client.x < 0)
            result.category = HitTestCategory::LeftOfGroupHeader;
        else if (pt_client.x >= last_column_right)
            result.category = HitTestCategory::RightOfGroupHeader;

        return;
    }

    result.index = vertical_hit_test_result.item_leftmost;
    result.insertion_index = vertical_hit_test_result.item_rightmost;
    result.category = HitTestCategory::NotOnItem;
}

ListView::ItemVisibility ListView::get_item_visibility(size_t index)
{
    const auto item_area_height = get_item_area_height();
    const auto item_start_position = get_item_position(index);
    const auto item_end_position = get_item_position_bottom(index);

    if (item_end_position < m_scroll_position)
        return ItemVisibility::AboveViewport;

    if (item_start_position >= m_scroll_position + item_area_height)
        return ItemVisibility::BelowViewport;

    // The case where both the top and bottom of the item is obscured is ignored as it has little practical use
    if (item_start_position < m_scroll_position) {
        return ItemVisibility::ObscuredAbove;
    }

    if (item_end_position > m_scroll_position + item_area_height) {
        return ItemVisibility::ObscuredBelow;
    }

    return ItemVisibility::FullyVisible;
}

bool ListView::is_partially_visible(size_t index)
{
    const auto item_visibility = get_item_visibility(index);

    return item_visibility == ItemVisibility::FullyVisible || item_visibility == ItemVisibility::ObscuredAbove
        || item_visibility == ItemVisibility::ObscuredBelow;
}

bool ListView::is_fully_visible(size_t index)
{
    return get_item_visibility(index) == ItemVisibility::FullyVisible;
}

int ListView::get_last_viewable_item()
{
    if (!m_items.size())
        return 0;

    const auto item_area_height = get_item_area_height();
    const auto vertical_hit_test_result = vertical_hit_test(m_scroll_position + item_area_height);

    const int index = vertical_hit_test_result.item_leftmost;
    const auto item_visibility = get_item_visibility(index);

    if (item_visibility == ItemVisibility::BelowViewport || item_visibility == ItemVisibility::ObscuredBelow)
        return (std::max)(0, index - 1);

    return index;
}

int ListView::get_first_viewable_item()
{
    const auto item_count = gsl::narrow<int>(m_items.size());
    if (item_count == 0)
        return 0;

    const auto vertical_hit_test_result = vertical_hit_test(m_scroll_position);

    const int index = vertical_hit_test_result.item_rightmost;
    const auto item_visibility = get_item_visibility(index);

    if (item_visibility == ItemVisibility::AboveViewport || item_visibility == ItemVisibility::ObscuredAbove)
        return (std::min)(index + 1, item_count);

    return index;
}

ListView::VerticalHitTestResult ListView::vertical_hit_test(int y) const
{
    const auto item_count = gsl::narrow<int>(m_items.size());

    if (item_count == 0)
        return {VerticalPositionCategory::NoItems};

    auto max = item_count;
    auto min = 0;

    while (min <= max) {
        const auto middle = (min + max) / 2;
        const auto item_group_headers_top = get_item_position(middle, true);
        const auto item_top = get_item_position(middle);
        const auto item_bottom = get_item_position_bottom(middle);
        if (y >= item_bottom)
            min = middle + 1;
        else if (y < item_group_headers_top)
            max = middle - 1;
        else if (y < item_top)
            return {VerticalPositionCategory::OnGroupHeader, middle, middle};
        else
            return {VerticalPositionCategory::OnItem, middle, middle};
    }

    const int index_before = std::clamp(max, 0, item_count - 1);
    const int index_after = std::clamp(min, 0, item_count);

    return {VerticalPositionCategory::BetweenItems, index_before, index_after};
}

} // namespace uih

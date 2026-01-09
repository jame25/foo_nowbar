#include "stdafx.h"

#include "list_view.h"

#define GROUP_STRING_COMPARE strcmp

namespace uih {

const char* ListView::get_item_text(size_t index, size_t column)
{
    if (index >= m_items.size())
        return "";
    if (m_items[index]->m_subitems.size() != get_column_count())
        update_item_data(index);
    if (column >= m_items[index]->m_subitems.size())
        return "";
    return m_items[index]->m_subitems[column];
}

ListView::ItemTransaction::~ItemTransaction() noexcept
{
    if (!m_start_index)
        return;

    m_list_view.calculate_item_positions(*m_start_index);
    m_list_view.update_scroll_info(true, true, false);
    m_list_view.invalidate_all(false, true);
}

void ListView::ItemTransaction::insert_items(size_t index_start, size_t count, const InsertItem* items)
{
    m_start_index = std::min(index_start, m_start_index.value_or(index_start));
    m_list_view.insert_items_in_internal_state(index_start, count, items);
}

void ListView::ItemTransaction::remove_items(const pfc::bit_array& mask)
{
    m_list_view.remove_items_in_internal_state(mask);
    m_start_index = 0;
}

ListView::ItemTransaction ListView::start_transaction()
{
    return {*this};
}

void ListView::insert_items(size_t index_start, size_t count, const InsertItem* items,
    const std::optional<lv::SavedScrollPosition>& saved_scroll_position)
{
    insert_items_in_internal_state(index_start, count, items);
    calculate_item_positions(index_start);

    if (saved_scroll_position)
        restore_scroll_position(*saved_scroll_position);
    else
        update_scroll_info();

    RedrawWindow(get_wnd(), nullptr, nullptr, RDW_INVALIDATE);
}

void ListView::replace_items(size_t index_start, size_t count, const InsertItem* items)
{
    assert(count > 0);

    if (count == 0)
        return;

    replace_items_in_internal_state(index_start, count, items);
    calculate_item_positions(index_start);

    if (m_group_count > 0 || m_variable_height_items) {
        update_scroll_info();
        invalidate_all();
    } else {
        invalidate_items(index_start, count);
    }
}

void ListView::remove_items(const pfc::bit_array& mask)
{
    remove_items_in_internal_state(mask);
    calculate_item_positions();
    update_scroll_info();
    RedrawWindow(get_wnd(), nullptr, nullptr, RDW_INVALIDATE);
}

void ListView::remove_all_items()
{
    if (m_timer_inline_edit)
        exit_inline_edit();

    m_items.clear();
    update_scroll_info();

    RedrawWindow(get_wnd(), nullptr, nullptr, RDW_INVALIDATE);
}

void ListView::replace_items_in_internal_state(size_t index_start, size_t countl, const InsertItem* items)
{
    std::vector<t_item_ptr> items_prev(m_items);
    size_t l;
    size_t countitems = m_items.size();
    size_t newgroupcount = 0;
    size_t oldgroupcount = 0;

    // Calculate old group count
    {
        size_t countl2 = index_start + countl < countitems ? countl + 1 : countl;
        for (l = 0; l < countl2; l++) {
            size_t i;
            size_t count = m_group_count;
            size_t index = l + index_start;
            for (i = 0; i < count; i++) {
                if ((!index || m_items[index - 1]->m_groups[i] != m_items[index]->m_groups[i]))
                    oldgroupcount++;
            }
            // oldgroupcount += get_item_display_group_count(index);
        }
    }

    {
        for (l = 0; l < countl; l++) {
            size_t i;
            size_t count = m_group_count;
            size_t index = l + index_start;
            t_item_ptr item;
            t_item_ptr item_old;
            item_old = m_items[index];
            {
                item = storage_create_item();
                item->m_selected = m_items[index]->m_selected;
                item->m_subitems = items[l].m_subitems;
                m_items[index] = item;
                item->m_display_index = index ? m_items[index - 1]->m_display_index + 1 : 0;
                item->m_groups.resize(count);
            }
            bool b_new = false;

            bool b_left_same_above = true;
            bool b_right_same_above = true;
            bool b_self_same_above = true;
            for (i = 0; i < count; i++) {
                bool b_left_same = false;
                bool b_right_same = false;
                bool b_self_same = false;
                if (!b_new && index) {
                    b_left_same = b_left_same_above
                        && !GROUP_STRING_COMPARE(items[l].m_groups[i], m_items[index - 1]->m_groups[i]->m_text);
                }
                if (!b_new && index + 1 < countitems && l + 1 >= countl) {
                    b_right_same = b_right_same_above
                        && !GROUP_STRING_COMPARE(items[l].m_groups[i], m_items[index + 1]->m_groups[i]->m_text);
                }
                if (!b_new && item_old.is_valid()) {
                    b_self_same = b_self_same_above
                        && !GROUP_STRING_COMPARE(items[l].m_groups[i], item_old->m_groups[i]->m_text);
                }
                if (b_new || (!b_left_same && !b_right_same && !b_self_same)) {
                    item->m_groups[i] = storage_create_group();
                    item->m_groups[i]->m_text = items[l].m_groups[i];
                    b_new = true;
                    item->m_display_index++;
                }
                if (b_left_same && b_right_same) {
                    item->m_groups[i] = m_items[index - 1]->m_groups[i];
                    t_group_ptr test;
                    {
                        test = m_items[index + 1]->m_groups[i];
                        size_t j = index + 1;
                        while (j < countitems && test == m_items[j]->m_groups[i]) {
                            m_items[j]->m_groups[i] = item->m_groups[i];
                            j++;
                        }
                    }
                } else if (b_left_same)
                    item->m_groups[i] = m_items[index - 1]->m_groups[i];
                else if (b_right_same) {
                    item->m_display_index++;
                    item->m_groups[i] = m_items[index + 1]->m_groups[i];
                } else if (b_self_same) {
                    item->m_groups[i] = item_old->m_groups[i];
                    item->m_display_index++;
                }
                b_right_same_above = b_right_same;
                b_left_same_above = b_left_same;
                b_self_same_above = b_self_same;
            }
            if (l + 1 == countl && index + 1 < countitems) {
                for (i = 0; i < count; i++) {
                    {
                        if (items_prev[index]->m_groups[i] == items_prev[index + 1]->m_groups[i]) {
                            if (m_items[index + 1]->m_groups[i] != m_items[index]->m_groups[i]) {
                                t_group_ptr newgroup = storage_create_group();
                                newgroup->m_text = (items_prev[index]->m_groups[i]->m_text);
                                size_t j = index + 1;
                                while (j < countitems && items_prev[index]->m_groups[i] == items_prev[j]->m_groups[i]) {
                                    m_items[j]->m_groups[i] = newgroup;
                                    j++;
                                };
                            }
                        }
                    }
                }
            }
        }
    }
    {
        size_t countl2 = index_start + countl < countitems ? countl + 1 : countl;
        for (l = 0; l < countl2; l++) {
            size_t i;
            size_t count = m_group_count;
            size_t index = l + index_start;
            for (i = 0; i < count; i++) {
                if ((!index || m_items[index - 1]->m_groups[i] != m_items[index]->m_groups[i]))
                    newgroupcount++;
            }
        }
    }
    {
        size_t j = index_start + countl;

        // console::formatter() << newgroupcount << " " << oldgroupcount;

        while (j < countitems) {
            m_items[j]->m_display_index += ((newgroupcount - oldgroupcount));
            j++;
        }
    }
}

void ListView::insert_items_in_internal_state(size_t index_start, size_t pcountitems, const InsertItem* items)
{
    size_t countl = pcountitems;
    m_items.insert(m_items.begin() + index_start, countl, t_item_ptr());

    if (m_highlight_selected_item_index != pfc_infinite && m_highlight_selected_item_index >= index_start)
        m_highlight_selected_item_index += countl;

    const std::optional<std::vector<t_item_ptr>> items_prev
        = m_group_count > 0 ? std::make_optional(m_items) : std::nullopt;

    size_t countitems = m_items.size();
    size_t newgroupcount = 0;
    size_t oldgroupcount = 0;

    // Calculate old group count
    {
        size_t index = index_start + countl;
        if (index < countitems) {
            size_t i;
            size_t count = m_group_count;
            for (i = 0; i < count; i++) {
                if ((!index_start || m_items[index_start - 1]->m_groups[i] != m_items[index]->m_groups[i]))
                    oldgroupcount++;
            }
        }
    }

    // Determine grouping

    {
        t_item_ptr* p_items = m_items.data();

        concurrency::parallel_for(size_t{0}, countl, [this, p_items, index_start, items](size_t l) {
            size_t count = m_group_count;
            size_t index = l + index_start;
            Item* item;
            item = storage_create_item();
            p_items[index] = item;
            item->m_subitems = items[l].m_subitems;
            item->m_groups.resize(count);
        });

        for (size_t l = 0; l < countl; l++) {
            size_t i;
            size_t count = m_group_count;
            size_t index = l + index_start;
            Item* item = p_items[index].get_ptr();

            item->m_display_index = index ? p_items[index - 1]->m_display_index + 1 : 0;
            if (m_variable_height_items) {
                if (item->m_subitems.size() != get_column_count())
                    update_item_data(index);
                item->update_line_count();
            }

            bool b_new = false;
            bool b_left_same_above = true;
            bool b_right_same_above = true;
            for (i = 0; i < count; i++) {
                bool b_left_same = false;
                bool b_right_same = false;
                if (!b_new && index) {
                    b_left_same = b_left_same_above
                        && !GROUP_STRING_COMPARE(items[l].m_groups[i], m_items[index - 1]->m_groups[i]->m_text);
                }
                if (!b_new && index + 1 < countitems && l + 1 >= countl) {
                    b_right_same = b_right_same_above
                        && !GROUP_STRING_COMPARE(items[l].m_groups[i], m_items[index + 1]->m_groups[i]->m_text);
                }
                if (b_new || (!b_left_same && !b_right_same)) {
                    item->m_groups[i] = storage_create_group();
                    item->m_groups[i]->m_text = (items[l].m_groups[i]);
                    b_new = true;
                    item->m_display_index++;
                }
                if (b_left_same && b_right_same) {
                    item->m_groups[i] = m_items[index - 1]->m_groups[i];
                    t_group_ptr test;
                    {
                        test = m_items[index + 1]->m_groups[i];
                        size_t j = index + 1;
                        while (j < countitems && test == m_items[j]->m_groups[i]) {
                            m_items[j]->m_groups[i] = item->m_groups[i];
                            j++;
                        }
                    }
                } else if (b_left_same)
                    item->m_groups[i] = m_items[index - 1]->m_groups[i];
                else if (b_right_same) {
                    item->m_display_index++;
                    item->m_groups[i] = m_items[index + 1]->m_groups[i];
                }
                b_right_same_above = b_right_same;
                b_left_same_above = b_left_same;
            }
        }
    }
    {
        size_t index = index_start + countl;
        if (m_group_count > 0 && index_start && index < countitems) {
            const auto& items_prev_value = *items_prev;

            const size_t index_prev = index_start - 1;
            const size_t count = m_group_count;
            for (size_t i = 0; i < count; i++) {
                if (items_prev_value[index_prev]->m_groups[i] == items_prev_value[index]->m_groups[i]) {
                    if (m_items[index]->m_groups[i] != m_items[index - 1]->m_groups[i]) {
                        t_group_ptr newgroup = storage_create_group();
                        newgroup->m_text = (items_prev_value[index_prev]->m_groups[i]->m_text);
                        size_t j = index;
                        while (j < countitems
                            && items_prev_value[index_prev]->m_groups[i] == items_prev_value[j]->m_groups[i]) {
                            m_items[j]->m_groups[i] = newgroup;
                            j++;
                        }
                    }
                }
            }
        }
    }

    // Determine new group count
    {
        size_t countl2 = index_start + countl < countitems ? countl + 1 : countl;
        for (size_t l = 0; l < countl2; l++) {
            size_t i;
            size_t count = m_group_count;
            size_t index = l + index_start;
            for (i = 0; i < count; i++) {
                if ((!index || m_items[index - 1]->m_groups[i] != m_items[index]->m_groups[i]))
                    newgroupcount++;
            }
        }
    }

    // Correct subsequent display indices
    {
        const size_t j_start = index_start + countl;
        size_t j = j_start;

        // console::formatter() << newgroupcount << " " << oldgroupcount;

        while (j < countitems) {
            m_items[j]->m_display_index += ((countl + newgroupcount - oldgroupcount));
            j++;
        }
    }
}

void ListView::calculate_item_positions(size_t index_start)
{
    if (index_start >= get_item_count())
        return;

    int y_pointer = 0;
    if (m_group_count)
        while (index_start && get_item_display_group_count(index_start) == 0)
            index_start--;
    if (!index_start)
        y_pointer += 0; // m_item_height * m_group_count;
    else {
        if (m_group_count)
            y_pointer = get_item_group_bottom(index_start - 1) + 1;
        else
            y_pointer = get_item_position(index_start - 1) + get_item_height(index_start - 1);
    }
    size_t i;
    size_t count = m_items.size();
    int group_height_counter = 0;
    const auto group_minimum_inner_height = get_group_minimum_inner_height();
    for (i = index_start; i < count; i++) {
        const auto groups = gsl::narrow<int>(get_item_display_group_count(i));
        if (groups) {
            if (group_height_counter && group_height_counter < group_minimum_inner_height)
                y_pointer += group_minimum_inner_height - group_height_counter;
            group_height_counter = 0;
        }
        group_height_counter += get_item_height(i);
        y_pointer += groups * m_group_height;
        m_items[i]->m_display_position = y_pointer;
        y_pointer += get_item_height(i);
    }
}

void ListView::remove_item(size_t index)
{
    if (m_timer_inline_edit)
        exit_inline_edit();
    remove_item_in_internal_state(index);
    calculate_item_positions();
    update_scroll_info();
    RedrawWindow(get_wnd(), nullptr, nullptr, RDW_INVALIDATE);
}

void ListView::remove_items_in_internal_state(const pfc::bit_array& mask)
{
    if (m_timer_inline_edit)
        exit_inline_edit();

    for (size_t i = m_items.size(); i; i--) {
        if (mask[i - 1])
            remove_item_in_internal_state(i - 1);
    }
}

void ListView::remove_item_in_internal_state(size_t index)
{
    size_t gc = 0;
    size_t k;
    size_t count2 = m_items[index]->m_groups.size();

    // if (index)
    {
        for (k = 0; k < count2; k++) {
            if ((!index || m_items[index]->m_groups[k] != m_items[index - 1]->m_groups[k])
                && (index + 1 >= m_items.size() || m_items[index]->m_groups[k] != m_items[index + 1]->m_groups[k])) {
                gc++;
            }
            // else break;
        }
    }
    // else gc = count2;

    // t_item_ptr item = m_items[index];
    m_items.erase(m_items.begin() + index);

    if (index < m_items.size()) {
        size_t j;
        if (index) {
            size_t i;
            size_t count = m_items[index]->m_groups.size();
            for (i = 0; i < count; i++) {
                if (!GROUP_STRING_COMPARE(
                        m_items[index - 1]->m_groups[i]->m_text, m_items[index]->m_groups[i]->m_text)) {
                    t_group_ptr newgroup = m_items[index - 1]->m_groups[i];
                    j = index;
                    while (j < m_items.size()
                        && (!i || m_items[index - 1]->m_groups[i - 1] == m_items[j]->m_groups[i - 1])
                        && !GROUP_STRING_COMPARE(
                            m_items[index - 1]->m_groups[i]->m_text, m_items[j]->m_groups[i]->m_text)) {
                        if (j == index && m_items[j]->m_groups[i] != newgroup)
                            gc++;
                        m_items[j]->m_groups[i] = newgroup;
                        j++;
                    };
                } else
                    break;
            }
        }
        j = index;
        while (j < m_items.size()) {
            m_items[j]->m_display_index -= (1 + gc);
            j++;
        }
    }

    if (m_highlight_selected_item_index != pfc_infinite) {
        if (m_highlight_selected_item_index > index)
            m_highlight_selected_item_index--;
        else if (m_highlight_selected_item_index == index)
            m_highlight_selected_item_index = pfc_infinite;
    }
}

} // namespace uih

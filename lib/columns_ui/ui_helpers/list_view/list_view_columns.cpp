#include "stdafx.h"

#include "list_view.h"

namespace uih {

int ListView::get_columns_width()
{
    return std::accumulate(
        m_columns.begin(), m_columns.end(), 0, [](auto&& value, auto&& column) { return value + column.m_size; });
}

int ListView::get_columns_display_width()
{
    return std::accumulate(m_columns.begin(), m_columns.end(), 0,
        [](auto&& value, auto&& column) { return value + column.m_display_size; });
}

int ListView::get_column_display_width(size_t index)
{
    return m_columns.at(index).m_display_size;
}

size_t ListView::get_column_count()
{
    return m_columns.size();
}

uih::alignment ListView::get_column_alignment(size_t index)
{
    uih::alignment ret = uih::ALIGN_LEFT;
    assert(index < get_column_count());
    if (index < get_column_count())
        ret = m_columns[index].m_alignment;
    return ret;
}

void ListView::set_columns(std::vector<Column> columns)
{
    m_columns = std::move(columns);

    update_column_sizes();

    if (m_initialised) {
        build_header();
        update_horizontal_scroll_info();
    }
}

void ListView::set_column_widths(std::vector<int> widths)
{
    assert(m_columns.size() == widths.size());
    for (size_t i{}; i < m_columns.size(); ++i)
        m_columns[i].m_size = widths[i];

    update_column_sizes();

    if (m_wnd_header)
        SendMessage(m_wnd_header, WM_SETREDRAW, FALSE, NULL);

    update_header();

    if (m_wnd_header)
        SendMessage(m_wnd_header, WM_SETREDRAW, TRUE, NULL);

    invalidate_all();
    on_size();
}

void ListView::update_column_sizes()
{
    const auto rc = get_items_rect();
    int display_width = RECT_CX(rc);
    int width = get_columns_width();
    int total_weight = 0;
    int indent = get_total_indentation();

    if (display_width > indent)
        display_width -= indent;
    else
        display_width = 0;

    size_t count = m_columns.size();

    for (auto&& column : m_columns)
        column.m_display_size = column.m_size;

    if (m_autosize) {
        for (auto&& column : m_columns)
            total_weight += column.m_autosize_weight;

        pfc::array_t<bool> sized;
        pfc::array_t<int> deltas;
        sized.set_count(count);
        deltas.set_count(count);
        sized.fill_null();
        deltas.fill_null();
        size_t sized_count = count;
        int width_difference = display_width - width;

        while (width_difference && total_weight && sized_count) {
            int width_difference_local = width_difference;
            int total_weight_local = total_weight;

            for (size_t i{0}; i < count; i++) {
                if (!sized[i] && total_weight_local) {
                    deltas[i] = MulDiv(m_columns[i].m_autosize_weight, width_difference_local, total_weight_local);
                    width_difference_local -= deltas[i];
                    total_weight_local -= m_columns[i].m_autosize_weight;
                }
            }

            for (size_t i{0}; i < count; i++) {
                if (!sized[i]) {
                    int delta = deltas[i];
                    if (m_columns[i].m_display_size + delta <= 0) {
                        total_weight -= m_columns[i].m_autosize_weight;
                        sized[i] = true;
                        sized_count--;
                        width_difference += m_columns[i].m_display_size;
                        m_columns[i].m_display_size = 0;
                    } else {
                        m_columns[i].m_display_size += delta;
                        width_difference -= delta;
                    }
                }
            }
        }
    }
}

} // namespace uih

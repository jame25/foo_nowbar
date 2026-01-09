#include "stdafx.h"

#include "list_view.h"

namespace uih {

void ListView::get_selection_state(pfc::bit_array_var& out)
{
    storage_get_selection_state(out);
}

void ListView::set_selection_state(const pfc::bit_array& p_affected, const pfc::bit_array& p_status, bool b_notify,
    notification_source_t p_notification_source)
{
    pfc::bit_array_bittable p_changed(get_item_count());
    if (storage_set_selection_state(p_affected, p_status, &p_changed)) {
        invalidate_items(p_changed);
        if (b_notify)
            notify_on_selection_change(p_changed, p_status, p_notification_source);
    }
}

size_t ListView::get_focus_item()
{
    size_t ret = storage_get_focus_item();
    if (ret >= get_item_count())
        ret = pfc_infinite;
    return ret;
}

void ListView::set_focus_item(size_t index, bool b_notify)
{
    size_t old = storage_get_focus_item();
    if (old != index) {
        storage_set_focus_item(index);
        on_focus_change(old, index);
        if (b_notify)
            notify_on_focus_item_change(index);
    }
}

bool ListView::get_item_selected(size_t index)
{
    return storage_get_item_selected(index);
}

size_t ListView::get_selection_count(size_t max)
{
    return storage_get_selection_count(max);
}

size_t ListView::storage_get_selection_count(size_t max)
{
    size_t i;
    size_t count = m_items.size();
    size_t ret = 0;
    ;
    for (i = 0; i < count; i++) {
        if (get_item_selected(i))
            ret++;
        if (ret == max)
            break;
    }
    return ret;
}

void ListView::set_item_selected(size_t index, bool b_state)
{
    if (b_state)
        set_selection_state(pfc::bit_array_one(index), pfc::bit_array_one(index));
    else
        set_selection_state(pfc::bit_array_one(index), pfc::bit_array_false());
}
void ListView::set_item_selected_single(size_t index, bool b_notify, notification_source_t p_notification_source)
{
    if (index < m_items.size()) {
        set_selection_state(pfc::bit_array_true(), pfc::bit_array_one(index), b_notify, p_notification_source);
        set_focus_item(index, b_notify);
        // ensure_visible(index);
    }
}

// DEFAULT STORAGE
size_t ListView::storage_get_focus_item()
{
    return m_focus_index;
}

void ListView::storage_set_focus_item(size_t index)
{
    m_focus_index = index;
}

void ListView::storage_get_selection_state(pfc::bit_array_var& out) // storage
{
    size_t i;
    size_t count = m_items.size();
    for (i = 0; i < count; i++)
        out.set(i, m_items[i]->m_selected);
}

bool ListView::storage_set_selection_state(const pfc::bit_array& p_affected, const pfc::bit_array& p_status,
    pfc::bit_array_var* p_changed) // storage, returns hint if sel actually changed
{
    bool b_changed = false;
    size_t i;
    size_t count = m_items.size();
    for (i = 0; i < count; i++) {
        if (p_affected[i] && p_status[i] != get_item_selected(i)) {
            b_changed = true;
            m_items[i]->m_selected = p_status[i];
            if (p_changed)
                p_changed->set(i, true);
        }
    }
    return b_changed;
}

bool ListView::storage_get_item_selected(size_t index)
{
    return m_items[index]->m_selected;
}

} // namespace uih

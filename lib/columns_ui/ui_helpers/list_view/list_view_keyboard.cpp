#include "stdafx.h"

#include "list_view.h"

namespace uih {

bool ListView::on_wm_keydown(WPARAM wp, LPARAM lp)
{
    SendMessage(get_wnd(), WM_CHANGEUISTATE, MAKEWPARAM(UIS_CLEAR, UISF_HIDEFOCUS), NULL);

    if (notify_on_keyboard_keydown_filter(WM_KEYDOWN, wp, lp))
        return true;

    const auto is_alt_down = (HIWORD(lp) & KF_ALTDOWN) != 0;
    const auto is_ctrl_down = (GetKeyState(VK_CONTROL) & KF_UP) != 0;
    const auto process_ctrl_char_shortcuts = is_ctrl_down && !is_alt_down;

    switch (wp) {
    case VK_TAB:
        uih::handle_tab_down(get_wnd());
        return true;
    case VK_HOME:
    case VK_DOWN:
    case VK_END:
    case VK_PRIOR:
    case VK_NEXT:
    case VK_UP:
        process_navigation_keydown(wp, ((HIWORD(lp) & KF_ALTDOWN) != 0), (HIWORD(lp) & KF_REPEAT) != 0);
        return true;
    case VK_SPACE: {
        if (is_ctrl_down) {
            const size_t focus = get_focus_item();

            if (focus != pfc_infinite)
                set_item_selected(focus, !get_item_selected(focus));
            return true;
        }
        return false;
    }
    case VK_RETURN: {
        const size_t focus = get_focus_item();
        if (focus != pfc_infinite)
            execute_default_action(focus, pfc_infinite, true, is_ctrl_down);
        return true;
    }
    case VK_SHIFT:
        if (!(HIWORD(lp) & KF_REPEAT)) {
            size_t focus = get_focus_item();
            m_shift_start = focus != pfc_infinite ? focus : 0;
        }
        return false;
    case VK_F2:
        activate_inline_editing();
        return true;
    case VK_DELETE:
        return notify_on_keyboard_keydown_remove();
    case VK_F3:
        return notify_on_keyboard_keydown_search();
    case 'A':
        if (process_ctrl_char_shortcuts && m_selection_mode == SelectionMode::Multiple) {
            set_selection_state(pfc::bit_array_true(), pfc::bit_array_true());
            return true;
        }
        return false;
    case 'C':
        return process_ctrl_char_shortcuts && notify_on_keyboard_keydown_copy();
    case 'F':
        return process_ctrl_char_shortcuts && notify_on_keyboard_keydown_search();
    case 'V':
        return process_ctrl_char_shortcuts && notify_on_keyboard_keydown_paste();
    case 'X':
        return process_ctrl_char_shortcuts && notify_on_keyboard_keydown_cut();
    case 'Y':
        return process_ctrl_char_shortcuts && notify_on_keyboard_keydown_redo();
    case 'Z':
        return process_ctrl_char_shortcuts && notify_on_keyboard_keydown_undo();
    default:
        return false;
    }
}
} // namespace uih

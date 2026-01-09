#pragma once

namespace uih {
class Menu {
public:
    enum Flags {
        flag_checked = (1 << 0),
        flag_radiochecked = (1 << 1),
        flag_default = (1 << 2)
    };

    Menu() { m_handle = CreatePopupMenu(); }
    ~Menu()
    {
        if (m_handle) {
            DestroyMenu(m_handle);
            m_handle = nullptr;
        }
    }

    uint32_t size()
    {
        const int size = GetMenuItemCount(m_handle);
        THROW_LAST_ERROR_IF(size < 0);
        return gsl::narrow<uint32_t>(size);
    }

    HMENU detach()
    {
        HMENU ret = m_handle;
        m_handle = nullptr;
        return ret;
    }
    void insert_command(uint32_t index, const wchar_t* text, uint32_t id, uint32_t flags = NULL)
    {
        MENUITEMINFO mii;
        memset(&mii, 0, sizeof(MENUITEMINFO));
        mii.cbSize = sizeof(MENUITEMINFO);
        mii.fMask = MIIM_ID | MIIM_STRING | MIIM_STATE | MIIM_FTYPE;
        mii.dwTypeData = const_cast<wchar_t*>(text);
        mii.cch = gsl::narrow<UINT>(wcslen(text));
        mii.fType = MFT_STRING;
        mii.fState = MFS_ENABLED;
        if (flags & flag_radiochecked)
            mii.fType |= MFT_RADIOCHECK;
        if (flags & (flag_checked | flag_radiochecked))
            mii.fState |= MFS_CHECKED;
        if (flags & (flag_default))
            mii.fState |= MFS_DEFAULT;
        mii.wID = id;
        InsertMenuItem(m_handle, index, TRUE, &mii);
    }
    void insert_submenu(uint32_t index, const wchar_t* text, HMENU submenu, uint32_t flags = NULL)
    {
        MENUITEMINFO mii;
        memset(&mii, 0, sizeof(MENUITEMINFO));
        mii.cbSize = sizeof(MENUITEMINFO);
        mii.fMask = MIIM_SUBMENU | MIIM_STRING | MIIM_FTYPE;
        mii.dwTypeData = const_cast<wchar_t*>(text);
        mii.cch = gsl::narrow<UINT>(wcslen(text));
        mii.fType = MFT_STRING;
        mii.fState = MFS_ENABLED;
        mii.hSubMenu = submenu;
        InsertMenuItem(m_handle, index, TRUE, &mii);
    }
    void insert_separator(uint32_t index, uint32_t flags = NULL)
    {
        MENUITEMINFO mii;
        memset(&mii, 0, sizeof(MENUITEMINFO));
        mii.cbSize = sizeof(MENUITEMINFO);
        mii.fMask = MIIM_FTYPE;
        mii.fType = MFT_SEPARATOR;
        InsertMenuItem(m_handle, index, TRUE, &mii);
    }
    void append_command(const wchar_t* text, uint32_t id, uint32_t flags = NULL)
    {
        insert_command(size(), text, id, flags);
    }
    void append_submenu(const wchar_t* text, HMENU submenu, uint32_t flags = NULL)
    {
        insert_submenu(size(), text, submenu, flags);
    }
    void append_separator(uint32_t flags = NULL) { insert_separator(size(), flags); }
    auto run(HWND wnd, const POINT& pt) const
    {
        return TrackPopupMenu(m_handle, TPM_RIGHTBUTTON | TPM_NONOTIFY | TPM_RETURNCMD, pt.x, pt.y, 0, wnd, nullptr);
    }

private:
    HMENU m_handle;
};
} // namespace uih

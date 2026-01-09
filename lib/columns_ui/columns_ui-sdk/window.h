#ifndef _UI_EXTENSION_EXTENSION_H_
#define _UI_EXTENSION_EXTENSION_H_

/**
 * \file window.h
 * \brief Window API
 */

namespace ui_helpers {
/** \brief Simple class used to hold window positions */
class window_position_t {
public:
    int x;
    int y;
    unsigned cx;
    unsigned cy;
    window_position_t(int i_x, int i_y, unsigned u_cx, unsigned u_cy) : x(i_x), y(i_y), cx(u_cx), cy(u_cy) {}
    window_position_t(const RECT& rc) : x(rc.left), y(rc.top), cx(rc.right - rc.left), cy(rc.bottom - rc.top) {}
    window_position_t() {}
    window_position_t(HWND wnd_relative, int i_x, int i_y, unsigned u_cx, unsigned u_cy)
    {
        RECT rc;
        GetClientRect(wnd_relative, &rc);
        MapWindowPoints(wnd_relative, HWND_DESKTOP, (LPPOINT)&rc, 2);
        x = rc.left; // + (RECT_CX(rc)-cx)/2;
        y = rc.top; // + (RECT_CY(rc)-cy)/2;
        cx = u_cx;
        cy = u_cy;
    }

    void convert_to_rect(RECT& p_out) const
    {
        p_out.left = x;
        p_out.top = y;
        p_out.right = x + cx;
        p_out.bottom = y + cy;
    }
    void set_from_rect(const RECT& rc)
    {
        x = (rc.left);
        y = (rc.top);
        cx = (rc.right - rc.left);
        cy = (rc.bottom - rc.top);
    }
};

/** The window position {0,0,0,0} */
extern const window_position_t window_position_null;
}; // namespace ui_helpers

namespace uie {

/** \brief Class describing the size limits of a window */
struct size_limit_t {
    /** \brief The minimum height of the window */
    unsigned min_height;
    /** \brief The maximum height of the window */
    unsigned max_height;
    /** \brief The minimum width of the window */
    unsigned min_width;
    /** \brief The maximum width of the window */
    unsigned max_width;
    /**
     * \brief Default constructor
     * \remark Initialises max_height, max_width with MAXLONG because WIN32 API takes windows sizes as signed
     */
    size_limit_t() : min_height(0), max_height(MAXLONG), min_width(0), max_width(MAXLONG) {}
};

/**
 * \brief Base class for window (panel or toolbar) services (formerly known as UI
 * extensions).
 */
class NOVTABLE window : public extension_base {
public:
    /**
     * \brief Get whether is a single-instance window (as opposed to a
     * multi-instance window).
     *
     * \note Do not explicitly override. The service factory implements this method.
     */
    virtual const bool get_is_single_instance() const = 0;

    /**
     * \brief Get the category of this window.
     *
     * Pre-defined categories you can use are `"Toolbars"`, `"Panels"`, `"Splitters"`,
     * `"Playlist views"` and `"Visualisations"`.
     *
     * \param [out]    out        receives the category of the panel, utf-8 encoded
     */
    virtual void get_category(pfc::string_base& out) const = 0;

    /**
     * \brief Get a shorter name of the window.
     *
     * \param [out]    out    receives the short name of the extension, e.g. "Order" instead
     *                        of "Playback order", or "Playlists" instead of "Playlist switcher"
     *
     * \return                true if the extension has a short name
     */
    virtual bool get_short_name(pfc::string_base& out) const { return false; }

    /**
     * \brief Get a description of the extension.
     *
     * \param [out]    out    receives the description of the extension,
     *                        e.g. "Drop-down list for displaying and changing the current playback order"
     *
     * \return true if the extension has a description
     */
    virtual bool get_description(pfc::string_base& out) const { return false; }

    /**
     * \brief Get the type of the extension.
     *
     * \return               one or more `uie::window_type_t` flags, combined using bitwise OR
     *
     * \see uie::window_type_t
     */
    virtual unsigned get_type() const = 0;

    /**
     * \brief Get whether the panel prefers to be created in multiple instances in the toolbars.
     *
     * For example, this could be a spacer panel.
     *
     * \return               true if the panel prefers to be created in multiple instances
     */
    virtual bool get_prefer_multiple_instances() const { return false; }

    /**
     * \brief Get whether the window is available to be inserted in a particular host.
     *
     * For multi-instance windows, you can always return `true`.
     *
     * For single-instance windows, if an existing host has been assigned to the window,
     * return false if its GUID is equal to the GUID of `p_host`. Otherwise, return `true.
     *
     * \return whether this instance can be created in or moved to the given host
     */
    virtual bool is_available(const window_host_ptr& p_host) const = 0;

    /**
     * \brief Create the panel or toolbar window or transfer the window to a new host.
     *
     * A canonical implementation of this method is similar to:
     *
     * \code{.cpp}
     * if (m_wnd) {
     *     ShowWindow(m_wnd, SW_HIDE);
     *     SetParent(m_wnd, parent);
     *     m_host->relinquish_ownership(get_wnd());
     *     m_host = host;
     *     SetWindowPos(get_wnd(), nullptr, position.x, position.y, position.cx, position.cy, SWP_NOZORDER);
     * } else {
     *     m_host = host;
     *     m_wnd = CreateWindowEx(ex_styles, class_name, window_name, styles,
     *         position.x, position.y, position.cx, position.cy, parent,
     *         reinterpret_cast<HMENU>(0), core_api::get_my_instance(), nullptr);
     * }
     *
     * return m_wnd;
     * \endcode
     *
     * Note that the case where this method is called to transfer an already-created window to
     * a new host is only relevant for single-instance windows. For multi-instance windows, you can
     * instead call `uBugCheck()` or do something similar if that happens.
     *
     * \remark
     * - The window must have the `WS_CHILD` window style. It must not have the `WS_POPUP`,
     *   `WS_CAPTION` or `WS_VISIBLE` styles.
     * - Do not make the window visible – the host will do this when ready.
     * - Use the `WS_EX_CONTROLPARENT` extended style if the window has child windows that receive
     *   keyboard input, and you want them to be included in tab operations in the host window.
     * - Do not directly create a common control as your window. You must create
     *   an intermediate window to contain any common controls (or other child windows) that
     *   communicate to the parent window via `WM_COMMAND` and `WM_NOTIFY` window messages.
     * - If this window is not itself hosting another panel or toolbar, it can be dialog managed
     *   (i.e. using `modeless_dialog_manager`) if required.
     * - The window must have a dialog item ID of 0.
     *
     * \pre May only be called if is_available() returned true.
     *
     * \param [in]    wnd_parent        Handle to the window to use as the parent for your window
     * \param [in]    p_host            Pointer to the host that creates the extension.
     *                                This parameter may not be NULL.
     * \param [in]    p_position        Initial position of the window
     * \return                        Window handle of the panel or toolbar window
     */
    virtual HWND create_or_transfer_window(HWND wnd_parent, const window_host_ptr& p_host,
        const ui_helpers::window_position_t& p_position = ui_helpers::window_position_null)
        = 0;

    /**
     * \brief Destroy the window.
     *
     * Typically, you would call `DestroyWindow()` in this method, and clean up any
     * resources that are no longer required.
     *
     * In normal circumstances, the window will always be destroyed using this method.
     */
    virtual void destroy_window() = 0;

    /**
     * \brief Get the extension window handle.
     *
     * \pre May only be called on hosted extensions.
     *
     * \return Window handle of the extension window (or
     *         `nullptr` if the window has not been created)
     */
    virtual HWND get_wnd() const = 0;

    /**
     * \brief This function is not used.
     *
     * \note Handle the `WM_GETMINMAXINFO` message to indicate size limits to the host.
     *
     * \param [out]    p_out    Not used.
     */
    virtual void get_size_limits(size_limit_t& p_out) const
    {
        MINMAXINFO mmi;
        memset(&mmi, 0, sizeof(MINMAXINFO));
        mmi.ptMaxTrackSize.x = MAXLONG;
        mmi.ptMaxTrackSize.y = MAXLONG;
        SendMessage(get_wnd(), WM_GETMINMAXINFO, 0, (LPARAM)&mmi);

        p_out.min_height = mmi.ptMinTrackSize.y;
        p_out.min_width = mmi.ptMinTrackSize.x;
        p_out.max_width = mmi.ptMaxTrackSize.x;
        p_out.max_height = mmi.ptMaxTrackSize.y;
    }

    /**
     * \brief Create an extension by ID.
     *
     * \param [in]    guid            GUID of a ui_extension
     * \param [out]   p_out           Receives a pointer to the window.
     *
     * \return                        true if the window was found and instantiated.
     *                                You may assume that if the method returns true,
     *                                p_out is valid.
     */
    static inline bool create_by_guid(const GUID& guid, window_ptr& p_out)
    {
        service_enum_t<window> e;
        service_ptr_t<window> ptr;

        if (e.first(ptr))
            do {
                if (ptr->get_extension_guid() == guid) {
                    p_out.copy(ptr);
                    return true;
                }
            } while (e.next(ptr));
        return false;
    }

    /**
     * \brief Helper function. Handle a press of the Tab key by focusing the next or previous tabbable window.
     *
     * \param [in]    wnd_focus       Handle to the currently focused window
     * \return                        The handle to the window that was activated, or `nullptr`
     *                                if the focus was not changed.
     */
    static HWND g_on_tab(HWND wnd_focus);

    /**
     * \brief    Helper function. Process keyboard shortcuts using
     * keyboard_shortcut_manager_v2::process_keydown_simple(). Requires foobar2000 >= 0.9.5.
     *
     * \param [in]    wp              Key down message WPARAM value.
     * \return                        If a shortcut was executed.
     */
    static bool g_process_keydown_keyboard_shortcuts(WPARAM wp);

    FB2K_MAKE_SERVICE_INTERFACE_ENTRYPOINT(window);
};

/**
 * \brief Subclass of uie::window, specifically for menu bars.
 */
class NOVTABLE menu_window : public window {
public:
    /**
     * \brief Handle menu accelerator key presses (e.g. Alt+F for the File menu).
     *
     * Called by the host in its WM_MENUCHAR handler to notify extension that a menu
     * was requested to be opened.
     *
     * You should check whether the accelerator key pressed is one of yours.
     *
     * \pre May only be called on hosted extensions.
     *
     * \param[in] chr character that was pressed
     * \return whether you claimed the accelerator key, and showed/will show your menu
     */
    virtual bool on_menuchar(unsigned short chr) = 0;

    /**
     * \brief Focus the menu window.
     *
     * \pre May only be called on hosted extensions.
     */
    virtual void set_focus() = 0;

    /**
     * \brief Retrieve whether the menu has the keyboard focus.
     *
     * \pre May only be called on hosted extensions.
     * \return whether your menu has keyboard focus
     */
    virtual bool is_menu_focused() const = 0;

    /**
     * \brief    Show underlines under characters indicating menu accelerators.
     *
     * \remark   Relevant when `SystemParametersInfo()` returns `FALSE` for `SPI_GETKEYBOARDCUES`.
     *
     * \remark   Do not change the state within this function call. Use `PostMessage()`.
     *
     * \par      Implementation example
     * \code{.cpp}
     * PostMessage(wnd_menu, WM_UPDATEUISTATE, MAKEWPARAM(UIS_CLEAR , UISF_HIDEACCEL), 0);
     * \endcode
     */
    virtual void show_accelerators() = 0;

    /**
     * \brief    Indicates that you should stop underlining menu access keys in your menu
     *
     * \remark   Applicable only if your menu underlines menu access keys only when
     *           activated by the keyboard. This is typically determined by the
     *           `SPI_GETKEYBOARDCUES` system parameter.
     *
     * \remark   Do not change the state within this function call. Use `PostMessage()`.
     *
     * \par      Implementation example
     * \code{.cpp}     * if (GetFocus() == m_wnd)
     *     return;
     *
     * BOOL show_keyboard_cues{TRUE};
     * SystemParametersInfo(SPI_GETKEYBOARDCUES, 0, &show_keyboard_cues, 0);
     * PostMessage(wnd_menu, WM_UPDATEUISTATE, MAKEWPARAM(show_keyboard_cues ? UIS_CLEAR : UIS_SET, UISF_HIDEACCEL), 0);
     * \endcode
     */
    virtual void hide_accelerators() = 0;

    FB2K_MAKE_SERVICE_INTERFACE(menu_window, window);
};

/**
 * \brief Subclass of uie::menu_window, with additional functions.
 */
class NOVTABLE menu_window_v2 : public menu_window {
public:
    /**
     * \brief Retrieve the handle of the window that was focused before the menu was.
     *
     * Implementations should track the previously focused window using the `WM_SETFOCUS`
     * and `WM_KILLFOCUS` window messages.
     *
     * \pre May only be called on hosted extensions.
     *
     * \return HWND of the previously focused window, or nullptr if
     *         no such window or the menu bar is not currently focused.
     */
    virtual HWND get_previous_focus_window() const = 0;

    FB2K_MAKE_SERVICE_INTERFACE(menu_window_v2, menu_window);
};

/**
 * \brief Subclass of uie::window for playlist views
 */
class NOVTABLE playlist_window : public window {
public:
    /**
     * \brief Focus the playlist window.
     *
     * \pre May only be called on hosted extensions.
     */
    virtual void set_focus() = 0;

    FB2K_MAKE_SERVICE_INTERFACE(playlist_window, window);
};

/** \brief Standard implementation of uie::menu_node_command_t, for an "Options" menu item */
class menu_node_configure : public uie::menu_node_command_t {
    window_ptr p_this;
    pfc::string8 m_title;

public:
    bool get_display_data(pfc::string_base& p_out, unsigned& p_displayflags) const override
    {
        p_out = m_title;
        p_displayflags = 0;
        return true;
    }

    bool get_description(pfc::string_base& p_out) const override { return false; }
    void execute() override { p_this->show_config_popup(p_this->get_wnd()); }
    menu_node_configure(window* wnd, const char* p_title = "Options") : p_this(wnd), m_title(p_title) {}
};

template <class T>
class window_factory;

template <class T>
class window_factory_single;

template <class T>
class window_factory_transparent_single;

} // namespace uie

template <class T, bool B>
class window_implementation : public T {
    virtual const bool get_is_single_instance() const { return B; }

protected:
    window_implementation() {}

    friend uie::window_factory<T>;
    friend uie::window_factory_single<T>;
    friend uie::window_factory_transparent_single<T>;
};

namespace uie {

/**
 * \brief Service factory for multiple instance windows.
 * \par Usage example
 * \code{.cpp} * static window_factory<my_uie> foo_extension;
 * \endcode
 */
template <class T>
class window_factory : public service_factory_base_t<window> {
public:
    window_factory() : service_factory_base_t<window>() {}

    ~window_factory() {}

    void instance_create(service_ptr_t<service_base>& p_out) override
    {
        service_impl_t<::window_implementation<T, false>>* item = new service_impl_t<::window_implementation<T, false>>;
        p_out = (service_base*)(window*)(::window_implementation<T, false>*)item;
    }
};

/**
 * \brief Service factory for single instance windows.
 *
 * \note Single instance windows are not recommended. Implement a
 * multi-instance window wherever possible.
 *
 * \par Usage example
 * \code{.cpp} * static window_factory_single<my_window> foo_extension2;
 * \endcode
 * The static instance of <code>my_window</code> can be accessed
 * as <code>foo_extension2.get_static_instance()</code>.
 */
template <class T>
class window_factory_single : public service_factory_base_t<window> {
    service_impl_single_t<window_implementation<T, true>> g_instance;

public:
    window_factory_single() : service_factory_base_t<window>() {}

    ~window_factory_single() {}

    void instance_create(service_ptr_t<service_base>& p_out) override
    {
        p_out = (service_base*)(window*)(window_implementation<T, true>*)&g_instance;
    }

    inline T& get_static_instance() { return (T&)g_instance; }
};

/**
 * \brief Service factory for single instance windows.
 *
 * \note Single instance windows are not recommended. Implement a
 * multi-instance window wherever possible.
 *
 * \par Usage example
 * \code{.cpp} * static window_factory_single_transparent<my_window> foo_extension3;
 * \endcode
 * The static instance of <code>my_window</code> can be accessed
 * as <code>foo_extension3</code>.
 */
template <class T>
class window_factory_transparent_single
    : public service_factory_base_t<window>
    , public service_impl_single_t<window_implementation<T, true>> {
public:
    window_factory_transparent_single() : service_factory_base_t<window>() {}

    void instance_create(service_ptr_t<service_base>& p_out) override
    {
        p_out = (service_base*)(window*)(window_implementation<T, true>*)this;
    }

    inline T& get_static_instance() const { return *(T*)this; }
};

/**
 * \brief Helper class to hold information about ui_extension services
 */
class window_info_simple {
public:
    GUID guid;
    pfc::string8 name;
    pfc::string8 category;
    bool prefer_multiple_instances;
    unsigned type;
    window_info_simple() : guid(pfc::guid_null), prefer_multiple_instances(false), type(NULL) {}
};

/**
 * \brief Helper class to hold information about many ui_extension services
 */
class window_info_list_simple : public pfc::list_t<window_info_simple> {
public:
    /**
     * \brief window_info_simple comparison function
     */
    static int compare(const window_info_simple& n1, const window_info_simple& n2)
    {
        int rv = 0;
        rv = uStringCompare(n1.category, n2.category);
        if (!rv)
            rv = uStringCompare(n1.name, n2.name);
        return rv;
    }
    /**
     * \brief Helper function to get the name of a ui_extension by its GUID.
     * \pre You must populate the list first
     */
    void get_name_by_guid(const GUID& in, pfc::string_base& out);

    /**
     * \brief Helper function to sort the extensions, first by category, then by name.
     */

    void sort_by_category_and_name() { sort_t(compare); }
};

} // namespace uie
#endif

#ifndef _UI_EXTENSION_HOST_H_
#define _UI_EXTENSION_HOST_H_

/**
 * \file window_host.h
 * \brief Window Host API
 */

namespace uie {

/**
 * \brief Interface for window_host service.
 *
 * This interface is to be implemented by panel and toolbar hosts.
 *
 * \remark The host must not be dialog managed.
 *
 * \remark Hosts must forward the following messages to hosted windows:
 * - `WM_SETTINGCHANGE`
 * - `WM_SYSCOLORCHANGE`
 * - `WM_TIMECHANGE`
 */
class NOVTABLE window_host : public service_base {
public:
    /**
     * \brief Get the unique ID of the host.
     *
     * This GUID is used to identify a specific host.
     *
     * \return host GUID
     */
    virtual const GUID& get_host_guid() const = 0;

    /**
     * \brief Notify this host about changed size limits of a hosted window.
     *
     * \pre May only be called by a hosted window.
     *
     * \param[in] wnd window handle of the panel or toolbar
     * \param[in] flags a combination of uie::size_limit_flag_t flags indicating which size limits changed
     *
     * \see uie::size_limit_flag_t
     */
    virtual void on_size_limit_change(HWND wnd, unsigned flags) = 0;

    /**
     * \brief Get whether the host supports resizing hosted windows.
     *
     * \param[in] wnd   handle to the window to test
     * \return   combination of uie::resize_flag_t flags to indicate whether
     *           the width or height can be modified
     *
     * \pre May only be called by a hosted window.
     *
     * \see uie::resize_flag_t
     */
    virtual unsigned is_resize_supported(HWND wnd) const = 0;

    /**
     * \brief Ask for your window to be resized.
     *
     * Implementers: If you cannot fully meet the request, do not attempt to partially fulfil it.
     * For example, if a request is made to modify both the width and height but you can only modify
     * one of those, do not change the size at all.
     *
     * \param[in] wnd   handle to the window to test
     * \return   combination of uie::size_height and uie::size_width to indicate whether
     *           the width or height is being modified
     *
     * \see uie::resize_flag_t
     */
    virtual bool request_resize(HWND wnd, unsigned flags, unsigned width, unsigned height) = 0;

    /**
     * \brief Instantiate the ui_status_text_override service, that can be used to display status messages.
     *
     * Implementers: if you wish to display status bar text in the main window,
     * simply use ui_control::override_status_text_create. Hybrid panel-hosts can forward the call
     * to their host.
     * If alternatively you wish to display the text in your own status area,
     * you are responsible for implementing ui_status_text_override. Be sure to obey certain conventions:
     * - Releasing the ui_status_text_override object should restore the text if revert_text has not been
     *   called.
     *
     * \pre May only be called by a hosted window.
     *
     * \param[out] p_out   receives new ui_status_text_override instance.
     * \return true on success, false on failure (out of memory / no GUI loaded / etc)
     */
    virtual bool override_status_text_create(service_ptr_t<ui_status_text_override>& p_out) = 0;

    /**
     * \brief Query if keyboard shortcuts should be processed.
     *
     * Use this to determine whether keyboard shortcuts should be processed.
     * Do not process them if this method returns false.
     * Shortcuts can be processed using the keyboard_shortcut_manager service
     * from the foobar2000 SDK.
     *
     * Keyboard shortcuts would not be processed, for example, if the panel
     * is hosted in a popup window. In this case, the method returns false.
     *
     * If the method does return true, whether you process keyboard shortcuts
     * will depend on the type of functionality your control offers. For example,
     * in a edit control you may wish not to process keyboard shortcuts.
     *
     * The user must be able to navigate using the tab key. If VK_TAB is not
     * processed by the keyboard_shortcut_manager and the TAB press is not being
     * handled by the dialog manager, you should use g_on_tab() to change to the
     * next control.
     *
     * \par Usage example
     * \code{.cpp}     * case WM_KEYDOWN:
     *     if (p_host->get_keyboardshortcuts_enabled() &&
     * static_api_ptr_t<keyboard_shortcut_manager>()->on_keydown_xxxx(wp)) break; else if (wp == VK_TAB)
     * window::g_on_tab(wnd); break;
     * \endcode
     *
     * \pre May only be called by a hosted window.
     *
     * \return whether keyboard shortcuts should be processed
     */
    virtual bool get_keyboard_shortcuts_enabled() const { return true; }

    /**
     * \brief Query if a hosted window is visible.
     *
     * \note A hidden window may still have the `WS_VISIBLE` style if the host uses another mechanism
     * to hide panels.
     *
     * \param[in] wnd   handle to the window to test
     * \pre May only be called by a hosted window.
     *
     * \return whether window is visible.
     */
    virtual bool is_visible(HWND wnd) const = 0;

    /**
     * \brief Query if a hosted window can be hidden or shown.
     *
     * \param[in] wnd   handle to the window to test
     * \param[in] desired_visibility   whether you want the window to be visible
     * \pre May only be called by a hosted window.
     *
     * \return whether the required visibility can be set.
     */
    virtual bool is_visibility_modifiable(HWND wnd, bool desired_visibility) const = 0;

    /**
     * \brief Hide or show a hosted window.
     *
     * \param[in] wnd   handle to the window to test
     * \param[in] visibility   whether you want the window to be visible
     * \pre May only be called by a hosted window.
     *
     * \return whether the requested visibility was set
     */
    virtual bool set_window_visibility(HWND wnd, bool visibility) = 0;

    /**
     * \brief Relinquish ownership of a window instance.
     *
     * \note This method mainly relates to legacy functionality.
     *       Do not call it outside an implementation of
     *       `window::create_or_transfer_window()`.
     *
     * \pre May only be called by a hosted window.
     *
     * \param[in] wnd window handle of the panel or toolbar
     *
     * \see window::create_or_transfer_window
     */
    virtual void relinquish_ownership(HWND wnd) = 0;

    FB2K_MAKE_SERVICE_INTERFACE_ENTRYPOINT(window_host);
};

/**
 *  \brief Subclass of window_host, providing methods for retrieving information about child panels
 *
 *  New in SDK version 6.5.
 *
 *  In addition to the methods exposed through the window_host interface,
 *  this interface provides information about children panels that are in the same container/splitter.
 *  Note that these do not necessarily have the same window_host instance.
 */
class window_host_ex : public window_host {
public:
    /**
     * \brief    Retrieve a list of child panels. This is used by Filter panels
     *           so they can link themselves together.
     *
     * \param[in]    p_out             receives the list of panels
     */
    virtual void get_children(pfc::list_base_t<uie::window_ptr>& p_out) = 0;

    FB2K_MAKE_SERVICE_INTERFACE(window_host_ex, window_host);
};

/**
 *  \brief Subclass of window_host, providing methods for external control
 *
 * \note This service relates to legacy functionality. It is only implemented by the host for
 * the toolbars area.
 *
 *  In addition to the methods exposed through the window_host interface,
 *  this interface provides information about the host and its state as well
 *  as methods to manage hosted windows.
 */
class NOVTABLE window_host_with_control : public window_host {
public:
    /**
     * \brief Get supported window types.
     *
     * \return a combination of uie::window_type_t flags to indicate recommended types for the host
     * \see uie::window_type_t
     */
    virtual unsigned get_supported_types() const = 0;

    /**
     * \brief Insert new instance of a hosted window.
     *
     * Creates an instance of the specified windows and inserts it into the host's
     * client area. Single-instance windows should remove themselves from the
     * old host (if there is one).
     *
     * \pre May only be called if is_available() returned true.
     *
     * \param[in] guid unique ID of the window to be inserted
     * \param[in] height desired height of the new panel
     * \param[in] width desired width of the new panel
     *
     * \see is_available, window::init_or_take_ownership
     */
    virtual void insert_extension(const GUID& guid, unsigned height, unsigned width) = 0;

    /**
     * \brief Insert an existing instance of a hosted window.
     *
     * Inserts the given window instance into the host's client area.
     *
     * \pre May only be called if is_available() returned true.
     *
     * \param[in] p_ext pointer to the window instance to be inserted
     * \param[in] height desired height of the new panel
     * \param[in] width desired width of the new panel
     *
     * \see is_available, window::init_or_take_ownership
     */
    virtual void insert_extension(window_ptr& p_ext, unsigned height, unsigned width)
        = 0; // insert existing instance (or new if it hasnt been initialised)

    /**
     * \brief Get the name of the host.
     *
     * Get a user-readable name of the host.
     *
     * \warning
     * Do not use the name to identify hosts; use host GUIDs instead.
     *
     * \param[out] out receives the name of the host, e.g. "My UI/Sidebar"
     *
     * \see get_host_guid
     */
    virtual void get_name(pfc::string_base& out) const = 0;

    /**
     * \brief Get availability of the host.
     *
     * \return true if it is possible to insert a window into the host.
     *
     * \see insert_extension(const GUID &, unsigned, unsigned), insert_extension(window *, unsigned, unsigned)
     */
    virtual bool is_available() const = 0;

    FB2K_MAKE_SERVICE_INTERFACE(window_host_with_control, window_host);
};

/**
 * \brief Service factory for window hosts.
 * \par Usage example
 * \code{.cpp} * static window_host_factory<my_window_host> foo_host;
 * \endcode
 */
template <class T>
class window_host_factory : public service_factory_t<T> {};

/**
 * \brief Service factory for window hosts.
 * \par Usage example
 * \code{.cpp} * static window_host_factory<my_window_host> foo_host;
 * \endcode
 * The static instance of <code>my_window_host</code> can be accessed
 * as <code>foo_host.get_static_instance()</code>.
 */
template <class T>
class window_host_factory_single : public service_factory_single_t<T> {
public:
    inline operator uie::window_host_ptr() { return uie::window_host_ptr(&this->get_my_instance()); }
};

/**
 * \brief Service factory for window hosts.
 * \par Usage example
 * \code{.cpp} * static window_host_factory_transparent<my_window_host> foo_host2;
 * \endcode
 * The static instance of <code>my_window_host</code> can be accessed
 * as <code>foo_host2</code>.
 */
template <class T>
class window_host_factory_transparent_single : public service_factory_single_transparent_t<T> {};

} // namespace uie

#endif

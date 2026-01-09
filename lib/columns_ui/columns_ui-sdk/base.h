#ifndef _COLUMNS_SDK_BASE_H_
#define _COLUMNS_SDK_BASE_H_

namespace uie {
/** \brief Base class for uie::window. */
class NOVTABLE extension_base : public service_base {
public:
    /**
     * \brief Get unique ID representing this extension.
     *
     * The returned GUID must be unique per registered extension.
     *
     * \note Because the return type is a reference, you must return either a static
     * local variable or a global variable.
     *
     * \return extension GUID
     */
    virtual const GUID& get_extension_guid() const = 0;

    /**
     * \brief Get a user-readable name of the extension.
     *
     * \warning
     * Do not use the name to identify extensions; use extension GUIDs instead.
     *
     * \param [out] out receives the name of the extension, e.g. `"Spectrum analyser"`
     *
     * \see get_extension_guid
     */
    virtual void get_name(pfc::string_base& out) const = 0;

    /**
     * \brief Set instance configuration data.
     *
     * \remark
     * - This method is only called before `window::create_or_transfer_window()` (i.e. it will not
     *   be called once your window has been created)
     * - It must not be used by single instance extensions
     * - You should handle the case of an empty stream
     *
     * \note Consider both forwards and backwards compatibility deciding upon a data
     * format, when implementing this method and when implementing `get_config()`.
     *
     * \throw Throws pfc::exception on failure
     *
     * \param [in]    p_reader        Pointer to configuration data stream
     * \param [in]    p_size          Size of data in stream
     * \param [in]    p_abort         Signals abort of operation
     */
    virtual void set_config(stream_reader* p_reader, t_size p_size, abort_callback& p_abort) {}

    /**
     * \brief Get instance configuration data.
     *
     * \remark
     * Must not be used by single instance extensions.
     *
     * \note Consider both forwards and backwards compatibility deciding upon a data
     * format, when implementing this method and when implementing `set_config()`.
     *
     * \throw Throws pfc::exception on failure
     *
     * \param [out]    p_writer        Pointer to stream receiving configuration data
     * \param [in]     p_abort         Signals abort of operation
     */
    virtual void get_config(stream_writer* p_writer, abort_callback& p_abort) const {}

    /**
     * \brief Import previously exported instance configuration data.
     *
     * The export_config() method is used when exporting the Columns UI configuration and
     * ‘Any foobar2000 installation’ is selected as the target.
     *
     * The import_config() method is used when importing a configuration previously exported
     * with ‘Any foobar2000 installation’ selected as the target.
     *
     * You only need to consider overriding both methods if your normal configuration data
     * contains references to external resources specific to the current system.
     *
     * \remark
     * - This method is only called before `window::create_or_transfer_window()` (i.e. it will
     *   not be called once your window has been created)
     *
     * \note The default implementation simply calls set_config().
     *
     * \throw Throws pfc::exception on failure
     *
     * \param [in]    p_reader        Pointer to configuration data stream
     * \param [in]    p_size          Size of data in stream
     * \param [in]    p_abort         Signals abort of operation
     */
    virtual void import_config(stream_reader* p_reader, t_size p_size, abort_callback& p_abort)
    {
        set_config(p_reader, p_size, p_abort);
    }

    /**
     * \brief Export instance configuration data.
     *
     * This differs from get_config(), in that the data that is written is intended to be
     * transferable between different foobar2000 installations on different computers (i.e.
     * it is self-contained).
     *
     * The export_config() method is used when exporting the Columns UI configuration and
     * ‘Any foobar2000 installation’ is selected as the target.
     *
     * The import_config()   method is used when importing a configuration previously exported
     * with ‘Any foobar2000 installation’ selected as the target.
     *
     * You only need to consider overriding both methods if your normal configuration data
     * contains references to external resources specific to the current system.
     *
     * \remark
     * - This method is only called before `window::create_or_transfer_window()` (i.e. it will
     *   not be called once your window has been created)
     *
     * \note The default implementation simply calls get_config().
     *
     * \throw Throws pfc::exception on failure
     *
     * \param [out]    p_writer        Pointer to stream receiving configuration data
     * \param [in]     p_abort         Signals abort of operation
     */
    virtual void export_config(stream_writer* p_writer, abort_callback& p_abort) const
    {
        get_config(p_writer, p_abort);
    }

    /**
     * \brief Get whether the extension has a modal configuration window.
     *
     * The window is exposed through show_config_popup()
     *
     * \return true if the extension has a configuration window that can be shown using show_config_popup
     */
    virtual bool have_config_popup() const { return false; }

    /**
     * \brief Display a modal configuration dialog.
     *
     * \param [in]    wnd_parent    The window to use as the owner window for your configuration dialog
     *
     * \return false if the configuration did not change
     */
    virtual bool show_config_popup(HWND wnd_parent) { return false; }

    /**
     * \brief Retrieve menu items to be displayed in the host menu.
     *
     * \param [in]    p_hook        The interface you use to add your menu items
     */
    virtual void get_menu_items(menu_hook_t& p_hook) {}

    /**
     * \brief Helper function, set instance configuration data from raw pointer.
     *
     * \see set_config
     *
     * \throw Throws pfc::exception on failure
     *
     * \param [in]    p_data            Pointer to configuration data
     * \param [in]    p_size            Size of data
     * \param [in]    p_abort           Signals abort of operation
     */
    void set_config_from_ptr(const void* p_data, t_size p_size, abort_callback& p_abort);

    /**
     * \brief Helper function. Import instance configuration data from a raw pointer.
     *
     * \see import_config
     *
     * \throw Throws pfc::exception on failure
     *
     * \param [in]    p_data            Pointer to configuration data
     * \param [in]    p_size            Size of data in stream
     * \param [in]    p_abort           Signals abort of operation
     */
    void import_config_from_ptr(const void* p_data, t_size p_size, abort_callback& p_abort);

    /**
     * \brief Helper function. Write instance configuration data to an existing array.
     *
     * \see get_config
     *
     * \throw Throws pfc::exception on failure
     *
     * \param [out]    p_data            Array receiving configuration data
     * \param [in]     p_abort           Signals abort of operation
     * \param [in]     b_reset           Indicates whether the contents of the array should first be cleared
     */
    void get_config_to_array(pfc::array_t<uint8_t>& p_data, abort_callback& p_abort, bool b_reset = false) const;

    /**
     * \brief Helper function. Write instance configuration data to a new array.
     *
     * \see get_config
     *
     * \throw Throws pfc::exception on failure
     *
     * \param [in]     p_abort           Signals abort of operation
     */
    pfc::array_t<uint8_t> get_config_as_array(abort_callback& p_abort) const;

    /**
     * \brief Helper function. Export instance configuration data to an array.
     *
     * \see export_config
     *
     * \throw Throws pfc::exception on failure
     *
     * \param [out]   p_data             Array receiving exported configuration data
     * \param [in]    p_abort            Signals abort of operation
     * \param [in]    b_reset            Indicates whether the contents of the array should first be cleared
     */
    void export_config_to_array(pfc::array_t<uint8_t>& p_data, abort_callback& p_abort, bool b_reset = false) const;
};
}; // namespace uie

#endif //_COLUMNS_SDK_BASE_H_

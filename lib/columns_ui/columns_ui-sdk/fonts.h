#pragma once

namespace cui::fonts {

enum font_type_t {
    font_type_items,
    font_type_labels,
};
enum font_type_flag_t {
    font_type_flag_items = (1 << 0),
    font_type_flag_labels = (1 << 1),
};

/**
 * ID for the common items font.
 *
 * \note Added in Columns UI 3.0.0 alpha 1.
 */
constexpr GUID items_font_id{0x1d1be0c7, 0x59f8, 0x44ff, {0x9e, 0xbb, 0x07, 0xda, 0xf2, 0x27, 0xa9, 0x0f}};

/**
 * ID for the common labels font.
 *
 * \note Added in Columns UI 3.0.0 alpha 1.
 */
constexpr GUID labels_font_id{0x238cc90a, 0x9d4c, 0x471d, {0x8b, 0x02, 0xec, 0xdc, 0x78, 0x10, 0x58, 0x8c}};

/**
 * ID for the core console font.
 *
 * This corresponds to the `ui_font_console` font available via `ui_config_manager` when Columns UI is the active UI.
 *
 * \note Added in Columns UI 2.0.0.
 */
constexpr GUID core_console_font_id = {0xe9f4d060, 0x6feb, 0x4626, {0xb2, 0x5d, 0xba, 0x09, 0x0f, 0x75, 0xf7, 0xa5}};

/**
 * ID for the default core font.
 *
 * This corresponds to any font other than `ui_font_console`, `ui_font_lists` and `ui_font_playlists` available via
 * `ui_config_manager` when Columns UI is the active UI.
 *
 * \note Added in Columns UI 2.0.0.
 */
constexpr GUID core_default_font_id = {0x1adbc094, 0x3b35, 0x4bab, {0x82, 0xd5, 0x3b, 0xdd, 0x4a, 0x5c, 0x6a, 0xf9}};

/**
 * ID for the core lists font.
 *
 * This corresponds to the `ui_font_lists` and `ui_font_playlists` fonts available via `ui_config_manager` when Columns
 * UI is the active UI.
 *
 * \note Added in Columns UI 2.0.0.
 */
constexpr GUID core_lists_font_id = {0xfc3cb3a8, 0x72db, 0x4448, {0xb1, 0x2c, 0x2e, 0xf7, 0x6a, 0x15, 0xd7, 0x53}};

/**
 * Use this class if you wish to use the common fonts rather than implementing client
 */
class NOVTABLE common_callback {
public:
    virtual void on_font_changed(uint32_t changed_items_mask) const = 0;
};

/** One implementation in Columns UI - do not reimplement! */
class NOVTABLE manager : public service_base {
public:
    /** \brief Retrieves the font for the given client */
    virtual void get_font(const GUID& p_guid, LOGFONT& p_out) const = 0;

    /** \brief Retrieves common fonts. */
    virtual void get_font(const font_type_t p_type, LOGFONT& p_out) const = 0;
    /** \brief Sets your font as 'Custom' and to p_font */
    virtual void set_font(const GUID& p_guid, const LOGFONT& p_font) = 0;

    virtual void register_common_callback(common_callback* p_callback) = 0;
    virtual void deregister_common_callback(common_callback* p_callback) = 0;

    /** Helper */
    HFONT get_font(const GUID& p_guid) const
    {
        LOGFONT lf{};
        get_font(p_guid, lf);
        return CreateFontIndirect(&lf);
    }

    /** Helper */
    HFONT get_font(const font_type_t p_type) const
    {
        LOGFONT lf{};
        get_font(p_type, lf);
        return CreateFontIndirect(&lf);
    }

    FB2K_MAKE_SERVICE_INTERFACE_ENTRYPOINT(manager);
};

/**
 * Font management API with support for custom DPIs.
 *
 * This was intended for per-monitor DPI support. However, neither Columns UI
 * nor foobar2000 currently support per-monitor DPI.
 *
 * \note Requires Columns UI 1.7.0 beta 1 or newer.
 *
 * One implementation in Columns UI - do not reimplement!
 */
class NOVTABLE manager_v2 : public service_base {
public:
    /** \brief Retrieve the font for the given client */
    virtual LOGFONT get_client_font(GUID guid, unsigned dpi = USER_DEFAULT_SCREEN_DPI) const = 0;

    /** \brief Retrieve a common font. */
    virtual LOGFONT get_common_font(font_type_t type, unsigned dpi = USER_DEFAULT_SCREEN_DPI) const = 0;

    /** \brief Set your font as 'Custom' and to the specified font. */
    virtual void set_client_font(GUID guid, const LOGFONT& font, int point_size_tenths) = 0;

    virtual void register_common_callback(common_callback* callback) = 0;
    virtual void deregister_common_callback(common_callback* callback) = 0;

    HFONT get_client_font_handle(GUID guid, unsigned dpi = USER_DEFAULT_SCREEN_DPI) const
    {
        LOGFONT lf = get_client_font(guid, dpi);
        return CreateFontIndirect(&lf);
    }

    HFONT get_common_font_handle(const font_type_t type, unsigned dpi = USER_DEFAULT_SCREEN_DPI) const
    {
        LOGFONT lf = get_common_font(type, dpi);
        return CreateFontIndirect(&lf);
    }

    FB2K_MAKE_SERVICE_INTERFACE_ENTRYPOINT(manager_v2);
};

/**
 * Helper to simplify retrieving the font for a specific client.
 *
 * \see This has been superseded by get_log_font(), get_log_font_with_fallback()
 * and create_hfont_with_fallback()
 */
class helper {
public:
    void get_font(LOGFONT& p_out) const;
    HFONT get_font() const;

    helper(GUID font_id) : m_font_id(font_id) {}

private:
    GUID m_font_id;
};

class NOVTABLE client : public service_base {
public:
    virtual const GUID& get_client_guid() const = 0;
    virtual void get_name(pfc::string_base& p_out) const = 0;

    virtual font_type_t get_default_font_type() const = 0;

    virtual void on_font_changed() const = 0;

    template <class base>
    using factory = service_factory_single_t<base>;

    FB2K_MAKE_SERVICE_INTERFACE_ENTRYPOINT(client);

public:
    static bool create_by_guid(const GUID& p_guid, ptr& p_out);
};

}; // namespace cui::fonts

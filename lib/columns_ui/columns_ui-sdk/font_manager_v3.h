#pragma once

namespace cui::fonts {

PFC_DECLARE_EXCEPTION(exception_font_client_not_found, pfc::exception, "Font client not found");

#if NTDDI_VERSION >= 0x0A000004
static_assert(sizeof(DWRITE_FONT_AXIS_VALUE) == 8);
using axis_value = DWRITE_FONT_AXIS_VALUE;
#else
struct axis_value {
    uint32_t axis_tag;
    float value;
};
#endif

enum class font_family_model {
    /**
     * Returns the typographic font family name if known,
     * otherwise the weight-stretch-style family name.
     */
    automatic = 0,
    /**
     * Requests the weight-stretch-style family name.
     * May return an empty string if not known.
     */
    weight_stretch_style = 1,
    /**
     * Requests the typographic family name.
     * May return an empty string if not known.
     */
    typographic = 2,
};

/**
 * Defines text rendering options, as configured in Columns UI Colours and fonts preferences.
 *
 * \note Added in Columns UI 3.0.0.
 */
class NOVTABLE rendering_options : public service_base {
public:
    /**
     * Retrieves the rendering mode configured in Columns UI preferences.
     *
     * \note You do not need to use this method if you use `create_rendering_params()`,
     * `use_gdi_compatible_layout()` and `use_gdi_natural()`.
     */
    [[nodiscard]] virtual DWRITE_RENDERING_MODE rendering_mode() const noexcept = 0;

    /**
     * Retrieves the value of the 'Use greyscale anti-aliasing' setting in
     * Columns UI preferences.
     *
     * If enabled, `DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE` or `D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE`
     * should be used.
     *
     * \see `dwrite_text_antialiasing_mode()`, `d2d_text_antialiasing_mode()`
     */
    [[nodiscard]] virtual bool use_greyscale_antialiasing() const noexcept = 0;

    /**
     * Retrieves the value of the 'Use colour glyphs when available' setting in
     * Columns UI preferences.
     *
     * For `ID2D1RenderTarget::DrawTextLayout()` and similar methods, this refers to the
     * `D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT` flag.
     */
    [[nodiscard]] virtual bool use_colour_glyphs() const noexcept = 0;

    /**
     * Create an IDWriteRenderingParams object for the specified monitor and the values
     * returned by rendering_mode() and use_greyscale_antialiasing() value.
     */
    [[nodiscard]] virtual HRESULT create_rendering_params(
        IDWriteFactory* factory, HMONITOR monitor, IDWriteRenderingParams** rendering_params) const noexcept
        = 0;

#ifdef __WIL_COM_INCLUDED
    wil::com_ptr<IDWriteRenderingParams> create_wil_rendering_params(IDWriteFactory* factory, HMONITOR monitor) const
    {
        wil::com_ptr<IDWriteRenderingParams> rendering_params;
        THROW_IF_FAILED(create_rendering_params(factory, monitor, &rendering_params));
        return rendering_params;
    }
#endif

    /**
     * Helper function that returns the Direct2D text antialiasing mode.
     *
     * This can be used with `ID2D1RenderTarget::SetTextAntialiasMode()`.
     */
    [[nodiscard]] DWRITE_TEXT_ANTIALIAS_MODE dwrite_text_antialiasing_mode() const noexcept
    {
        if (use_greyscale_antialiasing())
            return DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE;

        return DWRITE_TEXT_ANTIALIAS_MODE_CLEARTYPE;
    }

#ifdef _D2D1_H_
    /**
     * Helper function that returns the Direct2D text antialiasing mode.
     *
     * This can be used with `ID2D1RenderTarget::SetTextAntialiasMode()`.
     */
    [[nodiscard]] D2D1_TEXT_ANTIALIAS_MODE d2d_text_antialiasing_mode() const noexcept
    {
        if (rendering_mode() == DWRITE_RENDERING_MODE_ALIASED)
            return D2D1_TEXT_ANTIALIAS_MODE_ALIASED;

        if (use_greyscale_antialiasing())
            return D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE;

        return D2D1_TEXT_ANTIALIAS_MODE_DEFAULT;
    }
#endif

    /**
     * Helper function that returns whether a GDI-compatible text layout should be used.
     *
     * If this returns true, `IDWriteFactory::CreateGdiCompatibleTextLayout()` should be used instead of
     * `IDWriteFactory::CreateTextLayout()`.
     */
    [[nodiscard]] bool use_gdi_compatible_layout() const noexcept
    {
        const auto rendering_mode_ = rendering_mode();

        return rendering_mode_ == DWRITE_RENDERING_MODE_ALIASED || rendering_mode_ == DWRITE_RENDERING_MODE_GDI_CLASSIC
            || rendering_mode_ == DWRITE_RENDERING_MODE_GDI_NATURAL;
    }

    /**
     * Helper function that returns the value of the `useGdiNatural` argument of
     * `IDWriteFactory::CreateGdiCompatibleTextLayout()`.
     *
     * \see `use_gdi_compatible_layout()`
     */
    [[nodiscard]] bool use_gdi_natural() const noexcept
    {
        return rendering_mode() == DWRITE_RENDERING_MODE_GDI_NATURAL;
    }

    FB2K_MAKE_SERVICE_INTERFACE(rendering_options, service_base)
};

/**
 * Implementation of rendering_options. Can be used to create an instance of `rendering_options` when the `manager_v3`
 * service is missing (due to a compatible version of Columns UI not being installed).
 *
 * To create an instance of this class, use `fb2k::service_new()` function:
 *
 * \code{.cpp}
 * const auto rendering_options_ptr = fb2k::service_new<RenderingOptions>(
 *   // constructor arguments
 * );
 * \endcode
 */
class NOVTABLE rendering_options_impl : public rendering_options {
public:
    rendering_options_impl() {}

    rendering_options_impl(
        DWRITE_RENDERING_MODE rendering_mode, bool use_greyscale_antialiasing, bool use_colour_glyphs)
        : m_rendering_mode(rendering_mode)
        , m_use_greyscale_antialiasing(use_greyscale_antialiasing)
        , m_use_colour_glyphs(use_colour_glyphs)
    {
    }

    DWRITE_RENDERING_MODE rendering_mode() const noexcept override { return m_rendering_mode; }
    bool use_greyscale_antialiasing() const noexcept override { return m_use_greyscale_antialiasing; }
    bool use_colour_glyphs() const noexcept override { return m_use_colour_glyphs; }
    HRESULT create_rendering_params(
        IDWriteFactory* factory, HMONITOR monitor, IDWriteRenderingParams** rendering_params) const noexcept override
    {
        if (!m_rendering_params.is_valid() || m_last_monitor != monitor) {
            const HRESULT hr = dwrite_utils::create_custom_rendering_params(
                factory, monitor, m_rendering_mode, m_rendering_params.receive_ptr());

            if (FAILED(hr))
                return hr;

            m_last_monitor = monitor;
        }

        *rendering_params = m_rendering_params.get_ptr();
        (*rendering_params)->AddRef();
        return S_OK;
    }

private:
    DWRITE_RENDERING_MODE m_rendering_mode{DWRITE_RENDERING_MODE_DEFAULT};
    bool m_use_greyscale_antialiasing{};
    bool m_use_colour_glyphs{true};
    mutable HMONITOR m_last_monitor{};
    mutable pfc::com_ptr_t<IDWriteRenderingParams> m_rendering_params;
};

/**
 * Defines a font, as configured in Columns UI Colours and fonts preferences.
 *
 * \note Added in Columns UI 3.0.0.
 *
 * The recommended methods to use are create_text_format() and rendering_options().
 * However, access to the underlying data is also provided for less common use cases.
 */
class NOVTABLE font : public service_base {
public:
    [[nodiscard]] virtual const wchar_t* family_name(
        font_family_model font_family_model = font_family_model::automatic) noexcept
        = 0;
    [[nodiscard]] virtual DWRITE_FONT_WEIGHT weight() noexcept = 0;
    [[nodiscard]] virtual DWRITE_FONT_STRETCH stretch() noexcept = 0;
    [[nodiscard]] virtual DWRITE_FONT_STYLE style() noexcept = 0;

    /**
     * Get the font size, in DIP units.
     */
    [[nodiscard]] virtual float size() noexcept = 0;

    /**
     * Get the number of configured font axes. May be zero, in which case
     * the weight-stretch-style values should be used.
     */
    [[nodiscard]] virtual size_t axis_count() const noexcept = 0;

    /**
     * Get an axis value.
     *
     * \param index Axis index. Must be in the range 0 <= index < axis_count().
     * \throws std::out_of_range on invalid index
     */
    [[nodiscard]] virtual axis_value axis_value(size_t index) const = 0;

    /**
     * Get a GDI-compatible LOGFONT for this font.
     *
     * Note that the LOGFONT returned is the closest equivalent as GDI does
     * not support modern font features.
     */
    [[nodiscard]] virtual LOGFONT log_font() noexcept = 0;

    /**
     * Get a GDI-compatible LOGFONT for this font and a specific DPI.
     *
     * Note that the LOGFONT returned is the closest equivalent as GDI does
     * not support modern font features.
     */
    [[nodiscard]] virtual LOGFONT log_font_for_dpi(unsigned dpi) noexcept = 0;

    /**
     * Create a font fallback object that should be applied to the text
     * format using `IDWriteTextFormat1::SetFontFallback()`.
     *
     * \note You do not need to use this method if the text format was created
     * using `create_text_format()`.
     *
     * The custom font fallback object is used for the ‘Use alternative emoji font
     * selection logic’ option in Columns UI text rendering options.
     *
     * If a custom font fallback object is not in use, `S_FALSE` will be returned
     * and `*font_fallback` will be set to `nullptr`. Do not use `*font_fallback`
     * without checking that it is not equal to `nullptr`.
     */
    [[nodiscard]] virtual HRESULT create_font_fallback(
#ifdef DWRITE_2_H_INCLUDED
        IDWriteFontFallback** font_fallback
#else
        IUnknown** font_fallback
#endif
        ) noexcept
        = 0;

    /**
     * Create a DirectWrite text format for this font.
     *
     * The font fallback object for the font will be automatically configured if needed.
     *
     * No further customisation of the text format is performed. You should configure text
     * alignment, paragraph alignment, word wrapping and other options according to your needs.
     */
    [[nodiscard]] virtual HRESULT create_text_format(
        IDWriteTextFormat** text_format, const wchar_t* locale_name = L"") noexcept
        = 0;

    /**
     * Retrieves text rendering options.
     *
     * These options should be used when rendering text using the created text format.
     */
    [[nodiscard]] virtual rendering_options::ptr rendering_options() noexcept = 0;

#ifdef __WIL_COM_INCLUDED
    wil::com_ptr<IDWriteFontFallback> create_wil_font_fallback()
    {
        wil::com_ptr<IDWriteFontFallback> font_fallback;
        THROW_IF_FAILED(create_font_fallback(&font_fallback));
        return font_fallback;
    }

    wil::com_ptr<IDWriteFontFallback> try_create_wil_font_fallback() noexcept
    {
        wil::com_ptr<IDWriteFontFallback> font_fallback;
        (void)create_font_fallback(&font_fallback);
        return font_fallback;
    }

    wil::com_ptr<IDWriteTextFormat> create_wil_text_format(const wchar_t* locale_name = L"")
    {
        wil::com_ptr<IDWriteTextFormat> text_format;
        THROW_IF_FAILED(create_text_format(&text_format, locale_name));
        return text_format;
    }

    wil::com_ptr<IDWriteTextFormat> try_create_wil_text_format(const wchar_t* locale_name = L"") noexcept
    {
        wil::com_ptr<IDWriteTextFormat> text_format;
        (void)create_text_format(&text_format, locale_name);
        return text_format;
    }
#endif

    FB2K_MAKE_SERVICE_INTERFACE(font, service_base)
};

/**
 * DirectWrite-friendly manager_v3 interface.
 *
 * \note Added in Columns UI 3.0.0.
 */
class NOVTABLE manager_v3 : public service_base {
public:
    /**
     * Get a font definition for a particular font client or a common font.
     *
     * \param id font client or common font GUID
     * \return service pointer for a font instance.
     * \throw exception_font_client_not_found thrown if an invalid GUID is supplied
     */
    [[nodiscard]] virtual font::ptr get_font(GUID id) const = 0;

    /**
     * Retrieves the default text rendering options.
     *
     * \remark ``font::rendering_options()`` should be used in preference to this method. This method is only provided
     * to allow access to the configured rendering options when the font being used is not provided by Columns UI.
     *
     * \see ``font::rendering_options()``
     */
    [[nodiscard]] virtual rendering_options::ptr get_default_rendering_options() noexcept = 0;

    /**
     * Get the default Columns UI font fallback object when it’s required.
     *
     * \remark ``font::create_font_fallback()`` should be used in preference to this method. This method is only
     * provided to allow access to the font fallback object when the font being used is not provided by Columns UI.
     *
     * \see See ``font::create_font_fallback()`` for more details about the %font fallback object returned.
     */
    [[nodiscard]] virtual HRESULT get_default_font_fallback(
#ifdef DWRITE_2_H_INCLUDED
        IDWriteFontFallback** font_fallback
#else
        IUnknown** font_fallback
#endif
        ) noexcept
        = 0;

    /**
     * Set the font size for a client font.
     *
     * \param id font client GUID
     * \param size new size in DIP units
     * \throw exception_font_client_not_found thrown if an invalid GUID is supplied
     */
    virtual void set_font_size(GUID id, float size) = 0;

    /**
     * Register a new callback for when a font has changed.
     * @param id client or common font GUID
     * @param callback callback function
     * @return callback token that should be reset when you no longer want the callback function to be called
     */
    [[nodiscard]] virtual callback_token::ptr on_font_changed(GUID id, basic_callback::ptr callback) = 0;

    /**
     * Register a new callback for when the default rendering options have changed.
     *
     * \remark This callback should only be used if ``get_default_rendering_options()`` is being used. Do not use it if
     * you’re using ``font::rendering_options()``.
     *
     * @param callback callback function
     * @return callback token that should be reset when you no longer want the callback function to be called
     */
    [[nodiscard]] virtual callback_token::ptr on_default_rendering_options_changed(basic_callback::ptr callback) = 0;

    /**
     * Register a new callback for when the font fallback object has changed.
     *
     * \remark This callback should only be used if ``get_default_font_fallback()`` is being used. Do not use it if
     * you’re using ``font::font_fallback()``.
     *
     * @param callback callback function
     * @return callback token that should be reset when you no longer want the callback function to be called
     */
    [[nodiscard]] virtual callback_token::ptr on_default_font_fallback_changed(basic_callback::ptr callback) = 0;

    FB2K_MAKE_SERVICE_INTERFACE_ENTRYPOINT(manager_v3)
};

} // namespace cui::fonts

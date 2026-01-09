#pragma once

namespace uih::direct_write {

using AxisValues = std::unordered_map<uint32_t, float>;

class RenderingParams {
public:
    using Ptr = std::shared_ptr<RenderingParams>;

    RenderingParams(wil::com_ptr<IDWriteFactory1> factory, DWRITE_RENDERING_MODE rendering_mode,
        bool use_greyscale_antialiasing, bool use_colour_glyphs)
        : m_factory(std::move(factory))
        , m_rendering_mode(rendering_mode)
        , m_use_greyscale_antialiasing(use_greyscale_antialiasing)
        , m_use_colour_glyphs(use_colour_glyphs)
    {
    }

    wil::com_ptr<IDWriteRenderingParams> get(HWND wnd) const;
    DWRITE_RENDERING_MODE rendering_mode() const { return m_rendering_mode; }
    D2D1_TEXT_ANTIALIAS_MODE d2d_text_antialiasing_mode() const
    {
        if (m_rendering_mode == DWRITE_RENDERING_MODE_ALIASED)
            return D2D1_TEXT_ANTIALIAS_MODE_ALIASED;

        if (m_use_greyscale_antialiasing)
            return D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE;

        return D2D1_TEXT_ANTIALIAS_MODE_DEFAULT;
    }

    bool use_greyscale_antialiasing() const { return m_use_greyscale_antialiasing; }
    bool use_colour_glyphs() const { return m_use_colour_glyphs; }

private:
    mutable wil::com_ptr<IDWriteRenderingParams> m_rendering_params;
    mutable HMONITOR m_monitor{};

    wil::com_ptr<IDWriteFactory1> m_factory;
    DWRITE_RENDERING_MODE m_rendering_mode{};
    bool m_use_greyscale_antialiasing{};
    bool m_use_colour_glyphs{true};
};

struct EmojiFontSelectionConfig {
    std::wstring colour_emoji_family_name{};
    std::wstring monochrome_emoji_family_name{};
};

class TextLayout {
public:
    TextLayout(wil::com_ptr<IDWriteFactory1> factory, wil::com_ptr<IDWriteGdiInterop> gdi_interop,
        wil::com_ptr<IDWriteTextLayout> text_layout, RenderingParams::Ptr rendering_params)
        : m_factory(std::move(factory))
        , m_gdi_interop(std::move(gdi_interop))
        , m_text_layout(std::move(text_layout))
        , m_rendering_params(std::move(rendering_params))
    {
        m_text_layout_4 = m_text_layout.try_query<IDWriteTextLayout4>();
    }

    float get_max_height() const noexcept;
    float get_max_width() const noexcept;
    DWRITE_TEXT_METRICS get_metrics() const;
    DWRITE_OVERHANG_METRICS get_overhang_metrics() const;
    void render_with_transparent_background(HWND wnd, HDC dc, RECT output_rect, COLORREF default_colour,
        float x_origin_offset = 0.0f, wil::com_ptr<IDWriteBitmapRenderTarget> bitmap_render_target = {}) const;
    void set_colour(COLORREF colour, DWRITE_TEXT_RANGE text_range) const;
    void set_effect(IUnknown* effect, DWRITE_TEXT_RANGE text_range) const;
    void set_max_height(float value) const;
    void set_max_width(float value) const;
    void set_underline(bool is_underlined, DWRITE_TEXT_RANGE text_range) const;
    void set_family(const wchar_t* family_name, DWRITE_TEXT_RANGE text_range) const;
    void set_size(float size, DWRITE_TEXT_RANGE text_range) const;
    void set_wss(DWRITE_FONT_WEIGHT weight, std::variant<DWRITE_FONT_STRETCH, float> width_or_stretch,
        DWRITE_FONT_STYLE style, DWRITE_TEXT_RANGE text_range) const;

    const RenderingParams::Ptr& rendering_params() { return m_rendering_params; }

    const wil::com_ptr<IDWriteTextLayout>& text_layout() const { return m_text_layout; }

private:
    wil::com_ptr<IDWriteFactory1> m_factory;
    wil::com_ptr<IDWriteGdiInterop> m_gdi_interop;
    wil::com_ptr<IDWriteTextLayout> m_text_layout;
    wil::com_ptr<IDWriteTextLayout4> m_text_layout_4;
    RenderingParams::Ptr m_rendering_params;
};

struct TextPosition {
    int left{};
    float left_remainder_dip{};
    int top{};
    int width{};
    int height{};
};

class TextFormat {
public:
    TextFormat(std::shared_ptr<class Context> context, wil::com_ptr<IDWriteFactory1> factory,
        wil::com_ptr<IDWriteGdiInterop> gdi_interop, wil::com_ptr<IDWriteTextFormat> text_format,
        RenderingParams::Ptr rendering_params)
        : m_context(std::move(context))
        , m_factory(std::move(factory))
        , m_gdi_interop(std::move(gdi_interop))
        , m_text_format(std::move(text_format))
        , m_rendering_params(std::move(rendering_params))
    {
    }

    void set_text_alignment(DWRITE_TEXT_ALIGNMENT value = DWRITE_TEXT_ALIGNMENT_LEADING) const;
    void set_paragraph_alignment(DWRITE_PARAGRAPH_ALIGNMENT value) const;
    void set_word_wrapping(DWRITE_WORD_WRAPPING value) const;

    [[nodiscard]] int get_minimum_height(std::wstring_view text = std::wstring_view(L"", 0)) const;
    [[nodiscard]] TextPosition measure_text_position(
        std::wstring_view text, int height, float max_width = 65536.0f, bool enable_ellipsis = false) const;
    [[nodiscard]] int measure_text_width(std::wstring_view text) const;
    [[nodiscard]] TextLayout create_text_layout(
        std::wstring_view text, float max_width, float max_height, bool enable_ellipsis = false) const;
    [[nodiscard]] DWRITE_FONT_WEIGHT get_weight() const;
    [[nodiscard]] std::variant<DWRITE_FONT_STRETCH, float> get_stretch() const;
    [[nodiscard]] DWRITE_FONT_STYLE get_style() const;

private:
    mutable wil::com_ptr<IDWriteInlineObject> m_trimming_sign;
    std::shared_ptr<Context> m_context;
    wil::com_ptr<IDWriteFactory1> m_factory;
    wil::com_ptr<IDWriteGdiInterop> m_gdi_interop;
    wil::com_ptr<IDWriteTextFormat> m_text_format;
    RenderingParams::Ptr m_rendering_params;
};

struct Font {
    wil::com_ptr<IDWriteFont> font;
    std::wstring localised_name;
    DWRITE_FONT_WEIGHT weight{};
    DWRITE_FONT_STRETCH stretch{};
    DWRITE_FONT_STYLE style{};
    AxisValues axis_values;
};

struct AxisRange {
    uint32_t tag{};
    float min{};
    float max{};
    bool is_toggle{};
};

struct FontFamily {
    wil::com_ptr<IDWriteFontFamily> family;
    std::wstring wss_name;
    std::wstring typographic_name;
    bool is_symbol_font{};
    std::vector<AxisRange> axes;

    std::vector<Font> fonts() const;
    const std::wstring& display_name() const { return typographic_name.empty() ? wss_name : typographic_name; }
};

struct WeightStretchStyle {
    std::wstring family_name{L"Segoe UI"};
    DWRITE_FONT_WEIGHT weight{DWRITE_FONT_WEIGHT_REGULAR};
    DWRITE_FONT_STRETCH stretch{DWRITE_FONT_STRETCH_NORMAL};
    DWRITE_FONT_STYLE style{DWRITE_FONT_STYLE_NORMAL};
};

struct ResolvedFontNames {
    std::wstring family_name;
    std::wstring face_name;
};

class Context : public std::enable_shared_from_this<Context> {
public:
    using Ptr = std::shared_ptr<Context>;

    static Ptr s_create()
    {
        auto ptr = s_ptr.lock();

        if (!ptr) {
            ptr = std::make_shared<Context>();
            s_ptr = ptr;
        }

        return ptr;
    }

    Context();

    const wil::com_ptr<IDWriteFactory1>& factory() { return m_factory; }

    wil::com_ptr<IDWriteBitmapRenderTarget> create_bitmap_render_target(HDC dc, uint32_t width, uint32_t height) const;

    LOGFONT create_log_font(const wil::com_ptr<IDWriteFont>& font) const;
    LOGFONT create_log_font(const wil::com_ptr<IDWriteFontFace>& font_face) const;
    wil::com_ptr<IDWriteFont> create_font(const LOGFONT& log_font) const;
    TextFormat create_text_format(const wil::com_ptr<IDWriteFont>& font, float font_size);

    TextFormat create_text_format(const wil::com_ptr<IDWriteFontFamily>& font_family, DWRITE_FONT_WEIGHT weight,
        DWRITE_FONT_STRETCH stretch, DWRITE_FONT_STYLE style, float font_size, const AxisValues& axis_values = {});

    TextFormat create_text_format(const wchar_t* family_name, DWRITE_FONT_WEIGHT weight, DWRITE_FONT_STRETCH stretch,
        DWRITE_FONT_STYLE style, float font_size, const AxisValues& axis_values = {});

    TextFormat create_text_format(const LOGFONT& log_font, float font_size);

    std::optional<TextFormat> create_text_format_with_fallback(
        const LOGFONT& log_font, std::optional<float> font_size = {}) noexcept;

    TextFormat wrap_text_format(wil::com_ptr<IDWriteTextFormat> text_format,
        DWRITE_RENDERING_MODE rendering_mode = DWRITE_RENDERING_MODE_DEFAULT, bool use_greyscale_antialiasing = false,
        bool use_colour_glyphs = true, bool set_defaults = true);

    wil::com_ptr<IDWriteFontFallback> create_emoji_font_fallback(
        const EmojiFontSelectionConfig& emoji_font_selection_config) const;

    wil::com_ptr<IDWriteTypography> get_default_typography();

    std::optional<ResolvedFontNames> resolve_font_names(const wchar_t* wss_family_name,
        const wchar_t* typographic_family_name, DWRITE_FONT_WEIGHT weight, DWRITE_FONT_STRETCH stretch,
        DWRITE_FONT_STYLE style, const AxisValues& axis_values) const;

    std::optional<std::tuple<WeightStretchStyle, LOGFONT>> get_wss_and_logfont_for_axis_values(
        const wchar_t* typographic_family_name, const AxisValues& axis_values) const;

    std::vector<FontFamily> get_font_families() const;
    std::vector<std::wstring> get_emoji_font_families() const;

private:
    inline static std::weak_ptr<Context> s_ptr;

    wil::com_ptr<IDWriteFactory1> m_factory;
    wil::com_ptr<IDWriteGdiInterop> m_gdi_interop;
    wil::com_ptr<IDWriteTypography> m_default_typography;
};

std::wstring get_localised_string(const wil::com_ptr<IDWriteLocalizedStrings>& localised_strings);
float get_default_scaling_factor();

float dip_to_px(float dip, float scaling_factor = get_default_scaling_factor());
float px_to_dip(float px, float scaling_factor = get_default_scaling_factor());
float dip_to_pt(float dip);
float pt_to_dip(float point_size);

#if NTDDI_VERSION >= NTDDI_WIN10_RS3
std::vector<DWRITE_FONT_AXIS_VALUE> axis_values_to_vector(const AxisValues& values);
#endif

} // namespace uih::direct_write

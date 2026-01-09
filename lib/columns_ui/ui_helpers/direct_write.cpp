#include "stdafx.h"

#include "direct_write.h"

#include "direct_write_emoji.h"
#include "emoji.h"

using namespace std::string_view_literals;

namespace uih::direct_write {

namespace {

float stretch_to_width(DWRITE_FONT_STRETCH stretch)
{
    if (WI_EnumValue(stretch) < DWRITE_FONT_STRETCH_ULTRA_CONDENSED
        || WI_EnumValue(stretch) > DWRITE_FONT_STRETCH_ULTRA_EXPANDED)
        return 100.0f;

    if (stretch == DWRITE_FONT_STRETCH_ULTRA_EXPANDED)
        return 200.0f;

    if (stretch == DWRITE_FONT_STRETCH_EXTRA_EXPANDED)
        return 150.0f;

    return static_cast<float>(stretch - 1) * 12.5f + 50.0f;
}

DWRITE_FONT_STRETCH width_to_stretch(float width)
{
    if (width >= 200.0f)
        return DWRITE_FONT_STRETCH_ULTRA_EXPANDED;

    if (width >= 150.0f)
        return DWRITE_FONT_STRETCH_EXTRA_EXPANDED;

    if (width >= 125.0f)
        return DWRITE_FONT_STRETCH_EXPANDED;

    if (width >= 112.5f)
        return DWRITE_FONT_STRETCH_SEMI_EXPANDED;

    if (width >= 100.0f)
        return DWRITE_FONT_STRETCH_NORMAL;

    if (width >= 87.5f)
        return DWRITE_FONT_STRETCH_SEMI_CONDENSED;

    if (width >= 75.0f)
        return DWRITE_FONT_STRETCH_CONDENSED;

    if (width >= 62.5f)
        return DWRITE_FONT_STRETCH_EXTRA_CONDENSED;

    return DWRITE_FONT_STRETCH_ULTRA_CONDENSED;
}

wil::com_ptr<IDWriteFontCollection3> get_typographic_font_collection(const wil::com_ptr<IDWriteFactory1>& factory)
{
    const auto factory_7 = factory.try_query<IDWriteFactory7>();

    if (!factory_7)
        return {};

    wil::com_ptr<IDWriteFontCollection3> font_collection;
    THROW_IF_FAILED(factory_7->GetSystemFontCollection(FALSE, DWRITE_FONT_FAMILY_MODEL_TYPOGRAPHIC, &font_collection));

    wil::com_ptr<IDWriteFontSet1> font_set;
    THROW_IF_FAILED(font_collection->GetFontSet(&font_set));

    if (!font_set.try_query<IDWriteFontSet4>())
        return {};

    return font_collection;
}

wil::com_ptr<IDWriteFontCollection> get_wss_font_collection(const wil::com_ptr<IDWriteFactory1>& factory)
{
    wil::com_ptr<IDWriteFontCollection> font_collection;
    THROW_IF_FAILED(factory->GetSystemFontCollection(&font_collection));
    return font_collection;
}

wil::com_ptr<IDWriteFontCollection> get_auto_font_collection(const wil::com_ptr<IDWriteFactory1>& factory)
{
    if (const auto factory_7 = factory.try_query<IDWriteFactory7>()) {
        wil::com_ptr<IDWriteFontCollection2> font_collection_2;
        THROW_IF_FAILED(
            factory_7->GetSystemFontCollection(FALSE, DWRITE_FONT_FAMILY_MODEL_TYPOGRAPHIC, &font_collection_2));
        return font_collection_2;
    }

    wil::com_ptr<IDWriteFontCollection> font_collection;
    THROW_IF_FAILED(factory->GetSystemFontCollection(&font_collection));
    return font_collection;
}

wil::com_ptr<IDWriteFontSet4> get_font_set_4(const wil::com_ptr<IDWriteFactory1>& factory)
{
    const auto factory_7 = factory.query<IDWriteFactory7>();

    wil::com_ptr<IDWriteFontSet2> font_set;
    THROW_IF_FAILED(factory_7->GetSystemFontSet(FALSE, &font_set));

    return font_set.query<IDWriteFontSet4>();
}

constexpr COLORREF direct_write_colour_to_colorref(DWRITE_COLOR_F colour)
{
    constexpr auto transform_channel
        = [](auto value) { return gsl::narrow_cast<uint8_t>(std::clamp(std::round(value * 255.0f), 0.0f, 255.0f)); };

    return RGB(transform_channel(colour.r), transform_channel(colour.g), transform_channel(colour.b));
}

class __declspec(uuid("E50EB289-73D3-481C-B726-09B88105539A")) ColourEffect : public IUnknown {
public:
    explicit ColourEffect(COLORREF colour) : m_colour(colour) {}

    COLORREF GetColour() const { return m_colour; }

    HRESULT STDMETHODCALLTYPE QueryInterface(const IID& riid, void** ppvObject) noexcept override
    {
        if (__uuidof(ColourEffect) == riid) {
            AddRef();
            *ppvObject = this;
        } else if (__uuidof(IUnknown) == riid) {
            AddRef();
            *ppvObject = static_cast<IUnknown*>(this);
        } else {
            *ppvObject = nullptr;
            return E_FAIL;
        }

        return S_OK;
    }

    ULONG STDMETHODCALLTYPE AddRef() noexcept override { return ++m_ref_count; }

    ULONG STDMETHODCALLTYPE Release() noexcept override
    {
        if (--m_ref_count == 0) {
            delete this;
            return 0;
        }

        return m_ref_count;
    }

private:
    std::atomic<ULONG> m_ref_count{};
    COLORREF m_colour{};
};

class GdiTextRenderer : public IDWriteTextRenderer {
public:
    GdiTextRenderer(wil::com_ptr<IDWriteFactory1> factory, IDWriteBitmapRenderTarget* bitmapRenderTarget,
        IDWriteRenderingParams* renderingParams, COLORREF default_colour, bool use_colour_glyphs);

    HRESULT STDMETHODCALLTYPE QueryInterface(const IID& riid, void** ppvObject) noexcept override
    {
        if (__uuidof(IDWriteTextRenderer) == riid) {
            AddRef();
            *ppvObject = this;
        } else if (__uuidof(IDWritePixelSnapping) == riid) {
            AddRef();
            *ppvObject = static_cast<IDWritePixelSnapping*>(this);
        } else if (__uuidof(IUnknown) == riid) {
            AddRef();
            *ppvObject = static_cast<IUnknown*>(this);
        } else {
            *ppvObject = nullptr;
            return E_FAIL;
        }

        return S_OK;
    }

    ULONG STDMETHODCALLTYPE AddRef() noexcept override { return ++m_ref_count; }

    ULONG STDMETHODCALLTYPE Release() noexcept override
    {
        if (--m_ref_count == 0) {
            delete this;
            return 0;
        }

        return m_ref_count;
    }

    HRESULT STDMETHODCALLTYPE IsPixelSnappingDisabled(void* clientDrawingContext, BOOL* isDisabled) noexcept override
    {
        *isDisabled = FALSE;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetCurrentTransform(
        void* clientDrawingContext, DWRITE_MATRIX* transform) noexcept override
    {
        return m_render_target->GetCurrentTransform(transform);
    }

    HRESULT STDMETHODCALLTYPE GetPixelsPerDip(void* clientDrawingContext, FLOAT* pixelsPerDip) noexcept override
    {
        *pixelsPerDip = m_render_target->GetPixelsPerDip();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE DrawGlyphRun(void* clientDrawingContext, FLOAT baselineOriginX, FLOAT baselineOriginY,
        DWRITE_MEASURING_MODE measuringMode, const DWRITE_GLYPH_RUN* glyphRun,
        const DWRITE_GLYPH_RUN_DESCRIPTION* glyphRunDescription, IUnknown* clientDrawingEffect) noexcept override
    {
        wil::com_ptr<ColourEffect> colour_effect;
        if (clientDrawingEffect) {
            colour_effect = wil::try_com_query<ColourEffect>(clientDrawingEffect);
        }

        const COLORREF colour = colour_effect ? colour_effect->GetColour() : m_default_colour;

        wil::com_ptr<IDWriteColorGlyphRunEnumerator> colour_glyph_run_enumerator;

        if (m_use_colour_glyphs) {
            try {
                colour_glyph_run_enumerator = GetColorGlyphEnumerator(clientDrawingContext, baselineOriginX,
                    baselineOriginY, measuringMode, glyphRun, glyphRunDescription);
            }
            CATCH_RETURN()
        }

        if (!colour_glyph_run_enumerator) {
            return m_render_target->DrawGlyphRun(
                baselineOriginX, baselineOriginY, measuringMode, glyphRun, m_rendering_params.get(), colour);
        }

        while (true) {
            BOOL has_run{};

            RETURN_IF_FAILED(colour_glyph_run_enumerator->MoveNext(&has_run));

            if (!has_run)
                return S_OK;

            const DWRITE_COLOR_GLYPH_RUN* colour_glyph_run{};
            RETURN_IF_FAILED(colour_glyph_run_enumerator->GetCurrentRun(&colour_glyph_run));

            const auto colour_run_colour = colour_glyph_run->paletteIndex == 0xffff
                ? colour
                : direct_write_colour_to_colorref(colour_glyph_run->runColor);

            RETURN_IF_FAILED(
                m_render_target->DrawGlyphRun(colour_glyph_run->baselineOriginX, colour_glyph_run->baselineOriginY,
                    measuringMode, &colour_glyph_run->glyphRun, m_rendering_params.get(), colour_run_colour));
        }
    }

    HRESULT STDMETHODCALLTYPE DrawUnderline(void* clientDrawingContext, FLOAT baselineOriginX, FLOAT baselineOriginY,
        const DWRITE_UNDERLINE* underline, IUnknown* clientDrawingEffect) noexcept override
    {
        const auto x_start = baselineOriginX;
        const auto x_end = x_start
            + underline->width * (underline->readingDirection == DWRITE_READING_DIRECTION_RIGHT_TO_LEFT ? -1 : 1);
        const auto y_start = baselineOriginY + underline->offset;
        const auto y_end = y_start + underline->thickness;

        wil::com_ptr<ColourEffect> colour_effect;
        if (clientDrawingEffect) {
            colour_effect = wil::try_com_query<ColourEffect>(clientDrawingEffect);
        }

        const COLORREF colour = colour_effect ? colour_effect->GetColour() : m_default_colour;

        const auto dc = m_render_target->GetMemoryDC();
        const auto scaling_factor = m_render_target->GetPixelsPerDip();

        const auto scale_value
            = [scaling_factor](float value) { return gsl::narrow_cast<int>(value * scaling_factor + 0.5); };

        RECT underline_rect = {scale_value(x_start), scale_value(y_start), scale_value(x_end), scale_value(y_end)};
        wil::unique_hbrush brush(CreateSolidBrush(colour));
        FillRect(dc, &underline_rect, brush.get());

        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE DrawStrikethrough(void* clientDrawingContext, FLOAT baselineOriginX,
        FLOAT baselineOriginY, const DWRITE_STRIKETHROUGH* strikethrough,
        IUnknown* clientDrawingEffect) noexcept override
    {
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE DrawInlineObject(void* clientDrawingContext, FLOAT originX, FLOAT originY,
        IDWriteInlineObject* inlineObject, BOOL isSideways, BOOL isRightToLeft,
        IUnknown* clientDrawingEffect) noexcept override
    {
        wil::com_ptr<ColourEffect> colour_effect;
        std::optional<COLORREF> previous_default_colour;

        if (clientDrawingEffect) {
            colour_effect = wil::try_com_query<ColourEffect>(clientDrawingEffect);
        }

        if (colour_effect) {
            previous_default_colour = m_default_colour;
            m_default_colour = colour_effect->GetColour();
        }

        auto _ = gsl::finally([&] {
            if (previous_default_colour)
                m_default_colour = *previous_default_colour;
        });

        return inlineObject->Draw(
            clientDrawingContext, this, originX, originY, isSideways, isRightToLeft, clientDrawingEffect);
    }

private:
    wil::com_ptr<IDWriteColorGlyphRunEnumerator> GetColorGlyphEnumerator(void* clientDrawingContext,
        FLOAT baselineOriginX, FLOAT baselineOriginY, DWRITE_MEASURING_MODE measuringMode,
        const DWRITE_GLYPH_RUN* glyphRun, const DWRITE_GLYPH_RUN_DESCRIPTION* glyphRunDescription)
    {
        auto factory = m_factory.try_query<IDWriteFactory2>();

        if (!factory)
            return {};

        DWRITE_MATRIX transform{};
        THROW_IF_FAILED(GetCurrentTransform(clientDrawingContext, &transform));

        wil::com_ptr<IDWriteColorGlyphRunEnumerator> enumerator;

        const auto hr = factory->TranslateColorGlyphRun(
            baselineOriginX, baselineOriginY, glyphRun, glyphRunDescription, measuringMode, &transform, 0, &enumerator);

        if (hr == DWRITE_E_NOCOLOR)
            return {};

        THROW_IF_FAILED(hr);

        return enumerator;
    }

    std::atomic<ULONG> m_ref_count{};
    wil::com_ptr<IDWriteFactory1> m_factory;
    wil::com_ptr<IDWriteBitmapRenderTarget> m_render_target;
    wil::com_ptr<IDWriteRenderingParams> m_rendering_params;
    COLORREF m_default_colour{};
    bool m_use_colour_glyphs{true};
};

GdiTextRenderer::GdiTextRenderer(wil::com_ptr<IDWriteFactory1> factory, IDWriteBitmapRenderTarget* bitmapRenderTarget,
    IDWriteRenderingParams* renderingParams, COLORREF default_colour, bool use_colour_glyphs)
    : m_factory(std::move(factory))
    , m_render_target(bitmapRenderTarget)
    , m_rendering_params(renderingParams)
    , m_default_colour(default_colour)
    , m_use_colour_glyphs(use_colour_glyphs)
{
}

} // namespace

wil::com_ptr<IDWriteRenderingParams> RenderingParams::get(HWND wnd) const
{
    const auto root_window = GetAncestor(wnd, GA_ROOT);
    const auto monitor = MonitorFromWindow(root_window, MONITOR_DEFAULTTONEAREST);

    if (m_rendering_params && m_monitor == monitor)
        return m_rendering_params;

    wil::com_ptr<IDWriteRenderingParams> default_rendering_params;
    THROW_IF_FAILED(m_factory->CreateMonitorRenderingParams(monitor, &default_rendering_params));

    if (m_rendering_mode == DWRITE_RENDERING_MODE_DEFAULT) {
        m_monitor = monitor;
        m_rendering_params = std::move(default_rendering_params);
        return m_rendering_params;
    }

    const auto default_rendering_params_1 = default_rendering_params.try_query<IDWriteRenderingParams1>();

    const auto factory_2 = m_factory.try_query<IDWriteFactory2>();
    const auto default_rendering_params_2 = default_rendering_params.try_query<IDWriteRenderingParams2>();

    wil::com_ptr<IDWriteRenderingParams> custom_rendering_params;

    const auto gamma = default_rendering_params->GetGamma();
    const auto enhanced_contrast = default_rendering_params->GetEnhancedContrast();
    const auto cleartype_level = default_rendering_params->GetClearTypeLevel();
    const auto pixel_geometry = default_rendering_params->GetPixelGeometry();
    const auto greyscale_enhanced_contrast = default_rendering_params_1
        ? std::make_optional(default_rendering_params_1->GetGrayscaleEnhancedContrast())
        : std::nullopt;
    const auto grid_fit_mode
        = default_rendering_params_2 ? std::make_optional(default_rendering_params_2->GetGridFitMode()) : std::nullopt;

    if (factory_2 && grid_fit_mode) {
        wil::com_ptr<IDWriteRenderingParams2> custom_rendering_params_2;
        THROW_IF_FAILED(factory_2->CreateCustomRenderingParams(gamma, enhanced_contrast, *greyscale_enhanced_contrast,
            cleartype_level, pixel_geometry, m_rendering_mode, *grid_fit_mode, &custom_rendering_params_2));

        custom_rendering_params = std::move(custom_rendering_params_2);
    } else if (greyscale_enhanced_contrast) {
        wil::com_ptr<IDWriteRenderingParams1> custom_rendering_params_1;
        THROW_IF_FAILED(m_factory->CreateCustomRenderingParams(gamma, enhanced_contrast, *greyscale_enhanced_contrast,
            cleartype_level, pixel_geometry, m_rendering_mode, &custom_rendering_params_1));

        custom_rendering_params = std::move(custom_rendering_params_1);
    } else {
        THROW_IF_FAILED(m_factory->CreateCustomRenderingParams(
            gamma, enhanced_contrast, cleartype_level, pixel_geometry, m_rendering_mode, &custom_rendering_params));
    }

    m_monitor = monitor;
    m_rendering_params = custom_rendering_params;

    return custom_rendering_params;
}

float TextLayout::get_max_height() const noexcept
{
    return m_text_layout->GetMaxHeight();
}

float TextLayout::get_max_width() const noexcept
{
    return m_text_layout->GetMaxWidth();
}

DWRITE_TEXT_METRICS TextLayout::get_metrics() const
{
    DWRITE_TEXT_METRICS metrics{};
    THROW_IF_FAILED(m_text_layout->GetMetrics(&metrics));
    return metrics;
}

DWRITE_OVERHANG_METRICS TextLayout::get_overhang_metrics() const
{
    DWRITE_OVERHANG_METRICS overhang_metrics{};
    THROW_IF_FAILED(m_text_layout->GetOverhangMetrics(&overhang_metrics));
    return overhang_metrics;
}

void TextLayout::set_colour(COLORREF colour, DWRITE_TEXT_RANGE text_range) const
{
    const auto colour_effect = wil::com_ptr<ColourEffect>(new ColourEffect(colour));
    THROW_IF_FAILED(m_text_layout->SetDrawingEffect(colour_effect.get(), text_range));
}

void TextLayout::set_effect(IUnknown* effect, DWRITE_TEXT_RANGE text_range) const
{
    THROW_IF_FAILED(m_text_layout->SetDrawingEffect(effect, text_range));
}

void TextLayout::set_max_height(float value) const
{
    THROW_IF_FAILED(m_text_layout->SetMaxHeight(value));
}

void TextLayout::set_max_width(float value) const
{
    THROW_IF_FAILED(m_text_layout->SetMaxWidth(value));
}

void TextLayout::set_underline(bool is_underlined, DWRITE_TEXT_RANGE text_range) const
{
    THROW_IF_FAILED(m_text_layout->SetUnderline(is_underlined, text_range));
}

void TextLayout::set_family(const wchar_t* family_name, DWRITE_TEXT_RANGE text_range) const
{
    THROW_IF_FAILED(m_text_layout->SetFontFamilyName(family_name, text_range));
}

void TextLayout::set_size(float size, DWRITE_TEXT_RANGE text_range) const
{
    THROW_IF_FAILED(m_text_layout->SetFontSize(size, text_range));
}

void TextLayout::set_wss(DWRITE_FONT_WEIGHT weight, std::variant<DWRITE_FONT_STRETCH, float> width_or_stretch,
    DWRITE_FONT_STYLE style, DWRITE_TEXT_RANGE text_range) const
{
    if (m_text_layout_4) {
        const auto width = std::holds_alternative<float>(width_or_stretch)
            ? std::get<float>(width_or_stretch)
            : stretch_to_width(std::get<DWRITE_FONT_STRETCH>(width_or_stretch));

        std::array axis_values{
            DWRITE_FONT_AXIS_VALUE{DWRITE_FONT_AXIS_TAG_WEIGHT, static_cast<float>(weight)},
            DWRITE_FONT_AXIS_VALUE{DWRITE_FONT_AXIS_TAG_WIDTH, width},
            DWRITE_FONT_AXIS_VALUE{DWRITE_FONT_AXIS_TAG_ITALIC, style == DWRITE_FONT_STYLE_ITALIC ? 1.0f : 0.0f},
            DWRITE_FONT_AXIS_VALUE{DWRITE_FONT_AXIS_TAG_SLANT, style == DWRITE_FONT_STYLE_OBLIQUE ? 1.0f : 0.0f},
        };

        THROW_IF_FAILED(m_text_layout_4->SetFontAxisValues(
            axis_values.data(), gsl::narrow<UINT32>(axis_values.size()), text_range));
        return;
    }

    const auto stretch = std::holds_alternative<DWRITE_FONT_STRETCH>(width_or_stretch)
        ? std::get<DWRITE_FONT_STRETCH>(width_or_stretch)
        : width_to_stretch(std::get<float>(width_or_stretch));

    THROW_IF_FAILED(m_text_layout->SetFontWeight(weight, text_range));
    THROW_IF_FAILED(m_text_layout->SetFontStretch(stretch, text_range));
    THROW_IF_FAILED(m_text_layout->SetFontStyle(style, text_range));
}

void TextLayout::render_with_transparent_background(HWND wnd, HDC dc, RECT output_rect, COLORREF default_colour,
    float x_origin_offset, wil::com_ptr<IDWriteBitmapRenderTarget> bitmap_render_target) const
{
    const auto metrics = get_metrics();

    if (metrics.width <= 0.0f)
        return;

    const auto scaling_factor = get_default_scaling_factor();
    const auto overhang_metrics = get_overhang_metrics();
    const auto layout_width = m_text_layout->GetMaxWidth();
    const auto layout_height = m_text_layout->GetMaxHeight();
    const auto rect_width = wil::rect_width(output_rect);
    const auto rect_height = wil::rect_height(output_rect);

    const auto draw_left_px
        = std::max(0l, gsl::narrow_cast<long>((x_origin_offset - overhang_metrics.left) * scaling_factor) - 1);
    const auto draw_right_px = std::min(rect_width,
        gsl::narrow_cast<long>((x_origin_offset + layout_width + overhang_metrics.right) * scaling_factor + 1.0f) + 1);

    const auto draw_top_px = std::max(0l, gsl::narrow_cast<long>(-overhang_metrics.top * scaling_factor) - 1);
    const auto draw_bottom_px = std::min(
        rect_height, gsl::narrow_cast<long>((layout_height + overhang_metrics.bottom) * scaling_factor + 1.0f) + 1);

    const auto bitmap_width = std::min(rect_width, draw_right_px - draw_left_px);
    const auto bitmap_height = std::min(rect_height, draw_bottom_px - draw_top_px);

    if (bitmap_width <= 0 || bitmap_height <= 0)
        return;

    const auto is_shrunk_width = bitmap_width < rect_width;
    const auto is_shrunk_height = bitmap_height < rect_height;

    const auto source_x = output_rect.left + (is_shrunk_width ? draw_left_px : 0l);
    const auto source_y = output_rect.top + (is_shrunk_height ? draw_top_px : 0l);

    const auto draw_left_dip = is_shrunk_width ? gsl::narrow_cast<float>(draw_left_px) / scaling_factor : 0.0f;
    const auto draw_top_dip = is_shrunk_height ? gsl::narrow_cast<float>(draw_top_px) / scaling_factor : 0.0f;

    if (!bitmap_render_target) {
        THROW_IF_FAILED(
            m_gdi_interop->CreateBitmapRenderTarget(dc, bitmap_width, bitmap_height, &bitmap_render_target));
        THROW_IF_FAILED(bitmap_render_target->SetPixelsPerDip(scaling_factor));
    } else {
        SIZE sz{};
        THROW_IF_FAILED(bitmap_render_target->GetSize(&sz));

        if (bitmap_width > sz.cx || bitmap_height > sz.cy)
            THROW_IF_FAILED(bitmap_render_target->Resize(bitmap_width, bitmap_height));
    }

    const auto is_greyscale_antialiasing = m_rendering_params->use_greyscale_antialiasing();
    const auto bitmap_render_target_1 = bitmap_render_target.query<IDWriteBitmapRenderTarget1>();
    THROW_IF_FAILED(bitmap_render_target_1->SetTextAntialiasMode(
        is_greyscale_antialiasing ? DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE : DWRITE_TEXT_ANTIALIAS_MODE_CLEARTYPE));

    const auto rendering_params = m_rendering_params->get(wnd);
    const auto use_colour_glyphs = m_rendering_params->use_colour_glyphs();
    const auto memory_dc = bitmap_render_target->GetMemoryDC();

    wil::com_ptr<IDWriteTextRenderer> renderer = new GdiTextRenderer(
        m_factory, bitmap_render_target.get(), rendering_params.get(), default_colour, use_colour_glyphs);

    BitBlt(memory_dc, 0, 0, bitmap_width, bitmap_height, dc, source_x, source_y, SRCCOPY);
    THROW_IF_FAILED(m_text_layout->Draw(NULL, renderer.get(), -draw_left_dip + x_origin_offset, -draw_top_dip));

    BitBlt(dc, source_x, source_y, bitmap_width, bitmap_height, memory_dc, 0, 0, SRCCOPY);
}

void TextFormat::set_text_alignment(DWRITE_TEXT_ALIGNMENT value) const
{
    THROW_IF_FAILED(m_text_format->SetTextAlignment(value));
}

void TextFormat::set_paragraph_alignment(DWRITE_PARAGRAPH_ALIGNMENT value) const
{
    THROW_IF_FAILED(m_text_format->SetParagraphAlignment(value));
}

void TextFormat::set_word_wrapping(DWRITE_WORD_WRAPPING value) const
{
    THROW_IF_FAILED(m_text_format->SetWordWrapping(value));
}

int TextFormat::get_minimum_height(std::wstring_view text) const
{
    try {
        constexpr auto max_size = 65536.0f;
        const auto text_layout = create_text_layout(text, max_size, max_size);
        const auto metrics = text_layout.get_metrics();
        return gsl::narrow_cast<int>(metrics.height * get_default_scaling_factor() + 1);
    }
    CATCH_LOG()

    return 1;
}

TextPosition TextFormat::measure_text_position(
    std::wstring_view text, int height, float max_width, bool enable_ellipsis) const
{
    try {
        const auto scaling_factor = get_default_scaling_factor();
        const auto text_layout
            = create_text_layout(text, max_width, static_cast<float>(height) / scaling_factor, enable_ellipsis);
        const auto metrics = text_layout.get_metrics();
        const auto left = gsl::narrow_cast<int>(metrics.left * scaling_factor);
        const auto left_remainder_dip = metrics.left - gsl::narrow_cast<float>(left) / scaling_factor;

        return {left, left_remainder_dip, gsl::narrow_cast<int>((metrics.top) * scaling_factor),
            gsl::narrow_cast<int>(metrics.widthIncludingTrailingWhitespace * scaling_factor + 1),
            gsl::narrow_cast<int>(metrics.height * scaling_factor + 1)};
    }
    CATCH_LOG()

    return {};
}

int TextFormat::measure_text_width(std::wstring_view text) const
{
    try {
        const auto text_layout = create_text_layout(text, 65536.0f, 65536.0f);
        const auto metrics = text_layout.get_metrics();
        return gsl::narrow_cast<int>(metrics.widthIncludingTrailingWhitespace * get_default_scaling_factor() + 1);
    }
    CATCH_LOG()

    return 0;
}

TextLayout TextFormat::create_text_layout(
    std::wstring_view text, float max_width, float max_height, bool enable_ellipsis) const
{
    const auto text_length = gsl::narrow<uint32_t>(text.length());

    const auto rendering_mode = m_rendering_params->rendering_mode();

    wil::com_ptr<IDWriteTextLayout> text_layout;
    if (rendering_mode == DWRITE_RENDERING_MODE_ALIASED || rendering_mode == DWRITE_RENDERING_MODE_GDI_CLASSIC
        || rendering_mode == DWRITE_RENDERING_MODE_GDI_NATURAL)
        THROW_IF_FAILED(m_factory->CreateGdiCompatibleTextLayout(text.data(), text_length, m_text_format.get(),
            max_width, max_height, get_default_scaling_factor(), nullptr,
            rendering_mode == DWRITE_RENDERING_MODE_GDI_NATURAL, &text_layout));
    else
        THROW_IF_FAILED(m_factory->CreateTextLayout(
            text.data(), text_length, m_text_format.get(), max_width, max_height, &text_layout));

    const auto typography = m_context->get_default_typography();
    THROW_IF_FAILED(text_layout->SetTypography(typography.get(), {0, text_length}));

    if (enable_ellipsis) {
        if (!m_trimming_sign)
            THROW_IF_FAILED(m_factory->CreateEllipsisTrimmingSign(m_text_format.get(), &m_trimming_sign));

        DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
        THROW_IF_FAILED(text_layout->SetTrimming(&trimming, m_trimming_sign.get()));
    }

    return {m_factory, m_gdi_interop, text_layout, m_rendering_params};
}

DWRITE_FONT_WEIGHT TextFormat::get_weight() const
{
    return m_text_format->GetFontWeight();
}

std::variant<DWRITE_FONT_STRETCH, float> TextFormat::get_stretch() const
{
    if (const auto text_format_3 = m_text_format.try_query<IDWriteTextFormat3>(); text_format_3) {
        const auto axis_count = text_format_3->GetFontAxisValueCount();
        std::vector<DWRITE_FONT_AXIS_VALUE> axis_values{axis_count};
        try {
            THROW_IF_FAILED(text_format_3->GetFontAxisValues(axis_values.data(), axis_count));
        } catch (...) {
            LOG_CAUGHT_EXCEPTION();
            return m_text_format->GetFontStretch();
        }

        if (const auto iter
            = ranges::find_if(axis_values, [](auto& value) { return value.axisTag == DWRITE_FONT_AXIS_TAG_WIDTH; });
            iter != axis_values.end()) {
            return iter->value;
        }
    }

    return m_text_format->GetFontStretch();
}

DWRITE_FONT_STYLE TextFormat::get_style() const
{
    return m_text_format->GetFontStyle();
}

Context::Context()
{
    THROW_IF_FAILED(
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory1), m_factory.put_unknown()));
    THROW_IF_FAILED(m_factory->GetGdiInterop(&m_gdi_interop));
}

wil::com_ptr<IDWriteBitmapRenderTarget> Context::create_bitmap_render_target(
    HDC dc, uint32_t width, uint32_t height) const
{
    wil::com_ptr<IDWriteBitmapRenderTarget> bitmap_render_target;
    THROW_IF_FAILED(m_gdi_interop->CreateBitmapRenderTarget(dc, width, height, &bitmap_render_target));
    THROW_IF_FAILED(bitmap_render_target->SetPixelsPerDip(get_default_scaling_factor()));
    return bitmap_render_target;
}

LOGFONT Context::create_log_font(const wil::com_ptr<IDWriteFont>& font) const
{
    LOGFONT log_font{};
    BOOL is_system_font{};
    THROW_IF_FAILED(m_gdi_interop->ConvertFontToLOGFONT(font.get(), &log_font, &is_system_font));
    return log_font;
}

LOGFONT Context::create_log_font(const wil::com_ptr<IDWriteFontFace>& font_face) const
{
    LOGFONT log_font{};
    THROW_IF_FAILED(m_gdi_interop->ConvertFontFaceToLOGFONT(font_face.get(), &log_font));
    return log_font;
}

wil::com_ptr<IDWriteFont> Context::create_font(const LOGFONT& log_font) const
{
    wil::com_ptr<IDWriteFont> font;
    THROW_IF_FAILED(m_gdi_interop->CreateFontFromLOGFONT(&log_font, &font));
    return font;
}

TextFormat Context::create_text_format(const wil::com_ptr<IDWriteFont>& font, float font_size)
{
    wil::com_ptr<IDWriteFontFamily> font_family;
    THROW_IF_FAILED(font->GetFontFamily(&font_family));

    return create_text_format(font_family, font->GetWeight(), font->GetStretch(), font->GetStyle(), font_size);
}

TextFormat Context::create_text_format(const wil::com_ptr<IDWriteFontFamily>& font_family, DWRITE_FONT_WEIGHT weight,
    DWRITE_FONT_STRETCH stretch, DWRITE_FONT_STYLE style, float font_size, const AxisValues& axis_values)
{
    wil::com_ptr<IDWriteLocalizedStrings> family_names;
    THROW_IF_FAILED(font_family->GetFamilyNames(&family_names));

    uint32_t length{};
    THROW_IF_FAILED(family_names->GetStringLength(0, &length));

    std::vector<wchar_t> family_name(length + 1, 0);

    THROW_IF_FAILED(family_names->GetString(0, family_name.data(), length + 1));

    return create_text_format(family_name.data(), weight, stretch, style, font_size, axis_values);
}

TextFormat Context::create_text_format(const wchar_t* family_name, DWRITE_FONT_WEIGHT weight,
    DWRITE_FONT_STRETCH stretch, DWRITE_FONT_STYLE style, float font_size, const AxisValues& axis_values)
{
    if (const auto factory_7 = m_factory.try_query<IDWriteFactory7>(); factory_7 && !axis_values.empty()) {
        const auto axis_values_vector = axis_values_to_vector(axis_values);

        wil::com_ptr<IDWriteTextFormat3> text_format_3;
        THROW_IF_FAILED(factory_7->CreateTextFormat(family_name, nullptr, axis_values_vector.data(),
            gsl::narrow<uint32_t>(axis_values_vector.size()), font_size, L"", &text_format_3));

        return wrap_text_format(text_format_3);
    }

    wil::com_ptr<IDWriteTextFormat> text_format;
    THROW_IF_FAILED(
        m_factory->CreateTextFormat(family_name, NULL, weight, style, stretch, font_size, L"", &text_format));

    return wrap_text_format(text_format);
}

TextFormat Context::create_text_format(const LOGFONT& log_font, float font_size)
{
    const auto font = create_font(log_font);
    return create_text_format(font, font_size > 0 ? font_size : px_to_dip(20.0f));
}

std::optional<TextFormat> Context::create_text_format_with_fallback(
    const LOGFONT& log_font, std::optional<float> font_size) noexcept
{
    const auto resolved_size = font_size.value_or(-px_to_dip(gsl::narrow_cast<float>(log_font.lfHeight)));

    try {
        return create_text_format(log_font, resolved_size);
    }
    CATCH_LOG()

    try {
        return create_text_format(L"", static_cast<DWRITE_FONT_WEIGHT>(std::clamp(log_font.lfWeight, 1l, 999l)),
            DWRITE_FONT_STRETCH_NORMAL, log_font.lfItalic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL,
            resolved_size);
    }
    CATCH_LOG()

    return {};
}

TextFormat Context::wrap_text_format(wil::com_ptr<IDWriteTextFormat> text_format, DWRITE_RENDERING_MODE rendering_mode,
    bool use_greyscale_antialiasing, bool use_colour_glyphs, bool set_defaults)
{
    if (set_defaults) {
        THROW_IF_FAILED(text_format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP));
        THROW_IF_FAILED(text_format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER));
    }

    if (auto text_format_2 = text_format.try_query<IDWriteTextFormat2>(); text_format_2) {
        DWRITE_LINE_SPACING spacing{DWRITE_LINE_SPACING_METHOD_DEFAULT, 0, 0, 0, DWRITE_FONT_LINE_GAP_USAGE_DISABLED};
        THROW_IF_FAILED(text_format_2->SetLineSpacing(&spacing));
    }

    const auto rendering_params
        = std::make_shared<RenderingParams>(m_factory, rendering_mode, use_greyscale_antialiasing, use_colour_glyphs);

    return {shared_from_this(), m_factory, m_gdi_interop, std::move(text_format), rendering_params};
}

wil::com_ptr<IDWriteFontFallback> Context::create_emoji_font_fallback(
    const EmojiFontSelectionConfig& emoji_font_selection_config) const
{
    const auto factory_2 = m_factory.query<IDWriteFactory2>();

    wil::com_ptr<IDWriteFontFallback> system_font_fallback;
    THROW_IF_FAILED(factory_2->GetSystemFontFallback(&system_font_fallback));

    const auto font_collection = get_auto_font_collection(m_factory);
    return emoji_font_fallback::create_emoji_font_fallback(font_collection, std::move(system_font_fallback),
        emoji_font_selection_config.colour_emoji_family_name.c_str(),
        emoji_font_selection_config.monochrome_emoji_family_name.c_str());
}

wil::com_ptr<IDWriteTypography> Context::get_default_typography()
{
    if (!m_default_typography) {
        THROW_IF_FAILED(m_factory->CreateTypography(&m_default_typography));

        THROW_IF_FAILED(m_default_typography->AddFontFeature({DWRITE_FONT_FEATURE_TAG_TABULAR_FIGURES, 1}));
    }

    return m_default_typography;
}

std::optional<ResolvedFontNames> Context::resolve_font_names(const wchar_t* wss_family_name,
    const wchar_t* typographic_family_name, DWRITE_FONT_WEIGHT weight, DWRITE_FONT_STRETCH stretch,
    DWRITE_FONT_STYLE style, const AxisValues& axis_values) const
{
    const auto family_name = wcsnlen(typographic_family_name, 1) > 0 ? typographic_family_name : wss_family_name;

    try {
        const auto typographic_font_collection = get_typographic_font_collection(m_factory);
        const auto wss_font_collection = get_wss_font_collection(m_factory);
        wil::com_ptr<IDWriteFont> font;
        wil::com_ptr<IDWriteFontFamily> font_family;

        if (typographic_font_collection) {
            auto axis_values_vector = axis_values_to_vector(axis_values);

            if (axis_values_vector.empty()) {
                axis_values_vector.resize(DWRITE_STANDARD_FONT_AXIS_COUNT);

                const auto system_font_set = get_font_set_4(m_factory);
                const auto written_axis_count = system_font_set->ConvertWeightStretchStyleToFontAxisValues(
                    nullptr, 0, weight, stretch, style, 0, axis_values_vector.data());

                axis_values_vector.resize(written_axis_count);
            }

            wil::com_ptr<IDWriteFontList2> font_list;

            THROW_IF_FAILED(typographic_font_collection->GetMatchingFonts(
                family_name, axis_values_vector.data(), gsl::narrow<uint32_t>(axis_values_vector.size()), &font_list));

            if (font_list->GetFontCount() == 0)
                return {};

            THROW_IF_FAILED(font_list->GetFont(0, &font));

            THROW_IF_FAILED(font->GetFontFamily(&font_family));
        } else {
            BOOL exists{};
            uint32_t index{};
            THROW_IF_FAILED(wss_font_collection->FindFamilyName(wss_family_name, &index, &exists));

            if (!exists)
                return {};

            THROW_IF_FAILED(wss_font_collection->GetFontFamily(index, &font_family));

            THROW_IF_FAILED(font_family->GetFirstMatchingFont(weight, stretch, style, &font));
        }

        wil::com_ptr<IDWriteLocalizedStrings> face_names;
        THROW_IF_FAILED(font->GetFaceNames(&face_names));

        wil::com_ptr<IDWriteLocalizedStrings> family_names;
        THROW_IF_FAILED(font_family->GetFamilyNames(&family_names));

        return ResolvedFontNames{get_localised_string(family_names), get_localised_string(face_names)};
    }
    CATCH_LOG()

    return {};
}

std::optional<std::tuple<WeightStretchStyle, LOGFONT>> Context::get_wss_and_logfont_for_axis_values(
    const wchar_t* typographic_family_name, const AxisValues& axis_values) const
{
    try {
        const auto system_font_set = get_font_set_4(m_factory);

        if (!system_font_set)
            return {};

        const auto axis_values_vector = axis_values_to_vector(axis_values);

        const auto find_matching_font_face
            = [&system_font_set, &axis_values_vector](const auto* font_family) -> wil::com_ptr<IDWriteFontFace6> {
            wil::com_ptr<IDWriteFontSet4> matching_fonts;
            THROW_IF_FAILED(system_font_set->GetMatchingFonts(font_family, axis_values_vector.data(),
                gsl::narrow<uint32_t>(axis_values_vector.size()),
                DWRITE_FONT_SIMULATIONS_BOLD | DWRITE_FONT_SIMULATIONS_OBLIQUE, &matching_fonts));

            if (matching_fonts->GetFontCount() == 0)
                return {};

            wil::com_ptr<IDWriteFontFace5> font_face_5;
            THROW_IF_FAILED(matching_fonts->CreateFontFace(0, &font_face_5));

            return font_face_5.query<IDWriteFontFace6>();
        };

        auto font_face_6 = find_matching_font_face(typographic_family_name);

        if (!font_face_6)
            return {};

        wil::com_ptr<IDWriteLocalizedStrings> family_names;
        THROW_IF_FAILED(font_face_6->GetFamilyNames(DWRITE_FONT_FAMILY_MODEL_WEIGHT_STRETCH_STYLE, &family_names));

        auto family_name = get_localised_string(family_names);

        // Check if this family name actually exists (yes, sometimes it returns invalid values)
        font_face_6 = find_matching_font_face(family_name.c_str());

        if (!font_face_6) {
            font_face_6 = find_matching_font_face(L"Segoe UI");

            if (!font_face_6)
                return {};

            THROW_IF_FAILED(font_face_6->GetFamilyNames(DWRITE_FONT_FAMILY_MODEL_WEIGHT_STRETCH_STYLE, &family_names));

            family_name = get_localised_string(family_names);
        }

        const auto wss = WeightStretchStyle{
            family_name, font_face_6->GetWeight(), font_face_6->GetStretch(), font_face_6->GetStyle()};

        const auto log_font = create_log_font(font_face_6);

        return std::make_tuple(wss, log_font);
    }
    CATCH_LOG()

    return {};
}

std::vector<Font> FontFamily::fonts() const
{
    std::vector<Font> fonts;

    const auto count = family->GetFontCount();
    const auto family_1 = family.try_query<IDWriteFontFamily1>();

    for (auto index : std::ranges::views::iota(0u, count)) {
        wil::com_ptr<IDWriteFont> font;
        THROW_IF_FAILED(family->GetFont(index, &font));

        wil::com_ptr<IDWriteLocalizedStrings> localised_names;
        THROW_IF_FAILED(font->GetFaceNames(&localised_names));

        std::vector<DWRITE_FONT_AXIS_VALUE> axis_values;

        if (family_1) {
            wil::com_ptr<IDWriteFontFaceReference> font_face_reference;
            THROW_IF_FAILED(family_1->GetFontFaceReference(index, &font_face_reference));

            if (const auto font_face_reference_1 = font_face_reference.try_query<IDWriteFontFaceReference1>()) {
                const auto axis_count = font_face_reference_1->GetFontAxisValueCount();

                axis_values.resize(axis_count);
                THROW_IF_FAILED(font_face_reference_1->GetFontAxisValues(axis_values.data(), axis_count));
            }
        }

        AxisValues axis_values_map;

        for (auto [tag, value] : axis_values) {
            axis_values_map.insert_or_assign(WI_EnumValue(tag), value);
        }

        if (!axis_values.empty()) {
            const auto simulations = font->GetSimulations();

            if (simulations & DWRITE_FONT_SIMULATIONS_BOLD)
                axis_values_map[DWRITE_FONT_AXIS_TAG_WEIGHT] = 700.0f;

            if (simulations & DWRITE_FONT_SIMULATIONS_OBLIQUE)
                axis_values_map[DWRITE_FONT_AXIS_TAG_SLANT] = -20.0f;
        }

        const auto weight = font->GetWeight();
        const auto stretch = font->GetStretch();
        const auto style = font->GetStyle();

        fonts.emplace_back(
            std::move(font), get_localised_string(localised_names), weight, stretch, style, std::move(axis_values_map));
    }

    return fonts;
}

std::vector<FontFamily> Context::get_font_families() const
{
    const auto typographic_font_collection = get_typographic_font_collection(m_factory);

    const wil::com_ptr<IDWriteFontCollection> font_collection
        = typographic_font_collection ? typographic_font_collection : get_wss_font_collection(m_factory);

    std::vector<FontFamily> families;
    const auto family_count = font_collection->GetFontFamilyCount();

    for (const auto index : std::ranges::views::iota(0u, family_count)) {
        wil::com_ptr<IDWriteFontFamily> family;
        THROW_IF_FAILED(font_collection->GetFontFamily(index, &family));

        wil::com_ptr<IDWriteLocalizedStrings> family_names;
        THROW_IF_FAILED(family->GetFamilyNames(&family_names));

        wil::com_ptr<IDWriteFont> first_font;
        THROW_IF_FAILED(family->GetFont(0, &first_font));

        const auto is_symbol_font = first_font->IsSymbolFont() != 0;

        std::vector<DWRITE_FONT_AXIS_RANGE> axis_ranges;
        std::unordered_map<uint32_t, std::set<std::tuple<float, float>>> unique_axis_ranges_map;
        std::wstring wss_family_name;
        std::wstring typographic_family_name;

        if (typographic_font_collection) {
            typographic_family_name = get_localised_string(family_names);

            const auto family_2 = family.query<IDWriteFontFamily2>();

            wil::com_ptr<IDWriteFontSet1> font_set;
            THROW_IF_FAILED(family_2->GetFontSet(&font_set));

            wil::com_ptr<IDWriteFontSet1> first_font_resources_set;
            THROW_IF_FAILED(font_set->GetFirstFontResources(&first_font_resources_set));

            const auto resources_count = first_font_resources_set->GetFontCount();

            const bool has_variations = ranges::any_of(
                ranges::views::iota(0u, resources_count), [&first_font_resources_set](auto font_index) {
                    wil::com_ptr<IDWriteFontResource> font_resource;
                    THROW_IF_FAILED(first_font_resources_set->CreateFontResource(font_index, &font_resource));
                    return font_resource->HasVariations();
                });

            if (has_variations) {
                uint32_t set_axis_count{};
                HRESULT hr = font_set->GetFontAxisRanges(nullptr, 0, &set_axis_count);

                THROW_HR_IF(hr, hr != E_NOT_SUFFICIENT_BUFFER);

                if (set_axis_count > 0) {
                    axis_ranges.resize(set_axis_count);
                    THROW_IF_FAILED(font_set->GetFontAxisRanges(axis_ranges.data(), set_axis_count, &set_axis_count));
                }

                for (const auto font_index : ranges::views::iota(0u, resources_count)) {
                    uint32_t resource_axis_count{};
                    hr = first_font_resources_set->GetFontAxisRanges(nullptr, 0, &resource_axis_count);

                    THROW_HR_IF(hr, hr != E_NOT_SUFFICIENT_BUFFER);

                    std::vector<DWRITE_FONT_AXIS_RANGE> resource_axis_ranges{};
                    if (resource_axis_count > 0) {
                        resource_axis_ranges.resize(resource_axis_count);
                        THROW_IF_FAILED(first_font_resources_set->GetFontAxisRanges(
                            font_index, resource_axis_ranges.data(), resource_axis_count, &resource_axis_count));
                    }

                    for (auto&& axis_range : resource_axis_ranges) {
                        auto& set = unique_axis_ranges_map[WI_EnumValue(axis_range.axisTag)];
                        set.emplace(axis_range.minValue, axis_range.maxValue);
                    }
                }
            }

            wil::com_ptr<IDWriteFontFace5> font_face;
            THROW_IF_FAILED(font_set->CreateFontFace(0, &font_face));

            const auto font_face_6 = font_face.query<IDWriteFontFace6>();

            wil::com_ptr<IDWriteLocalizedStrings> wss_family_name_strings;
            THROW_IF_FAILED(
                font_face_6->GetFamilyNames(DWRITE_FONT_FAMILY_MODEL_WEIGHT_STRETCH_STYLE, &wss_family_name_strings));

            wss_family_name = get_localised_string(wss_family_name_strings);
        } else {
            wss_family_name = get_localised_string(family_names);
        }

        const auto calculate_is_toggle = [&unique_axis_ranges_map](auto tag) {
            if (!unique_axis_ranges_map.contains(tag))
                return false;

            auto& unique_ranges = unique_axis_ranges_map.at(tag);

            return unique_ranges.size() == 2 && ranges::all_of(unique_ranges, [](auto&& element) {
                return std::get<0>(element) == std::get<1>(element);
            });
        };

        const auto axis_ranges_view = axis_ranges | ranges::views::filter([](auto&& range) {
            return range.minValue != range.maxValue;
        }) | ranges::views::transform([&calculate_is_toggle](auto&& range) {
            const auto tag = WI_EnumValue(range.axisTag);
            return AxisRange{tag, range.minValue, range.maxValue, calculate_is_toggle(tag)};
        }) | ranges::to<std::vector>;

        families.emplace_back(std::move(family), std::move(wss_family_name), std::move(typographic_family_name),
            is_symbol_font, axis_ranges_view);
    }

    mmh::in_place_sort(
        families,
        [](auto&& left, auto&& right) {
            return StrCmpLogicalW(left.display_name().c_str(), right.display_name().c_str());
        },
        false);

    return families;
}

std::vector<std::wstring> Context::get_emoji_font_families() const
{
    const auto font_collection = get_auto_font_collection(m_factory);

    std::vector<std::wstring> emoji_family_names;
    const auto family_count = font_collection->GetFontFamilyCount();

    for (const auto index : std::ranges::views::iota(0u, family_count)) {
        wil::com_ptr<IDWriteFontFamily> family;
        THROW_IF_FAILED(font_collection->GetFontFamily(index, &family));

        wil::com_ptr<IDWriteFont> font;
        THROW_IF_FAILED(family->GetFont(0, &font));

        BOOL exists{};
        THROW_IF_FAILED(font->HasCharacter(U'\U0001f600', &exists));

        if (!exists)
            continue;

        wil::com_ptr<IDWriteLocalizedStrings> family_names;
        THROW_IF_FAILED(family->GetFamilyNames(&family_names));

        emoji_family_names.emplace_back(get_localised_string(family_names));
    }

    mmh::in_place_sort(
        emoji_family_names, [](auto&& left, auto&& right) { return StrCmpLogicalW(left.c_str(), right.c_str()); },
        false);

    return emoji_family_names;
}

std::wstring get_localised_string(const wil::com_ptr<IDWriteLocalizedStrings>& localised_strings)
{
    std::array<wchar_t, LOCALE_NAME_MAX_LENGTH> locale_name;
    uint32_t locale_index{};
    BOOL exists{};

    if (GetUserDefaultLocaleName(locale_name.data(), LOCALE_NAME_MAX_LENGTH))
        THROW_IF_FAILED(localised_strings->FindLocaleName(locale_name.data(), &locale_index, &exists));

    if (!exists)
        THROW_IF_FAILED(localised_strings->FindLocaleName(L"en-us", &locale_index, &exists));

    if (!exists)
        locale_index = 0;

    uint32_t name_length{};
    THROW_IF_FAILED(localised_strings->GetStringLength(locale_index, &name_length));

    std::wstring localised_string;
    localised_string.resize(name_length);

    THROW_IF_FAILED(localised_strings->GetString(locale_index, localised_string.data(), name_length + 1));

    return localised_string;
}

float get_default_scaling_factor()
{
    return gsl::narrow_cast<float>(get_system_dpi_cached().cx) / gsl::narrow_cast<float>(USER_DEFAULT_SCREEN_DPI);
}

float px_to_dip(float px, float scaling_factor)
{
    return px / scaling_factor;
}

float dip_to_px(float dip, float scaling_factor)
{
    return dip * scaling_factor;
}

float dip_to_pt(float dip)
{
    return dip * 72.0f / gsl::narrow_cast<float>(USER_DEFAULT_SCREEN_DPI);
}

float pt_to_dip(float point_size)
{
    return point_size * gsl::narrow_cast<float>(USER_DEFAULT_SCREEN_DPI) / 72.0f;
}

std::vector<DWRITE_FONT_AXIS_VALUE> axis_values_to_vector(const AxisValues& values)
{
    return values | ranges::views::transform([](auto pair) {
        return DWRITE_FONT_AXIS_VALUE{static_cast<DWRITE_FONT_AXIS_TAG>(pair.first), pair.second};
    }) | ranges::to<std::vector>;
}

} // namespace uih::direct_write

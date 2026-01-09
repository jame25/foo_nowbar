#define NOMINMAX

#include <algorithm>
#include <ranges>
#include <unordered_set>

#include <gsl/gsl>

#include "../resvg/crates/c-api/resvg.h"

#include "../pfc/pfc.h"

#include "../foobar2000/SDK/foobar2000.h"

#include "../api/api.h"
#include "version.h"

using namespace std::string_view_literals;

namespace svg_services {

namespace {

DECLARE_COMPONENT_VERSION("SVG services", version, "compiled: " COMPILATION_DATE);

const std::unordered_map<int32_t, const char*> resvg_error_messages = {
    {RESVG_ERROR_NOT_AN_UTF8_STR, "Only UTF-8 files are supported"},
    {RESVG_ERROR_FILE_OPEN_FAILED, "Failed to open the file"},
    {RESVG_ERROR_MALFORMED_GZIP, "Compressed SVGs must use Gzip"},
    {RESVG_ERROR_ELEMENTS_LIMIT_REACHED, "Maximum number of elements allowed exceeded"},
    {RESVG_ERROR_INVALID_SIZE, "SVG dimensions are invalid"}, {RESVG_ERROR_PARSING_FAILED, "Failed to parse the SVG"}};

PFC_DECLARE_EXCEPTION(exception_svg_services_shutting_down, exception_svg_services, "SVG services is shutting down");
PFC_DECLARE_EXCEPTION(exception_svg_services_invalid_argument, exception_svg_services, "Invalid argument");

class exception_resvg : public exception_svg_services {
public:
    exception_resvg(int32_t code)
        : exception_svg_services(
              resvg_error_messages.contains(code) ? resvg_error_messages.at(code) : "Unknown resvg error")
        , m_code(code)
    {
    }
    [[nodiscard]] int32_t code() const { return m_code; }

private:
    int m_code;
};

void check_resvg_result(int32_t code)
{
    if (code != RESVG_OK)
        throw exception_resvg(code);
}

uint8_t divide_by_alpha(uint8_t value, uint8_t alpha)
{
    if (alpha == 0)
        return value;

    return gsl::narrow_cast<uint8_t>(
        std::clamp(std::lround(static_cast<float>(value) * 255.f / static_cast<float>(alpha)), 0l, 255l));
}

void transform_pixel_values(void* data, int width, int height, bool remove_premultiplied_alpha, bool swap_red_and_green)
{
    const auto dest_r_pos = swap_red_and_green ? 2 : 0;
    constexpr auto dest_g_pos = 1;
    const auto dest_b_pos = swap_red_and_green ? 0 : 2;

    const auto rows = static_cast<uint8_t*>(data);
    for (const auto row_index : std::ranges::views::iota(0, height)) {
        for (const auto column_index : std::ranges::views::iota(0, width)) {
            const auto row_offset = row_index * width * 4;
            const auto column_offset = column_index * 4;

            const auto r = rows[row_offset + column_offset];
            const auto g = rows[row_offset + column_offset + 1];
            const auto b = rows[row_offset + column_offset + 2];
            const auto a = rows[row_offset + column_offset + 3];

            rows[row_offset + column_offset + dest_r_pos] = remove_premultiplied_alpha ? divide_by_alpha(r, a) : r;
            rows[row_offset + column_offset + dest_g_pos] = remove_premultiplied_alpha ? divide_by_alpha(g, a) : g;
            rows[row_offset + column_offset + dest_b_pos] = remove_premultiplied_alpha ? divide_by_alpha(b, a) : b;
        }
    }
}

void validate_position(Position value)
{
    if (!std::unordered_set{Position::TopLeft, Position::Centred}.contains(value))
        throw exception_svg_services_invalid_argument("Invalid position");
}

void validate_scaling_mode(ScalingMode value)
{
    if (!std::unordered_set{ScalingMode::None, ScalingMode::Fit, ScalingMode::Zoom, ScalingMode::Stretch}.contains(
            value))
        throw exception_svg_services_invalid_argument("Invalid scaling mode");
}

void validate_pixel_format(PixelFormat value)
{
    if (!std::unordered_set{PixelFormat::BGRA, PixelFormat::PRGBA, PixelFormat::PBGRA}.contains(value))
        throw exception_svg_services_invalid_argument("Invalid pixel format");
}

class SVGDocumentImpl : public svg_document {
public:
    explicit SVGDocumentImpl(resvg_render_tree* tree) : m_tree(tree) {}

    SVGDocumentImpl(const SVGDocumentImpl&) = delete;
    SVGDocumentImpl& operator=(const SVGDocumentImpl&) = delete;

    SVGDocumentImpl(SVGDocumentImpl&&) = delete;
    SVGDocumentImpl& operator=(SVGDocumentImpl&&) = delete;

    ~SVGDocumentImpl()
    {
        resvg_tree_destroy(m_tree);
        m_tree = nullptr;
    }

    [[nodiscard]] Size get_size() const noexcept override
    {
        const auto size = resvg_get_image_size(m_tree);
        return {size.width, size.height};
    }

    [[nodiscard]] Rect get_view_box() const noexcept override
    {
        const auto size = resvg_get_image_size(m_tree);
        return {0, 0, size.width, size.height};
    }

    void render(int output_width, int output_height, Position position, ScalingMode scaling_mode,
        PixelFormat output_pixel_format, void* output_buffer, size_t output_buffer_size) const override
    {
        validate_position(position);
        validate_scaling_mode(scaling_mode);
        validate_pixel_format(output_pixel_format);

        if (output_width < 0)
            throw exception_svg_services_invalid_argument("Negative widths are invalid");

        if (output_height < 0)
            throw exception_svg_services_invalid_argument("Negative heights are invalid");

        if (output_buffer_size < gsl::narrow<size_t>(output_width) * gsl::narrow<size_t>(output_height) * size_t{4})
            throw exception_svg_services_invalid_argument("Output buffer is too small");

        if (output_width == 0 || output_height == 0)
            return;

        const auto size = get_size();
        const auto wider_than_original = static_cast<float>(size.width) / static_cast<float>(size.height)
            < static_cast<float>(output_width) / static_cast<float>(output_height);

        const auto [width_scaling_factor, height_scaling_factor]
            = [output_width, output_height, size, scaling_mode, wider_than_original]() {
                  const auto width_ratio = static_cast<double>(output_width) / static_cast<double>(size.width);
                  const auto height_ratio = static_cast<double>(output_height) / static_cast<double>(size.height);

                  if (scaling_mode == ScalingMode::None)
                      return std::make_tuple(1.0, 1.0);

                  if (scaling_mode == ScalingMode::Zoom) {
                      auto scaling_factor = wider_than_original ? width_ratio : height_ratio;
                      return std::make_tuple(scaling_factor, scaling_factor);
                  }

                  if (scaling_mode == ScalingMode::Fit) {
                      auto scaling_factor = wider_than_original ? height_ratio : width_ratio;
                      return std::make_tuple(scaling_factor, scaling_factor);
                  }

                  return std::make_tuple(width_ratio, height_ratio);
              }();

        const auto transform = resvg_transform{static_cast<float>(width_scaling_factor), 0.0f, 0.0f,
            static_cast<float>(height_scaling_factor),
            position == Position::TopLeft
                ? 0.0f
                : static_cast<float>((output_width - width_scaling_factor * size.width) / 2.0),
            position == Position::TopLeft
                ? 0.0f
                : static_cast<float>((output_height - height_scaling_factor * size.height) / 2.0)};

        resvg_render(m_tree, transform, output_width, output_height, static_cast<char*>(output_buffer));

        transform_pixel_values(output_buffer, output_width, output_height, output_pixel_format == PixelFormat::BGRA,
            output_pixel_format != PixelFormat::PRGBA);
    }

private:
    resvg_render_tree* m_tree{};
};

namespace state {

resvg_options* options_with_fonts{};
std::mutex mutex;
bool deinitialised{};

} // namespace state

void initialise_options_with_fonts()
{
    if (state::deinitialised)
        throw exception_svg_services_shutting_down();

    std::lock_guard lock(state::mutex);

    if (!state::options_with_fonts) {
        state::options_with_fonts = resvg_options_create();

        const auto timer = pfc::hires_timer::create_and_start();
        resvg_options_load_system_fonts(state::options_with_fonts);
        console::print(reinterpret_cast<const char*>(u8"SVG services – loaded fonts in: "),
            pfc::format_float(timer.query(), 0, 3), " s");
    }
}

void deinitialise_options_with_fonts()
{
    std::lock_guard lock(state::mutex);

    state::deinitialised = true;

    if (state::options_with_fonts) {
        resvg_options_destroy(state::options_with_fonts);
    }

    state::options_with_fonts = nullptr;
}

class SVGServicesImpl : public svg_services {
    svg_document::ptr open(const void* svg_data, size_t svg_data_size) const override
    {
        const std::string_view svg_string(static_cast<const char*>(svg_data), svg_data_size);
        const auto contains_text = std::ranges::search(svg_string, "<text"sv);

        resvg_render_tree* tree{};

        if (contains_text) {
            initialise_options_with_fonts();
            check_resvg_result(resvg_parse_tree_from_data(
                static_cast<const char*>(svg_data), svg_data_size, state::options_with_fonts, &tree));
        } else {
            auto* options = resvg_options_create();
            auto _ = gsl::finally([options] { resvg_options_destroy(options); });
            check_resvg_result(
                resvg_parse_tree_from_data(static_cast<const char*>(svg_data), svg_data_size, options, &tree));
        }

        return fb2k::service_new<SVGDocumentImpl>(tree);
    }
};

service_factory_t<SVGServicesImpl> _svg_services_impl;

class InitQuit : public initquit {
    void on_quit() override { deinitialise_options_with_fonts(); }
};

initquit_factory_t<InitQuit> _initquit;

} // namespace

} // namespace svg_services

#pragma once

#include <SDK/foobar2000-lite.h>

namespace svg_services {

PFC_DECLARE_EXCEPTION(exception_svg_services, pfc::exception, "SVG services error");

struct Size {
    double width{};
    double height{};
};

struct Rect {
    double x{};
    double y{};
    double width{};
    double height{};
};

enum class PixelFormat {
    BGRA,
    PRGBA,
    PBGRA,
};

enum class ScalingMode {
    None,
    Fit,
    Zoom,
    Stretch,
};

enum class Position {
    TopLeft,
    Centred,
};

class NOVTABLE svg_document : public service_base {
public:
    [[nodiscard]] virtual Size get_size() const noexcept = 0;

    [[nodiscard, deprecated("resvg no longer returns a separate view box size. Use get_size() instead.")]] virtual Rect
    get_view_box() const noexcept
        = 0;

    /**
     * \brief Render the SVG document
     *
     * \param output_width The width of the output bitmap
     * \param output_height The height of the output bitmap
     * \param position Where to position the rendered SVG in the output bitmap
     * \param scaling_mode The scaling mode to use to fit the SVG to the output
     * \param output_pixel_format The pixel format to render the SVG in
     * \param output_buffer Pointer to a writable buffer of at least size
     *                      render_width * render_height * 4.
     *                      This will receive a 32-bpp bitmap.
     * \param output_buffer_size Size of the output buffer in bytes
     */
    virtual void render(int output_width, int output_height, Position position, ScalingMode scaling_mode,
        PixelFormat output_pixel_format, void* output_buffer, size_t output_buffer_size) const
        = 0;

    FB2K_MAKE_SERVICE_INTERFACE(svg_document, service_base);
};

class NOVTABLE svg_services : public service_base {
public:
    /**
     * \brief Open an SVG document
     *
     * \param svg_data Pointer to a buffer containing the SVG document
     * \param svg_data_size Size of the SVG data in the buffer in bytes
     */
    virtual svg_document::ptr open(const void* svg_data, size_t svg_data_size) const = 0;

    FB2K_MAKE_SERVICE_INTERFACE_ENTRYPOINT(svg_services)
};

DECLARE_CLASS_GUID(svg_document, 0x9d367cb6, 0x50ca, 0x4511, 0xa0, 0x2f, 0x9f, 0x57, 0x3f, 0x9a, 0x2a, 0x33);
DECLARE_CLASS_GUID(svg_services, 0x8953198d, 0xf39d, 0x4186, 0xa9, 0xa3, 0xe5, 0xa6, 0x7c, 0xa6, 0x14, 0xa1);

} // namespace svg_services

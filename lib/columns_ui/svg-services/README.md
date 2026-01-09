# SVG services

[![Build status](https://github.com/reupen/svg-services/actions/workflows/build.yml/badge.svg)](https://github.com/reupen/svg-services/actions/workflows/build.yml?query=branch%3Amain)

This component provides an SVG renderer, for use by other foobar2000 components.

The [resvg library](https://github.com/RazrFalcon/resvg) is used to render SVGs.

## API usage (for component developers)

You only need to include the [`api/api.h`](api/api.h) header to use the API. You
can copy it to your source tree, or you can add this repository as a Git
submodule if you prefer.

A simple usage example:

```cpp
abort_callback_dummy aborter;
const auto render_height = 512;
const auto render_width = 512;
const auto file_path = "R(c:\path\to\svg\file)";

svg_services::svg_services::ptr svg_api;

if (!fb2k::std_api_try_get(svg_api)) {
    // Handle the case when the API isn’t available
}

const auto svg_data = filesystem::g_readWholeFile(file_path, 10'000'000, aborter);

std::vector<uint8_t> bitmap_data(
    static_cast<size_t>(render_width) * static_cast<size_t>(render_height) * size_t{4});
const auto svg_document = svg_api->open(svg_data->data(), svg_data->size());

svg_document->render(render_width, render_height, svg_services::Position::Centred, svg_services::ScalingMode::Fit,
    svg_services::PixelFormat::BGRA, bitmap_data.data(), bitmap_data.size());

// Pass data contained in bitmap_data to CreateDIBSection(), WIC etc.
```

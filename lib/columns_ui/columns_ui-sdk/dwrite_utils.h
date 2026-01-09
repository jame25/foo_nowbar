#pragma once

namespace cui::dwrite_utils {
/**
 * Get the HMONITOR to use with `cui::fonts::create_rendering_params()`
 * and `create_custom_rendering_params()` for a given HWND.
 *
 * @param wnd The HWND that is rendering text using DirectWrite
 * @return HMONITOR of the root window associated with the HWND, or `nullptr` on failure
 */
HMONITOR get_monitor_for_window(HWND wnd);

/**
 * Create DirectWrite rendering params for a monitor and specific rendering options.
 *
 * @param factory Pre-created DirectWrite factory
 * @param monitor Monitor handle returned by `get_monitor_for_window()`
 * @param rendering_mode Rendering mode returned by `cui::fonts::rendering_options::rendering_mode()`
 * @param rendering_params Receives a pointer to the created IDWriteRenderingParams on success
 * @return HRESULT indicating success or failure code
 */
HRESULT create_custom_rendering_params(IDWriteFactory* factory, HMONITOR monitor, DWRITE_RENDERING_MODE rendering_mode,
    IDWriteRenderingParams** rendering_params);

} // namespace cui::dwrite_utils

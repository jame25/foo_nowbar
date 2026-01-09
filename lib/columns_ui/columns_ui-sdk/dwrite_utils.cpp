#include "ui_extension.h"

#if CUI_SDK_DWRITE_ENABLED

namespace cui::dwrite_utils {

HRESULT create_custom_rendering_params(IDWriteFactory* factory, HMONITOR monitor, DWRITE_RENDERING_MODE rendering_mode,
    IDWriteRenderingParams** rendering_params)
{
    HRESULT hr{};
    pfc::com_ptr_t<IDWriteRenderingParams> monitor_rendering_params;
    if (FAILED(hr = factory->CreateMonitorRenderingParams(monitor, monitor_rendering_params.receive_ptr())))
        return hr;

    if (rendering_mode == DWRITE_RENDERING_MODE_DEFAULT) {
        *rendering_params = monitor_rendering_params.detach();
        return S_OK;
    }

    pfc::com_ptr_t<IDWriteRenderingParams1> monitor_rendering_params_1;
    (void)monitor_rendering_params->QueryInterface(
        __uuidof(IDWriteRenderingParams1), monitor_rendering_params_1.receive_void_ptr());

    pfc::com_ptr_t<IDWriteRenderingParams2> monitor_rendering_params_2;
    (void)monitor_rendering_params->QueryInterface(
        __uuidof(IDWriteRenderingParams2), monitor_rendering_params_2.receive_void_ptr());

    const auto gamma = monitor_rendering_params->GetGamma();
    const auto enhanced_contrast = monitor_rendering_params->GetEnhancedContrast();
    const auto cleartype_level = monitor_rendering_params->GetClearTypeLevel();
    const auto pixel_geometry = monitor_rendering_params->GetPixelGeometry();
    const auto greyscale_enhanced_contrast = monitor_rendering_params_1.is_valid()
        ? std::make_optional(monitor_rendering_params_1->GetGrayscaleEnhancedContrast())
        : std::nullopt;
    const auto grid_fit_mode = monitor_rendering_params_2.is_valid()
        ? std::make_optional(monitor_rendering_params_2->GetGridFitMode())
        : std::nullopt;

    pfc::com_ptr_t<IDWriteFactory1> factory_1;
    (void)factory->QueryInterface(__uuidof(IDWriteFactory1), factory_1.receive_void_ptr());

    pfc::com_ptr_t<IDWriteFactory2> factory_2;
    (void)factory->QueryInterface(__uuidof(IDWriteFactory2), factory_2.receive_void_ptr());

    if (factory_2.is_valid() && grid_fit_mode && greyscale_enhanced_contrast) {
        pfc::com_ptr_t<IDWriteRenderingParams2> custom_rendering_params_2;
        hr = factory_2->CreateCustomRenderingParams(gamma, enhanced_contrast, *greyscale_enhanced_contrast,
            cleartype_level, pixel_geometry, rendering_mode, *grid_fit_mode, custom_rendering_params_2.receive_ptr());

        *rendering_params = custom_rendering_params_2.detach();
    } else if (factory_1.is_valid() && greyscale_enhanced_contrast) {
        pfc::com_ptr_t<IDWriteRenderingParams1> custom_rendering_params_1;
        hr = factory_1->CreateCustomRenderingParams(gamma, enhanced_contrast, *greyscale_enhanced_contrast,
            cleartype_level, pixel_geometry, rendering_mode, custom_rendering_params_1.receive_ptr());

        *rendering_params = custom_rendering_params_1.detach();
    } else {
        hr = factory->CreateCustomRenderingParams(
            gamma, enhanced_contrast, cleartype_level, pixel_geometry, rendering_mode, rendering_params);
    }

    return hr;
}

HMONITOR get_monitor_for_window(HWND wnd)
{
    const auto root_window = GetAncestor(wnd, GA_ROOT);

    if (!root_window)
        return nullptr;

    return MonitorFromWindow(root_window, MONITOR_DEFAULTTONEAREST);
}

} // namespace cui::dwrite_utils

#endif

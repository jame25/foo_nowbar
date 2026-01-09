#include "stdafx.h"

#include "direct_2d.h"

namespace uih::d2d {

namespace {

float scale_channel(uint8_t value)
{
    return gsl::narrow_cast<float>(value) / 255.0f;
}

} // namespace

Context::Ptr Context::s_create()
{
    auto ptr = s_ptr.lock();

    if (!ptr) {
        ptr = std::make_shared<Context>();
        s_ptr = ptr;
    }

    return ptr;
}

Context::Context()
{
    THROW_IF_FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory), m_factory.put_void()));
}

wil::com_ptr<ID2D1HwndRenderTarget> Context::create_hwnd_render_target(HWND wnd)
{
    RECT rect{};
    GetClientRect(wnd, &rect);

    wil::com_ptr<ID2D1HwndRenderTarget> render_target;
    const auto base_properties = D2D1::RenderTargetProperties();
    const auto hwnd_properties = D2D1::HwndRenderTargetProperties(
        wnd, {gsl::narrow<unsigned>(wil::rect_width(rect)), gsl::narrow<unsigned>(wil::rect_height(rect))});
    THROW_IF_FAILED(m_factory->CreateHwndRenderTarget(&base_properties, &hwnd_properties, &render_target));

    return render_target;
}

D2D1_COLOR_F colorref_to_d2d_color(COLORREF colour)
{
    return D2D1::ColorF(
        scale_channel(GetRValue(colour)), scale_channel(GetGValue(colour)), scale_channel(GetBValue(colour)));
}

} // namespace uih::d2d

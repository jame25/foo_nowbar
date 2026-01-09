#pragma once

namespace uih::d2d {

class Context : public std::enable_shared_from_this<Context> {
public:
    using Ptr = std::shared_ptr<Context>;

    static Ptr s_create();

    Context();
    wil::com_ptr<ID2D1HwndRenderTarget> create_hwnd_render_target(HWND wnd);
    const wil::com_ptr<ID2D1Factory>& factory();

private:
    inline static std::weak_ptr<Context> s_ptr;

    wil::com_ptr<ID2D1Factory> m_factory;
};

D2D1_COLOR_F colorref_to_d2d_color(COLORREF colour);

} // namespace uih::d2d

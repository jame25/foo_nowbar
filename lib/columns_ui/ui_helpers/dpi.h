#pragma once

namespace uih::dpi {

class DpiAwareFunctionUnavailableError : public std::exception {
public:
    [[nodiscard]] const char* what() const override { return "This DPI-aware function is not available."; }
};

template <class Value, class ReleaseFunc>
class DpiContextHandle {
public:
    DpiContextHandle() = default;

    DpiContextHandle(DpiContextHandle&) = delete;
    DpiContextHandle& operator=(DpiContextHandle&) = delete;

    DpiContextHandle(DpiContextHandle&&) = default;
    DpiContextHandle& operator=(DpiContextHandle&&) = default;

    DpiContextHandle(wil::unique_hmodule&& user32, ReleaseFunc release_func, Value previous_value)
        : m_user32(std::move(user32))
        , m_release_func(release_func)
        , m_previous_value(previous_value)
    {
    }

    ~DpiContextHandle()
    {
        if (m_release_func)
            m_release_func(m_previous_value);
    }

private:
    wil::unique_hmodule m_user32;
    ReleaseFunc m_release_func{};
    Value m_previous_value{};
};

using SetThreadDpiAwarenessContextProc = DPI_AWARENESS_CONTEXT(__stdcall*)(DPI_AWARENESS_CONTEXT);
using SetThreadDpiAwarenessContextHandle = DpiContextHandle<DPI_AWARENESS_CONTEXT, SetThreadDpiAwarenessContextProc>;

using SetThreadDpiHostingBehaviorProc = DPI_HOSTING_BEHAVIOR(__stdcall*)(DPI_HOSTING_BEHAVIOR);
using SetThreadDpiHostingBehaviorHandle = DpiContextHandle<DPI_HOSTING_BEHAVIOR, SetThreadDpiHostingBehaviorProc>;

BOOL system_parameters_info_for_dpi(unsigned action, unsigned param, void* data, unsigned dpi);

[[nodiscard]] int get_system_metrics_for_dpi(int index, unsigned dpi);

[[nodiscard]] unsigned get_dpi_for_window(HWND wnd);

[[nodiscard]] std::shared_ptr<SetThreadDpiAwarenessContextHandle> set_thread_per_monitor_dpi_aware();

[[nodiscard]] std::shared_ptr<SetThreadDpiAwarenessContextHandle> reset_thread_per_monitor_dpi_awareness();

template <class Function>
auto with_default_thread_dpi_awareness(Function&& function)
{
    auto _ = reset_thread_per_monitor_dpi_awareness();
    return function();
}

[[nodiscard]] std::shared_ptr<SetThreadDpiHostingBehaviorHandle> set_thread_mixed_dpi_hosting();

/*
 * Per monitor DPI-aware version of DialogBox() (for modal dialogs).
 * Making DialogBox() per-monitor DPI-aware is more complicated as DialogBox() blocks and has its own message
 * loop, while the DPI-awareness context needs to be reset straight away.
 */
INT_PTR modal_dialog_box(
    UINT resource_id, HWND wnd, std::function<INT_PTR(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)> on_message);

/**
 * Per monitor DPI-aware version of CreateDialog() (for modeless dialogs).
 */
HWND modeless_dialog_box(
    UINT resource_id, HWND wnd, std::function<INT_PTR(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)> on_message);

int scale_value(unsigned target_dpi, int value, unsigned original_dpi = USER_DEFAULT_SCREEN_DPI);

} // namespace uih::dpi

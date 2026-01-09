Dark mode
=========

Columns UI 2.0.0 and newer feature an optional dark mode on Windows 10 version
2004 and newer.

If dark mode is active, panels should render system UI elements, such as common
controls and scroll bars, with a dark theme.

The following code sample shows how to enable dark scroll bars for a window
depending on whether dark mode is enabled:

::

    const auto is_dark = cui::colours::is_dark_mode_active()
    SetWindowTheme(hwnd, is_dark ? L"DarkMode_Explorer" : nullptr, nullptr);

Some common controls have a native dark mode that can also be activated using
``SetWindowTheme()`` and one of the following themes:

- ``DarkMode``
- ``DarkMode_Explorer``
- ``DarkMode_CFD``

Handling dynamic dark mode changes
----------------------------------

If you have an existing :class:`cui::colours::client` implementation, you
should:

1. Override :func:`cui::colours::client::get_supported_bools()` to return the
``cui::colours::bool_flag_dark_mode_enabled`` flag (use ``|`` to combine
multiple flags). For example:

::

    uint32_t get_supported_bools() const override
    {
        return colours::bool_flag_use_custom_active_item_frame | colours::bool_flag_dark_mode_enabled;
    }

2. Override :func:`cui::colours::client::on_bool_changed()` to handle dynamic
dark mode changes. For example:

::

    void on_bool_changed(uint32_t changed_items_mask) const override
    {
        if (changed_items_mask & colours::bool_flag_dark_mode_enabled) {
            const auto is_dark = cui::colours::is_dark_mode_active();
            for (auto hwnd : hwnds) {
                SetWindowTheme(hwnd, is_dark ? L"DarkMode_Explorer" : nullptr, nullptr);
            }
        }
    }

If you don’t have an existing :class:`cui::colours::client` implementation, you
can use :class:`cui::colours::dark_mode_notifier` to react to dark mode status
changes. For example:

::

    // Member variable
    std::unique_ptr<cui::colours::dark_mode_notifier> m_dark_mode_notifier;

    // Window procedure
    case WM_CREATE:
        // ...
        SetWindowTheme(hwnd, is_dark ? L"DarkMode_Explorer" : nullptr, nullptr);
        m_dark_mode_notifier = std::make_unique<cui::colours::dark_mode_notifier>([hwnd] {
            SetWindowTheme(hwnd, is_dark ? L"DarkMode_Explorer" : nullptr, nullptr);
        });
        return 0;
    case WM_DESTROY:
        m_dark_mode_notifier.reset();
        return 0;

Painting panel backgrounds
--------------------------

While many panels will automatically use the background colour configured in
Columns UI, there are some additional considerations to avoid glitches e.g. when
resizing panels.

If your panel uses a custom window class, it’s recommended to set the
``hbrBackground`` member of the ``WNDCLASS`` structure to ``nullptr`` when
registering the window class, and to explicitly handle erasing of your window’s
background. This is to avoid a non-dark system colour temporarily showing
through when resizing panels with dark mode enabled.

If your entire client area is covered by e.g. a child common control, you can
use the :func:`uie::win32::paint_background_using_parent()` function to simply
paint the parent window’s background in the window procedure for your custom
window class.

Similarly, if:

- you’re currently using ``uie::container_ui_extension`` (or any of its related
  variants)
- ``want_transparent_background`` is set to ``false`` in your and
  ``container_window::get_class_data()`` implementation; and
- your window procedure doesn’t handle ``WM_ERASEBKGRND`` explicitly

your panel is using ``COLOR_BTNFACE`` for its background (which is the same
colour in both light and dark modes). If this applies, you should be able to
migrate to :class:`uie::container_uie_window_v3_t` with a transparent background
to avoid ``COLOR_BTNFACE`` showing through when resizing panels.

Custom button images
--------------------

If you have an implementation of :class:`uie::button_v2`, you should generally
make :func:`uie::button_v2::get_item_bitmap` vary the image returned according
to the value returned by :func:`cui::colours::is_dark_mode_active()`, so that a
dark version of the image is returned when dark mode is active.

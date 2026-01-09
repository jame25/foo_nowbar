Using fonts
===========

Columns UI features centralised configuration of fonts on its Fonts preferences
page. This allows fonts for different parts of the UI to be configured in one
place.

Third-party panel components can add their own entries to the Fonts preferences
page and they can query the current configuration.

Adding a new entry
------------------

To add a new element to the Columns UI Fonts preferences page, you need to
implement a :class:`cui::fonts::client` service and register it using the
:class:`cui::fonts::client::factory` factory.

For an example of this, see `the Console panel source`_.

A single component can add multiple entries by implementing
:class:`cui::fonts::client` multiple times, each with a different client GUID.

Querying fonts
--------------

GDI
~~~

:func:`cui::fonts::create_hfont_with_fallback()` can be used to create an
``HFONT`` for a particular font client, while
:func:`cui::fonts::get_log_font_with_fallback()` and can be used to get the
``LOGFONT`` structure for a particular font client. There are also various other
utility functions for other scenarios in the :cpp:type:`cui::fonts` namespace.

DirectWrite
~~~~~~~~~~~

Panels using DirectWrite can use :func:`cui::fonts::get_font()` to retrieve a
:class:`cui::fonts::font` instance. The
:func:`cui::fonts::font::create_text_format()` member function can then be
called to create an ``IDWriteTextFormat`` object.

You should also apply the rendering options returned by
:func:`cui::fonts::font::rendering_options()` when rendering text using the text
format.

Note that :func:`cui::fonts::get_font()` requires Columns UI 3.0.0 or later.

If a compatible version of Columns UI isn’t installed,
:func:`cui::fonts::get_font()` will return an empty ``std::optional``. Fallback
logic should be implemented for this scenario.

Common fonts
~~~~~~~~~~~~

It’s also possible to pass the ID of a common font (i.e.
:var:`cui::fonts::items_font_id` or :var:`cui::fonts::labels_font_id`) rather
than the ID of a font client to the functions mentioned in the previous two
sections. However, given the ease of implementing a font client to add a custom
entry, it’s not generally recommended to use the common fonts directly.

Rendering text using DirectWrite
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Rendering using Direct2D
++++++++++++++++++++++++

Below is a simplified guide to using the font API with Direct2D. Error handling
is omitted, as is general Direct2D set-up and rendering code.

In your own code, using the `error handling helpers from the Windows
Implementation Libraries`_ is recommended.

Refer to the Direct2D documentation for more general information on how to use
Direct2D (for example, the `Create a simple Direct2D application`_ article).

1. Create a text format and query rendering options
...................................................

When your panel window is created, and when the
:func:`cui::fonts::client::on_font_changed()` method of your font client is
called, create or recreate a DirectWrite text format and query the rendering
options:

.. code-block:: cpp

    struct TextFormatWrapper
    {
        pfc::com_ptr_t<IDWriteTextFormat> text_format;
        cui::fonts::rendering_options::ptr rendering_options;
    };

    TextFormatWrapper create_text_format()
    {
        TextFormatWrapper text_format_wrapper;
        const auto font = cui::fonts::get_font(my_font_id);

        if (font) {
            (void)font->create_text_format(text_format_wrapper.text_format.receive_ptr());
            text_format_wrapper.rendering_options = font->rendering_options();
        }

        if (!text_format_wrapper.text_format.is_valid()) {
            // Implement fallback path here – for example, using
            // cui::fonts::get_log_font_with_fallback()
            // and IDWriteGdiInterop
        }

        text_format_wrapper.text_format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        text_format_wrapper.text_format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        return text_format_wrapper;
    }

2. Create a text layout
.......................

Whenever the text being rendered changes, create a new DirectWrite text layout:

.. code-block:: cpp

    pfc::com_ptr_t<IDWriteTextLayout> create_text_layout(IDWriteFactory* factory,
        const TextFormatWrapper& text_format_wrapper)
    {
        HRESULT hr{};
        pfc::com_ptr_t<IDWriteTextLayout> text_layout;

        if (text_format_wrapper.rendering_options->use_gdi_compatible_layout()) {
            const auto use_gdi_natural = text_format_wrapper.rendering_options->use_gdi_natural();
            hr = factory->CreateGdiCompatibleTextLayout(
                /* arguments omitted */, use_gdi_natural, text_layout.receive_ptr());
        } else {
            hr = factory->CreateTextLayout(/* arguments omitted */, text_layout.receive_ptr());
        }

        if (FAILED(hr)) {
            // Handle error
        }

        return text_layout;
    }

3. Render text using Direct2D
.............................

Within your Direct2D rendering logic, render the text layout with the correct
rendering parameters:

.. code-block:: cpp

    void render_text_layout(IDWriteFactory* factory, ID2D1RenderTarget* render_target, IDWriteTextLayout* layout, HWND wnd,
        const cui::fonts::rendering_options::ptr& rendering_options)
    {
        HRESULT hr{};
        const auto monitor = cui::dwrite_utils::get_monitor_for_window(wnd);

        pfc::com_ptr_t<IDWriteRenderingParams> rendering_params;
        hr = rendering_options->create_rendering_params(factory, monitor, rendering_params.receive_ptr());

        if (FAILED(hr)) {
            // Handle error
        }

        render_target->SetTextRenderingParams(rendering_params.get_ptr());
        render_target->SetTextAntialiasMode(rendering_options->d2d_text_antialiasing_mode());

        // Note: D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT is only supported on Windows 8.1 and newer.
        // Do not set the flag on older versions of Windows, as it will cause rendering to fail.
        // Check for support of D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT by e.g. querying the render
        // target for the ID2D1DeviceContext1 interface.
        const auto draw_text_opts = rendering_options->use_colour_glyphs() ? D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT
            : D2D1_DRAW_TEXT_OPTIONS_NONE;
        render_target->DrawTextLayout(/* origin */, layout, /* D2D brush */, draw_text_opts);
    }

Rendering directly to a GDI device context
++++++++++++++++++++++++++++++++++++++++++

If you wish to render text using DirectWrite directly to a GDI device context
(without using Direct2D), you’ll need to implement a custom IDWriteTextRenderer_
object.

See `Render to a GDI surface`_ and the `GdiInterop sample`_ for examples of
this.

.. _create a simple direct2d application: https://learn.microsoft.com/en-gb/windows/win32/direct2d/direct2d-quickstart

.. _error handling helpers from the windows implementation libraries: https://github.com/microsoft/wil/wiki/Error-handling-helpers

.. _gdiinterop sample: https://github.com/microsoft/Windows-classic-samples/tree/main/Samples/Win7Samples/multimedia/DirectWrite/GdiInterop

.. _idwritetextrenderer: https://learn.microsoft.com/en-gb/windows/win32/api/dwrite/nn-dwrite-idwritetextrenderer

.. _render to a gdi surface: https://learn.microsoft.com/en-gb/windows/win32/directwrite/render-to-a-gdi-surface

.. _the console panel source: https://github.com/reupen/console_panel/blob/38983f68cea0bb6843ce8401f8601bb0651bc8c4/foo_uie_console/main.cpp#L624-L659

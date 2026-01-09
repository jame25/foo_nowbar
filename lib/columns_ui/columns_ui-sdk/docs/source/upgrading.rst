Upgrading the SDK
=================

Version 8.0.0
-------------

:class:`cui::fonts::manager_v3` and related services are no longer marked
experimental.

Version 8.0.0-rc.1
------------------

New in this version
~~~~~~~~~~~~~~~~~~~

The following services were updated:

- :class:`cui::fonts::manager_v3`
- :class:`cui::fonts::rendering_options`
- :class:`cui::fonts::font`

The following functions were updated:

- :func:`cui::dwrite_utils::create_custom_rendering_params()`

Version 8.0.0-beta.1
--------------------

Highlights
~~~~~~~~~~

This version adds a new, experimental :class:`cui::fonts::manager_v3` service
with DirectWrite support.

New in this version
~~~~~~~~~~~~~~~~~~~

The following services were added:

- :class:`cui::fonts::manager_v3`
- :class:`cui::fonts::rendering_options`
- :class:`cui::fonts::font`

The following enumerations were added:

- :class:`cui::fonts::font_family_model`

The following exceptions were added:

- :class:`cui::fonts::exception_font_client_not_found`

The following named constants were added:

- :var:`cui::fonts::items_font_id`
- :var:`cui::fonts::labels_font_id`
- :var:`cui::fonts::core_console_font_id`
- :var:`cui::fonts::core_default_font_id`
- :var:`cui::fonts::core_lists_font_id`

The following helpers were added:

- :func:`cui::fonts::get_log_font()`
- :func:`cui::fonts::get_log_font_with_fallback()`
- :func:`cui::fonts::create_hfont_with_fallback()`
- :func:`cui::fonts::get_font()`
- :func:`cui::fonts::on_common_font_changed()`
- :func:`cui::dwrite_utils::get_monitor_for_window()`
- :func:`cui::dwrite_utils::create_custom_rendering_params()`

Bug fixes
~~~~~~~~~

- The project file was updated to remove ``..\columns_ui-sdk`` from referenced
  file paths. This makes it possible for names other than ``columns_ui-sdk`` to
  be used for the directory containing the Columns UI SDK.
- :class:`cui::colours::client::factory` and
  :class:`cui::fonts::client::factory` are now defined to be
  ``service_factory_single_t`` rather than ``service_factory_t``.

Deprecated in this version
~~~~~~~~~~~~~~~~~~~~~~~~~~

The following functions were deprecated:

- :func:`uStatus_SetText()`
- :func:`win32_helpers::message_box()`

Removed in this version
~~~~~~~~~~~~~~~~~~~~~~~

The following deprecated named constants were removed:

- ``cui::panels::guid_playlist_view``

The following enumerations were removed:

- ``cui::fonts::font_mode_t``

The following structs were removed:

- ``uTOOLINFO``
- ``uHDITEM``

The following preprocessor macros were removed:

- ``RECT_CX()``
- ``RECT_CY()``

The following deprecated classes were removed:

- :class:`uie::container_menu_ui_extension`
- :class:`uie::container_window_autorelease_t`
- :class:`uie::container_window_release_t`

The following deprecated functions were removed:

- :func:`uHeader_InsertItem()`
- :func:`uHeader_SetItemText()`
- :func:`uHeader_SetItemWidth()`
- :func:`uToolTip_AddTool()`
- :func:`uComboBox_SelectString()`
- :func:`win32_helpers::send_message_to_all_children()`
- :func:`win32_helpers::tooltip_add_tool()`

Version 7.0.0
-------------

New in this version
~~~~~~~~~~~~~~~~~~~

This version fixes a compilation error when the ``<version>`` header had not
been included, and it also fixes a crash in
``container_uie_window_v3_t::destroy_window()`` if it’s called when the window
has already been destroyed, or was never created.

Version 7.0.0-beta.2
--------------------

New in this version
~~~~~~~~~~~~~~~~~~~

This version adds an option to :class:`uie::container_window_v3` to disable the
forwarding of ``WM_SETTINGCHANGE`` messages to direct child windows.

This is useful when hosting the Win32 toolbar control as it can misbehave when
handling this message.

Version 7.0.0-beta.1
--------------------

This version of the Columns UI SDK requires Visual Studio 2022.

The project file was also renamed ``columns_ui-sdk-public.vcxproj``.

New in this version
~~~~~~~~~~~~~~~~~~~

This version adds:

- `support for the Columns UI dark mode`_
- preliminary support for compiling x64 panels
- :class:`uie::container_uie_window_v3_t`
- :class:`uie::container_window_v3`
- :class:`uie::simple_command_menu_node`
- :class:`uie::splitter_item_full_v2_t`
- :class:`uie::splitter_item_full_v3_t`
- :class:`uie::splitter_item_full_v3_impl_t`
- :member:`uie::splitter_window::size_and_dpi`
- :class:`cui::fcl::dataset_v2`
- :class:`cui::fonts::manager_v2`
- :func:`cui::config_objects::get_locked_panel_resizing_allowed()`
- :func:`uie::win32::paint_background_using_parent()`

Renamed namespaces
~~~~~~~~~~~~~~~~~~

- The ``ui_extension`` namespace was renamed ``uie``
- The ``columns_ui`` namespace was renamed ``cui``

Aliases exist for the old names for backwards compatibility.

Deprecated in this version
~~~~~~~~~~~~~~~~~~~~~~~~~~

The following classes were deprecated:

- :class:`uie::container_ui_extension_t`
- :class:`uie::container_ui_extension`
- :class:`uie::container_menu_ui_extension`
- :class:`uie::container_uie_window_t`
- :class:`uie::container_window_autorelease_t`
- :class:`uie::container_window_release_t`
- :class:`uie::container_window`

The following functions were deprecated:

- :func:`uHeader_InsertItem()`
- :func:`uHeader_SetItemText()`
- :func:`uHeader_SetItemWidth()`
- :func:`uToolTip_AddTool()`
- :func:`uComboBox_SelectString()`
- :func:`win32_helpers::send_message_to_all_children()`
- :func:`win32_helpers::tooltip_add_tool()`

Removed in this version
~~~~~~~~~~~~~~~~~~~~~~~

Example components are no longer bundled with the SDK. These are now published
on GitHub:

- `Example panel`_
- `Console panel`_

The following classes were removed:

- :class:`uie::window_base_t`
- :class:`logfont_os_menu`
- :class:`logfont_os_icon`
- :class:`logfont_os_from_utf8`
- :class:`logfont_utf8_from_os`

The following functions were removed:

- :func:`uGetClassLong()`
- :func:`uSetClassLong()`
- :func:`convert_logfont_utf8_to_os()`
- :func:`convert_logfont_os_to_utf8()`

The following macros were removed:

- :c:macro:`uT()`
- :c:macro:`uTS()`
- :c:macro:`Tu()`
- :c:macro:`TSu()`

.. _console panel: https://github.com/reupen/console_panel

.. _example panel: https://github.com/reupen/example_panel

.. _support for the columns ui dark mode: Darkmode

.. include:: toc.rst

Getting started
===============

The Columns UI SDK provides interfaces you can use to:

- create windows controlled by a host and embedded in the host’s window
- provide information about commands to be used as a toolbar button

Installation
------------

You’ll need:

- `Microsoft Visual Studio 2022`_
- `foobar2000 SDK`_

To install, `download the SDK`_ and extract the archive alongside the foobar2000
subdirectory of your foobar2000 SDK.

Usage
-----

Insert the columns_ui-sdk project into your solution, and add it as a dependency
for your project. Then ``#include "columns_ui-sdk/ui_extension.h"`` in your
project as needed.

Examples
--------

Two examples are published on GitHub:

- `Example panel`_ – a simple panel that displays some text and implements a
  context menu item
- `Console panel`_ – a console viewer

Panel APIs
----------

APIs
~~~~

Components wishing to provide a panel or toolbar should implement
:class:`uie::window`. Specific subclasses exist for:

- Menus – :class:`uie::menu_window`
- Playlists – :class:`uie::playlist_window`
- Splitter panels – :class:`uie::splitter_window`

Components wishing to host panels and toolbars should implement
:class:`uie::window_host`.

Helpers
~~~~~~~

:type:`uie::container_uie_window_v3` aims to simplify the implementation of a
panel or toolbar, creating a window for you and partially implementing
:class:`uie::window`. It may not be suitable if you want to support multiple UI
components, or you wish to also allow your panel to be opened as a pop-up
window.

Button APIs
-----------

APIs
~~~~

The base class for buttons is :class:`uie::button`.

If you wish to provide default bitmaps and additional information for your menu
items, derive from :class:`uie::menu_button`. If you wish to implement a custom
button not based upon a menu item, derive from :class:`uie::custom_button`.

Standard windows
~~~~~~~~~~~~~~~~

The GUIDs for the standard panels may be found in the :type:`cui::panels`
namespace. The GUIDs for the standard toolbars may be found in the
:type:`cui::toolbars` namespace.

You may use these GUIDs to create the standard windows in your own component; do
not use them as GUIDs for your own windows.

.. _console panel: https://github.com/reupen/console_panel

.. _download the sdk: https://yuo.be/columns-ui-sdk

.. _example panel: https://github.com/reupen/example_panel

.. _foobar2000 sdk: http://www.foobar2000.org/SDK

.. _microsoft visual studio 2022: https://visualstudio.microsoft.com/downloads/

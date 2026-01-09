#include "stdafx.h"

/**
 * \section class_sec Custom controls
 * \subsection TrackbarBase Track bar
 * The main API of the custom track bar is defined in \link ui_extension::track_bar TrackbarBase\endlink.
 * You can derive from this to provide custom rendering, or use the standard rendering provided by
 * \link ui_extension::track_bar_impl Trackbar\endlink.
 * \n\n
 * Hosts should implement \link ui_extension::track_bar_host TrackbarCallback\endlink.
 *
 * \section misc_sec Misc helpers
 * \subsection ss_uxtheme Uxtheme API wrapper
 * See uxtheme_handle.
 * \subsection ss_libpng libpng wrapper
 * See libpng_handle.
 * \subsection ss_utf8win32 UTF8 Win32 API wrappers
 * See utf8api.h.
 *
 * Hosts to the track bar should implement \link ui_extension::track_bar_host TrackbarCallback\endlink.
 */

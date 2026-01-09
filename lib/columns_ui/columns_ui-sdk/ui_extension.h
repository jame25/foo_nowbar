#ifndef _UI_EXTENSION_H_
#define _UI_EXTENSION_H_

#define UI_EXTENSION_VERSION "8.0.0"

#ifndef CUI_SDK_DWRITE_ENABLED
#define CUI_SDK_DWRITE_ENABLED __has_include(<dwrite_3.h>)
#endif

#include <algorithm>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>

// Included first, because pfc.h includes winsock2.h
#include "../pfc/pfc.h"

#if CUI_SDK_DWRITE_ENABLED
#include <dwrite_3.h>
#include <d2d1.h>
#endif
#include <shlwapi.h>

#include "../foobar2000/SDK/foobar2000-lite.h"
#include "../foobar2000/SDK/config_object.h"
#include "../foobar2000/SDK/contextmenu_manager.h"
#include "../foobar2000/SDK/initquit.h"
#include "../foobar2000/SDK/titleformat.h"
#include "../foobar2000/SDK/ui.h"

class stream_writer_memblock_ref : public stream_writer {
public:
    stream_writer_memblock_ref(pfc::array_t<t_uint8>& p_data, bool b_reset = false) : m_data(p_data)
    {
        if (b_reset)
            m_data.set_size(0);
    }
    void write(const void* p_buffer, t_size p_bytes, abort_callback& p_abort) override
    {
        m_data.append_fromptr((t_uint8*)p_buffer, p_bytes);
    }

private:
    pfc::array_t<t_uint8>& m_data;
};

class stream_writer_memblock : public stream_writer {
public:
    stream_writer_memblock() {}
    void write(const void* p_buffer, t_size p_bytes, abort_callback& p_abort) override
    {
        m_data.append_fromptr((t_uint8*)p_buffer, p_bytes);
    }
    pfc::array_t<t_uint8> m_data;
};

/**
 * \brief Namespace for functionality relating to hosted windows (commonly known as
 * panels and toolbars, and formerly called UI extensions).
 */
namespace uie {

/**
 * \brief Flags indicating the type of the UI extension.
 *
 * \remarks Combine multiple flags using bitwise or,
 * if an extension supports more than one type.
 *
 * \see window::get_type(), window_host_with_control::get_supported_types()
 */
enum window_type_t {
    /** The extension is a sidebar panel. */
    type_panel = (1 << 0),
    /** The extension is a toolbar panel. */
    type_toolbar = (1 << 1),
    /** The extension is a playlist panel. */
    type_playlist = (1 << 2),
    /** The extension is a layout panel. */
    type_layout = (1 << 3),
    /** The extension is a splitter panel. */
    type_splitter = (1 << 4),
};

/**
 * \brief Flags indicating a change of a size limit.
 *
 * Combine multiple flags using bitwise or,
 * if more than one size limit changed.
 *
 * \see window_host::on_size_limit_change()
 */
enum size_limit_flag_t {
    /** The minimum width changed. */
    size_limit_minimum_width = (1 << 0),
    /** The maximum width changed. */
    size_limit_maximum_width = (1 << 1),
    /** The minimum height changed. */
    size_limit_minimum_height = (1 << 2),
    /** The maximum height changed. */
    size_limit_maximum_height = (1 << 3),
    /** All size limits changed. */
    size_limit_all
        = size_limit_minimum_width | size_limit_maximum_width | size_limit_minimum_height | size_limit_maximum_height,
};

/**
 * \brief Flags indicating whether the size and height parameters are valid.
 *
 * Combine multiple flags using bitwise or.
 *
 * \see uie::window_host::is_resize_supported(), uie::window_host::request_resize()
 */
enum resize_flag_t {
    /** The width parameter is valid. */
    size_width = (1 << 0),
    /** The height parameter is valid. */
    size_height = (1 << 1),
};

class visualisation;
class visualisation_host;

typedef visualisation visualization;

typedef service_ptr_t<class visualisation> visualisation_ptr;
typedef service_ptr_t<class visualisation_host> visualisation_host_ptr;

typedef visualisation_ptr visualization_ptr;
typedef visualisation_host_ptr visualization_host_ptr;

typedef service_ptr_t<class window> window_ptr;
typedef service_ptr_t<class window_host> window_host_ptr;

typedef service_ptr_t<class splitter_window> splitter_window_ptr;
typedef service_ptr_t<class splitter_window_v2> splitter_window_v2_ptr;
typedef service_ptr_t<class menu_window> menu_window_ptr;
typedef service_ptr_t<class playlist_window> playlist_window_ptr;

typedef pfc::refcounted_object_ptr_t<class menu_node_t> menu_node_ptr;

static const window_ptr window_ptr_null = window_ptr();
} // namespace uie

namespace ui_extension = uie;

#include "menu.h"
#include "window_host.h"
#include "base.h"
#include "window.h"
#include "win32_helpers.h"
#include "window_helper.h"
#include "container_window_v3.h"
#include "container_uie_window_v3.h"
#include "splitter.h"
#include "visualisation.h"
#include "buttons.h"
#include "callback.h"
#include "columns_ui.h"
#include "colours.h"
#include "fonts.h"

#if CUI_SDK_DWRITE_ENABLED
#include "dwrite_utils.h"
#include "font_manager_v3.h"
#endif

#include "font_utils.h"

namespace ui_extension = uie;
namespace columns_ui = cui;

#endif

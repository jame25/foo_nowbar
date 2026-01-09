#ifndef _UI_HELPERS_H_
#define _UI_HELPERS_H_

#ifndef NTDDI_WIN10_CU
#define NTDDI_WIN10_CU 0x0A00000D
#endif

#ifndef _WIN32_WINNT
#define _WIN32_WINNT _WIN32_WINNT_WIN10
#endif

#ifndef NTDDI_VERSION
#define NTDDI_VERSION NTDDI_WIN10_CU
#endif

#define _SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING
#define OEMRESOURCE

#include <algorithm>
#include <functional>
#include <memory>
#include <optional>
#include <ranges>
#include <regex>
#include <set>
#include <span>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <variant>
#include <vector>

#include <ppl.h>

#include <gsl/gsl>
#include <range/v3/all.hpp>

// Included before windows.h, because pfc.h includes winsock2.h
#include "../pfc/pfc.h"

#include <windows.h>
#include <WindowsX.h>
#include <SHLWAPI.H>
#include <vssym32.h>
#include <uxtheme.h>
#include <shldisp.h>
#include <ShlObj.h>
#include <gdiplus.h>
#include <Usp10.h>
#include <CommonControls.h>
#include <intsafe.h>
#include <d2d1.h>
#include <dwrite_3.h>

#include <wil/com.h>
#include <wil/resource.h>
#include <wil/win32_helpers.h>

#include "../mmh/stdafx.h"

#ifndef RECT_CX
#define RECT_CX(rc) ((rc).right - (rc).left)
#endif

#ifndef RECT_CY
#define RECT_CY(rc) ((rc).bottom - (rc).top)
#endif

#include "literals.h"
#include "event_token.h"

#include "win32_helpers.h"
#include "ole.h"

#include "dialog.h"
#include "dpi.h"
#include "container_window.h"
#include "message_hook.h"
#include "trackbar.h"
#include "solid_fill.h"
#include "theming.h"

#include "gdi.h"
#include "uniscribe.h"
#include "uniscribe_text_out.h"
#include "text_out_helpers.h"
#include "drag_image.h"
#include "info_box.h"
#include "menu.h"
#include "window_subclasser.h"

#include "ole/data_object.h"
#include "ole/enum_format_etc.h"

#endif

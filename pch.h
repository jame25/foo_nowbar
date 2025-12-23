#pragma once

// Windows headers - must be before GDI+ and ATL
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <objbase.h>   // Required for IID definitions before GDI+
#include <shlwapi.h>
#include <mmsystem.h>  // For timeGetTime used by pfc
#pragma comment(lib, "winmm.lib")

// GDI+ - after Windows headers
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

// ATL base only (no WTL)
#include <atlbase.h>

// STL
#include <memory>
#include <string>
#include <optional>
#include <functional>
#include <vector>
#include <list>
#include <mutex>
#include <algorithm>

// foobar2000 SDK (inside columns_ui) - use full header for all functionality
#include "../columns_ui/foobar2000/SDK/foobar2000.h"
#include "../columns_ui/foobar2000/SDK/coreDarkMode.h"
#include "../columns_ui/foobar2000/helpers/helpers.h"

// Columns UI SDK
#include "../columns_ui/columns_ui-sdk/ui_extension.h"

// Project headers
#include "guids.h"
#include "version.h"

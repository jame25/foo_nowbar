#pragma once

// Windows headers - must be before GDI+ and ATL
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <objbase.h> // Required for IID definitions before GDI+
#include <mmsystem.h>
#include <timeapi.h> // For timeGetTime used by pfc
#pragma comment(lib, "winmm.lib")

// GDI+ - after Windows headers
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

// DWM API for Windows 11 acrylic/backdrop effects
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")

// ATL base only (no WTL)
#include <atlbase.h>

// STL
#include <algorithm>
#include <vector>
#include <string>
#include <map>
#include <memory>
#include <functional>
#include <mutex>
#include <atomic>
#include <chrono>

// ----------------------------------------------------------------------------
// SDK INCLUDE PATH HANDLING
// ----------------------------------------------------------------------------

// 1. foobar2000 SDK
// Check for sibling folder (GitHub Actions / Local Sibling setup)
#if __has_include("../columns_ui/foobar2000/SDK/foobar2000.h")
    #include "../columns_ui/foobar2000/SDK/foobar2000.h"
    #include "../columns_ui/foobar2000/SDK/coreDarkMode.h"
    #include "../columns_ui/foobar2000/helpers/helpers.h"
// Check for internal lib folder (Local Subfolder setup)
#elif __has_include("lib/columns_ui/foobar2000/SDK/foobar2000.h")
    #include "lib/columns_ui/foobar2000/SDK/foobar2000.h"
    #include "lib/columns_ui/foobar2000/SDK/coreDarkMode.h"
    #include "lib/columns_ui/foobar2000/helpers/helpers.h"
#else
    #error "Cannot find foobar2000 SDK! Checked '../columns_ui' and 'lib/columns_ui'"
#endif

// 2. Columns UI SDK
#if __has_include("../columns_ui/columns_ui-sdk/ui_extension.h")
    #include "../columns_ui/columns_ui-sdk/ui_extension.h"
#elif __has_include("lib/columns_ui/columns_ui-sdk/ui_extension.h")
    #include "lib/columns_ui/columns_ui-sdk/ui_extension.h"
#endif

// 3. SVG Services API
#if __has_include("../columns_ui/svg-services/api/api.h")
    #include "../columns_ui/svg-services/api/api.h"
#elif __has_include("lib/columns_ui/svg-services/api/api.h")
    #include "lib/columns_ui/svg-services/api/api.h"
#endif

// Project headers
#include "guids.h"
#include "version.h"

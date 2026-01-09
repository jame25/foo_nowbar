#pragma once

#define OEMRESOURCE

#include <atomic>
#include <concepts>
#include <optional>

#include <ppl.h>

#include <gsl/gsl>

// Included before windows.h, because pfc.h includes winsock2.h
#include "../pfc/pfc.h"

#include <windows.h>
#include <SHLWAPI.H>

#include <wil/result.h>

#include "../foobar2000/SDK/foobar2000-lite.h"
#include "../foobar2000/SDK/cfg_var.h"
#include "../foobar2000/SDK/config_object.h"
#include "../foobar2000/SDK/coreDarkMode.h"
#include "../foobar2000/SDK/initquit.h"
#include "../foobar2000/SDK/library_callbacks.h"
#include "../foobar2000/SDK/main_thread_callback.h"
#include "../foobar2000/SDK/modeless_dialog.h"

#include "../mmh/stdafx.h"
#include "../ui_helpers/stdafx.h"

#include "config_var.h"
#include "config_object.h"
#include "console.h"
#include "dark_mode.h"
#include "dialog.h"
#include "fcl.h"
#include "info_box.h"
#include "initquit.h"
#include "library.h"
#include "low_level_hook.h"
#include "sort.h"
#include "utility.h"
#include "wil.h"

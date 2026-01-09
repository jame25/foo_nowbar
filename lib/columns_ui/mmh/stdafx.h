#pragma once

#ifndef _WIN32_WINNT
#define _WIN32_WINNT _WIN32_WINNT_WIN10
#endif

#include <algorithm>
#include <numeric>

#include <assert.h>
#include <ppl.h>

#include <gsl/gsl>

// Included before windows.h, because pfc.h includes winsock2.h
#include "../pfc/pfc.h"

#include <windows.h>
#include <VersionHelpers.h>

#include <wil/win32_helpers.h>

#include "algorithm.h"
#include "pfc_interop.h"

#include "osversion.h"
#include "exception.h"
#include "functional.h"
#include "sort.h"
#include "string.h"
#include "thread.h"
#include "comptr.h"
#include "utility.h"

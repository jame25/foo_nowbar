#include "stdafx.h"

namespace mmh {

bool is_windows_10_or_newer()
{
    static auto result = IsWindows10OrGreater();
    return result;
}

bool is_windows_7_or_newer()
{
    static bool result = IsWindows7OrGreater();
    return result;
}

bool is_windows_vista_or_newer()
{
    static bool result = IsWindowsVistaOrGreater();
    return result;
}

bool is_wine()
{
    static bool result = [] {
        wil::unique_hmodule ntdll_module(LoadLibrary(L"ntdll.dll"));

        if (!ntdll_module.is_valid())
            return false;

        return GetProcAddress(ntdll_module.get(), "wine_get_version") != nullptr;
    }();

    return result;
}

} // namespace mmh

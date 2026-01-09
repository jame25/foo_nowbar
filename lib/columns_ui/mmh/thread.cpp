#include "stdafx.h"

namespace mmh {

HRESULT set_thread_description(HANDLE thread, PCWSTR thread_description)
{
    const wil::unique_hmodule kernel_module(LoadLibraryExW(L"kernelbase.dll", nullptr, LOAD_LIBRARY_SAFE_CURRENT_DIRS));

    if (!kernel_module)
        return E_NOTIMPL;

    const auto set_thread_description = GetProcAddressByFunctionDeclaration(kernel_module.get(), SetThreadDescription);

    if (!set_thread_description)
        return E_NOTIMPL;

    return set_thread_description(thread, thread_description);
}

} // namespace mmh

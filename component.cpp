#include "pch.h"
#include "version.h"
#include "guids.h"

// Module instance handle for dialog creation
HINSTANCE g_hInstance = nullptr;

// Declare component version
DECLARE_COMPONENT_VERSION(
    FOO_NOWBAR_DISPLAY_NAME,
    FOO_NOWBAR_VERSION_STRING,
    FOO_NOWBAR_DESCRIPTION
);

// Prevent component from being unloaded (required for proper cleanup)
VALIDATE_COMPONENT_FILENAME("foo_nowbar.dll");

// GDI+ initialization
namespace {
    ULONG_PTR g_gdiplusToken = 0;
    
    class GdiplusInitializer : public initquit {
    public:
        void on_init() override {
            Gdiplus::GdiplusStartupInput gdiplusStartupInput;
            Gdiplus::GdiplusStartup(&g_gdiplusToken, &gdiplusStartupInput, nullptr);
        }
        
        void on_quit() override {
            if (g_gdiplusToken) {
                Gdiplus::GdiplusShutdown(g_gdiplusToken);
                g_gdiplusToken = 0;
            }
        }
    };
    
    static initquit_factory_t<GdiplusInitializer> g_gdiplus_init;
}

// DllMain to capture module instance handle
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID /*reserved*/) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_hInstance = hModule;
    }
    return TRUE;
}

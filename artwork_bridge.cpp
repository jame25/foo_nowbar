#include "pch.h"
#include "artwork_bridge.h"
#include "preferences.h"
#include "core/control_panel_core.h"
#include <mutex>

// Global function pointers
pfn_foo_artwork_search g_artwork_search = nullptr;
pfn_foo_artwork_get_bitmap g_artwork_get_bitmap = nullptr;
pfn_foo_artwork_is_loading g_artwork_is_loading = nullptr;
pfn_foo_artwork_set_callback g_artwork_set_callback = nullptr;
pfn_foo_artwork_remove_callback g_artwork_remove_callback = nullptr;

// Module handle for foo_artwork
static HMODULE g_foo_artwork_module = nullptr;

// Pending artwork from callback (guarded by g_pending_mutex)
static std::mutex g_pending_mutex;
static HBITMAP g_pending_artwork_bitmap = nullptr;
static bool g_has_pending_artwork = false;

// Callback function that receives artwork results from foo_artwork.
// Called on foo_artwork's worker thread - must synchronize and marshal to main thread.
static void artwork_result_callback(bool success, HBITMAP bitmap) {
    if (success && bitmap) {
        {
            std::lock_guard<std::mutex> lock(g_pending_mutex);
            g_pending_artwork_bitmap = bitmap;
            g_has_pending_artwork = true;
        }

        // Marshal notification to main thread
        fb2k::inMainThread([]() {
            nowbar::ControlPanelCore::notify_online_artwork_received();
        });
    }
}

bool init_artwork_bridge() {
    // Try to get handle to already-loaded foo_artwork module
    g_foo_artwork_module = GetModuleHandleW(L"foo_artwork.dll");

    if (!g_foo_artwork_module) {
        // foo_artwork not installed/loaded
        return false;
    }

    // Resolve function pointers - these are extern "C" exports from foo_artwork
    g_artwork_search = (pfn_foo_artwork_search)
        GetProcAddress(g_foo_artwork_module, "foo_artwork_search");

    g_artwork_get_bitmap = (pfn_foo_artwork_get_bitmap)
        GetProcAddress(g_foo_artwork_module, "foo_artwork_get_bitmap");

    g_artwork_is_loading = (pfn_foo_artwork_is_loading)
        GetProcAddress(g_foo_artwork_module, "foo_artwork_is_loading");

    g_artwork_set_callback = (pfn_foo_artwork_set_callback)
        GetProcAddress(g_foo_artwork_module, "foo_artwork_set_callback");

    g_artwork_remove_callback = (pfn_foo_artwork_remove_callback)
        GetProcAddress(g_foo_artwork_module, "foo_artwork_remove_callback");

    // Register our callback to receive artwork results
    if (g_artwork_set_callback) {
        g_artwork_set_callback(artwork_result_callback);
    }

    // Return true if at least the search function is available
    return g_artwork_search != nullptr;
}

void shutdown_artwork_bridge() {
    // Unregister our specific callback (multi-callback safe)
    if (g_artwork_remove_callback) {
        g_artwork_remove_callback(artwork_result_callback);
    } else if (g_artwork_set_callback) {
        g_artwork_set_callback(nullptr); // Fallback for older foo_artwork
    }
    std::lock_guard<std::mutex> lock(g_pending_mutex);
    g_pending_artwork_bitmap = nullptr;
    g_has_pending_artwork = false;
}

void request_online_artwork(const char* artist, const char* title) {
    // Check if online artwork is enabled in preferences
    if (!get_nowbar_online_artwork()) {
        return;
    }

    // Check if bridge is available
    if (!g_artwork_search) {
        return;
    }

    // Clear any pending artwork from previous search
    {
        std::lock_guard<std::mutex> lock(g_pending_mutex);
        g_pending_artwork_bitmap = nullptr;
        g_has_pending_artwork = false;
    }

    // Request artwork from foo_artwork
    g_artwork_search(artist, title);
}

bool has_pending_online_artwork() {
    std::lock_guard<std::mutex> lock(g_pending_mutex);
    return g_has_pending_artwork;
}

HBITMAP get_pending_online_artwork() {
    std::lock_guard<std::mutex> lock(g_pending_mutex);
    HBITMAP bitmap = g_pending_artwork_bitmap;
    g_pending_artwork_bitmap = nullptr;
    g_has_pending_artwork = false;
    return bitmap;
}

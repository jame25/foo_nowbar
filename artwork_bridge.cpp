#include "pch.h"
#include "artwork_bridge.h"
#include "preferences.h"
#include "core/control_panel_core.h"
#include <mutex>
#include <string>

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

// Last requested artist & title for search deduplication
static std::string g_last_requested_artist;
static std::string g_last_requested_title;

// Callback function that receives artwork results from foo_artwork.
// Called on foo_artwork's worker thread - must synchronize and marshal to main thread.
static void artwork_result_callback(bool success, HBITMAP bitmap) {
    {
        std::lock_guard<std::mutex> lock(g_pending_mutex);
        // Failure/clear notifications supersede any unconsumed successful result.
        g_pending_artwork_bitmap = success ? bitmap : nullptr;
        g_has_pending_artwork = g_pending_artwork_bitmap != nullptr;
    }

    // Always marshal notification to main thread so that title formats (%foo_artwork_title%, %foo_artwork_artist%)
    // and UI repaints are updated even if no artwork image was found in API search
    fb2k::inMainThread([]() {
        nowbar::ControlPanelCore::notify_online_artwork_received();
    });
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
    g_last_requested_artist.clear();
    g_last_requested_title.clear();
}

void clear_pending_online_artwork() {
    std::lock_guard<std::mutex> lock(g_pending_mutex);
    g_pending_artwork_bitmap = nullptr;
    g_has_pending_artwork = false;
    g_last_requested_artist.clear();
    g_last_requested_title.clear();
}

void request_online_artwork(const char* artist, const char* title) {
    // Check if online artwork is enabled in preferences
    if (!get_nowbar_online_artwork()) {
        return;
    }

    // Check if bridge is available
    if (!g_artwork_search) {
        init_artwork_bridge();
        if (!g_artwork_search) return;
    }

    const char* safe_artist = artist ? artist : "";
    const char* safe_title = title ? title : "";

    if (safe_artist[0] == '\0' && safe_title[0] == '\0') {
        return;
    }

    // Deduplicate: If already requested this exact artist & title, don't re-issue search
    {
        std::lock_guard<std::mutex> lock(g_pending_mutex);
        if (g_last_requested_artist == safe_artist && g_last_requested_title == safe_title) {
            return;
        }
        g_last_requested_artist = safe_artist;
        g_last_requested_title = safe_title;
        g_pending_artwork_bitmap = nullptr;
        g_has_pending_artwork = false;
    }

    // Request artwork from foo_artwork
    g_artwork_search(safe_artist, safe_title);
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

#include "pch.h"
#include "playback_state.h"
#include "../preferences.h"
#include "../artwork_bridge.h"

namespace nowbar {

// Pointer-based singleton to allow explicit destruction during on_quit()
static PlaybackStateManager* g_instance = nullptr;
static bool g_shutdown_called = false;

PlaybackStateManager& PlaybackStateManager::get() {
    if (!g_instance && !g_shutdown_called) {
        g_instance = new PlaybackStateManager();
    }
    return *g_instance;
}

void PlaybackStateManager::shutdown() {
    g_shutdown_called = true;
    if (g_instance) {
        delete g_instance;
        g_instance = nullptr;
    }
}

bool PlaybackStateManager::is_available() {
    return g_instance != nullptr && !g_shutdown_called;
}

PlaybackStateManager::~PlaybackStateManager() {
    // Clear metadb_handle_ptr while services are still available
    m_state.current_track.release();
    // Base class destructor will unregister from play_callback_manager
}

PlaybackStateManager::PlaybackStateManager() 
    : play_callback_impl_base(flag_on_playback_all | flag_on_volume_change)
{
    // Initialize current state from playback control
    auto pc = playback_control::get();
    m_state.is_playing = pc->is_playing();
    m_state.is_paused = pc->is_paused();
    m_state.volume_db = pc->get_volume();
    m_state.playback_order = playlist_manager::get()->playback_order_get_active();
    
    if (m_state.is_playing) {
        m_state.playback_time = pc->playback_get_position();
        m_state.track_length = pc->playback_get_length();
        m_state.can_seek = pc->playback_can_seek();
        
        metadb_handle_ptr track;
        if (pc->get_now_playing(track)) {
            m_state.current_track = track;  // Store the track handle
            update_track_info(track);
        }
    }
}

void PlaybackStateManager::register_callback(IPlaybackStateCallback* cb) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_callbacks.push_back(cb);
}

void PlaybackStateManager::unregister_callback(IPlaybackStateCallback* cb) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_callbacks.erase(
        std::remove(m_callbacks.begin(), m_callbacks.end(), cb),
        m_callbacks.end()
    );
}

void PlaybackStateManager::on_playback_starting(play_control::t_track_command p_command, bool p_paused) noexcept {
    try {
        m_state.is_playing = true;
        m_state.is_paused = p_paused;
        notify_state_changed();
    } catch (...) {}
}

void PlaybackStateManager::on_playback_new_track(metadb_handle_ptr p_track) noexcept {
    try {
        ++m_stream_metadata_revision;
        clear_pending_online_artwork();

        auto pc = playback_control::get();
        m_state.is_playing = true;
        m_state.is_paused = pc->is_paused();
        m_state.playback_time = 0.0;
        m_state.track_length = pc->playback_get_length();
        m_state.can_seek = pc->playback_can_seek();
        m_state.current_track = p_track;  // Store the track handle
        m_state.playback_order = playlist_manager::get()->playback_order_get_active();  // Refresh playback order

        // Reset preview skip flag for new track
        m_preview_skip_triggered = false;

        // Check if we should skip this track due to low rating
        if (check_and_skip_low_rating(p_track)) {
            return;  // Track is being skipped, don't update UI
        }

        // Reset consecutive skip counter on successful playback
        m_consecutive_rating_skips = 0;

        update_track_info(p_track);
        notify_track_changed();
        // notify_state_changed() intentionally omitted here:
        // on_playback_starting() already fired a state change before this callback,
        // and on_track_changed() handles all track-specific updates (theme, formats,
        // artwork, invalidation).  The redundant state notification was causing
        // evaluate_title_formats() and invalidate() to run a third time per track change.
    } catch (...) {}
}

void PlaybackStateManager::on_playback_stop(play_control::t_stop_reason p_reason) noexcept {
    try {
        ++m_stream_metadata_revision;
        // When starting another track (manual next/prev), don't notify UI of "stopped" state
        // The on_playback_new_track() callback will fire immediately after with correct state.
        // Notifying "stopped" here would cause the UI to clear artwork/caches unnecessarily,
        // causing a visual flash during track transitions.
        if (p_reason == play_control::stop_reason_starting_another) {
            return;
        }

        // Handle infinite playback before clearing state
        if (p_reason == play_control::stop_reason_eof && get_nowbar_infinite_playback_enabled()) {
            handle_infinite_playback();
        }

        clear_pending_online_artwork();

        m_state.is_playing = false;
        m_state.is_paused = false;
        m_state.playback_time = 0.0;
        m_state.track_length = 0.0;
        m_state.can_seek = false;
        m_state.current_track.release();  // Clear the track handle

        // Clear track info to reset to initial state
        m_state.track_title.reset();
        m_state.track_artist.reset();
        m_state.track_album.reset();

        notify_state_changed();
    } catch (...) {}
}

void PlaybackStateManager::on_playback_seek(double p_time) noexcept {
    try {
        m_state.playback_time = p_time;
        notify_time_changed(p_time);
    } catch (...) {}
}

void PlaybackStateManager::on_playback_pause(bool p_state) noexcept {
    try {
        m_state.is_paused = p_state;
        notify_state_changed();
    } catch (...) {}
}

void PlaybackStateManager::on_playback_time(double p_time) noexcept {
    try {
        // on_playback_time fires on the audio decoder thread.
        // Marshal to the main thread to avoid data races with UI paint().
        fb2k::inMainThread([p_time]() {
            if (!is_available()) return;
            auto& mgr = get();
            mgr.m_state.playback_time = p_time;
            mgr.notify_time_changed(p_time);
            mgr.check_preview_skip(p_time);
        });
    } catch (...) {}
}

void PlaybackStateManager::on_volume_change(float p_new_val) noexcept {
    try {
        m_state.volume_db = p_new_val;
        notify_volume_changed(p_new_val);
    } catch (...) {}
}

void PlaybackStateManager::on_playback_dynamic_info(const file_info& p_info) noexcept {
    try {
        // on_playback_dynamic_info fires on the audio decoder thread (e.g. VBR bitrate changes).
        // Marshal to main thread to update dynamic format evaluations.
        fb2k::inMainThread([]() {
            if (!is_available()) return;
            auto& mgr = get();
            mgr.notify_dynamic_info_changed();
        });
    } catch (...) {}
}

void PlaybackStateManager::on_playback_dynamic_info_track(const file_info& p_info) noexcept {
    try {
        const pfc::string8 previous_artist = m_state.track_artist;
        const pfc::string8 previous_title = m_state.track_title;
        const pfc::string8 previous_album = m_state.track_album;
        // Extract metadata from dynamic info (for streaming sources like internet radio)
        const char* title = nullptr;
        const char* artist = nullptr;

        // Standard metadata
        if (p_info.meta_exists("TITLE")) {
            title = p_info.meta_get("TITLE", 0);
        }
        if (p_info.meta_exists("ARTIST")) {
            artist = p_info.meta_get("ARTIST", 0);
        }

        // ICY metadata (common in internet radio)
        if (!title && p_info.meta_exists("STREAMTITLE")) {
            title = p_info.meta_get("STREAMTITLE", 0);
        }
        if (!title && p_info.meta_exists("ICY_TITLE")) {
            title = p_info.meta_get("ICY_TITLE", 0);
        }

        // Alternative artist fields
        if (!artist && p_info.meta_exists("ALBUMARTIST")) {
            artist = p_info.meta_get("ALBUMARTIST", 0);
        }
        if (!artist && p_info.meta_exists("PERFORMER")) {
            artist = p_info.meta_get("PERFORMER", 0);
        }

        bool changed = false;

        // If artist is missing, attempt to split "Artist - Title" from stream title
        if ((!artist || strlen(artist) == 0) && title && strlen(title) > 0) {
            std::string t_str = title;
            const std::string delimiters[] = { " - ", " ˗ ", " / ", " by " };
            for (const auto& delim : delimiters) {
                size_t pos = t_str.find(delim);
                if (pos != std::string::npos) {
                    m_state.track_artist = (delim == " by " ? t_str.substr(pos + delim.length()) : t_str.substr(0, pos)).c_str();
                    m_state.track_title = (delim == " by " ? t_str.substr(0, pos) : t_str.substr(pos + delim.length())).c_str();
                    changed = true;
                    break;
                }
            }
        }

        // Treat each native event as one song snapshot, not a patch to the old song.
        if (!changed && ((title && *title) || (artist && *artist))) {
            m_state.track_title = title ? title : "";
            m_state.track_artist = artist ? artist : "";
            changed = true;
        }

        // Fallback check for streams discovered via foo_artwork (e.g. ?azuracast_api / ?radioreg_api / ACRCloud)
        auto pc = playback_control::get();
        if (!changed && m_state.track_artist.is_empty() && m_state.track_title.is_empty() &&
            (pc->is_playing() || pc->is_paused())) {
            static service_ptr_t<titleformat_object> tf_fa_title, tf_fa_artist;
            if (!tf_fa_title.is_valid()) {
                titleformat_compiler::get()->compile_safe(tf_fa_title, "%foo_artwork_title%");
            }
            if (!tf_fa_artist.is_valid()) {
                titleformat_compiler::get()->compile_safe(tf_fa_artist, "%foo_artwork_artist%");
            }
            pfc::string8 fa_title, fa_artist;
            pc->playback_format_title(nullptr, fa_title, tf_fa_title, nullptr, playback_control::display_level_all);
            pc->playback_format_title(nullptr, fa_artist, tf_fa_artist, nullptr, playback_control::display_level_all);

            if (!fa_title.is_empty() && fa_title != "?" && !fa_artist.is_empty() && fa_artist != "?") {
                m_state.track_title = fa_title;
                m_state.track_artist = fa_artist;
                changed = true;
            }
        }

        if (changed) {
            const char* album = p_info.meta_get("ALBUM", 0);
            m_state.track_album = album ? album : "";
            if (m_state.track_artist == previous_artist && m_state.track_title == previous_title &&
                m_state.track_album == previous_album) return;

            if (m_state.track_artist != previous_artist || m_state.track_title != previous_title) {
                clear_pending_online_artwork();
            }

            // Leave the playback callback before notifying panels. Only the newest
            // queued song snapshot should refresh artwork, never restart the full
            // track-change workflow (waveform decoding, progress reset, etc.).
            const auto revision = ++m_stream_metadata_revision;
            const auto track = m_state.current_track;
            const auto song_artist = m_state.track_artist;
            const auto song_title = m_state.track_title;
            const auto song_album = m_state.track_album;
            fb2k::inMainThread([revision, track, song_artist, song_title, song_album]() {
                if (!is_available()) return;
                auto& mgr = get();
                if (mgr.m_stream_metadata_revision != revision ||
                    mgr.m_state.current_track != track || mgr.m_state.track_artist != song_artist ||
                    mgr.m_state.track_title != song_title || mgr.m_state.track_album != song_album) return;
                mgr.notify_track_info_changed();
            });
        }
    } catch (...) {}
}

void PlaybackStateManager::on_playback_edited(metadb_handle_ptr p_track) noexcept {
    try {
        if (!m_state.is_playing && !m_state.is_paused) return;
        if (!m_state.current_track.is_valid()) return;

        if (p_track.is_valid() && p_track == m_state.current_track) {
            update_track_info(p_track);
            notify_track_info_changed();
        }
    } catch (...) {}
}

void PlaybackStateManager::on_changed_sorted(metadb_handle_list_cref p_items_sorted, bool p_fromhook) {
    if (!m_state.is_playing && !m_state.is_paused) return;
    if (!m_state.current_track.is_valid()) return;

    if (metadb_handle_list_helper::bsearch_by_pointer(p_items_sorted, m_state.current_track) != pfc_infinite) {
        update_track_info(m_state.current_track);
        notify_track_info_changed();
    }
}

void PlaybackStateManager::update_track_info(metadb_handle_ptr p_track) {
    if (!p_track.is_valid()) return;
    
    // Get track info using new API
    metadb_info_container::ptr info_container = p_track->get_info_ref();
    if (info_container.is_valid()) {
        const file_info& info = info_container->info();
        
        // Title
        const char* title = info.meta_get("TITLE", 0);
        m_state.track_title = title ? title : pfc::string_filename_ext(p_track->get_path()).get_ptr();
        
        // Artist
        const char* artist = info.meta_get("ARTIST", 0);
        m_state.track_artist = artist ? artist : "";
        
        // Album
        const char* album = info.meta_get("ALBUM", 0);
        m_state.track_album = album ? album : "";
    }

    // Check %foo_artwork_title% and %foo_artwork_artist% for discovered stream metadata (e.g. ?azuracast_api / ?radioreg_api)
    auto pc = playback_control::get();
    if ((m_state.track_artist.is_empty() || m_state.track_title.is_empty()) &&
        (pc->is_playing() || pc->is_paused())) {
        static service_ptr_t<titleformat_object> tf_fa_title, tf_fa_artist;
        if (!tf_fa_title.is_valid()) {
            titleformat_compiler::get()->compile_safe(tf_fa_title, "%foo_artwork_title%");
        }
        if (!tf_fa_artist.is_valid()) {
            titleformat_compiler::get()->compile_safe(tf_fa_artist, "%foo_artwork_artist%");
        }
        pfc::string8 fa_title, fa_artist;
        pc->playback_format_title(nullptr, fa_title, tf_fa_title, nullptr, playback_control::display_level_all);
        pc->playback_format_title(nullptr, fa_artist, tf_fa_artist, nullptr, playback_control::display_level_all);

        if (!fa_title.is_empty() && fa_title != "?" && !fa_artist.is_empty() && fa_artist != "?") {
            m_state.track_title = fa_title;
            m_state.track_artist = fa_artist;
        }
    }
}

void PlaybackStateManager::notify_state_changed() {
    std::vector<IPlaybackStateCallback*> callbacks;
    PlaybackState state_snapshot;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        callbacks = m_callbacks;
        state_snapshot = m_state;
    }
    for (auto* cb : callbacks) {
        cb->on_playback_state_changed(state_snapshot);
    }
}

void PlaybackStateManager::notify_time_changed(double time) {
    std::vector<IPlaybackStateCallback*> callbacks;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        callbacks = m_callbacks;
    }
    for (auto* cb : callbacks) {
        cb->on_playback_time_changed(time);
    }
}

void PlaybackStateManager::notify_volume_changed(float volume) {
    std::vector<IPlaybackStateCallback*> callbacks;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        callbacks = m_callbacks;
    }
    for (auto* cb : callbacks) {
        cb->on_volume_changed(volume);
    }
}

void PlaybackStateManager::notify_track_changed() {
    std::vector<IPlaybackStateCallback*> callbacks;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        callbacks = m_callbacks;
    }
    for (auto* cb : callbacks) {
        cb->on_track_changed();
    }
}

void PlaybackStateManager::notify_track_info_changed() {
    std::vector<IPlaybackStateCallback*> callbacks;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        callbacks = m_callbacks;
    }
    for (auto* cb : callbacks) {
        cb->on_track_info_changed();
    }
}

void PlaybackStateManager::notify_dynamic_info_changed() {
    std::vector<IPlaybackStateCallback*> callbacks;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        callbacks = m_callbacks;
    }
    for (auto* cb : callbacks) {
        cb->on_dynamic_info_changed();
    }
}

// Callback to start playback on main thread
class InfinitePlaybackCallback : public main_thread_callback {
public:
    InfinitePlaybackCallback(t_size start_index) : m_start_index(start_index) {}

    void callback_run() override {
        auto pm = playlist_manager::get();
        t_size playlist = pm->get_active_playlist();
        if (playlist != pfc_infinite) {
            pm->playlist_set_focus_item(playlist, m_start_index);
            playback_control::get()->start();
        }
    }

private:
    t_size m_start_index;
};

void PlaybackStateManager::handle_infinite_playback() {
    auto pm = playlist_manager::get();
    auto pc = playback_control::get();

    // Debounce: if infinite playback was triggered less than 10 seconds ago, skip.
    // This prevents a feedback loop when the output device becomes unavailable
    // (e.g. Bluetooth headset disconnected) — playback fails immediately after
    // start(), triggering another EOF stop, which would re-trigger infinite playback.
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - m_last_infinite_playback_time).count();
    if (m_last_infinite_playback_time.time_since_epoch().count() != 0 && elapsed < 10) {
        return;
    }
    m_last_infinite_playback_time = now;

    // Don't add tracks if "stop after current" is enabled
    if (pc->get_stop_after_current()) return;

    t_size active_playlist = pm->get_active_playlist();
    if (active_playlist == pfc_infinite) return;

    // Check if playlist is an autoplaylist - if so, don't interfere
    try {
        auto apm = autoplaylist_manager::get();
        if (apm->is_client_present(active_playlist)) {
            return;  // This is an autoplaylist, skip infinite playback
        }
    } catch (...) {
        // autoplaylist_manager might not be available, continue
    }

    // Get the position where we'll insert new tracks
    t_size playlist_count = pm->playlist_get_item_count(active_playlist);
    t_size insert_position = playlist_count;

    // Collect genres from the last few tracks in the playlist (up to 10)
    std::set<pfc::string8> genres;
    t_size scan_count = std::min(playlist_count, (t_size)10);

    for (t_size i = 0; i < scan_count; i++) {
        t_size idx = playlist_count - 1 - i;
        metadb_handle_ptr track;
        if (pm->playlist_get_item_handle(track, active_playlist, idx) && track.is_valid()) {
            metadb_info_container::ptr info = track->get_info_ref();
            if (info.is_valid()) {
                const file_info& fi = info->info();
                // Check multiple genre fields
                for (t_size g = 0; g < fi.meta_get_count_by_name("genre"); g++) {
                    const char* genre = fi.meta_get("genre", g);
                    if (genre && strlen(genre) > 0) {
                        genres.insert(pfc::string8(genre));
                    }
                }
            }
        }
    }

    // Get all tracks from the library
    auto library = library_manager::get();
    pfc::list_t<metadb_handle_ptr> library_items;
    library->get_all_items(library_items);

    if (library_items.get_count() == 0) return;

    // Collect current playlist tracks to avoid duplicates
    std::set<pfc::string8> playlist_paths;
    for (t_size i = 0; i < playlist_count; i++) {
        metadb_handle_ptr track;
        if (pm->playlist_get_item_handle(track, active_playlist, i) && track.is_valid()) {
            playlist_paths.insert(pfc::string8(track->get_path()));
        }
    }

    // Find matching tracks from library
    pfc::list_t<metadb_handle_ptr> matching_tracks;
    pfc::list_t<metadb_handle_ptr> fallback_tracks;

    for (t_size i = 0; i < library_items.get_count(); i++) {
        metadb_handle_ptr track = library_items[i];
        if (!track.is_valid()) continue;

        // Skip if already in playlist
        if (playlist_paths.count(pfc::string8(track->get_path())) > 0) continue;

        metadb_info_container::ptr info = track->get_info_ref();
        if (!info.is_valid()) continue;

        const file_info& fi = info->info();

        // Check if track matches any of our collected genres
        bool matches_genre = false;
        if (!genres.empty()) {
            for (t_size g = 0; g < fi.meta_get_count_by_name("genre"); g++) {
                const char* genre = fi.meta_get("genre", g);
                if (genre && genres.count(pfc::string8(genre)) > 0) {
                    matches_genre = true;
                    break;
                }
            }
        }

        if (matches_genre) {
            matching_tracks.add_item(track);
        } else {
            fallback_tracks.add_item(track);
        }
    }

    // Select tracks to add (prefer matching, fall back to random)
    pfc::list_t<metadb_handle_ptr> tracks_to_add;
    const int tracks_to_select = 15;

    // First, add from matching tracks (randomized)
    if (matching_tracks.get_count() > 0) {
        // Shuffle matching tracks
        for (t_size i = matching_tracks.get_count() - 1; i > 0; i--) {
            std::uniform_int_distribution<t_size> dist(0, i);
            t_size j = dist(m_rng);
            if (i != j) {
                metadb_handle_ptr temp = matching_tracks[i];
                matching_tracks.replace_item(i, matching_tracks[j]);
                matching_tracks.replace_item(j, temp);
            }
        }

        for (t_size i = 0; i < matching_tracks.get_count() && tracks_to_add.get_count() < tracks_to_select; i++) {
            tracks_to_add.add_item(matching_tracks[i]);
        }
    }

    // If we don't have enough, add from fallback (random tracks)
    if (tracks_to_add.get_count() < tracks_to_select && fallback_tracks.get_count() > 0) {
        // Shuffle fallback tracks
        for (t_size i = fallback_tracks.get_count() - 1; i > 0; i--) {
            std::uniform_int_distribution<t_size> dist(0, i);
            t_size j = dist(m_rng);
            if (i != j) {
                metadb_handle_ptr temp = fallback_tracks[i];
                fallback_tracks.replace_item(i, fallback_tracks[j]);
                fallback_tracks.replace_item(j, temp);
            }
        }

        for (t_size i = 0; i < fallback_tracks.get_count() && tracks_to_add.get_count() < tracks_to_select; i++) {
            tracks_to_add.add_item(fallback_tracks[i]);
        }
    }

    if (tracks_to_add.get_count() == 0) return;

    // Add tracks to playlist
    bit_array_false selection;
    pm->playlist_insert_items(active_playlist, insert_position, tracks_to_add, selection);

    // Start playback from the first new track using main thread callback
    auto mtcm = main_thread_callback_manager::get();
    mtcm->add_callback(fb2k::service_new<InfinitePlaybackCallback>(insert_position));
}

// Callback to skip to next track on main thread
class PreviewSkipCallback : public main_thread_callback {
public:
    void callback_run() override {
        playback_control::get()->next();
    }
};

void PlaybackStateManager::check_preview_skip(double current_time) {
    // Skip if already triggered for this track
    if (m_preview_skip_triggered) return;

    // Get preview mode setting: 0=Off, 1=35%, 2=50%, 3=60sec
    int preview_mode = get_nowbar_preview_mode();
    if (preview_mode == 0) return;  // Preview disabled

    // Need valid track length for percentage modes
    double track_length = m_state.track_length;
    if (track_length <= 0.0 && preview_mode != 3) return;

    // Calculate preview threshold
    double preview_threshold = 0.0;
    switch (preview_mode) {
        case 1:  // 35% of track
            preview_threshold = track_length * 0.35;
            break;
        case 2:  // 50% of track
            preview_threshold = track_length * 0.50;
            break;
        case 3:  // 60 seconds
            preview_threshold = 60.0;
            break;
        default:
            return;
    }

    // For fixed duration, don't skip if track is shorter than the threshold
    // (let it play to the end naturally)
    if (preview_mode == 3 && track_length > 0.0 && track_length <= preview_threshold) {
        return;
    }

    // Check if we've reached the preview threshold
    if (current_time >= preview_threshold) {
        m_preview_skip_triggered = true;

        // Skip to next track using main thread callback
        auto mtcm = main_thread_callback_manager::get();
        mtcm->add_callback(fb2k::service_new<PreviewSkipCallback>());
    }
}

// Callback to skip to next track on main thread (for low rating skip)
class LowRatingSkipCallback : public main_thread_callback {
public:
    void callback_run() override {
        playback_control::get()->next();
    }
};

bool PlaybackStateManager::check_and_skip_low_rating(metadb_handle_ptr p_track) {
    // Check if skip low rating is enabled
    if (!get_nowbar_skip_low_rating_enabled()) {
        return false;
    }

    // Check if we've hit the consecutive skip limit (prevent infinite loops)
    const int MAX_CONSECUTIVE_SKIPS = 10;
    if (m_consecutive_rating_skips >= MAX_CONSECUTIVE_SKIPS) {
        // Reset counter and allow this track to play
        m_consecutive_rating_skips = 0;
        return false;
    }

    // Validate track handle
    if (!p_track.is_valid()) {
        return false;
    }

    // Get the rating threshold
    int threshold = get_nowbar_skip_low_rating_threshold();

    // Evaluate %rating% using title formatting
    // foo_playcount exposes rating through title formatting
    try {
        // Compile once and cache the titleformat object
        if (!m_rating_format.is_valid()) {
            titleformat_compiler::get()->compile(m_rating_format, "%rating%");
        }
        if (m_rating_format.is_valid()) {
            pfc::string8 rating_str;
            p_track->format_title(nullptr, rating_str, m_rating_format, nullptr);

            // rating_str will be "1", "2", "3", "4", "5", or empty (no rating)
            // Only skip if there's a valid rating that's at or below threshold
            if (!rating_str.is_empty()) {
                int rating = atoi(rating_str.c_str());
                if (rating >= 1 && rating <= threshold) {
                    // Increment consecutive skip counter
                    m_consecutive_rating_skips++;

                    // Skip to next track using main thread callback
                    auto mtcm = main_thread_callback_manager::get();
                    mtcm->add_callback(fb2k::service_new<LowRatingSkipCallback>());
                    return true;
                }
            }
        }
    } catch (...) {
        // If title formatting fails, don't skip
    }

    return false;
}

} // namespace nowbar

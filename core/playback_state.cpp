#include "pch.h"
#include "playback_state.h"

namespace nowbar {

PlaybackStateManager& PlaybackStateManager::get() {
    // Singleton - persists for component lifetime
    static PlaybackStateManager instance;
    return instance;
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

void PlaybackStateManager::on_playback_starting(play_control::t_track_command p_command, bool p_paused) {
    m_state.is_playing = true;
    m_state.is_paused = p_paused;
    notify_state_changed();
}

void PlaybackStateManager::on_playback_new_track(metadb_handle_ptr p_track) {
    auto pc = playback_control::get();
    m_state.is_playing = true;
    m_state.is_paused = pc->is_paused();
    m_state.playback_time = 0.0;
    m_state.track_length = pc->playback_get_length();
    m_state.can_seek = pc->playback_can_seek();
    m_state.current_track = p_track;  // Store the track handle
    
    update_track_info(p_track);
    notify_track_changed();
    notify_state_changed();
}

void PlaybackStateManager::on_playback_stop(play_control::t_stop_reason p_reason) {
    m_state.is_playing = false;
    m_state.is_paused = false;
    m_state.playback_time = 0.0;
    m_state.track_length = 0.0;
    m_state.can_seek = false;
    m_state.current_track.release();  // Clear the track handle
    notify_state_changed();
}

void PlaybackStateManager::on_playback_seek(double p_time) {
    m_state.playback_time = p_time;
    notify_time_changed(p_time);
}

void PlaybackStateManager::on_playback_pause(bool p_state) {
    m_state.is_paused = p_state;
    notify_state_changed();
}

void PlaybackStateManager::on_playback_time(double p_time) {
    m_state.playback_time = p_time;
    notify_time_changed(p_time);
}

void PlaybackStateManager::on_volume_change(float p_new_val) {
    m_state.volume_db = p_new_val;
    notify_volume_changed(p_new_val);
}

void PlaybackStateManager::on_playback_dynamic_info_track(const file_info& p_info) {
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
    
    // Update state if we found meaningful metadata
    bool changed = false;
    if (title && strlen(title) > 0) {
        m_state.track_title = title;
        changed = true;
    }
    if (artist && strlen(artist) > 0) {
        m_state.track_artist = artist;
        changed = true;
    }
    
    if (changed) {
        notify_track_changed();
        notify_state_changed();
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
}

void PlaybackStateManager::notify_state_changed() {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto* cb : m_callbacks) {
        cb->on_playback_state_changed(m_state);
    }
}

void PlaybackStateManager::notify_time_changed(double time) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto* cb : m_callbacks) {
        cb->on_playback_time_changed(time);
    }
}

void PlaybackStateManager::notify_volume_changed(float volume) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto* cb : m_callbacks) {
        cb->on_volume_changed(volume);
    }
}

void PlaybackStateManager::notify_track_changed() {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto* cb : m_callbacks) {
        cb->on_track_changed();
    }
}

} // namespace nowbar

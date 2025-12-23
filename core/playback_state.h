#pragma once
#include "pch.h"

namespace nowbar {

// Playback state structure
struct PlaybackState {
    bool is_playing = false;
    bool is_paused = false;
    double playback_time = 0.0;
    double track_length = 0.0;
    bool can_seek = false;
    
    // Playback order
    int playback_order = 0;  // 0=default, 1=repeat_playlist, 2=repeat_track, 3=random, 4=shuffle
    
    // Volume
    float volume_db = 0.0f;  // in dB, 0 = max, -100 = min
    bool is_muted = false;
    
    // Track info
    pfc::string8 track_title;
    pfc::string8 track_artist;
    pfc::string8 track_album;
};

// Callback interface for playback state changes
class IPlaybackStateCallback {
public:
    virtual ~IPlaybackStateCallback() = default;
    virtual void on_playback_state_changed(const PlaybackState& state) = 0;
    virtual void on_playback_time_changed(double time) = 0;
    virtual void on_volume_changed(float volume_db) = 0;
    virtual void on_track_changed() = 0;
};

// Playback state manager - uses play_callback_impl_base which self-registers
class PlaybackStateManager : private play_callback_impl_base {
public:
    static PlaybackStateManager& get();
    
    void register_callback(IPlaybackStateCallback* cb);
    void unregister_callback(IPlaybackStateCallback* cb);
    
    const PlaybackState& get_state() const { return m_state; }
    
private:
    PlaybackStateManager();
    ~PlaybackStateManager() = default;
    
    // play_callback overrides
    void on_playback_starting(play_control::t_track_command p_command, bool p_paused) override;
    void on_playback_new_track(metadb_handle_ptr p_track) override;
    void on_playback_stop(play_control::t_stop_reason p_reason) override;
    void on_playback_seek(double p_time) override;
    void on_playback_pause(bool p_state) override;
    void on_playback_time(double p_time) override;
    void on_volume_change(float p_new_val) override;
    void on_playback_edited(metadb_handle_ptr p_track) override {}
    void on_playback_dynamic_info(const file_info& p_info) override {}
    void on_playback_dynamic_info_track(const file_info& p_info) override;
    
    void update_track_info(metadb_handle_ptr p_track);
    void notify_state_changed();
    void notify_time_changed(double time);
    void notify_volume_changed(float volume);
    void notify_track_changed();
    
    PlaybackState m_state;
    std::vector<IPlaybackStateCallback*> m_callbacks;
    mutable std::mutex m_mutex;
};

} // namespace nowbar

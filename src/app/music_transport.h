#pragma once

#include <stdint.h>

enum MusicTransportState : uint8_t {
    MUSIC_TRANSPORT_STOPPED,
    MUSIC_TRANSPORT_PLAYING,
    MUSIC_TRANSPORT_PAUSED,
};

enum MusicTransportCommand : uint8_t {
    MUSIC_TRANSPORT_PLAY_PAUSE,
    MUSIC_TRANSPORT_NEXT,
    MUSIC_TRANSPORT_PREVIOUS,
    MUSIC_TRANSPORT_STOP,
    MUSIC_TRANSPORT_COMPLETE,
    MUSIC_TRANSPORT_FAILURE,
};

enum MusicTransportEffect : uint8_t {
    MUSIC_TRANSPORT_NO_EFFECT,
    MUSIC_TRANSPORT_OPEN_TRACK,
    MUSIC_TRANSPORT_CLOSE_TRACK,
};

struct MusicTransportResult {
    MusicTransportEffect effect;
    uint8_t track_index;
};

class MusicTransport {
public:
    MusicTransport();

    MusicTransportResult apply(MusicTransportCommand command, uint8_t track_count);

    MusicTransportState state() const { return state_; }
    uint8_t track_index() const { return track_index_; }
    bool has_current_track() const { return state_ != MUSIC_TRANSPORT_STOPPED; }

private:
    MusicTransportState state_;
    uint8_t track_index_;
};
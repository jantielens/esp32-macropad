#include "music_transport.h"

MusicTransport::MusicTransport()
    : state_(MUSIC_TRANSPORT_STOPPED), track_index_(0) {}

MusicTransportResult MusicTransport::apply(MusicTransportCommand command, uint8_t track_count) {
    MusicTransportResult result = {MUSIC_TRANSPORT_NO_EFFECT, track_index_};

    if (state_ == MUSIC_TRANSPORT_STOPPED) {
        if (command == MUSIC_TRANSPORT_PLAY_PAUSE && track_count > 0) {
            track_index_ = 0;
            state_ = MUSIC_TRANSPORT_PLAYING;
            result.effect = MUSIC_TRANSPORT_OPEN_TRACK;
            result.track_index = track_index_;
        }
        return result;
    }

    if (command == MUSIC_TRANSPORT_PLAY_PAUSE) {
        state_ = state_ == MUSIC_TRANSPORT_PLAYING ? MUSIC_TRANSPORT_PAUSED
                                                    : MUSIC_TRANSPORT_PLAYING;
        return result;
    }

    if (command == MUSIC_TRANSPORT_STOP || command == MUSIC_TRANSPORT_FAILURE ||
        command == MUSIC_TRANSPORT_COMPLETE) {
        if (command == MUSIC_TRANSPORT_COMPLETE && state_ == MUSIC_TRANSPORT_PAUSED) return result;
        if (command == MUSIC_TRANSPORT_COMPLETE && track_index_ + 1 < track_count) {
            result.effect = MUSIC_TRANSPORT_CLOSE_TRACK;
            ++track_index_;
            state_ = MUSIC_TRANSPORT_PLAYING;
            result.track_index = track_index_;
            return result;
        }
        state_ = MUSIC_TRANSPORT_STOPPED;
        track_index_ = 0;
        result.effect = MUSIC_TRANSPORT_CLOSE_TRACK;
        result.track_index = 0;
        return result;
    }

    if (command == MUSIC_TRANSPORT_NEXT) {
        result.effect = MUSIC_TRANSPORT_CLOSE_TRACK;
        if (track_index_ + 1 < track_count) {
            ++track_index_;
            state_ = MUSIC_TRANSPORT_PLAYING;
            result.track_index = track_index_;
        } else {
            state_ = MUSIC_TRANSPORT_STOPPED;
            track_index_ = 0;
            result.track_index = 0;
        }
        return result;
    }

    if (command == MUSIC_TRANSPORT_PREVIOUS) {
        result.effect = MUSIC_TRANSPORT_CLOSE_TRACK;
        if (track_index_ > 0) --track_index_;
        state_ = MUSIC_TRANSPORT_PLAYING;
        result.track_index = track_index_;
    }
    return result;
}
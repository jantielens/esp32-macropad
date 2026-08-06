#include <cstdio>
#include <cstdlib>

#include "music_transport.h"

static void check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

int main() {
    MusicTransport transport;

    check(transport.apply(MUSIC_TRANSPORT_NEXT, 2).effect == MUSIC_TRANSPORT_NO_EFFECT,
          "stopped next must be a no-op");
    check(transport.apply(MUSIC_TRANSPORT_PREVIOUS, 2).effect == MUSIC_TRANSPORT_NO_EFFECT,
          "stopped previous must be a no-op");
    check(transport.apply(MUSIC_TRANSPORT_STOP, 2).effect == MUSIC_TRANSPORT_NO_EFFECT,
          "stopped stop must be a no-op");
    check(transport.apply(MUSIC_TRANSPORT_PLAY_PAUSE, 0).effect == MUSIC_TRANSPORT_NO_EFFECT,
          "empty CD must not open a track");

    MusicTransportResult result = transport.apply(MUSIC_TRANSPORT_PLAY_PAUSE, 2);
    check(result.effect == MUSIC_TRANSPORT_OPEN_TRACK && result.track_index == 0 &&
              transport.state() == MUSIC_TRANSPORT_PLAYING,
          "play must start track zero");
    transport.apply(MUSIC_TRANSPORT_PLAY_PAUSE, 2);
    check(transport.state() == MUSIC_TRANSPORT_PAUSED, "play/pause must pause");
    transport.apply(MUSIC_TRANSPORT_PLAY_PAUSE, 2);
    check(transport.state() == MUSIC_TRANSPORT_PLAYING, "play/pause must resume");

    result = transport.apply(MUSIC_TRANSPORT_PREVIOUS, 2);
    check(result.effect == MUSIC_TRANSPORT_CLOSE_TRACK && result.track_index == 0 &&
              transport.state() == MUSIC_TRANSPORT_PLAYING,
          "previous at track zero must restart it");
    result = transport.apply(MUSIC_TRANSPORT_NEXT, 2);
    check(result.track_index == 1 && transport.state() == MUSIC_TRANSPORT_PLAYING,
          "next must advance to the next track");
    result = transport.apply(MUSIC_TRANSPORT_COMPLETE, 2);
    check(result.effect == MUSIC_TRANSPORT_CLOSE_TRACK && transport.state() == MUSIC_TRANSPORT_STOPPED &&
              transport.track_index() == 0,
          "final completion must stop at home without wrap");

    transport.apply(MUSIC_TRANSPORT_PLAY_PAUSE, 2);
    result = transport.apply(MUSIC_TRANSPORT_FAILURE, 2);
    check(result.effect == MUSIC_TRANSPORT_CLOSE_TRACK && transport.state() == MUSIC_TRANSPORT_STOPPED &&
              transport.track_index() == 0,
          "failure must stop at home for later recovery");
    check(transport.apply(MUSIC_TRANSPORT_PLAY_PAUSE, 2).track_index == 0,
          "later play must recover from track zero");

    std::puts("music transport checks passed");
    return 0;
}
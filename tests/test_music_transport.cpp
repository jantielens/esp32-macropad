#include <gtest/gtest.h>

#include "music_transport.h"

TEST(MusicTransport, AppliesStateTransitionsAndRecoversFromFailures) {
    MusicTransport transport;

    EXPECT_EQ(transport.apply(MUSIC_TRANSPORT_NEXT, 2).effect, MUSIC_TRANSPORT_NO_EFFECT);
    EXPECT_EQ(transport.apply(MUSIC_TRANSPORT_PREVIOUS, 2).effect, MUSIC_TRANSPORT_NO_EFFECT);
    EXPECT_EQ(transport.apply(MUSIC_TRANSPORT_STOP, 2).effect, MUSIC_TRANSPORT_NO_EFFECT);
    EXPECT_EQ(transport.apply(MUSIC_TRANSPORT_PLAY_PAUSE, 0).effect, MUSIC_TRANSPORT_NO_EFFECT);

    MusicTransportResult result = transport.apply(MUSIC_TRANSPORT_PLAY_PAUSE, 2);
      EXPECT_EQ(result.effect, MUSIC_TRANSPORT_OPEN_TRACK);
      EXPECT_EQ(result.track_index, 0);
      EXPECT_EQ(transport.state(), MUSIC_TRANSPORT_PLAYING);
    transport.apply(MUSIC_TRANSPORT_PLAY_PAUSE, 2);
      EXPECT_EQ(transport.state(), MUSIC_TRANSPORT_PAUSED);
    transport.apply(MUSIC_TRANSPORT_PLAY_PAUSE, 2);
      EXPECT_EQ(transport.state(), MUSIC_TRANSPORT_PLAYING);

    result = transport.apply(MUSIC_TRANSPORT_PREVIOUS, 2);
      EXPECT_EQ(result.effect, MUSIC_TRANSPORT_CLOSE_TRACK);
      EXPECT_EQ(result.track_index, 0);
      EXPECT_EQ(transport.state(), MUSIC_TRANSPORT_PLAYING);
    result = transport.apply(MUSIC_TRANSPORT_NEXT, 2);
      EXPECT_EQ(result.track_index, 1);
      EXPECT_EQ(transport.state(), MUSIC_TRANSPORT_PLAYING);
    result = transport.apply(MUSIC_TRANSPORT_COMPLETE, 2);
      EXPECT_EQ(result.effect, MUSIC_TRANSPORT_CLOSE_TRACK);
      EXPECT_EQ(transport.state(), MUSIC_TRANSPORT_STOPPED);
      EXPECT_EQ(transport.track_index(), 0);

    transport.apply(MUSIC_TRANSPORT_PLAY_PAUSE, 2);
    result = transport.apply(MUSIC_TRANSPORT_FAILURE, 2);
      EXPECT_EQ(result.effect, MUSIC_TRANSPORT_CLOSE_TRACK);
      EXPECT_EQ(transport.state(), MUSIC_TRANSPORT_STOPPED);
      EXPECT_EQ(transport.track_index(), 0);
      EXPECT_EQ(transport.apply(MUSIC_TRANSPORT_PLAY_PAUSE, 2).track_index, 0);
}
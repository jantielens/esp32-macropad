#pragma once

#include "board_config.h"

#if HAS_MUSIC_ANALYSIS && HAS_AUDIO && HAS_SOUND_PLAYER

#include <stddef.h>
#include <stdint.h>

struct PadConfig;

// Demand flags are rebuilt from configured [music:analysis.*] consumers.
enum MusicAnalysisDemand : uint8_t {
    MUSIC_ANALYSIS_NONE = 0,
    MUSIC_ANALYSIS_RMS = 1 << 0,
    MUSIC_ANALYSIS_PEAK = 1 << 1,
    MUSIC_ANALYSIS_BANDS = 1 << 2,
};

struct MusicAnalysisSnapshot {
    uint8_t rms;       // 0..100, pre-volume full-scale PCM
    uint8_t peak;      // 0..100, pre-volume full-scale PCM
    uint8_t bands[8];  // 0..100, fixed logarithmic visualizer bands
    bool playing;
};

void music_analysis_init();
void music_analysis_set_demand(uint8_t demand);
uint8_t music_analysis_get_demand();
void music_analysis_set_playing(bool playing);
void music_analysis_reset();
void music_analysis_process(const int16_t* stereo_frames, size_t frame_count);
void music_analysis_get_snapshot(MusicAnalysisSnapshot* out);
void music_analysis_collect_demand(const char* templ, uint8_t* demand);
void music_analysis_collect_pad_demand(const PadConfig* config, uint8_t* demand);
void music_analysis_rebuild_demand();

#endif

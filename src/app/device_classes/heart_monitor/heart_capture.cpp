#include "heart_capture.h"

#include <stdio.h>
#include <string.h>

namespace heart_monitor {

const char* state_name(State state) {
    switch (state) {
        case State::Idle: return "idle";
        case State::Recording: return "recording";
        case State::AwaitingSave: return "awaiting_save";
        case State::Saving: return "saving";
        case State::Saved: return "saved";
        case State::Error: return "error";
    }
    return "error";
}

void Recorder::set_error(const char* error) {
    snprintf(snapshot_.error, sizeof(snapshot_.error), "%s", error);
    snapshot_.state = State::Error;
    snapshot_.live = BpmResult();
}

void Recorder::release() {
    if (capture_.samples) io_.free_samples(capture_.samples);
    capture_.samples = nullptr;
    snapshot_.retained = false;
}

bool Recorder::command(Command command, uint32_t now_ms, uint32_t now_us, bool ota) {
    if (command == Command::Stop) {
        if (snapshot_.state != State::Recording) return false;
        finish("manual_stop", false, now_ms);
        return true;
    }
    if (ota || snapshot_.state == State::Recording || snapshot_.state == State::Saving ||
        snapshot_.state == State::AwaitingSave) return false;
    if (command == Command::RetrySave) {
        if (!snapshot_.retained) return false;
        snapshot_.state = State::AwaitingSave;
        return true;
    }
    if (command == Command::Discard) {
        release();
        snapshot_.state = State::Idle;
        snapshot_.error[0] = '\0';
        snapshot_.elapsed_ms = 0;
        snapshot_.samples = 0;
        snapshot_.live = BpmResult();
        return true;
    }
    if (command != Command::Start || snapshot_.retained) return false;
    snapshot_.error[0] = '\0';
    snapshot_.samples = 0;
    snapshot_.elapsed_ms = 0;
    snapshot_.live = BpmResult();
    estimator_.reset(sample_rate_hz);
    last_beat_ms_ = 0;
    if (!io_.storage_available(max_samples)) {
        set_error("storage_unavailable_or_full");
        return false;
    }
    capture_ = Capture{};
    capture_.samples = io_.allocate_samples(max_samples);
    if (!capture_.samples) {
        set_error("psram_allocation_failed");
        return false;
    }
    capture_.started_ms = now_ms;
    capture_.started_us = now_us;
    const char* error = io_.start_sensor(capture_);
    if (error) {
        io_.stop_sensor();
        release();
        set_error(error);
        return false;
    }
    snapshot_.state = State::Recording;
    snapshot_.retained = true;
    return true;
}

void Recorder::finish(const char* reason, bool interrupted, uint32_t now_ms) {
    snapshot_.live = BpmResult();
    if (!io_.stop_sensor()) {
        reason = "sensor_shutdown_failed";
        interrupted = true;
    }
    capture_.duration_ms = now_ms - capture_.started_ms;
    capture_.stop_reason = reason;
    capture_.interrupted = interrupted;
    snapshot_.elapsed_ms = capture_.duration_ms;
    if (interrupted) snprintf(snapshot_.error, sizeof(snapshot_.error), "%s", reason);
    snapshot_.state = State::AwaitingSave;
}

void Recorder::tick(uint32_t now_ms, uint32_t now_us, bool ota) {
    if (snapshot_.state == State::Recording) {
        snapshot_.elapsed_ms = now_ms - capture_.started_ms;
        if (ota) {
            finish("ota_interrupted", true, now_ms);
            return;
        }
        if (snapshot_.elapsed_ms >= max_duration_ms) {
            finish("duration_limit", false, now_ms);
            return;
        }
        RawSample batch[31] = {};
        size_t count = 0;
        const char* error = io_.read_sensor(batch, count, capture_);
        if (error) {
            finish(error, true, now_ms);
            return;
        }
        if (count > 31 || count > max_samples - capture_.count) {
            finish("buffer_capacity", true, now_ms);
            return;
        }
        for (size_t index = 0; index < count; ++index) {
            batch[index].read_elapsed_us = now_us - capture_.started_us;
            const uint32_t previous_beats = estimator_.result().beat_count;
            estimator_.push(batch[index].ir, batch[index].red);
            snapshot_.live = estimator_.result();
            Sample& stored = capture_.samples[capture_.count++];
            stored.red = batch[index].red;
            stored.ir = batch[index].ir;
            stored.read_elapsed_us = batch[index].read_elapsed_us;
            stored.live = snapshot_.live;
            stored.beat_interval_valid = false;
            if (snapshot_.live.beat_detected) {
                Beat beat;
                if (estimator_.beat_at(estimator_.beat_history_count() - 1, beat))
                    stored.beat_interval_valid = beat.ibi_valid;
            }
            if (snapshot_.live.beat_count != previous_beats) last_beat_ms_ = now_ms;
        }
        snapshot_.samples = static_cast<uint32_t>(capture_.count);
        snapshot_.live.pulse = snapshot_.live.pulse && now_ms - last_beat_ms_ < 200;
        if (capture_.count == max_samples) finish("sample_limit", false, now_ms);
        return;
    }
    if (snapshot_.state != State::AwaitingSave || ota) return;
    snapshot_.state = State::Saving;
    char filename[sizeof(snapshot_.filename)] = {};
    const char* error = io_.save(capture_, filename, sizeof(filename));
    if (error) {
        set_error(error);
        return;
    }
    snprintf(snapshot_.filename, sizeof(snapshot_.filename), "%s", filename);
    snapshot_.error[0] = '\0';
    if (capture_.interrupted) {
        snprintf(snapshot_.error, sizeof(snapshot_.error), "%s", capture_.stop_reason);
    }
    snapshot_.state = capture_.interrupted ? State::Error : State::Saved;
    release();
}

bool write_csv(const Capture& capture, CsvWrite write, void* context) {
    char line[768];
    int size = snprintf(line, sizeof(line),
        "# format,heart_capture_v1\n"
        "# channels,red,ir\n"
        "# sensor_address,0x57\n# part_id,0x15\n# revision,%u\n"
        "# sensor_sample_rate_hz,%lu\n# sample_average,1\n# effective_sample_rate_hz,%lu\n"
        "# pulse_width_us,%u\n# adc_range_na,%u\n# led_red_code,%u\n# led_ir_code,%u\n"
        "# fifo_rollover,false\n# unix_time_s,%lld\n# started_uptime_ms,%lu\n"
        "# nominal_time_basis,stored_sample_index_not_hardware_sequence\n"
        "# read_elapsed_us_basis,host_fifo_poll_not_sample_timestamp\n"
        "sample_index,nominal_time_us,read_elapsed_us,red,ir\n",
        capture.revision, static_cast<unsigned long>(sample_rate_hz),
        static_cast<unsigned long>(sample_rate_hz), pulse_width_us, adc_range_na,
        led_amplitude, led_amplitude, static_cast<long long>(capture.unix_time_s),
        static_cast<unsigned long>(capture.started_ms));
    if (size < 0 || static_cast<size_t>(size) >= sizeof(line) ||
        write(context, line, size) != static_cast<size_t>(size)) return false;
    for (size_t index = 0; index < capture.count; ++index) {
        const Sample& sample = capture.samples[index];
        size = snprintf(line, sizeof(line), "%lu,%lu,%lu,%lu,%lu\n",
            static_cast<unsigned long>(index), static_cast<unsigned long>(index * (1000000 / sample_rate_hz)),
            static_cast<unsigned long>(sample.read_elapsed_us), static_cast<unsigned long>(sample.red),
            static_cast<unsigned long>(sample.ir));
        if (size < 0 || static_cast<size_t>(size) >= sizeof(line) ||
            write(context, line, size) != static_cast<size_t>(size)) return false;
    }
    size = snprintf(line, sizeof(line),
        "# duration_ms,%lu\n# samples,%lu\n# max_poll_gap_us,%lu\n"
        "# fifo_overflow_counter,%u\n# ambient_overflow_events,%lu\n"
        "# lost_samples,unknown_if_interrupted\n# stop_reason,%s\n"
        "# capture_status,%s\n# export_complete,true\n",
        static_cast<unsigned long>(capture.duration_ms), static_cast<unsigned long>(capture.count),
        static_cast<unsigned long>(capture.max_poll_gap_us), capture.overflow_counter,
        static_cast<unsigned long>(capture.ambient_overflow_events), capture.stop_reason,
        capture.interrupted ? "interrupted" : "complete");
    return size >= 0 && static_cast<size_t>(size) < sizeof(line) &&
        write(context, line, size) == static_cast<size_t>(size);
}

}
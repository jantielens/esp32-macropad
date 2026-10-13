#include "heart_recorder.h"

#if IS_HEART_MONITOR

#include "storage.h"
#include "heart_catalog.h"
#include "ota_activity.h"
#include "log_manager.h"
#include "version.h"
#include <MAX30105.h>
#include <Wire.h>
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <esp_rom_crc.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <time.h>

#ifndef HEART_I2C_SDA
#define HEART_I2C_SDA 52
#endif
#ifndef HEART_I2C_SCL
#define HEART_I2C_SCL 51
#endif
#ifndef HEART_I2C_FREQUENCY
#define HEART_I2C_FREQUENCY 400000
#endif

namespace heart_monitor {
namespace {

constexpr uint8_t sensor_address = 0x57;
constexpr uint8_t status_register = 0x00;
constexpr uint8_t fifo_pointer_register = 0x04;
constexpr uint8_t fifo_data_register = 0x07;
constexpr uint8_t fifo_config_register = 0x08;
constexpr uint8_t mode_register = 0x09;
constexpr uint8_t identity_register = 0xfe;
constexpr uint32_t poll_limit_us = 250000;

SemaphoreHandle_t recorder_mutex = nullptr;
QueueHandle_t command_queue = nullptr;
Snapshot published;
bool command_pending = false;
bool worker_ready = false;

void publish_snapshot();

bool read_registers(uint8_t address, uint8_t* data, size_t count) {
    Wire1.beginTransmission(sensor_address);
    if (Wire1.write(address) != 1 || Wire1.endTransmission(false) != 0) return false;
    if (Wire1.requestFrom(sensor_address, count) != count) {
        while (Wire1.available()) Wire1.read();
        return false;
    }
    for (size_t index = 0; index < count; ++index) data[index] = Wire1.read();
    return true;
}

bool write_register(uint8_t address, uint8_t value) {
    Wire1.beginTransmission(sensor_address);
    if (Wire1.write(address) != 1 || Wire1.write(value) != 1) return false;
    return Wire1.endTransmission() == 0;
}

class SensorStorageIO : public CaptureIO {
public:
    Sample* allocate_samples(size_t count) override {
        return static_cast<Sample*>(heap_caps_malloc(count * sizeof(Sample), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    }
    void free_samples(Sample* samples) override { heap_caps_free(samples); }
    bool storage_available(size_t sample_count) override {
        if (!storage_mount()) return false;
        const uint64_t total = Storage.totalBytes();
        const uint64_t used = Storage.usedBytes();
        const uint64_t free_bytes = total >= used ? total - used : 0;
        const uint64_t required = capture_storage_required(sample_count);
        if (free_bytes >= required) return true;
        LOGW("Heart", "Insufficient capture storage: free=%llu required=%llu samples=%lu",
             static_cast<unsigned long long>(free_bytes), static_cast<unsigned long long>(required),
             static_cast<unsigned long>(sample_count));
        return false;
    }
    const char* start_sensor(Capture& capture) override {
        csv_rollback_pending_ = false;
        bool reserved = false;
        for (unsigned attempt = 0; attempt < 8 && !reserved; ++attempt) {
            snprintf(capture.session_id, sizeof(capture.session_id), "%08lx",
                     static_cast<unsigned long>(esp_random()));
            reserved = true;
            const char* suffixes[] = {"csv", "json", "csv.part", "json.part"};
            for (const char* suffix : suffixes) {
                char path[104];
                snprintf(path, sizeof(path), "/storage/heart/capture-%s.%s", capture.session_id, suffix);
                if (Storage.exists(path)) reserved = false;
            }
        }
        if (!reserved) return "filename_collision";
        if (!Wire1.begin(HEART_I2C_SDA, HEART_I2C_SCL, HEART_I2C_FREQUENCY)) return "i2c_init_failed";
        Wire1.setTimeOut(20);
        uint8_t identity[2] = {};
        if (!read_registers(identity_register, identity, sizeof(identity))) {
            LOGW("Heart", "Sensor identity read failed at 0x57 (SDA=%d SCL=%d)", HEART_I2C_SDA, HEART_I2C_SCL);
            return "sensor_missing_or_wrong_id";
        }
        if (identity[1] != 0x15) {
            LOGW("Heart", "Unexpected sensor part ID: 0x%02x (revision 0x%02x)", identity[1], identity[0]);
            return "sensor_missing_or_wrong_id";
        }
        if (!sensor_.begin(Wire1, HEART_I2C_FREQUENCY, sensor_address)) return "sensor_init_failed";
        sensor_.setup(led_amplitude, 1, 2, sample_rate_hz, pulse_width_us, adc_range_na);
        if (!write_register(mode_register, 0x83)) return "sensor_config_failed";
        if (!write_register(fifo_config_register, 0x00)) return "sensor_config_failed";
        sensor_.clearFIFO();
        uint8_t config[8] = {};
        if (!read_registers(fifo_config_register, config, sizeof(config))) return "sensor_config_failed";
        if (config[0] != 0x00 || config[1] != 0x83 || config[2] != 0x27 ||
            config[4] != led_amplitude || config[5] != led_amplitude) {
            LOGW("Heart", "Sensor config mismatch: FIFO=0x%02x mode=0x%02x SPO2=0x%02x red=0x%02x IR=0x%02x",
                 config[0], config[1], config[2], config[4], config[5]);
            return "sensor_config_failed";
        }
        uint8_t pointers[3] = {};
        if (!read_registers(fifo_pointer_register, pointers, sizeof(pointers)) ||
            pointers[0] || pointers[1] || pointers[2]) return "fifo_reset_failed";
        uint8_t status[2];
        if (!read_registers(status_register, status, sizeof(status))) return "i2c_read_failed";
        capture.revision = identity[0];
        const time_t unix_time = time(nullptr);
        capture.unix_time_s = unix_time >= 1700000000 ? unix_time : 0;
        capture.started_ms = millis();
        capture.started_us = micros();
        last_poll_us_ = capture.started_us;
        last_sample_ms_ = capture.started_ms;
        return write_register(mode_register, 0x03) ? nullptr : "sensor_start_failed";
    }
    const char* read_sensor(RawSample* batch, size_t& count, Capture& capture) override {
        count = 0;
        const uint32_t now_us = micros();
        const uint32_t gap = now_us - last_poll_us_;
        last_poll_us_ = now_us;
        if (gap > capture.max_poll_gap_us) capture.max_poll_gap_us = gap;
        uint8_t pointers[3] = {};
        uint8_t status[2] = {};
        if (!read_registers(fifo_pointer_register, pointers, sizeof(pointers)) ||
            !read_registers(status_register, status, sizeof(status))) return "i2c_read_failed";
        capture.overflow_counter = pointers[1] & 0x1f;
        if (status[0] & 0x20) ++capture.ambient_overflow_events;
        if (status[0] & 0x01) return "sensor_reset";
        if (capture.overflow_counter || (status[0] & 0x80)) return "fifo_overflow_or_full";
        if (gap >= poll_limit_us) return "fifo_service_gap";
        const size_t available = ((pointers[0] & 0x1f) + 32 - (pointers[2] & 0x1f)) % 32;
        while (count < available) {
            const size_t remaining = available - count;
            const size_t records = remaining < 5 ? remaining : 5;
            uint8_t bytes[30];
            if (!read_registers(fifo_data_register, bytes, records * 6)) return "fifo_short_read";
            for (size_t index = 0; index < records; ++index) {
                const uint8_t* source = bytes + index * 6;
                batch[count].red = ((uint32_t(source[0]) << 16) | (uint32_t(source[1]) << 8) | source[2]) & 0x3ffff;
                batch[count].ir = ((uint32_t(source[3]) << 16) | (uint32_t(source[4]) << 8) | source[5]) & 0x3ffff;
                ++count;
            }
        }
        if (count) last_sample_ms_ = millis();
        return millis() - last_sample_ms_ >= 1000 ? "sensor_no_samples" : nullptr;
    }
    bool stop_sensor() override { return write_register(mode_register, 0x83); }
    const char* save(const Capture& capture, char* filename, size_t size) override;
private:
    MAX30105 sensor_;
    uint32_t last_poll_us_ = 0;
    uint32_t last_sample_ms_ = 0;
    bool csv_rollback_pending_ = false;
};

SensorStorageIO sensor_storage;
Recorder recorder(sensor_storage);

void publish_snapshot() {
    xSemaphoreTake(recorder_mutex, portMAX_DELAY);
    published = recorder.snapshot();
    xSemaphoreGive(recorder_mutex);
}

struct ExportContext {
    File* file;
    uint32_t writes = 0;
    uint32_t crc32 = 0;
};

size_t export_write(void* context, const char* bytes, size_t size) {
    ExportContext& output = *static_cast<ExportContext*>(context);
    if (ota_activity_is_active()) return 0;
    const size_t written = output.file->write(reinterpret_cast<const uint8_t*>(bytes), size);
    output.crc32 = esp_rom_crc32_le(output.crc32, reinterpret_cast<const uint8_t*>(bytes), written);
    if (++output.writes % 32 == 0) vTaskDelay(1);
    return written;
}

bool export_check(void*) {
    if (ota_activity_is_active()) return false;
    vTaskDelay(1);
    return true;
}

bool verify_export(const char* path, size_t bytes, uint32_t expected_crc32) {
    File file = Storage.open(path, FILE_READ);
    if (!file || file.size() != bytes) {
        file.close();
        return false;
    }
    uint8_t buffer[512];
    uint32_t crc32 = 0;
    size_t read_bytes = 0;
    unsigned reads = 0;
    while (read_bytes < bytes) {
        if (ota_activity_is_active()) {
            file.close();
            return false;
        }
        const size_t count = file.read(buffer, sizeof(buffer));
        if (!count) break;
        read_bytes += count;
        crc32 = esp_rom_crc32_le(crc32, buffer, count);
        if (++reads % 32 == 0) vTaskDelay(1);
    }
    file.close();
    return read_bytes == bytes && crc32 == expected_crc32;
}

const char* SensorStorageIO::save(const Capture& capture, char* filename, size_t size) {
    publish_snapshot();
    if (ota_activity_is_active()) return "ota_save_deferred";
    if (!storage_available(capture.count)) return "storage_unavailable_or_full";
    if (!Storage.exists("/storage") && !Storage.mkdir("/storage")) return "directory_failed";
    if (!Storage.exists("/storage/heart") && !Storage.mkdir("/storage/heart")) return "directory_failed";
    char path[96] = {};
    char partial[104] = {};
    char report_path[96] = {};
    char report_partial[104] = {};
    snprintf(path, sizeof(path), "/storage/heart/capture-%s.csv", capture.session_id);
    snprintf(partial, sizeof(partial), "%s.part", path);
    snprintf(report_path, sizeof(report_path), "/storage/heart/capture-%s.json", capture.session_id);
    snprintf(report_partial, sizeof(report_partial), "%s.part", report_path);
    if (csv_rollback_pending_) {
        if (Storage.exists(report_path)) return "filename_collision";
        if (Storage.exists(path) && !Storage.remove(path)) return "pair_cleanup_failed";
        csv_rollback_pending_ = false;
    }
    if (Storage.exists(path) || Storage.exists(report_path)) return "filename_collision";
    File file = Storage.open(partial, FILE_WRITE);
    if (!file) return "file_open_failed";
    ExportContext output;
    output.file = &file;
    const bool written = write_csv(capture, export_write, &output);
    const size_t bytes = file.position();
    file.flush();
    const bool failed = file.getWriteError() != 0;
    file.close();
    if (!written || failed) return ota_activity_is_active() ? "ota_save_deferred" : "file_write_failed";
    if (ota_activity_is_active()) return "ota_save_deferred";
    if (!verify_export(partial, bytes, output.crc32))
        return ota_activity_is_active() ? "ota_save_deferred" : "file_verify_failed";
    char checksum[9];
    snprintf(checksum, sizeof(checksum), "%08lx", static_cast<unsigned long>(output.crc32));
    ReportIdentity identity;
    identity.raw_csv = strrchr(path, '/') + 1;
    identity.csv_bytes = bytes;
    identity.csv_crc32 = checksum;
    identity.firmware_version = FIRMWARE_VERSION;
    identity.board = "jc4880p433-heart";
    file = Storage.open(report_partial, FILE_WRITE);
    if (!file) return "report_open_failed";
    output.writes = output.crc32 = 0;
    const bool report_written = write_session_json(capture, identity, export_write, &output, export_check);
    const size_t report_bytes = file.position();
    file.flush();
    const bool report_failed = file.getWriteError() != 0;
    file.close();
    if (!report_written || report_failed)
        return ota_activity_is_active() ? "ota_save_deferred" : "report_write_failed";
    if (!verify_export(report_partial, report_bytes, output.crc32))
        return ota_activity_is_active() ? "ota_save_deferred" : "report_verify_failed";
    if (ota_activity_is_active()) return "ota_save_deferred";
    if (!catalog_prepare()) return "catalog_prepare_failed";
    const PairCommit committed = commit_session_pair(partial, path, report_partial, report_path,
        [](void*, const char* source, const char* target) { return Storage.rename(source, target); },
        [](void*, const char* target) { return Storage.remove(target); }, nullptr);
    if (committed != PairCommit::Complete) {
        csv_rollback_pending_ = committed == PairCommit::CleanupFailed;
        if (committed == PairCommit::CsvFailed) return "file_rename_failed";
        return committed == PairCommit::CleanupFailed ? "pair_cleanup_failed" : "report_rename_failed";
    }
    snprintf(filename, size, "%s", path);
    if (!catalog_add(capture.session_id))
        LOGW("Heart", "Capture saved; catalog indexing deferred for %s", capture.session_id);
    LOGI("Heart", "Saved %lu raw samples to %s and session report to %s (%s)",
         static_cast<unsigned long>(capture.count), path, report_path,
         capture.interrupted ? "interrupted" : "complete");
    storage_publish_usage();
    return nullptr;
}

void recorder_task(void*) {
    for (;;) {
        Command command;
        if (xQueueReceive(command_queue, &command, 0) == pdTRUE) {
            const bool accepted = recorder.command(command, millis(), micros(), ota_activity_is_active());
            if (!accepted) {
                const Snapshot snapshot = recorder.snapshot();
                LOGW("Heart", "Recorder command rejected: state=%s reason=%s", state_name(snapshot.state),
                     snapshot.error[0] ? snapshot.error : "invalid_state_or_ota");
            }
            xSemaphoreTake(recorder_mutex, portMAX_DELAY);
            published = recorder.snapshot();
            command_pending = false;
            xSemaphoreGive(recorder_mutex);
        }
        recorder.tick(millis(), micros(), ota_activity_is_active());
        publish_snapshot();
        const Snapshot snapshot = recorder.snapshot();
        if ((snapshot.state == State::Idle || snapshot.state == State::Saved || snapshot.state == State::Error) &&
            !snapshot.retained) catalog_import_tick();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

}
}

void heart_recorder_init() {
    using namespace heart_monitor;
    if (!catalog_init()) LOGW("Heart", "Session catalog initialization failed");
    recorder_mutex = xSemaphoreCreateMutex();
    command_queue = xQueueCreate(1, sizeof(Command));
    if (!recorder_mutex || !command_queue ||
        xTaskCreate(recorder_task, "heart_capture", 8192, nullptr, 2, nullptr) != pdPASS) {
        published.state = State::Error;
        snprintf(published.error, sizeof(published.error), "worker_init_failed");
        LOGE("Heart", "Recorder worker initialization failed");
        return;
    }
    worker_ready = true;
}

heart_monitor::Snapshot heart_recorder_snapshot() {
    using namespace heart_monitor;
    if (!recorder_mutex) return published;
    xSemaphoreTake(recorder_mutex, portMAX_DELAY);
    const Snapshot snapshot = published;
    xSemaphoreGive(recorder_mutex);
    return snapshot;
}

bool heart_recorder_command(heart_monitor::Command command) {
    using namespace heart_monitor;
    if (!worker_ready || (ota_activity_is_active() && command != Command::Stop)) return false;
    xSemaphoreTake(recorder_mutex, portMAX_DELAY);
    const bool busy = published.state == State::Recording || published.state == State::Saving ||
                      published.state == State::AwaitingSave;
    bool allowed = !command_pending;
    if (command == Command::Start) allowed = allowed && !busy && !published.retained;
    else if (command == Command::Stop) allowed = allowed && published.state == State::Recording;
    else allowed = allowed && !busy && published.retained;
    if (allowed) {
        command_pending = xQueueSend(command_queue, &command, 0) == pdTRUE;
        allowed = command_pending;
    }
    xSemaphoreGive(recorder_mutex);
    return allowed;
}

#endif
#include "web_portal_logs.h"
#include "board_config.h"
#include "config_manager.h"
#include "web_portal_auth.h"
#include "web_portal_state.h"
#include "remote_log.h"
#include "ota_activity.h"

bool portal_logs_access_enabled() {
    const DeviceConfig* config = web_portal_get_current_config();
    return !web_portal_is_ap_mode_active() && config;
}

namespace {
void log_status(AsyncWebServerRequest* request, int code, const char* body) {
    auto* response = request->beginResponse(code, "application/json", body);
    response->addHeader("Cache-Control", "no-store");
    request->send(response);
}
}

#if DEBUG_CRASH_API_ENABLED
#include <atomic>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {
enum class DebugCrashMode : uintptr_t { Abort, Assertion, InvalidWrite };
std::atomic<bool> debug_crash_pending{false};

void debug_crash_task(void* argument) {
    vTaskDelay(pdMS_TO_TICKS(500));
    if (ota_activity_is_active()) {
        debug_crash_pending.store(false);
        vTaskDelete(nullptr);
        return;
    }
    const auto mode = static_cast<DebugCrashMode>(reinterpret_cast<uintptr_t>(argument));
    if (mode == DebugCrashMode::Assertion) {
        assert(false && "Debug crash API assertion");
    } else if (mode == DebugCrashMode::InvalidWrite) {
        volatile uintptr_t fault_address = 0;
        *reinterpret_cast<volatile uint32_t*>(fault_address) = 0xdeadbeef;
    }
    abort();
}
}

void handleDebugCrash(AsyncWebServerRequest* request) {
    if (!portal_logs_access_enabled()) {
        log_status(request, 403, "{\"error\":\"Crash injection requires full portal access\"}");
        return;
    }
    if (!portal_auth_gate(request)) return;
    if (ota_activity_is_active()) {
        log_status(request, 503, "{\"error\":\"Crash injection unavailable during firmware update\"}");
        return;
    }
    if (!request->hasParam("confirm") || request->getParam("confirm")->value() != "crash" ||
        !request->hasParam("mode")) {
        log_status(request, 400, "{\"error\":\"Specify mode and confirm=crash\"}");
        return;
    }
    const auto& mode_name = request->getParam("mode")->value();
    DebugCrashMode mode;
    if (mode_name == "abort") mode = DebugCrashMode::Abort;
    else if (mode_name == "assert") {
        #ifdef NDEBUG
        log_status(request, 501, "{\"error\":\"Assertions disabled in this build\"}");
        return;
        #else
        mode = DebugCrashMode::Assertion;
        #endif
    } else if (mode_name == "invalid_write") mode = DebugCrashMode::InvalidWrite;
    else {
        log_status(request, 400, "{\"error\":\"Mode must be abort, assert or invalid_write\"}");
        return;
    }
    bool expected = false;
    if (!debug_crash_pending.compare_exchange_strong(expected, true)) {
        log_status(request, 409, "{\"error\":\"Crash already scheduled\"}");
        return;
    }
    if (xTaskCreate(debug_crash_task, "debug_crash", 4096,
        reinterpret_cast<void*>(static_cast<uintptr_t>(mode)), 1, nullptr) != pdPASS) {
        debug_crash_pending.store(false);
        log_status(request, 503, "{\"error\":\"Unable to create crash task\"}");
        return;
    }
    log_status(request, 202, "{\"scheduled\":true,\"delay_ms\":500}");
}
#endif

#if REMOTE_LOG_BUFFER_RECORDS > 0
#include <ArduinoJson.h>
#include <ChunkPrint.h>
#include <WebResponseImpl.h>
#include <new>
#include <atomic>
#include <esp_core_dump.h>
#include <esp_partition.h>
#include <esp_system.h>
#include <esp_idf_version.h>
#if ESP_IDF_VERSION_MAJOR >= 5
#include <esp_app_desc.h>
#else
#include <esp_ota_ops.h>
#endif

namespace {
bool log_number(AsyncWebServerRequest* request, const char* name, uint32_t& value) {
    const String& text = request->getParam(name)->value();
    if (text.isEmpty()) return false;
    value = 0;
    for (size_t index = 0; index < text.length(); ++index) {
        const char digit = text[index];
        if (digit < '0' || digit > '9' || value > (UINT32_MAX - uint32_t(digit - '0')) / 10) return false;
        value = value * 10 + uint32_t(digit - '0');
    }
    return true;
}

class LogJsonAllocator final : public ArduinoJson::Allocator {
public:
    explicit LogJsonAllocator(uint8_t* storage, size_t capacity = 8192) : storage_(storage), capacity_(capacity) {}
    void* allocate(size_t size) override {
        const size_t alignment = alignof(max_align_t);
        const size_t aligned = (used_ + alignment - 1) / alignment * alignment;
        if (aligned > capacity_ || size > capacity_ - aligned) return nullptr;
        used_ = aligned + size;
        return storage_ + aligned;
    }
    void deallocate(void*) override {}
    void* reallocate(void*, size_t) override { return nullptr; }
    void reset() { used_ = 0; }
private:
    uint8_t* storage_;
    size_t capacity_;
    size_t used_ = 0;
};

bool write_log_snapshot(Print& output, const RemoteLogSnapshot& snapshot, bool reset) {
    LogJsonAllocator allocator(snapshot.json_storage);
    JsonDocument doc(&allocator);
    doc["available"] = true;
    doc["boot_id"] = snapshot.boot_id;
    doc["capacity"] = snapshot.capacity;
    doc["oldest"] = snapshot.range.oldest;
    doc["newest"] = snapshot.range.newest;
    doc["next"] = snapshot.range.cursor;
    doc["missed"] = snapshot.range.missed;
    doc["dropped"] = snapshot.dropped;
    doc["reset"] = reset || snapshot.range.reset;
    doc["has_more"] = snapshot.range.has_more;
    doc["boot_complete"] = snapshot.boot_complete;
    doc["boot_truncated"] = snapshot.boot_truncated;
    char header[512];
    if (doc.overflowed() || measureJson(doc) >= sizeof(header)) return false;
    const size_t length = serializeJson(doc, header, sizeof(header));
    output.write(reinterpret_cast<const uint8_t*>(header), length - 1);
    output.print(",\"records\":[");
    for (size_t index = 0; index < snapshot.count; ++index) {
        if (index) output.write(',');
        doc.clear();
        allocator.reset();
        doc["sequence"] = snapshot.records[index].sequence;
        doc["line"] = static_cast<const char*>(snapshot.records[index].line);
        if (doc.overflowed()) return false;
        serializeJson(doc, output);
    }
    output.print("]}");
    return true;
}

class LogLengthPrint : public Print {
public:
    size_t count = 0;
    size_t write(uint8_t) override { ++count; return 1; }
};

class RemoteLogResponse final : public AsyncAbstractResponse {
public:
    RemoteLogResponse(const RemoteLogSnapshot* snapshot, bool reset)
        : snapshot_(snapshot), reset_(reset), started_(millis()) {
        _code = 200;
        _contentType = "application/json";
        _sendContentLength = true;
        _chunked = false;
        LogLengthPrint counter;
        valid_ = write_log_snapshot(counter, *snapshot_, reset_);
        _contentLength = counter.count;
        addHeader("Cache-Control", "no-store");
    }

    ~RemoteLogResponse() override { remote_log_release_snapshot(); }

    bool _sourceValid() const override {
        return valid_ && uint32_t(millis() - started_) < 10000 && !ota_activity_is_active();
    }

    size_t _fillBuffer(uint8_t* buffer, size_t max_length) override {
        if (!_sourceValid() || offset_ >= _contentLength) return 0;
        size_t count = _contentLength - offset_;
        if (count > max_length) count = max_length;
        if (count > 1024) count = 1024;
        ChunkPrint output(buffer, offset_, count);
        if (!write_log_snapshot(output, *snapshot_, reset_)) return 0;
        offset_ += count;
        return count;
    }

private:
    const RemoteLogSnapshot* snapshot_;
    bool reset_;
    uint32_t started_;
    size_t offset_ = 0;
    bool valid_ = false;
};

bool crash_access(AsyncWebServerRequest* request) {
    if (!portal_logs_access_enabled()) {
        log_status(request, 403, "{\"error\":\"Logs require full portal access\"}");
        return false;
    }
    if (!portal_auth_gate(request)) return false;
    if (ota_activity_is_active()) {
        log_status(request, 503, "{\"error\":\"Crash diagnostics paused during firmware update\"}");
        return false;
    }
    return true;
}

#if CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH
std::atomic<bool> crash_download_active{false};

bool crash_image(AsyncWebServerRequest* request, const esp_partition_t*& partition, size_t& offset, size_t& size, bool download = false) {
    partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_COREDUMP, nullptr);
    if (!partition) {
        log_status(request, download ? 404 : 200, "{\"available\":false,\"reason\":\"no_partition\"}");
        return false;
    }
    const esp_err_t result = esp_core_dump_image_check();
    if (result != ESP_OK) {
        log_status(request, download ? (result == ESP_ERR_NOT_FOUND ? 404 : 409) : 200, result == ESP_ERR_NOT_FOUND ?
            "{\"available\":false,\"reason\":\"not_found\"}" :
            "{\"available\":false,\"reason\":\"invalid_dump\"}");
        return false;
    }
    size_t address = 0;
    if (esp_core_dump_image_get(&address, &size) != ESP_OK || address < partition->address ||
        size < 20 || address - partition->address > partition->size ||
        size > partition->size - (address - partition->address)) {
        log_status(request, download ? 409 : 200, "{\"available\":false,\"reason\":\"invalid_dump\"}");
        return false;
    }
    offset = address - partition->address;
    return true;
}

class CrashDownloadResponse final : public AsyncAbstractResponse {
public:
    CrashDownloadResponse(const esp_partition_t* partition, size_t offset, size_t size)
        : partition_(partition), base_(offset), started_(millis()) {
        _code = 200;
        _contentType = "application/octet-stream";
        _sendContentLength = true;
        _chunked = false;
        _contentLength = size;
        addHeader("Cache-Control", "no-store");
        addHeader("Content-Disposition", "attachment; filename=\"device-coredump.bin\"");
        addHeader("X-Content-Type-Options", "nosniff");
    }
    ~CrashDownloadResponse() override { crash_download_active.store(false); }
    bool _sourceValid() const override {
        return valid_ && uint32_t(millis() - started_) < 30000 && !ota_activity_is_active();
    }
    size_t _fillBuffer(uint8_t* buffer, size_t max_length) override {
        if (!_sourceValid() || offset_ >= _contentLength) return 0;
        size_t count = _contentLength - offset_;
        if (count > max_length) count = max_length;
        if (count > 1024) count = 1024;
        if (esp_partition_read(partition_, base_ + offset_, buffer, count) != ESP_OK) {
            valid_ = false;
            return 0;
        }
        offset_ += count;
        return count;
    }
private:
    const esp_partition_t* partition_;
    size_t base_;
    uint32_t started_;
    size_t offset_ = 0;
    bool valid_ = true;
};
#endif
}
#endif

void handleGetCrashLog(AsyncWebServerRequest* request) {
    #if REMOTE_LOG_BUFFER_RECORDS > 0
    if (!crash_access(request)) return;
    #if CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH
    const esp_partition_t* partition = nullptr;
    size_t offset = 0;
    size_t size = 0;
    if (!crash_image(request, partition, offset, size)) return;
    const auto* snapshot = remote_log_snapshot(false, false, 0, 1);
    if (!snapshot) {
        log_status(request, 503, "{\"error\":\"Crash summary workspace unavailable\"}");
        return;
    }
    char body[2048];
    bool serialized = false;
    {
        LogJsonAllocator allocator(snapshot->json_storage, 6144);
        JsonDocument doc(&allocator);
        doc["available"] = true;
        doc["size"] = size;
        doc["partition_size"] = partition->size;
        doc["current_reset_reason"] = static_cast<int>(esp_reset_reason());
        char current_sha256[65];
        #if ESP_IDF_VERSION_MAJOR >= 5
        esp_app_get_elf_sha256(current_sha256, sizeof(current_sha256));
        #else
        esp_ota_get_app_elf_sha256(current_sha256, sizeof(current_sha256));
        #endif
        doc["current_elf_sha256"] = current_sha256;
    #if CONFIG_ESP_COREDUMP_DATA_FORMAT_ELF
        static_assert(sizeof(esp_core_dump_summary_t) <= 2048, "Crash summary exceeds PSRAM workspace");
        auto* summary = new (snapshot->json_storage + 6144) esp_core_dump_summary_t{};
        if (esp_core_dump_get_summary(summary) == ESP_OK) {
            auto add_hex = [](JsonObject target, const char* key, uint32_t value) {
                char text[11];
                snprintf(text, sizeof(text), "0x%08lx", static_cast<unsigned long>(value));
                target[JsonString(key, JsonString::Copied)] = text;
            };
            summary->exc_task[sizeof(summary->exc_task) - 1] = '\0';
            summary->app_elf_sha256[sizeof(summary->app_elf_sha256) - 1] = '\0';
            doc["task"] = summary->exc_task;
            doc["elf_sha256"] = reinterpret_cast<const char*>(summary->app_elf_sha256);
            add_hex(doc.as<JsonObject>(), "pc", summary->exc_pc);
            JsonObject registers = doc["registers"].to<JsonObject>();
            #if CONFIG_IDF_TARGET_ARCH_RISCV
            doc["architecture"] = "riscv";
            doc["exception_cause"] = summary->ex_info.mcause;
            add_hex(doc.as<JsonObject>(), "trap_value", summary->ex_info.mtval);
            add_hex(registers, "MEPC", summary->exc_pc);
            add_hex(registers, "RA", summary->ex_info.ra);
            add_hex(registers, "SP", summary->ex_info.sp);
            add_hex(registers, "MSTATUS", summary->ex_info.mstatus);
            add_hex(registers, "MTVEC", summary->ex_info.mtvec);
            add_hex(registers, "MCAUSE", summary->ex_info.mcause);
            add_hex(registers, "MTVAL", summary->ex_info.mtval);
            #elif CONFIG_IDF_TARGET_ARCH_XTENSA
            doc["architecture"] = "xtensa";
            doc["exception_cause"] = summary->ex_info.exc_cause;
            add_hex(doc.as<JsonObject>(), "fault_address", summary->ex_info.exc_vaddr);
            add_hex(registers, "PC", summary->exc_pc);
            add_hex(registers, "EXCCAUSE", summary->ex_info.exc_cause);
            add_hex(registers, "EXCVADDR", summary->ex_info.exc_vaddr);
            for (unsigned index = 0; index < sizeof(summary->ex_info.epcx) / sizeof(summary->ex_info.epcx[0]); ++index) {
                if (!(summary->ex_info.epcx_reg_bits & (1U << index))) continue;
                char name[8];
                snprintf(name, sizeof(name), "EPC%u", index + 1);
                add_hex(registers, name, summary->ex_info.epcx[index]);
            }
            #endif
            #if CONFIG_IDF_TARGET_ARCH_RISCV || CONFIG_IDF_TARGET_ARCH_XTENSA
            for (unsigned index = 0; index < sizeof(summary->ex_info.exc_a) / sizeof(summary->ex_info.exc_a[0]); ++index) {
                char name[8];
                snprintf(name, sizeof(name), "A%u", index);
                add_hex(registers, name, summary->ex_info.exc_a[index]);
            }
            #endif
        }
        #if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 0)
        char reason[192] = {};
        if (esp_core_dump_get_panic_reason(reason, sizeof(reason)) == ESP_OK) {
            reason[sizeof(reason) - 1] = '\0';
            doc["panic_reason"] = reason;
        }
        #endif
    #endif
        if (!doc.overflowed() && measureJson(doc) < sizeof(body)) {
            serializeJson(doc, body, sizeof(body));
            serialized = true;
        }
    }
    remote_log_release_snapshot();
    if (!serialized) {
        log_status(request, 503, "{\"error\":\"Crash summary unavailable\"}");
        return;
    }
    log_status(request, 200, body);
    #else
    log_status(request, 200, "{\"available\":false,\"reason\":\"disabled\"}");
    #endif
    #else
    if (!portal_logs_access_enabled()) {
        log_status(request, 403, "{\"error\":\"Logs require full portal access\"}");
        return;
    }
    if (!portal_auth_gate(request)) return;
    log_status(request, 200, "{\"available\":false,\"reason\":\"disabled\"}");
    #endif
}

void handleDownloadCrashLog(AsyncWebServerRequest* request) {
    #if REMOTE_LOG_BUFFER_RECORDS > 0 && CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH
    if (!crash_access(request)) return;
    bool expected = false;
    if (!crash_download_active.compare_exchange_strong(expected, true)) {
        log_status(request, 429, "{\"error\":\"Crash download busy\"}");
        return;
    }
    const esp_partition_t* partition = nullptr;
    size_t offset = 0;
    size_t size = 0;
    if (!crash_image(request, partition, offset, size, true)) {
        crash_download_active.store(false);
        return;
    }
    auto* response = new (std::nothrow) CrashDownloadResponse(partition, offset, size);
    if (!response) {
        crash_download_active.store(false);
        log_status(request, 503, "{\"error\":\"Crash download unavailable\"}");
        return;
    }
    request->send(response);
    #else
    if (!portal_logs_access_enabled()) {
        log_status(request, 403, "{\"error\":\"Logs require full portal access\"}");
        return;
    }
    if (!portal_auth_gate(request)) return;
    log_status(request, 404, "{\"error\":\"Crash capture is disabled\"}");
    #endif
}

void handleGetLogs(AsyncWebServerRequest* request) {
    if (!portal_logs_access_enabled()) {
        log_status(request, 403, "{\"error\":\"Logs require full portal access\"}");
        return;
    }
    if (!portal_auth_gate(request)) return;
    if (!remote_log_available()) {
        #if REMOTE_LOG_BUFFER_RECORDS > 0
        log_status(request, 200, "{\"available\":false,\"reason\":\"psram_unavailable\"}");
        #else
        log_status(request, 200, "{\"available\":false,\"reason\":\"disabled\"}");
        #endif
        return;
    }
    #if REMOTE_LOG_BUFFER_RECORDS > 0
    if (ota_activity_is_active()) {
        log_status(request, 503, "{\"error\":\"Logs paused during firmware update\"}");
        return;
    }
    uint32_t after = 0;
    uint32_t limit = REMOTE_LOG_RESPONSE_RECORDS;
    uint32_t boot_id = 0;
    const bool has_after = request->hasParam("after");
    const bool has_boot_id = request->hasParam("boot_id");
    if ((has_after && !log_number(request, "after", after)) ||
        (request->hasParam("limit") && (!log_number(request, "limit", limit) || !limit)) ||
        (has_boot_id && !log_number(request, "boot_id", boot_id))) {
        log_status(request, 400, "{\"error\":\"Invalid log cursor or limit\"}");
        return;
    }
    const String source = request->hasParam("source") ? request->getParam("source")->value() : "recent";
    if (source != "recent" && source != "boot") {
        log_status(request, 400, "{\"error\":\"Invalid log source\"}");
        return;
    }
    bool reset = false;
    const auto* snapshot = remote_log_snapshot(source == "boot", has_after, after, limit);
    if (snapshot && has_boot_id && snapshot->boot_id != boot_id) {
        remote_log_release_snapshot();
        snapshot = remote_log_snapshot(source == "boot", false, 0, limit);
        reset = true;
    }
    if (!snapshot) {
        log_status(request, 429, "{\"error\":\"Log snapshot busy\"}");
        return;
    }
    auto* response = new (std::nothrow) RemoteLogResponse(snapshot, reset);
    if (!response) {
        remote_log_release_snapshot();
        log_status(request, 503, "{\"error\":\"Log response unavailable\"}");
        return;
    }
    if (!response->_sourceValid()) {
        delete response;
        log_status(request, 503, "{\"error\":\"Log response unavailable\"}");
        return;
    }
    request->send(response);
    #endif
}
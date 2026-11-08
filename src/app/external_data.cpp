#include "external_data.h"

#if HAS_EXTERNAL_DATA
#include "binding_value.h"
#include "bounded_psram_json_allocator.h"
#include "log_manager.h"
#include "net_activity.h"
#include "ota_activity.h"
#include "rtos_task_utils.h"

#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <esp_arduino_version.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <freertos/semphr.h>
#include <lwip/netdb.h>
#include <atomic>
#include <new>
#include <string.h>
#include <time.h>

#if ESP_ARDUINO_VERSION_MAJOR < 3
// ESP32 core 3.3.12 trust anchors converted to the legacy bundle format.
static const uint8_t x509_bundle_start[] = {
#include "external_data_ca_bundle.inc"
};
#endif

namespace {
constexpr size_t BODY_LIMIT = 16 * 1024;
constexpr size_t DOCUMENT_LIMIT = 48 * 1024;
constexpr uint32_t FETCH_DEADLINE_MS = 30000;
constexpr uint32_t IO_TIMEOUT_MS = 4000;
constexpr uint32_t MIN_RETRY_MS = 60000;
constexpr uint32_t MAX_RETRY_MS = 3600000;
constexpr uint32_t TASK_STACK_BYTES = 16384;

struct Slot {
    ExternalDataSourceConfig config{};
    BoundedPsramJsonAllocator allocators[2]{
        BoundedPsramJsonAllocator(DOCUMENT_LIMIT), BoundedPsramJsonAllocator(DOCUMENT_LIMIT)};
    JsonDocument* document = nullptr;
    uint8_t document_allocator = 0;
    uint32_t generation = 1;
    uint32_t document_generation = 0;
    uint32_t next_attempt = 0;
    uint32_t retry_ms = MIN_RETRY_MS;
    uint64_t last_success_ms = 0;
    uint32_t last_duration_ms = 0;
    size_t last_size = 0;
    uint16_t last_http_status = 0;
    const char* error = nullptr;
    bool active = false;
    bool success = false;
    bool completed = false;
    bool fetching = false;
};

struct FetchStats {
    size_t size = 0;
    uint16_t http_status = 0;
};

struct Job {
    ExternalDataSourceConfig config;
    uint32_t slot;
    uint32_t generation;
    uint32_t revision;
    uint32_t ota_epoch;
    uint32_t started;
};

Slot* slots = nullptr;
SemaphoreHandle_t mutex = nullptr;
TaskHandle_t worker_task = nullptr;
RtosTaskPsramAlloc worker_alloc{};
std::atomic<uint32_t> revision{0};
std::atomic<bool> worker_failed{false};

void destroy_document(JsonDocument* document) {
    if (!document) return;
    document->~JsonDocument();
    heap_caps_free(document);
}

bool valid_job(const Job& job) {
    return !ota_activity_is_active() && ota_activity_epoch() == job.ota_epoch &&
           revision.load(std::memory_order_acquire) == job.revision &&
           WiFi.status() == WL_CONNECTED &&
           uint32_t(millis() - job.started) < FETCH_DEADLINE_MS;
}

class ResponseReader {
public:
    ResponseReader(WiFiClient& client, const Job& job)
        : client_(client), job_(job), last_data_(millis()) {}

    // Returns a byte, -1 for EOF, or -2 for timeout/cancellation.
    int read() {
        while (true) {
            if (!valid_job(job_)) return -2;
            if (offset_ < length_) return buffer_[offset_++];
            const int available = client_.available();
            if (available > 0) {
                const size_t amount = available < static_cast<int>(sizeof(buffer_))
                                          ? static_cast<size_t>(available) : sizeof(buffer_);
                const int received = client_.read(buffer_, amount);
                if (received < 0) return -2;
                if (received > 0) {
                    offset_ = 0;
                    length_ = static_cast<size_t>(received);
                    last_data_ = millis();
                    continue;
                }
            }
            if (!client_.connected()) return -1;
            if (uint32_t(millis() - last_data_) >= IO_TIMEOUT_MS) return -2;
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    bool line(char* output, size_t capacity, size_t& total) {
        size_t used = 0;
        bool cr = false;
        for (;;) {
            const int c = read();
            if (c < 0 || ++total > 8192) return false;
            if (cr) {
                if (c != '\n') return false;
                output[used] = '\0';
                return true;
            }
            if (c == '\r') {
                cr = true;
            } else {
                if (c == '\n' || c == 0 || used + 1 >= capacity) return false;
                output[used++] = static_cast<char>(c);
            }
        }
    }

private:
    WiFiClient& client_;
    const Job& job_;
    uint32_t last_data_;
    uint8_t buffer_[512]{};
    size_t offset_ = 0;
    size_t length_ = 0;
};

bool decimal_size(const char* text, size_t& value) {
    if (!*text) return false;
    value = 0;
    for (const char* p = text; *p; ++p) {
        if (*p < '0' || *p > '9') return false;
        const unsigned digit = *p - '0';
        if (value > (SIZE_MAX - digit) / 10) return false;
        value = value * 10 + digit;
    }
    return true;
}

bool chunk_size(const char* text, size_t& value) {
    value = 0;
    bool digit_seen = false;
    for (const char* p = text; *p && *p != ';'; ++p) {
        unsigned digit;
        if (*p >= '0' && *p <= '9') digit = *p - '0';
        else if (*p >= 'a' && *p <= 'f') digit = *p - 'a' + 10;
        else if (*p >= 'A' && *p <= 'F') digit = *p - 'A' + 10;
        else return false;
        if (value > (SIZE_MAX - digit) / 16) return false;
        value = value * 16 + digit;
        digit_seen = true;
    }
    return digit_seen;
}

class JsonReader {
public:
    JsonReader(const char* data, size_t size) : data_(data), size_(size) {}
    int read() {
        return offset_ < size_ ? static_cast<unsigned char>(data_[offset_++]) : -1;
    }
    size_t readBytes(char* buffer, size_t length) {
        const size_t count = length < size_ - offset_ ? length : size_ - offset_;
        memcpy(buffer, data_ + offset_, count);
        offset_ += count;
        return count;
    }
    size_t offset() const { return offset_; }
private:
    const char* data_;
    size_t size_;
    size_t offset_ = 0;
};

bool json_whitespace(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

bool complete_json(const char* body, size_t size, size_t consumed) {
    size_t first = 0;
    while (first < size && json_whitespace(body[first])) ++first;
    if (first == size || consumed > size) return false;
    if ((body[first] == '-' || (body[first] >= '0' && body[first] <= '9')) && consumed) {
        const char last = body[consumed - 1];
        if (!json_whitespace(last) && (!last || !strchr("-+0123456789.eE", last))) return false;
    }
    for (size_t i = consumed; i < size; ++i)
        if (!json_whitespace(body[i])) return false;
    return true;
}

bool resolve_address(const char* host, IPAddress& address) {
    if (address.fromString(host)) return true;
    addrinfo hints{};
    hints.ai_socktype = SOCK_STREAM;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    hints.ai_family = AF_UNSPEC;
#else
    hints.ai_family = AF_INET;
#endif
    addrinfo* result = nullptr;
    if (lwip_getaddrinfo(host, nullptr, &hints, &result) != 0 || !result) return false;
    bool found = false;
    for (const addrinfo* entry = result; entry; entry = entry->ai_next) {
        if (entry->ai_family == AF_INET) {
            const auto* ipv4 = reinterpret_cast<const sockaddr_in*>(entry->ai_addr);
            address = IPAddress(ipv4->sin_addr.s_addr);
            found = true;
            break;
        }
#if ESP_ARDUINO_VERSION_MAJOR >= 3 && CONFIG_LWIP_IPV6
        if (entry->ai_family == AF_INET6) {
            const auto* ipv6 = reinterpret_cast<const sockaddr_in6*>(entry->ai_addr);
            address = IPAddress(IPv6, ipv6->sin6_addr.s6_addr);
            found = true;
            break;
        }
#endif
    }
    lwip_freeaddrinfo(result);
    return found;
}

const char* read_response(WiFiClient& client, const Job& job, char* body, FetchStats& stats) {
    ResponseReader reader(client, job);
    char line[1024];
    size_t headers = 0;
    if (!reader.line(line, sizeof(line), headers)) return "Response headers interrupted";
    if (strncmp(line, "HTTP/1.1 ", 9) && strncmp(line, "HTTP/1.0 ", 9))
        return "Invalid HTTP response";
    if (strlen(line) >= 12 && line[9] >= '1' && line[9] <= '5' &&
        line[10] >= '0' && line[10] <= '9' && line[11] >= '0' && line[11] <= '9' &&
        (!line[12] || line[12] == ' ')) {
        stats.http_status = (line[9] - '0') * 100 + (line[10] - '0') * 10 + line[11] - '0';
    }
    if (stats.http_status != 200)
        return "HTTP request failed";
    bool chunked = false;
    bool has_length = false;
    bool has_transfer = false;
    size_t content_length = 0;
    while (true) {
        if (!reader.line(line, sizeof(line), headers)) return "Response headers interrupted";
        if (!line[0]) break;
        char* colon = strchr(line, ':');
        if (!colon) return "Invalid HTTP response headers";
        *colon++ = '\0';
        while (*colon == ' ' || *colon == '\t') ++colon;
        size_t length = strlen(colon);
        while (length && (colon[length - 1] == ' ' || colon[length - 1] == '\t'))
            colon[--length] = '\0';
        if (strcasecmp(line, "Content-Length") == 0) {
            if (has_length || !decimal_size(colon, content_length)) return "Invalid HTTP content length";
            has_length = true;
        } else if (strcasecmp(line, "Transfer-Encoding") == 0) {
            if (has_transfer || strcasecmp(colon, "chunked")) return "Unsupported HTTP transfer encoding";
            has_transfer = true;
            chunked = true;
        } else if (strcasecmp(line, "Content-Encoding") == 0 && strcasecmp(colon, "identity")) {
            return "Unsupported HTTP content encoding";
        }
    }
    if (chunked && has_length) return "Ambiguous HTTP response length";
    if (has_length && content_length > BODY_LIMIT) return "Response exceeds 16 KiB";
    size_t& used = stats.size;
    if (chunked) {
        for (;;) {
            size_t chunk_header = 0;
            if (!reader.line(line, sizeof(line), chunk_header)) return "Chunk header interrupted";
            size_t size = 0;
            if (!chunk_size(line, size)) return "Invalid HTTP chunk";
            if (size > BODY_LIMIT - used) return "Response exceeds 16 KiB";
            if (!size) {
                do {
                    if (!reader.line(line, sizeof(line), headers)) return "HTTP trailers interrupted";
                } while (line[0]);
                break;
            }
            for (size_t i = 0; i < size; ++i) {
                const int c = reader.read();
                if (c < 0) return "Response body interrupted";
                body[used++] = static_cast<char>(c);
            }
            if (reader.read() != '\r' || reader.read() != '\n') return "Invalid HTTP chunk";
        }
    } else if (has_length) {
        for (size_t i = 0; i < content_length; ++i) {
            const int c = reader.read();
            if (c < 0) return "Response body interrupted";
            body[used++] = static_cast<char>(c);
        }
    } else {
        for (;;) {
            const int c = reader.read();
            if (c == -1) break;
            if (c < 0) return "Response body interrupted";
            if (used == BODY_LIMIT) return "Response exceeds 16 KiB";
            body[used++] = static_cast<char>(c);
        }
    }
    body[used] = '\0';
    return nullptr;
}

const char* fetch(const Job& job, JsonDocument& document, FetchStats& stats) {
    ExternalDataUrl parts;
    if (!external_data_split_url(job.config.url, parts)) return "Invalid source URL";
    if (parts.https && time(nullptr) < 1609459200) return "Clock not synchronized";
    if (!valid_job(job)) return "Fetch interrupted";
    auto* body = static_cast<char*>(heap_caps_malloc(BODY_LIMIT + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!body) return "Response allocation failed";
    WiFiClient plain;
    WiFiClientSecure secure;
    WiFiClient& client = parts.https ? static_cast<WiFiClient&>(secure) : plain;
    if (parts.https) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
        secure.useBuiltinCACertBundle();
#else
        secure.setCACertBundle(x509_bundle_start);
#endif
        secure.setHandshakeTimeout(IO_TIMEOUT_MS / 1000);
    }
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    client.setTimeout(IO_TIMEOUT_MS);
    client.setConnectionTimeout(IO_TIMEOUT_MS);
#else
    client.setTimeout(IO_TIMEOUT_MS / 1000);
#endif
    const char* error = nullptr;
    IPAddress address;
    if (!resolve_address(parts.host, address)) {
        error = "DNS lookup failed";
    } else if (!valid_job(job)) {
        error = "Fetch interrupted";
    } else if (!(parts.https ? secure.connect(address, parts.port, parts.host, nullptr, nullptr, nullptr)
                             : plain.connect(address, parts.port, IO_TIMEOUT_MS))) {
        error = parts.https ? "HTTPS connection or certificate verification failed" : "HTTP connection failed";
    } else if (!valid_job(job)) {
        error = "Fetch interrupted";
    } else {
        char host_header[280];
        const bool default_port = parts.port == (parts.https ? 443 : 80);
        if (parts.ipv6) {
            if (default_port) snprintf(host_header, sizeof(host_header), "[%s]", parts.host);
            else snprintf(host_header, sizeof(host_header), "[%s]:%u", parts.host, parts.port);
        } else {
            if (default_port) snprintf(host_header, sizeof(host_header), "%s", parts.host);
            else snprintf(host_header, sizeof(host_header), "%s:%u", parts.host, parts.port);
        }
        const char* request_format = "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: ESP32-Macropad\r\n"
                                     "Accept: application/json\r\n"
                                     "Accept-Encoding: identity\r\nConnection: close\r\n\r\n";
        const int request_length = snprintf(nullptr, 0, request_format, parts.target, host_header);
        if (request_length < 0 ||
            client.printf(request_format, parts.target, host_header) != static_cast<size_t>(request_length)) {
            error = "HTTP request write failed";
        } else {
            net_activity_mark(NET_CH_HTTP);
            error = read_response(client, job, body, stats);
            if (!error && !valid_job(job)) error = "Fetch interrupted";
            if (!error) {
                size_t json_size = stats.size;
                while (json_size && json_whitespace(body[json_size - 1])) --json_size;
                JsonReader reader(body, json_size);
                const DeserializationError result = deserializeJson(document, reader);
                if (result || document.overflowed() || !complete_json(body, json_size, reader.offset()))
                    error = result == DeserializationError::NoMemory || document.overflowed()
                                ? "JSON allocation limit exceeded" : "Invalid JSON response";
            }
        }
    }
    client.stop();
    heap_caps_free(body);
    if (!error && !valid_job(job)) error = "Fetch interrupted";
    return error;
}

void worker(void*) {
    for (;;) {
        Job job{};
        bool found = false;
        JsonDocument* retired = nullptr;
        if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
            const uint32_t now = millis();
            for (unsigned i = 0; i < EXTERNAL_DATA_MAX_SOURCES; ++i) {
                Slot& slot = slots[i];
                if (slot.document && (!slot.active || slot.document_generation != slot.generation)) {
                    retired = slot.document;
                    slot.document = nullptr;
                    break;
                }
                if (!slot.active || static_cast<int32_t>(now - slot.next_attempt) < 0 ||
                    ota_activity_is_active()) continue;
                if (WiFi.status() != WL_CONNECTED) {
                    slot.error = "Wi-Fi disconnected";
                    slot.next_attempt = now + MIN_RETRY_MS;
                    continue;
                }
                job = {slot.config, i, slot.generation,
                       revision.load(std::memory_order_acquire), ota_activity_epoch(), now};
                slot.fetching = true;
                found = true;
                break;
            }
            xSemaphoreGive(mutex);
        }
        if (retired) {
            destroy_document(retired);
            continue;
        }
        if (!found) {
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        Slot& slot = slots[job.slot];
        void* memory = heap_caps_malloc(sizeof(JsonDocument), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        const uint8_t candidate_allocator = slot.document_allocator ^ 1;
        JsonDocument* candidate = memory ? new (memory) JsonDocument(&slot.allocators[candidate_allocator]) : nullptr;
        FetchStats stats;
        const char* error = candidate ? fetch(job, *candidate, stats) : "JSON document allocation failed";
        JsonDocument* previous = nullptr;
        bool report_error = false;
        if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
            if (slot.active && slot.generation == job.generation) slot.fetching = false;
            if (slot.active && slot.generation == job.generation && valid_job(job)) {
                slot.completed = true;
                slot.last_duration_ms = uint32_t(millis() - job.started);
                slot.last_http_status = stats.http_status;
                if (!error) {
                    previous = slot.document;
                    slot.document = candidate;
                    slot.document_allocator = candidate_allocator;
                    slot.document_generation = slot.generation;
                    candidate = nullptr;
                    slot.last_success_ms = static_cast<uint64_t>(esp_timer_get_time() / 1000);
                    slot.last_size = stats.size;
                    slot.success = true;
                    slot.error = nullptr;
                    slot.retry_ms = MIN_RETRY_MS;
                    slot.next_attempt = millis() + slot.config.refresh_seconds * 1000;
                } else {
                    slot.error = error;
                    slot.next_attempt = millis() + slot.retry_ms;
                    slot.retry_ms = slot.retry_ms < MAX_RETRY_MS / 2 ? slot.retry_ms * 2 : MAX_RETRY_MS;
                    report_error = true;
                }
            } else if (slot.active && slot.generation == job.generation &&
                       revision.load(std::memory_order_acquire) == job.revision &&
                       !ota_activity_is_active() && ota_activity_epoch() == job.ota_epoch) {
                slot.completed = true;
                slot.last_duration_ms = uint32_t(millis() - job.started);
                slot.last_http_status = stats.http_status;
                slot.error = WiFi.status() == WL_CONNECTED ? "Fetch timed out" : "Wi-Fi disconnected";
                slot.next_attempt = millis() + MIN_RETRY_MS;
            }
            xSemaphoreGive(mutex);
        }
        if (report_error) LOGW("ExtData", "%s: %s", job.config.name, error);
        destroy_document(previous);
        destroy_document(candidate);
    }
}
}

void external_data_init(const ExternalDataConfig& config) {
    mutex = xSemaphoreCreateMutex();
    if (!mutex) {
        worker_failed = true;
        LOGE("ExtData", "Unable to create source mutex");
        return;
    }
    void* memory = heap_caps_malloc(sizeof(Slot) * EXTERNAL_DATA_MAX_SOURCES,
                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!memory) {
        worker_failed = true;
        LOGE("ExtData", "Unable to allocate source slots in PSRAM");
        return;
    }
    Slot* allocated = static_cast<Slot*>(memory);
    for (unsigned i = 0; i < EXTERNAL_DATA_MAX_SOURCES; ++i) new (&allocated[i]) Slot{};
    if (xSemaphoreTake(mutex, portMAX_DELAY) != pdTRUE) {
        for (unsigned i = 0; i < EXTERNAL_DATA_MAX_SOURCES; ++i) allocated[i].~Slot();
        heap_caps_free(memory);
        worker_failed = true;
        LOGE("ExtData", "Unable to initialize source slots");
        return;
    }
    slots = allocated;
    xSemaphoreGive(mutex);
    external_data_configure(config);
}

void external_data_configure(const ExternalDataConfig& config) {
    if (!mutex) return;
    if (xSemaphoreTake(mutex, portMAX_DELAY) != pdTRUE) return;
    if (!slots) {
        xSemaphoreGive(mutex);
        return;
    }
    revision.fetch_add(1, std::memory_order_acq_rel);
    const uint32_t now = millis();
    for (unsigned index = 0; index < EXTERNAL_DATA_MAX_SOURCES; ++index) {
        Slot& slot = slots[index];
        if (!slot.active) continue;
        const ExternalDataSourceConfig* match = nullptr;
        for (uint32_t i = 0; i < config.count; ++i)
            if (strcmp(slot.config.name, config.sources[i].name) == 0) match = &config.sources[i];
        if (!match || strcmp(slot.config.url, match->url)) {
            ++slot.generation;
            slot.active = false;
            slot.success = false;
            slot.error = nullptr;
            slot.completed = false;
            slot.fetching = false;
        }
        if (match) {
            slot.active = true;
            slot.config = *match;
            slot.next_attempt = now;
        }
    }
    for (uint32_t i = 0; i < config.count; ++i) {
        bool exists = false;
        for (unsigned index = 0; index < EXTERNAL_DATA_MAX_SOURCES; ++index) {
            const Slot& slot = slots[index];
            if (slot.active && !strcmp(slot.config.name, config.sources[i].name)) exists = true;
        }
        if (exists) continue;
        for (unsigned index = 0; index < EXTERNAL_DATA_MAX_SOURCES; ++index) {
            Slot& slot = slots[index];
            if (slot.active) continue;
            ++slot.generation;
            slot.config = config.sources[i];
            slot.active = true;
            slot.success = false;
            slot.error = nullptr;
            slot.completed = false;
            slot.fetching = false;
            slot.next_attempt = now;
            slot.retry_ms = MIN_RETRY_MS;
            break;
        }
    }
    xSemaphoreGive(mutex);
    if (config.count && !worker_task) {
        if (!rtos_create_task_psram_stack(worker, "external_data", TASK_STACK_BYTES,
                                           nullptr, 2, &worker_task, &worker_alloc)) {
            worker_failed = true;
            LOGE("ExtData", "Unable to create source worker");
        } else {
            worker_failed = false;
        }
    }
}

bool external_data_get_value(const char* name, const char* path, char* out, size_t out_len) {
    if (!mutex || xSemaphoreTake(mutex, 0) != pdTRUE) return false;
    if (!slots) {
        xSemaphoreGive(mutex);
        return false;
    }
    bool resolved = false;
    for (unsigned index = 0; index < EXTERNAL_DATA_MAX_SOURCES; ++index) {
        const Slot& slot = slots[index];
        if (slot.active && slot.document && slot.document_generation == slot.generation &&
            strcmp(slot.config.name, name) == 0) {
            resolved = binding_value_select_json(slot.document->as<JsonVariantConst>(), path, out, out_len);
            break;
        }
    }
    xSemaphoreGive(mutex);
    return resolved;
}

static const char* source_state(const Slot& slot) {
    if (worker_failed) return "error";
    return slot.success ? (slot.error ? "stale" : "healthy") :
                          (slot.error ? "error" : "waiting");
}

bool external_data_get_stat(const char* name, const char* key, char* out, size_t out_len) {
    if (!name || !key || !out || !out_len || !mutex || xSemaphoreTake(mutex, 0) != pdTRUE)
        return false;
    if (!slots) {
        xSemaphoreGive(mutex);
        return false;
    }
    int written = -1;
    for (unsigned index = 0; index < EXTERNAL_DATA_MAX_SOURCES; ++index) {
        const Slot& slot = slots[index];
        if (!slot.active || strcmp(slot.config.name, name)) continue;
        if (!strcmp(key, "state")) {
            written = snprintf(out, out_len, "%s", source_state(slot));
        } else if (!strcmp(key, "error")) {
            const char* error = worker_failed ? "Source worker unavailable" : slot.error;
            if (error) written = snprintf(out, out_len, "%s", error);
        } else if (!strcmp(key, "age") && slot.success) {
            const uint64_t now = static_cast<uint64_t>(esp_timer_get_time() / 1000);
            written = snprintf(out, out_len, "%llu", static_cast<unsigned long long>((now - slot.last_success_ms) / 1000));
        } else if (!strcmp(key, "next") && !worker_failed && !slot.fetching && !ota_activity_is_active()) {
            const int32_t remaining = static_cast<int32_t>(slot.next_attempt - millis());
            const uint32_t seconds = remaining > 0 ? (static_cast<uint32_t>(remaining) + 999) / 1000 : 0;
            written = snprintf(out, out_len, "%u", static_cast<unsigned>(seconds));
        } else if (!strcmp(key, "duration") && slot.completed) {
            written = snprintf(out, out_len, "%u", static_cast<unsigned>(slot.last_duration_ms));
        } else if (!strcmp(key, "size") && slot.success) {
            written = snprintf(out, out_len, "%u", static_cast<unsigned>(slot.last_size));
        } else if (!strcmp(key, "http_status") && slot.completed && slot.last_http_status) {
            written = snprintf(out, out_len, "%u", static_cast<unsigned>(slot.last_http_status));
        }
        break;
    }
    xSemaphoreGive(mutex);
    return written >= 0 && static_cast<size_t>(written) < out_len;
}

#if HAS_MCP
ExternalDataInspectResult external_data_inspect(const char* name, const char* path,
                                               size_t offset, JsonObject result) {
    if (!mutex) return EXTERNAL_DATA_INSPECT_UNAVAILABLE;
    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(20)) != pdTRUE)
        return EXTERNAL_DATA_INSPECT_BUSY;
    if (!slots) {
        xSemaphoreGive(mutex);
        return EXTERNAL_DATA_INSPECT_UNAVAILABLE;
    }
    const Slot* match = nullptr;
    if (slots) {
        for (unsigned index = 0; index < EXTERNAL_DATA_MAX_SOURCES; ++index) {
            const Slot& slot = slots[index];
            if (slot.active && strcmp(slot.config.name, name) == 0) {
                match = &slot;
                break;
            }
        }
    }
    ExternalDataInspectResult status = EXTERNAL_DATA_INSPECT_NOT_FOUND;
    if (match) {
        result["source"] = JsonString(name, JsonString::Copied);
        result["state"] = source_state(*match);
        if (worker_failed) result["error"] = "Source worker unavailable";
        else if (match->error) result["error"] = match->error;
        const bool available = match->document && match->document_generation == match->generation;
        result["available"] = available;
        if (!available) status = EXTERNAL_DATA_INSPECT_OK;
        else {
            const uint64_t now = static_cast<uint64_t>(esp_timer_get_time() / 1000);
            result["age_seconds"] = (now - match->last_success_ms) / 1000;
            result["path"] = JsonString(path, JsonString::Copied);
            status = binding_value_inspect_json(match->document->as<JsonVariantConst>(), path, offset, result)
                         ? EXTERNAL_DATA_INSPECT_OK : EXTERNAL_DATA_INSPECT_BAD_PATH;
        }
    }
    xSemaphoreGive(mutex);
    return status;
}
#endif

void external_data_write_status(JsonArray output, const ExternalDataConfig& config, bool include_url) {
    const bool locked = mutex && xSemaphoreTake(mutex, pdMS_TO_TICKS(20)) == pdTRUE;
    for (uint32_t i = 0; i < config.count; ++i) {
        const auto& source = config.sources[i];
        JsonObject item = output.add<JsonObject>();
        item["name"] = JsonString(source.name, JsonString::Copied);
        if (include_url) item["url"] = JsonString(source.url, JsonString::Copied);
        item["refresh_seconds"] = source.refresh_seconds;
        const Slot* match = nullptr;
        if (locked && slots) {
            for (unsigned index = 0; index < EXTERNAL_DATA_MAX_SOURCES; ++index) {
                const Slot& slot = slots[index];
                if (slot.active && strcmp(slot.config.name, source.name) == 0) match = &slot;
            }
        }
        if (worker_failed || !mutex) {
            item["state"] = "error";
            item["error"] = "Source worker unavailable";
        } else if (!locked) {
            item["state"] = "busy";
        } else if (!match) {
            item["state"] = "waiting";
        } else {
            item["state"] = source_state(*match);
            if (match->error) item["error"] = match->error;
            if (match->success) {
                const uint64_t now = static_cast<uint64_t>(esp_timer_get_time() / 1000);
                item["age_seconds"] = (now - match->last_success_ms) / 1000;
            }
        }
    }
    if (locked) xSemaphoreGive(mutex);
}
#endif

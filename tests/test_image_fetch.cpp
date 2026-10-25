#include "image_fetch.h"
#include "net_activity.h"
#include "rtos_task_utils.h"
#include <HTTPClient.h>
#include <cassert>
#include <cstdlib>
#include <cstdio>

unsigned long millis() { return fetch_now; }
bool ota_activity_is_active() { return fetch_ota; }
void net_activity_mark(net_channel_t) {}

bool rtos_create_task_psram_stack_pinned(TaskFunction_t task, const char*, uint32_t,
    void*, UBaseType_t, TaskHandle_t* handle, RtosTaskPsramAlloc*, BaseType_t) {
    fetch_worker = task;
    *handle = reinterpret_cast<TaskHandle_t>(1);
    return true;
}

bool image_decode_to_rgb565(const uint8_t*, size_t, uint16_t width, uint16_t height,
    ImageScaleMode, uint16_t** pixels, size_t* size, uint16_t) {
    ++fetch_decodes;
    if (fetch_on_decode) {
        auto hook = std::move(fetch_on_decode);
        fetch_on_decode = nullptr;
        hook();
    }
    fetch_decoded = true;
    if (!fetch_decode_ok) return false;
    *size = width * height * 2;
    *pixels = static_cast<uint16_t*>(malloc(*size));
    (*pixels)[0] = static_cast<uint16_t>(fetch_decodes);
    return true;
}

static void step() {
    fetch_decoded = false;
    fetch_delays = 0;
    try { fetch_worker(nullptr); } catch (const FetchStepComplete&) {}
}

static image_slot_t request(const char* url, uint32_t interval = 0) {
    const auto slot = image_fetch_request(url, "", "", 2, 2, interval);
    assert(slot != IMAGE_SLOT_INVALID);
    return slot;
}

static const uint16_t* frame(image_slot_t slot) {
    uint16_t width = 0, height = 0;
    const auto* pixels = image_fetch_get_frame(slot, &width, &height);
    assert(!pixels || (width == 2 && height == 2));
    return pixels;
}

int main() {
    image_fetch_init();
    auto authenticated = image_fetch_request("https://camera/private", "camera", "test-password", 2, 2, 0);
    assert(authenticated != IMAGE_SLOT_INVALID);
    step();
    assert(!fetch_authorizations.back().empty());
    image_fetch_update_source(authenticated, "http://public/image");
    step();
    assert(fetch_authorizations.back().empty());
    image_fetch_cancel(authenticated);

    authenticated = image_fetch_request("https://camera/private", "camera", "test-password", 2, 2, 0);
    assert(authenticated != IMAGE_SLOT_INVALID);
    step();
    assert(!fetch_authorizations.back().empty());
    image_fetch_cancel(authenticated);
    const auto reused = request("");
    assert(reused == authenticated);
    image_fetch_update_source(reused, "https://public/image");
    step();
    assert(fetch_authorizations.back().empty());
    image_fetch_cancel_all();
    step();
    assert(fetch_live_clients == 0);

    assert(image_fetch_url_valid("https://images.example/cover.jpg"));
    assert(image_fetch_url_valid("http://[::1]:8080/image"));
    for (const char* url : {"ftp://host/image", "http:///image", "https://u:p@host/image",
            "https://host/[mqtt:image]", "https://host/ERR:unknown", "https://host/a b",
            "https://host:0/image", "https://host:65536/image", "http://[]/image"}) {
        assert(!image_fetch_url_valid(url));
    }
        std::string longest = "https://host/" + std::string(242, 'a');
        assert(longest.size() == 255 && image_fetch_url_valid(longest.c_str()));
        longest += 'a';
        assert(!image_fetch_url_valid(longest.c_str()));
    auto slot = request("https://host/a");
    step();
    const auto* original = frame(slot);
    assert(original);
    const size_t requests = fetch_requests.size();
    assert(!image_fetch_update_source(slot, "https://host/a"));
    step();
    assert(fetch_requests.size() == requests);

    assert(image_fetch_update_source(slot, "https://host/b"));
    assert(frame(slot) == original);
    fetch_on_get = [slot] {
        image_fetch_update_source(slot, "https://host/c");
        image_fetch_update_source(slot, "https://host/d");
    };
    step();
    assert(fetch_requests[fetch_requests.size() - 2] == "https://host/b");
    assert(fetch_requests.back() == "https://host/d");
    assert(image_fetch_has_new_frame(slot));
    assert(frame(slot) != original);

    const auto* retained = frame(slot);
    image_fetch_update_source(slot, "https://host/failure");
    fetch_response.status = 503;
    step();
    assert(frame(slot) == retained);
    const size_t failed_requests = fetch_requests.size();
    fetch_response.status = 200;
    step();
    assert(fetch_requests.size() == failed_requests);

    image_fetch_update_source(slot, "https://host/decode-failure");
    fetch_decode_ok = false;
    step();
    assert(frame(slot) == retained);
    fetch_decode_ok = true;
    image_fetch_update_source(slot, "https://host/incomplete");
    fetch_response.length = 20;
    const unsigned decodes = fetch_decodes;
    step();
    assert(fetch_decodes == decodes);
    assert(frame(slot) == retained);
    fetch_response.length = 5;

    image_fetch_update_source(slot, "https://host/clearing");
    fetch_on_read = [slot] { image_fetch_update_source(slot, ""); };
    step();
    assert(!frame(slot));
    assert(!image_fetch_has_new_frame(slot));
    assert(fetch_decodes == decodes);

    image_fetch_update_source(slot, "https://host/old");
    fetch_on_decode = [slot] {
        image_fetch_cancel(slot);
        assert(request("https://host/new") == slot);
    };
    step();
    assert(!frame(slot));
    step();
    assert(fetch_requests.back() == "https://host/new");
    assert(frame(slot));

    image_fetch_update_source(slot, "https://host/paused");
    image_fetch_pause_slot(slot);
    const size_t paused_requests = fetch_requests.size();
    step();
    assert(fetch_requests.size() == paused_requests);
    image_fetch_resume_slot(slot);
    step();
    assert(fetch_requests.back() == "https://host/paused");

    image_fetch_update_source(slot, "https://host/ota");
    fetch_on_read = [] { fetch_ota = true; };
    step();
    const size_t ota_requests = fetch_requests.size();
    fetch_ota = false;
    step();
    assert(fetch_requests.size() == ota_requests + 1);
    image_fetch_cancel_all();

    slot = request("");
    const size_t idle_requests = fetch_requests.size();
    step();
    assert(fetch_requests.size() == idle_requests);
    image_fetch_update_source(slot, "https://host/stream");
    fetch_response.content_type = "multipart/x-mixed-replace; boundary=frame";
    fetch_response.body = "--frame\r\nContent-Length: 5\r\n\r\nimage\r\n"
                          "--frame\r\nContent-Length: 5\r\n\r\nimage\r\n";
    fetch_response.length = -1;
    step();
    assert(frame(slot));
    const size_t stream_requests = fetch_requests.size();
    const unsigned stream_decodes = fetch_decodes;
    fetch_on_read = [slot] {
        image_fetch_update_source(slot, "https://host/snapshot");
        fetch_response = FetchResponse{};
    };
    step();
    assert(fetch_requests.size() == stream_requests + 1);
    assert(fetch_requests.back() == "https://host/snapshot");
    assert(fetch_decodes == stream_decodes + 1);
    assert(frame(slot));

    image_fetch_update_source(slot, "https://host/suspended");
    fetch_on_read = [] { image_fetch_suspend(); };
    step();
    const size_t suspended_requests = fetch_requests.size();
    image_fetch_unsuspend();
    step();
    assert(fetch_requests.size() == suspended_requests + 1);
    assert(frame(slot));
    image_fetch_cancel_all();

    fetch_response = FetchResponse{};
    const auto cleared = request("https://host/clear-idle");
    step();
    const auto cancelled = request("https://host/cancel-idle");
    step();
    const auto streaming = request("https://host/busy-stream");
    fetch_response.content_type = "multipart/x-mixed-replace; boundary=frame";
    fetch_response.body = "--frame\r\nContent-Length: 5\r\n\r\nimage\r\n"
                          "--frame\r\nContent-Length: 5\r\n\r\nimage\r\n";
    fetch_response.length = -1;
    step();
    assert(fetch_live_clients == 3);
    image_fetch_update_source(cleared, "");
    image_fetch_cancel(cancelled);
    const unsigned busy_decodes = fetch_decodes;
    step();
    assert(fetch_decodes == busy_decodes + 1);
    assert(frame(streaming));
    assert(fetch_live_clients == 1);
    image_fetch_cancel_all();
    step();
    assert(fetch_live_clients == 0);

    slot = request("https://host/header-ota");
    fetch_response.body = std::string(100, 'x') + "\r\n" + fetch_response.body;
    const unsigned header_decodes = fetch_decodes;
    fetch_on_byte_read = [] { fetch_ota = true; };
    step();
    assert(fetch_decodes == header_decodes);
    assert(!frame(slot));
    assert(fetch_live_clients == 0);
    fetch_ota = false;
    fetch_response = FetchResponse{};
    step();
    assert(frame(slot));
    assert(fetch_authorizations.back().empty());
    image_fetch_cancel_all();

    slot = request("https://host/periodic", 1000);
    step();
    const size_t periodic_requests = fetch_requests.size();
    step();
    assert(fetch_requests.size() == periodic_requests);
    fetch_now += 1000;
    step();
    assert(fetch_requests.size() == periodic_requests + 1);
    image_fetch_cancel_all();
    std::puts("PASS: image sources, stale completion, retention, clear, reuse, streaming, pause, OTA and refresh");
}
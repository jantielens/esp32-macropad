#include <cassert>
#include "../src/app/reboot_service.cpp"

#define DEVICE_TELEMETRY_H
size_t device_telemetry_free_sketch_space();
#include "../src/app/web_portal_ota.cpp"

static uint32_t clock_ms = 0;
static unsigned restarts = 0;
static unsigned audio_requests = 0;
static AudioRebootStatus audio_status = AUDIO_REBOOT_PENDING;

unsigned long millis() { return clock_ms; }
void esp_restart() { ++restarts; }
void audio_request_reboot() { ++audio_requests; }
AudioRebootStatus audio_reboot_status() { return audio_status; }
bool portal_auth_gate(AsyncWebServerRequest* request) { return request->authorized; }
size_t device_telemetry_free_sketch_space() { return 1024; }
void net_activity_mark(net_channel_t) {}

static void reset_test() {
    ota_activity_finish();
    ota_activity_finish_network_transition();
    reboot_pending = false;
    reboot_ota_complete = false;
    reboot_owns_transition = false;
    reboot_not_before = 0;
    reboot_shutdown_started = false;
    reboot_shutdown_at = 0;
    clock_ms = 0;
    restarts = 0;
    audio_requests = 0;
    audio_status = AUDIO_REBOOT_PENDING;
    Update = TestFirmwareUpdate{};
}

int main() {
    reset_test();
    assert(ota_activity_try_begin_config_write());
    reboot_request(100);
    reboot_service_loop();
    assert(audio_requests == 0 && restarts == 0);
    ota_activity_finish_config_write();
    reboot_service_loop();
    assert(audio_requests == 1 && !ota_activity_try_begin());
    audio_status = AUDIO_REBOOT_COMPLETE;
    clock_ms = 99;
    reboot_service_loop();
    assert(restarts == 0);
    clock_ms = 100;
    reboot_service_loop();
    assert(restarts == 1);

    reset_test();
    assert(ota_activity_try_begin());
    reboot_request(0);
    reboot_service_loop();
    assert(audio_requests == 0);
    reboot_request(500, REBOOT_OTA_COMPLETE);
    reboot_service_loop();
    assert(audio_requests == 1);
    audio_status = AUDIO_REBOOT_ERROR;
    clock_ms = 500;
    reboot_service_loop();
    assert(restarts == 1 && ota_activity_is_active());

    reset_test();
    assert(ota_activity_try_begin_network_transition());
    reboot_request(200, REBOOT_NETWORK_TRANSITION);
    reboot_service_loop();
    clock_ms = 999;
    reboot_service_loop();
    assert(restarts == 0 && audio_requests == 1);
    clock_ms = 1000;
    reboot_service_loop();
    assert(restarts == 1 && audio_requests == 1);

    reset_test();
    clock_ms = UINT32_MAX - 50;
    reboot_request(100);
    reboot_service_loop();
    audio_status = AUDIO_REBOOT_COMPLETE;
    clock_ms = 20;
    reboot_request(100);
    clock_ms = 48;
    reboot_service_loop();
    assert(restarts == 0 && audio_requests == 1);
    clock_ms = 49;
    reboot_service_loop();
    assert(restarts == 1);

    reset_test();
    audio_status = AUDIO_REBOOT_COMPLETE;
    reboot_request(100);
    for (clock_ms = 0; clock_ms < 100; clock_ms += 10) {
        reboot_request(100);
        reboot_service_loop();
        assert(restarts == 0);
    }
    reboot_request(100);
    reboot_service_loop();
    assert(restarts == 1 && audio_requests == 1);

    reset_test();
    assert(ota_activity_try_begin());
    reboot_request(0);
    reboot_request(500, REBOOT_OTA_COMPLETE);
    clock_ms = 100;
    reboot_request(500, REBOOT_OTA_COMPLETE);
    audio_status = AUDIO_REBOOT_COMPLETE;
    clock_ms = 499;
    reboot_service_loop();
    assert(restarts == 0);
    clock_ms = 500;
    reboot_service_loop();
    assert(restarts == 1);

    reset_test();
    assert(ota_activity_try_begin());
    reboot_request(0);
    reboot_service_loop();
    ota_activity_finish();
    reboot_service_loop();
    assert(audio_requests == 1 && !ota_activity_try_begin_config_write());

    reset_test();
    AsyncWebServerRequest interrupted;
    interrupted.body_length = 4;
    uint8_t bytes[4] = {};
    handleOTAUpload(&interrupted, "test.bin", 0, bytes, 2, false);
    assert(ota_activity_is_active() && Update.writes == 1);
    reboot_request(0);
    reboot_service_loop();
    assert(audio_requests == 0);
    Update.on_abort = [] {
        assert(ota_activity_is_active());
        assert(!ota_activity_try_begin_config_write());
    };
    interrupted.disconnect();
    assert(Update.aborts == 1 && !ota_activity_is_active());
    audio_status = AUDIO_REBOOT_COMPLETE;
    reboot_service_loop();
    assert(restarts == 1 && audio_requests == 1);

    reset_test();
    AsyncWebServerRequest owner;
    AsyncWebServerRequest rejected;
    owner.body_length = rejected.body_length = 4;
    handleOTAUpload(&owner, "test.bin", 0, bytes, 2, false);
    handleOTAUpload(&rejected, "test.bin", 0, bytes, 2, false);
    assert(rejected.response_code == 409);
    handleOTAUpload(&rejected, "test.bin", 2, bytes, 2, true);
    rejected.disconnect();
    assert(Update.writes == 1 && Update.aborts == 0 && ota_activity_is_active());
    owner.disconnect();
    assert(Update.aborts == 1 && !ota_activity_is_active());

    AsyncWebServerRequest replacement;
    replacement.body_length = 4;
    handleOTAUpload(&replacement, "test.bin", 0, bytes, 2, false);
    owner.disconnect();
    assert(Update.aborts == 1 && ota_activity_is_active());
    handleOTAUpload(&replacement, "test.bin", 2, bytes, 2, true);
    assert(replacement.response_code == 200);
    replacement.disconnect();
    assert(Update.aborts == 1 && ota_activity_is_active());
    audio_status = AUDIO_REBOOT_COMPLETE;
    clock_ms = 499;
    reboot_service_loop();
    assert(restarts == 0);
    clock_ms = 500;
    reboot_service_loop();
    assert(restarts == 1);

    for (unsigned failure = 0; failure < 3; ++failure) {
        reset_test();
        AsyncWebServerRequest failed;
        failed.body_length = 4;
        Update.begin_ok = failure != 0;
        Update.write_ok = failure != 1;
        Update.end_ok = failure != 2;
        handleOTAUpload(&failed, "test.bin", 0, bytes, 4, true);
        assert(failed.response_code == 500 && Update.aborts == 1);
        assert(!ota_activity_is_active());
        failed.disconnect();
        assert(Update.aborts == 1);
    }
}
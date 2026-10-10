#!/usr/bin/env python3
import pathlib
import subprocess
import tempfile


root = pathlib.Path(__file__).resolve().parents[1]
arduino_stub = r'''
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <cassert>
struct TestSerial {
    unsigned long baud = 0;
    bool connected = false;
    std::string output;
#if ARDUINO_USB_CDC_ON_BOOT && ARDUINO_USB_MODE
    uint32_t timeout = 100;
    int capacity = 1024;
    void setTxTimeoutMs(uint32_t value) { timeout = value; }
    int availableForWrite() { assert(timeout == 0); return capacity; }
#endif
    void begin(unsigned long value) { baud = value; }
    explicit operator bool() const { return connected; }
    void print(const char* value) {
#if ARDUINO_USB_CDC_ON_BOOT && ARDUINO_USB_MODE
        assert(timeout == 0 && capacity >= static_cast<int>(std::string(value).size()));
#endif
        output += value;
    }
};
extern TestSerial Serial;
inline unsigned long millis() { return 42; }
'''
harness = r'''
#include "log_manager.h"
#include <cassert>
TestSerial Serial;
std::string remoteOutput;
void remote_log_init() {}
bool remote_log_available() { return true; }
void remote_log_append(const char* line) { remoteOutput += line; }
int main() {
    log_write(LOG_LEVEL_INFO, "SYS", "Before init");
    assert(Serial.output.empty());
    log_init(115200);
    assert(Serial.baud == 115200);
    log_write(LOG_LEVEL_INFO, "SYS", "Boot %d", 7);
#if ARDUINO_USB_CDC_ON_BOOT
    assert(Serial.output.empty());
    Serial.connected = true;
    log_write(LOG_LEVEL_INFO, "SYS", "Boot %d", 7);
#endif
    assert(Serial.output == "[42ms] I SYS: Boot 7\n");
    Serial.connected = false;
    log_write(LOG_LEVEL_ERROR, "SYS", "Disconnected");
#if ARDUINO_USB_CDC_ON_BOOT
    assert(Serial.output == "[42ms] I SYS: Boot 7\n");
#else
    assert(Serial.output == "[42ms] I SYS: Boot 7\n[42ms] E SYS: Disconnected\n");
#endif
#if ARDUINO_USB_CDC_ON_BOOT && ARDUINO_USB_MODE
    Serial.connected = true;
    const std::string previous = Serial.output;
    Serial.capacity = 0;
    log_write(LOG_LEVEL_INFO, "SYS", "Host not reading");
    assert(Serial.output == previous);
#if HAS_REMOTE_LOG
    assert(remoteOutput.find("Host not reading") != std::string::npos);
#endif
    Serial.capacity = 1;
    log_write(LOG_LEVEL_INFO, "SYS", "Insufficient space");
    assert(Serial.output == previous);
    Serial.connected = false;
    log_write(LOG_LEVEL_INFO, "SYS", "USB unplugged");
    Serial.connected = true;
    log_write(LOG_LEVEL_INFO, "SYS", "USB reconnected without reader");
    assert(Serial.output == previous);
#if HAS_REMOTE_LOG
    assert(remoteOutput.find("USB reconnected without reader") != std::string::npos);
#endif
    Serial.capacity = 1024;
    log_write(LOG_LEVEL_INFO, "SYS", "Host reading again");
    assert(Serial.output == previous + "[42ms] I SYS: Host reading again\n");
#endif
}
'''

with tempfile.TemporaryDirectory() as directory:
    temporary = pathlib.Path(directory)
    (temporary / "Arduino.h").write_text(arduino_stub)
    (temporary / "board_config.h").write_text("#pragma once\n")
    (temporary / "remote_log.h").write_text(
        "#pragma once\nvoid remote_log_init();\nbool remote_log_available();\n"
        "void remote_log_append(const char* line);\n")
    for filename in ("log_manager.cpp", "log_manager.h"):
        (temporary / filename).write_text((root / "src/app" / filename).read_text())
    (temporary / "test.cpp").write_text(harness)
    for hid, cdc, mode, remote in (
        (1, 0, 0, 0), (0, 0, 0, 0), (0, 1, 0, 0),
        (0, 1, 1, 0), (0, 1, 1, 1),
    ):
        executable = temporary / f"test_{hid}_{cdc}_{mode}_{remote}"
        subprocess.run([
            "c++", "-std=c++11", "-Wall", "-Wextra", "-Werror",
            f"-DHAS_USB_HID={hid}", f"-DARDUINO_USB_CDC_ON_BOOT={cdc}",
            f"-DARDUINO_USB_MODE={mode}", f"-DHAS_REMOTE_LOG={remote}",
            "-I", directory, str(temporary / "log_manager.cpp"),
            str(temporary / "test.cpp"), "-o", str(executable),
        ], check=True)
        subprocess.run([str(executable)], check=True)
print("PASS: UART/native CDC behavior preserved; hardware CDC drops blocked output and preserves portal logs")
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
struct TestSerial {
    unsigned long baud = 0;
    bool connected = false;
    std::string output;
    void begin(unsigned long value) { baud = value; }
    explicit operator bool() const { return connected; }
    void print(const char* value) { output += value; }
};
extern TestSerial Serial;
inline unsigned long millis() { return 42; }
'''
harness = r'''
#include "log_manager.h"
#include <cassert>
TestSerial Serial;
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
}
'''

with tempfile.TemporaryDirectory() as directory:
    temporary = pathlib.Path(directory)
    (temporary / "Arduino.h").write_text(arduino_stub)
    (temporary / "board_config.h").write_text("#pragma once\n")
    for filename in ("log_manager.cpp", "log_manager.h"):
        (temporary / filename).write_text((root / "src/app" / filename).read_text())
    (temporary / "test.cpp").write_text(harness)
    for hid, cdc in ((1, 0), (0, 0), (0, 1)):
        executable = temporary / f"test_{hid}_{cdc}"
        subprocess.run([
            "c++", "-std=c++11", "-Wall", "-Wextra", "-Werror",
            f"-DHAS_USB_HID={hid}", f"-DARDUINO_USB_CDC_ON_BOOT={cdc}",
            "-I", directory, str(temporary / "log_manager.cpp"),
            str(temporary / "test.cpp"), "-o", str(executable),
        ], check=True)
        subprocess.run([str(executable)], check=True)
print("PASS: HID and UART logs do not require USB; CDC connection checks are preserved")
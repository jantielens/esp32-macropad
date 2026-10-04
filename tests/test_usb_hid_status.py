#!/usr/bin/env python3
import pathlib
import subprocess
import tempfile


root = pathlib.Path(__file__).resolve().parents[1]
source = (root / "src/app/usb_hid.cpp").read_text()
status_function = source.split("const char* usb_hid_status() {", 1)[1].split("\n}", 1)[0]
harness = r'''
#include <atomic>
#include <cassert>
#include <cstring>
std::atomic<bool> initialized{false};
void* hid = nullptr;
bool mounted = false;
bool suspended = false;
bool tud_mounted() { return mounted; }
bool tud_suspended() { return suspended; }
const char* usb_hid_status() {
''' + status_function + r'''
}
void expect_status(const char* expected) {
    assert(std::strcmp(usb_hid_status(), expected) == 0);
}
int main() {
    expect_status("disabled");
    int device = 0;
    hid = &device;
    expect_status("error");
    initialized.store(true);
    expect_status("ready");
    mounted = true;
    expect_status("connected");
    suspended = true;
    expect_status("suspended");
    suspended = false;
    expect_status("connected");
    mounted = false;
    expect_status("ready");
    suspended = true;
    expect_status("ready");
    initialized.store(false);
    expect_status("error");
    hid = nullptr;
    expect_status("disabled");
}
'''

with tempfile.TemporaryDirectory() as directory:
    temporary = pathlib.Path(directory)
    test_source = temporary / "test.cpp"
    test_source.write_text(harness)
    executable = temporary / "test_usb_hid_status"
    subprocess.run([
        "c++", "-std=c++11", "-Wall", "-Wextra", "-Werror",
        str(test_source), "-o", str(executable),
    ], check=True)
    subprocess.run([str(executable)], check=True)
print("PASS: USB HID status distinguishes startup, enumeration, suspend, resume, and disconnect")
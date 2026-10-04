#!/usr/bin/env python3
import pathlib
import subprocess
import tempfile


root = pathlib.Path(__file__).resolve().parents[1]
source = (root / "src/app/drivers/gt911_touch_driver.cpp").read_text()
init_start = source.index("void GT911_TouchDriver::init()")
init_end = source.index("\nTouchReadStatus GT911_TouchDriver::gt911Read", init_start)
write_start = source.index("bool GT911_TouchDriver::writeReg(")
write_end = source.index("\nbool GT911_TouchDriver::readBlock", write_start)

harness = r'''
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#define TOUCH_RST -1
#define TOUCH_I2C_SDA 8
#define TOUCH_I2C_SCL 9
#define TOUCH_I2C_ADDR 0x5d
#define DISPLAY_WIDTH 100
#define DISPLAY_HEIGHT 200
#define GT911_POINT_INFO 0x814e
#define GT911_WIRE_NAME "MockWire"
static unsigned errors = 0;
static unsigned initialized = 0;
static unsigned lock_count = 0;
static int lock_depth = 0;
static void log_info(const char* format) {
    if (std::strstr(format, "Touch initialized")) ++initialized;
}
#define LOGI(tag, format, ...) log_info(format)
#define LOGE(...) (++errors)
#define GT911_I2C_LOCK() (++lock_count, ++lock_depth)
#define GT911_I2C_UNLOCK() (--lock_depth)
static uint8_t highByte(uint16_t value) { return value >> 8; }
static uint8_t lowByte(uint16_t value) { return value & 0xff; }
struct MockWire {
    std::array<uint8_t, 2> results{};
    std::array<uint8_t, 3> bytes{};
    unsigned transmissions = 0;
    unsigned writes = 0;
    void begin(int, int, int) {}
    void beginTransmission(uint8_t address) { assert(address == TOUCH_I2C_ADDR); }
    uint8_t endTransmission() {
        assert(transmissions < results.size());
        return results[transmissions++];
    }
    void write(uint8_t value) {
        assert(lock_depth == 1);
        assert(writes < bytes.size());
        bytes[writes++] = value;
    }
};
static MockWire wire;
#define GT911_WIRE wire
class GT911_TouchDriver {
    uint8_t addr = TOUCH_I2C_ADDR;
public:
    void init();
    bool writeReg(uint16_t reg, uint8_t value);
};
'''
harness += source[init_start:init_end] + source[write_start:write_end]
harness += r'''
int main() {
    for (const auto results : {std::array<uint8_t, 2>{0, 0},
                               std::array<uint8_t, 2>{0, 4},
                               std::array<uint8_t, 2>{4, 0}}) {
        wire = MockWire{};
        wire.results = results;
        errors = initialized = lock_count = 0;
        lock_depth = 0;
        GT911_TouchDriver driver;
        driver.init();
        assert(lock_depth == 0);
        if (results[0]) {
            assert(wire.transmissions == 1 && wire.writes == 0 && lock_count == 0);
        } else {
            assert(wire.transmissions == 2 && wire.writes == 3 && lock_count == 1);
            assert(wire.bytes[0] == 0x81 && wire.bytes[1] == 0x4e && wire.bytes[2] == 0);
        }
        assert(initialized == unsigned(!results[0] && !results[1]));
        assert(errors == unsigned(results[0] || results[1]));
    }
}
'''

with tempfile.TemporaryDirectory() as directory:
    test_source = pathlib.Path(directory) / "gt911.cpp"
    executable = pathlib.Path(directory) / "gt911"
    test_source.write_text(harness)
    subprocess.run(["c++", "-std=c++11", "-Wall", "-Wextra", "-Werror",
                    str(test_source), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
print("PASS: GT911 initialization checks pending-data clear and releases the I2C lock")

snapshot_harness = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>
#include "touch_sample.h"
#define DISPLAY_WIDTH 100
#define DISPLAY_HEIGHT 200
#define GT911_POINT_INFO 0x814e
#define GT911_POINT_1 0x814f
static int lock_depth = 0;
#define LOGI(...) assert(lock_depth == 0)
static unsigned transforms = 0;
#define GT911_I2C_LOCK() (++lock_depth)
#define GT911_I2C_UNLOCK() (--lock_depth)
static uint8_t status_byte = 0x82;
static std::vector<uint8_t> records{3, 10, 0, 20, 0, 7, 0, 99, 9, 30, 0, 40, 0, 8, 0, 88};
static bool short_read = false, ack_ok = true;
static unsigned acks = 0;
class GT911_TouchDriver {
    uint8_t rotation = 0;
    bool calibrationEnabled = false;
    uint16_t calXMin = 0, calXMax = 0, calYMin = 0, calYMax = 0;
    TouchSnapshot lastSnapshot;
    TouchSnapshot transformedSnapshot;
    bool transformPending = true;
    TouchReadStatus gt911Read();
    void applyRotation(uint16_t&, uint16_t&) const;
    void transform(uint16_t&, uint16_t&) const;
    bool readBlock(uint16_t reg, uint8_t* output, uint8_t length) {
        assert(lock_depth == 1);
        if (reg == GT911_POINT_INFO) { *output = status_byte; return true; }
        assert(reg == GT911_POINT_1 && length == (status_byte & 15) * 8);
        if (short_read || records.size() < length) return false;
        std::memcpy(output, records.data(), length);
        return true;
    }
    bool writeReg(uint16_t reg, uint8_t value) {
        assert(lock_depth == 1 && reg == GT911_POINT_INFO && value == 0);
        ++acks;
        return ack_ok;
    }
public:
    bool isTouched();
    bool getTouch(uint16_t*, uint16_t*, uint16_t*);
    TouchSample readSample();
    TouchSnapshot readSnapshot();
    void setCalibration(uint16_t, uint16_t, uint16_t, uint16_t);
    void setRotation(uint8_t);
};
'''
snapshot_harness += source[init_end:write_start].replace(
    "void GT911_TouchDriver::transform(uint16_t& tx, uint16_t& ty) const {",
    "void GT911_TouchDriver::transform(uint16_t& tx, uint16_t& ty) const { ++transforms; assert(lock_depth == 0);")
snapshot_harness += source[source.index("void GT911_TouchDriver::applyRotation"):]
snapshot_harness += r'''
int main() {
    GT911_TouchDriver driver;
    auto snapshot = driver.readSnapshot();
    assert(snapshot.status == TouchReadStatus::Fresh && snapshot.count == 2 && lock_depth == 0);
    assert(snapshot.contacts[0].id == 3 && snapshot.contacts[1].id == 9);
    assert(snapshot.contacts[1].horizontal == 30 && snapshot.contacts[1].vertical == 40);
    std::swap_ranges(records.begin(), records.begin() + 8, records.begin() + 8);
    snapshot = driver.readSnapshot();
    assert(snapshot.contacts[0].id == 9 && snapshot.contacts[1].id == 3);
    for (unsigned rotation = 0; rotation < 4; ++rotation) {
        driver.setRotation(rotation);
        snapshot = driver.readSnapshot();
        const uint16_t horizontal[] = {30, 40, 69, 159};
        const uint16_t vertical[] = {40, 69, 159, 30};
        assert(snapshot.contacts[0].horizontal == horizontal[rotation]);
        assert(snapshot.contacts[0].vertical == vertical[rotation]);
    }
    driver.setRotation(0);
    const auto committed = driver.readSnapshot();
    status_byte = 0;
    const unsigned cached_transforms = transforms;
    snapshot = driver.readSnapshot();
    assert(snapshot.status == TouchReadStatus::Unchanged && transforms == cached_transforms);
    driver.setRotation(1);
    snapshot = driver.readSnapshot();
    assert(snapshot.contacts[0].horizontal == 40 && snapshot.contacts[0].vertical == 69);
    assert(transforms == cached_transforms + 2);
    driver.setRotation(0);
    driver.setCalibration(0, 198, 0, 398);
    snapshot = driver.readSnapshot();
    assert(snapshot.contacts[0].horizontal == 15 && snapshot.contacts[0].vertical == 20);
    driver.setCalibration(0, 99, 0, 199);
    snapshot = driver.readSnapshot();
    assert(snapshot.contacts[0].horizontal == committed.contacts[0].horizontal);
    status_byte = 0x82;
    short_read = true;
    const unsigned before_error = transforms;
    assert(driver.readSnapshot().status == TouchReadStatus::Error && lock_depth == 0);
    assert(transforms == before_error);
    short_read = false;
    ack_ok = false;
    records[1] = 55;
    snapshot = driver.readSnapshot();
    assert(snapshot.status == TouchReadStatus::Error && snapshot.contacts[0].horizontal == committed.contacts[0].horizontal);
    ack_ok = true;
    const unsigned prior_acks = acks;
    records[8] = records[0];
    assert(driver.readSnapshot().status == TouchReadStatus::Error && acks == prior_acks);
    records[8] = 16;
    assert(driver.readSnapshot().status == TouchReadStatus::Error && acks == prior_acks);
    status_byte = 0x86;
    assert(driver.readSnapshot().status == TouchReadStatus::Error && acks == prior_acks);
    status_byte = 0;
    snapshot = driver.readSnapshot();
    assert(snapshot.status == TouchReadStatus::Unchanged && snapshot.count == 2 && acks == prior_acks);
    assert(snapshot.contacts[0].horizontal == committed.contacts[0].horizontal);
    status_byte = 0x80;
    assert(driver.readSnapshot().count == 0 && lock_depth == 0);
    status_byte = 0x81;
    records[0] = 3;
    driver.setCalibration(0, 99, 0, 199);
    snapshot = driver.readSnapshot();
    assert(snapshot.count == 1 && snapshot.contacts[0].horizontal == 55);
    const auto sample = driver.readSample();
    assert(sample.pressed && sample.horizontal == 55 && sample.vertical == 40);
    records[1] = 56;
    assert(driver.isTouched());
    status_byte = 0;
    snapshot = driver.readSnapshot();
    assert(snapshot.contacts[0].horizontal == 56);
    records.resize(40);
    for (unsigned index = 0; index < 5; ++index) {
        records[index * 8] = index;
        records[index * 8 + 1] = 10 + index;
        records[index * 8 + 3] = 20 + index;
    }
    status_byte = 0x85;
    snapshot = driver.readSnapshot();
    assert(snapshot.count == 5 && snapshot.status == TouchReadStatus::Fresh);
    for (unsigned index = 0; index < 5; ++index) {
        assert(snapshot.contacts[index].id == index);
        assert(snapshot.contacts[index].horizontal == 10 + index);
        assert(snapshot.contacts[index].vertical == 20 + index);
    }
}
'''
with tempfile.TemporaryDirectory() as directory:
    test_source = pathlib.Path(directory) / "snapshots.cpp"
    executable = pathlib.Path(directory) / "snapshots"
    test_source.write_text(snapshot_harness)
    subprocess.run(["c++", "-std=c++11", "-Wall", "-Wextra", "-Werror",
                    "-I", str(root / "src/app"), str(test_source), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
print("PASS: GT911 coherent multi-contact snapshots, IDs, errors, acknowledgement, and rotation")
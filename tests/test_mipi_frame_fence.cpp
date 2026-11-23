#include "../src/app/drivers/mipi_frame_fence.h"
#include <lvgl.h>
#include <algorithm>
#include <array>
#include <cassert>

static void testFence() {
    MipiFrameFence fence;
    assert(!fence.complete());
    // Unknown interrupt core requires the conservative two completions.
    fence.arm(1);
    assert(!fence.complete());
    assert(fence.complete());
    assert(!fence.complete());

    MipiFrameFence firstObservation;
    firstObservation.arm(1);
    firstObservation.observeCore(1);
    assert(!firstObservation.complete());
    assert(firstObservation.complete());

    fence.observeCore(1);
    // An interrupt before arming is not an acknowledgement of this submission.
    assert(!fence.complete());
    fence.arm(1);
    assert(fence.complete());
    assert(!fence.complete());

    // A cross-core interrupt may have selected the old buffer before submission.
    fence.arm(0);
    assert(!fence.complete());
    assert(fence.complete());

    fence.arm(MipiFrameFence::unknownCore);
    assert(!fence.complete());
    assert(fence.complete());
    fence.arm(0x7fffffff); // FreeRTOS tskNO_AFFINITY
    assert(!fence.complete());
    assert(fence.complete());

    // Conflicting observations permanently disable the fast path.
    fence.observeCore(0);
    fence.observeCore(1);
    fence.arm(1);
    assert(!fence.complete());
    assert(fence.complete());

    MipiFrameFence invalidCore;
    invalidCore.observeCore(-1);
    invalidCore.observeCore(1);
    invalidCore.arm(1);
    assert(!invalidCore.complete());
    assert(invalidCore.complete());
}

static void testOwnership() {
    int first = 0, second = 0, other = 0;
    MipiDirectFrame frame;
    assert(!frame.inactive(&first, &second));
    frame.reset(&first);
    assert(frame.inactive(&first, &second) == &second);
    assert(!frame.inactive(&first, &first));
    assert(frame.accept(&first, &first, &second, true) == MipiDirectFrame::Flush::Invalid);
    assert(frame.accept(&other, &first, &second, true) == MipiDirectFrame::Flush::Invalid);
    assert(frame.accept(&second, &first, &second, false) == MipiDirectFrame::Flush::Pending);
    assert(!frame.inactive(&first, &second));
    assert(frame.accept(&first, &first, &second, true) == MipiDirectFrame::Flush::Invalid);
    assert(frame.accept(&second, &first, &second, true) == MipiDirectFrame::Flush::Present);
    frame.complete();
    assert(frame.inactive(&first, &second) == &first);
    assert(frame.accept(&second, &first, &second, true) == MipiDirectFrame::Flush::Invalid);
    assert(frame.accept(&first, &first, &second, true) == MipiDirectFrame::Flush::Present);
}

struct DirectFixture {
    static constexpr int width = 64;
    std::array<uint16_t, width * width> first{};
    std::array<uint16_t, width * width> second{};
    std::array<uint16_t, width * width> presented{};
    const uint16_t* scanout = first.data();
    MipiDirectFrame frame;
    unsigned flushes = 0;
    unsigned presentations = 0;
};

static void testDirtyFrames() {
    lv_init();
    DirectFixture f;
    f.frame.reset(f.first.data());
    lv_display_t* display = lv_display_create(f.width, f.width);
    assert(display);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, f.second.data(), f.first.data(),
                           sizeof(f.first), LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_user_data(display, &f);
    lv_display_set_flush_cb(display, [](lv_display_t* d, const lv_area_t*, uint8_t* pixels) {
        auto& fixture = *static_cast<DirectFixture*>(lv_display_get_user_data(d));
        assert(std::equal(fixture.presented.begin(), fixture.presented.end(), fixture.scanout));
        const auto result = fixture.frame.accept(pixels, fixture.first.data(), fixture.second.data(),
                                                 lv_display_flush_is_last(d));
        assert(result != MipiDirectFrame::Flush::Invalid);
        ++fixture.flushes;
        if (result == MipiDirectFrame::Flush::Present) {
            fixture.scanout = reinterpret_cast<const uint16_t*>(pixels);
            std::copy_n(fixture.scanout, fixture.presented.size(), fixture.presented.begin());
            fixture.frame.complete();
            ++fixture.presentations;
        }
        lv_display_flush_ready(d);
    });

    lv_obj_t* screen = lv_display_get_screen_active(display);
    lv_obj_remove_style_all(screen);
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    auto makeObject = [screen](int x, int y, lv_color_t color) {
        lv_obj_t* object = lv_obj_create(screen);
        lv_obj_remove_style_all(object);
        lv_obj_set_pos(object, x, y);
        lv_obj_set_size(object, 8, 8);
        lv_obj_set_style_bg_color(object, color, 0);
        lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
        return object;
    };
    lv_obj_t* left = makeObject(4, 4, lv_color_hex(0xff0000));
    lv_obj_t* right = makeObject(48, 48, lv_color_hex(0x00ff00));
    lv_refr_now(display);
    assert(f.presentations == 1);
    const uint16_t oldLeft = f.presented[6 * f.width + 6];
    const uint16_t oldRight = f.presented[50 * f.width + 50];
    assert(oldLeft && oldRight && oldLeft != oldRight);

    const unsigned beforeFlushes = f.flushes;
    lv_obj_set_style_bg_color(left, lv_color_hex(0x0000ff), 0);
    lv_obj_set_style_bg_color(right, lv_color_hex(0xffffff), 0);
    lv_refr_now(display);
    assert(f.flushes >= beforeFlushes + 2 && f.presentations == 2);
    const uint16_t newLeft = f.presented[6 * f.width + 6];
    const uint16_t newRight = f.presented[50 * f.width + 50];
    assert(newLeft != oldLeft && newRight != oldRight);

    lv_obj_set_style_bg_color(left, lv_color_hex(0xff0000), 0);
    lv_refr_now(display);
    assert(f.presentations == 3);
    assert(f.presented[6 * f.width + 6] == oldLeft);
    assert(f.presented[50 * f.width + 50] == newRight);
    assert(f.presented[30 * f.width + 30] == 0);

    f.first.fill(0);
    f.second.fill(0);
    f.presented.fill(0);
    lv_obj_invalidate(lv_display_get_layer_top(display));
    lv_refr_now(display);
    assert(f.presentations == 4);
    assert(f.presented[6 * f.width + 6] == oldLeft);
    assert(f.presented[50 * f.width + 50] == newRight);

    lv_display_delete(display);
    lv_deinit();
}

struct RotatedFixture {
    static constexpr int physicalWidth = 64;
    static constexpr int physicalHeight = 48;
    std::array<uint16_t, physicalWidth * physicalHeight> logical{};
    std::array<uint16_t, physicalWidth * physicalHeight> first{};
    std::array<uint16_t, physicalWidth * physicalHeight> second{};
    MipiDirectFrame frame;
    const uint16_t* scanout = first.data();
    int rotation = 0;
    int logicalWidth = 0;
    int logicalHeight = 0;
    unsigned presentations = 0;
    bool deferAfterArbitration = false;

    size_t physicalIndex(int x, int y) const {
        if (rotation == 1) return x * physicalWidth + physicalWidth - 1 - y;
        if (rotation == 2) return (physicalHeight - 1 - y) * physicalWidth + physicalWidth - 1 - x;
        return (physicalHeight - 1 - x) * physicalWidth + y;
    }
};

static void testRotatedDirtyFrames(int rotation) {
    lv_init();
    RotatedFixture f;
    f.rotation = rotation;
    f.logicalWidth = rotation == 2 ? f.physicalWidth : f.physicalHeight;
    f.logicalHeight = rotation == 2 ? f.physicalHeight : f.physicalWidth;
    f.frame.reset(f.first.data());
    lv_display_t* display = lv_display_create(f.logicalWidth, f.logicalHeight);
    assert(display);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, f.logical.data(), nullptr, sizeof(f.logical),
                           LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_user_data(display, &f);
    lv_display_set_flush_cb(display, [](lv_display_t* d, const lv_area_t*, uint8_t* pixels) {
        auto& fixture = *static_cast<RotatedFixture*>(lv_display_get_user_data(d));
        assert(pixels == reinterpret_cast<uint8_t*>(fixture.logical.data()));
        if (lv_display_flush_is_last(d)) {
            auto* next = static_cast<uint16_t*>(fixture.frame.inactive(
                fixture.first.data(), fixture.second.data()));
            assert(next && next != fixture.scanout);
            assert(fixture.frame.accept(next, fixture.first.data(), fixture.second.data(), true) ==
                   MipiDirectFrame::Flush::Present);
            if (fixture.deferAfterArbitration) {
                fixture.frame.cancel();
                assert(lv_async_call([](void* display) {
                    lv_obj_invalidate(lv_display_get_layer_top(static_cast<lv_display_t*>(display)));
                }, d) == LV_RESULT_OK);
                lv_display_flush_ready(d);
                return;
            }
            for (int y = 0; y < fixture.logicalHeight; ++y) {
                for (int x = 0; x < fixture.logicalWidth; ++x) {
                    next[fixture.physicalIndex(x, y)] = fixture.logical[y * fixture.logicalWidth + x];
                }
            }
            fixture.scanout = next;
            fixture.frame.complete();
            ++fixture.presentations;
        }
        lv_display_flush_ready(d);
    });

    lv_obj_t* screen = lv_display_get_screen_active(display);
    lv_obj_remove_style_all(screen);
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    auto makeObject = [screen](int x, int y, lv_color_t color) {
        lv_obj_t* object = lv_obj_create(screen);
        lv_obj_remove_style_all(object);
        lv_obj_set_pos(object, x, y);
        lv_obj_set_size(object, 8, 8);
        lv_obj_set_style_bg_color(object, color, 0);
        lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
        return object;
    };
    lv_obj_t* left = makeObject(2, 2, lv_color_hex(0xff0000));
    lv_obj_t* right = makeObject(f.logicalWidth - 10, f.logicalHeight - 10, lv_color_hex(0x00ff00));
    lv_refr_now(display);
    assert(f.presentations == 1);
    const size_t leftIndex = f.physicalIndex(4, 4);
    const size_t rightIndex = f.physicalIndex(f.logicalWidth - 8, f.logicalHeight - 8);
    const uint16_t originalLeft = f.scanout[leftIndex];
    const uint16_t originalRight = f.scanout[rightIndex];
    assert(originalLeft && originalRight && originalLeft != originalRight);
    lv_obj_set_style_bg_color(left, lv_color_hex(0x0000ff), 0);
    lv_refr_now(display);
    assert(f.presentations == 2 && f.scanout[leftIndex] != originalLeft);
    assert(f.scanout[rightIndex] == originalRight);
    lv_obj_set_style_bg_color(right, lv_color_hex(0xffffff), 0);
    lv_refr_now(display);
    assert(f.presentations == 3 && f.scanout[rightIndex] != originalRight);
    assert(f.scanout[leftIndex] != originalLeft);
    const uint16_t* previousScanout = f.scanout;
    const uint16_t previousLeft = f.scanout[leftIndex];
    f.deferAfterArbitration = true;
    lv_obj_set_style_bg_color(left, lv_color_hex(0xff0000), 0);
    lv_refr_now(display);
    assert(f.presentations == 3 && f.scanout == previousScanout);
    assert(f.scanout[leftIndex] == previousLeft);
    assert(f.frame.inactive(f.first.data(), f.second.data()) != previousScanout);

    f.deferAfterArbitration = false;
    lv_timer_handler();
    lv_refr_now(display);
    assert(f.presentations == 4 && f.scanout[leftIndex] == originalLeft);
    assert(f.scanout[rightIndex] != originalRight);
    f.first.fill(0);
    f.second.fill(0);
    lv_obj_invalidate(lv_display_get_layer_top(display));
    lv_refr_now(display);
    assert(f.presentations == 5 && f.scanout[leftIndex] && f.scanout[rightIndex]);

    lv_display_delete(display);
    lv_deinit();
}

int main() {
    testFence();
    testOwnership();
    testDirtyFrames();
    testRotatedDirtyFrames(1);
    testRotatedDirtyFrames(2);
    testRotatedDirtyFrames(3);
}

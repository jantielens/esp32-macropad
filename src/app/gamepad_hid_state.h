#pragma once

#include <stdint.h>
#include <stddef.h>

namespace gamepad_protocol {
constexpr uint8_t button_count = 16;
constexpr uint8_t stick_count = 2;
constexpr uint8_t axis_count = stick_count * 2;
constexpr int16_t axis_max = INT16_MAX;
constexpr int16_t axis_min = -axis_max;
constexpr uint8_t trigger_count = 2;
constexpr uint8_t trigger_max = UINT8_MAX;
constexpr uint8_t hat_control_count = 4;
constexpr uint8_t hat_position_count = 8;
constexpr uint8_t hat_neutral = 0;
constexpr uint8_t hat_offset = button_count;
constexpr uint8_t trigger_offset = hat_offset + hat_control_count;
constexpr size_t report_bytes = axis_count * sizeof(int16_t) +
    trigger_count * sizeof(uint8_t) + sizeof(uint8_t) + sizeof(uint16_t);
static_assert(button_count == sizeof(uint16_t) * 8, "Gamepad button report width mismatch");
static_assert(trigger_offset + trigger_count <= sizeof(uint32_t) * 8, "Gamepad control mask too small");
}

enum class GamepadControlKind : uint8_t { Button, Hat, Trigger };

struct GamepadControl {
    GamepadControlKind kind;
    uint8_t index;

    uint32_t mask() const {
        if (kind == GamepadControlKind::Button && index < gamepad_protocol::button_count) return uint32_t(1) << index;
        if (kind == GamepadControlKind::Hat && index < gamepad_protocol::hat_control_count) return uint32_t(1) << (gamepad_protocol::hat_offset + index);
        if (kind == GamepadControlKind::Trigger && index < gamepad_protocol::trigger_count) return uint32_t(1) << (gamepad_protocol::trigger_offset + index);
        return 0;
    }
};

struct GamepadReport {
    int16_t left_x = 0;
    int16_t left_y = 0;
    int16_t right_x = 0;
    int16_t right_y = 0;
    uint8_t left_trigger = 0;
    uint8_t right_trigger = 0;
    uint8_t hat = gamepad_protocol::hat_neutral;
    uint16_t buttons = 0;
    uint32_t generation = 0;
    uint32_t sequence = 0;

    bool same_value(const GamepadReport& other) const {
        return left_x == other.left_x && left_y == other.left_y &&
            right_x == other.right_x && right_y == other.right_y &&
            left_trigger == other.left_trigger && right_trigger == other.right_trigger &&
            hat == other.hat && buttons == other.buttons;
    }
};

class GamepadHidState {
public:
    static constexpr size_t owner_capacity = 64;
    static constexpr size_t transition_capacity = 128;
    static constexpr size_t tap_capacity = 3;
    static constexpr uint32_t tap_duration_ms = 50;

    GamepadHidState() { reset(); }

    static uint8_t hat_from_mask(uint32_t mask) {
        const int horizontal = int(bool(mask & (uint32_t(1) << (gamepad_protocol::hat_offset + 3)))) -
            int(bool(mask & (uint32_t(1) << (gamepad_protocol::hat_offset + 2))));
        const int vertical = int(bool(mask & (uint32_t(1) << (gamepad_protocol::hat_offset + 1)))) -
            int(bool(mask & (uint32_t(1) << gamepad_protocol::hat_offset)));
        if (vertical < 0) return horizontal < 0 ? 8 : horizontal > 0 ? 2 : 1;
        if (vertical > 0) return horizontal < 0 ? 6 : horizontal > 0 ? 4 : 5;
        return horizontal < 0 ? 7 : horizontal > 0 ? 3 : gamepad_protocol::hat_neutral;
    }

    void reset() {
        if (++generation_ == 0) ++generation_;
        standalone_ = 0;
        desired_mask_ = 0;
        left_x_ = left_y_ = right_x_ = right_y_ = 0;
        for (auto& owner : owners_) owner = Owner{};
        for (auto& tap : taps_) {
            if (tap.phase != TapPhase::Free) {
                tap.phase = TapPhase::Done;
                tap.success = false;
            }
        }
        head_ = 0;
        count_ = 0;
        enqueue(0, true);
    }

    bool hold(GamepadControl control, bool down) {
        const uint32_t mask = control.mask();
        if (!mask || (down && tap_owns(mask))) return false;
        if (down && (standalone_ & mask)) return true;
        if (!down && !(standalone_ & mask)) return true;
        if (down && !can_acquire()) return false;
        if (down) standalone_ |= mask;
        else standalone_ &= ~mask;
        refresh_mask();
        return true;
    }

    uint32_t acquire(GamepadControl control) {
        const uint32_t mask = control.mask();
        if (!mask || tap_owns(mask) || !can_acquire()) return 0;
        Owner* owner = allocate_owner();
        if (!owner) return 0;
        owner->mask = mask;
        refresh_mask();
        return owner->id;
    }

    uint32_t acquire_stick(uint8_t stick) {
        if (stick >= gamepad_protocol::stick_count) return 0;
        for (const auto& owner : owners_) {
            if (owner.id && owner.stick == int8_t(stick)) return 0;
        }
        Owner* owner = allocate_owner();
        if (!owner) return 0;
        owner->stick = int8_t(stick);
        return owner->id;
    }

    bool move_stick(uint32_t id, int16_t horizontal, int16_t vertical) {
        Owner* owner = find_owner(id);
        if (!owner || owner->stick < 0) return false;
        if (horizontal < gamepad_protocol::axis_min) horizontal = gamepad_protocol::axis_min;
        if (vertical < gamepad_protocol::axis_min) vertical = gamepad_protocol::axis_min;
        if (owner->stick == 0) { left_x_ = horizontal; left_y_ = vertical; }
        else { right_x_ = horizontal; right_y_ = vertical; }
        return true;
    }

    bool release(uint32_t id) {
        Owner* owner = find_owner(id);
        if (!owner) return false;
        if (owner->stick == 0) left_x_ = left_y_ = 0;
        if (owner->stick == 1) right_x_ = right_y_ = 0;
        *owner = Owner{};
        refresh_mask();
        return true;
    }

    bool tap(GamepadControl control, uint32_t token) {
        const uint32_t mask = control.mask();
        if (!token || !mask || (desired_mask_ & mask) || tap_owns(mask)) return false;
        if (control.kind == GamepadControlKind::Hat &&
            hat_from_mask(desired_mask_ | mask) == hat_from_mask(desired_mask_)) return false;
        Tap* slot = nullptr;
        for (auto& pending : taps_) {
            if (pending.token == token && pending.phase != TapPhase::Free) return false;
            if (!slot && pending.phase == TapPhase::Free) slot = &pending;
        }
        if (!slot) return false;
        const uint32_t owner = acquire(control);
        if (!owner) return false;
        slot->token = token;
        slot->owner = owner;
        slot->mask = mask;
        slot->press_sequence = sequence_;
        slot->phase = TapPhase::Press;
        return true;
    }

    void tick(uint32_t now) {
        for (auto& tap : taps_) {
            if (tap.phase == TapPhase::Held && uint32_t(now - tap.started_at) >= tap_duration_ms) {
                release(tap.owner);
                tap.release_sequence = sequence_;
                tap.phase = TapPhase::Release;
            }
        }
    }

    bool take_completion(uint32_t& token, bool& success) {
        for (auto& tap : taps_) {
            if (tap.phase == TapPhase::Done) {
                token = tap.token;
                success = tap.success;
                tap = Tap{};
                return true;
            }
        }
        return false;
    }

    bool next(GamepadReport& report) const {
        report = GamepadReport{};
        report.generation = generation_;
        uint32_t mask = desired_mask_;
        if (count_) {
            const Transition& transition = transitions_[head_];
            report.sequence = transition.sequence;
            if (transition.neutral) return true;
            mask = transition.mask;
        }
        report.left_x = left_x_;
        report.left_y = left_y_;
        report.right_x = right_x_;
        report.right_y = right_y_;
        report.buttons = uint16_t(mask);
        report.hat = hat_from_mask(mask);
        report.left_trigger = mask & (uint32_t(1) << gamepad_protocol::trigger_offset) ? gamepad_protocol::trigger_max : 0;
        report.right_trigger = mask & (uint32_t(1) << (gamepad_protocol::trigger_offset + 1)) ? gamepad_protocol::trigger_max : 0;
        return count_ || !report.same_value(submitted_);
    }

    void acknowledge(const GamepadReport& report, uint32_t now) {
        if (report.generation != generation_) return;
        if (report.sequence) {
            if (!count_ || transitions_[head_].sequence != report.sequence) return;
            head_ = (head_ + 1) % transition_capacity;
            --count_;
            for (auto& tap : taps_) {
                if (tap.phase == TapPhase::Press && tap.press_sequence == report.sequence) {
                    tap.started_at = now;
                    tap.phase = TapPhase::Held;
                } else if (tap.phase == TapPhase::Release && tap.release_sequence == report.sequence) {
                    tap.success = true;
                    tap.phase = TapPhase::Done;
                }
            }
        }
        submitted_ = report;
    }

private:
    struct Owner {
        uint32_t id = 0;
        uint32_t mask = 0;
        int8_t stick = -1;
    };
    struct Transition {
        uint32_t mask = 0;
        uint32_t sequence = 0;
        bool neutral = false;
    };
    enum class TapPhase : uint8_t { Free, Press, Held, Release, Done };
    struct Tap {
        uint32_t token = 0;
        uint32_t owner = 0;
        uint32_t mask = 0;
        uint32_t press_sequence = 0;
        uint32_t release_sequence = 0;
        uint32_t started_at = 0;
        TapPhase phase = TapPhase::Free;
        bool success = false;
    };
    Owner owners_[owner_capacity] = {};
    Transition transitions_[transition_capacity] = {};
    Tap taps_[tap_capacity] = {};
    GamepadReport submitted_;
    size_t head_ = 0;
    size_t count_ = 0;
    uint32_t generation_ = 0;
    uint32_t sequence_ = 0;
    uint32_t next_owner_ = 0;
    uint32_t standalone_ = 0;
    uint32_t desired_mask_ = 0;
    int16_t left_x_ = 0, left_y_ = 0, right_x_ = 0, right_y_ = 0;

    Owner* find_owner(uint32_t id) {
        if (!id) return nullptr;
        for (auto& owner : owners_) if (owner.id == id) return &owner;
        return nullptr;
    }
    Owner* allocate_owner() {
        for (auto& owner : owners_) {
            if (!owner.id) {
                do { ++next_owner_; } while (!next_owner_ || find_owner(next_owner_));
                owner.id = next_owner_;
                return &owner;
            }
        }
        return nullptr;
    }
    bool tap_owns(uint32_t mask) const {
        for (const auto& tap : taps_) {
            if (tap.mask == mask && tap.phase != TapPhase::Free && tap.phase != TapPhase::Done) return true;
        }
        return false;
    }
    bool can_acquire() const {
        size_t reserved_releases = 0;
        for (const auto& owner : owners_) if (owner.id && owner.mask) ++reserved_releases;
        for (uint32_t remaining = standalone_; remaining; remaining &= remaining - 1) ++reserved_releases;
        return count_ + reserved_releases + 2 <= transition_capacity;
    }
    void enqueue(uint32_t mask, bool neutral = false) {
        if (++sequence_ == 0) ++sequence_;
        Transition& transition = transitions_[(head_ + count_) % transition_capacity];
        transition.mask = mask;
        transition.sequence = sequence_;
        transition.neutral = neutral;
        ++count_;
    }
    void refresh_mask() {
        uint32_t mask = standalone_;
        for (const auto& owner : owners_) mask |= owner.mask;
        if (mask == desired_mask_) return;
        desired_mask_ = mask;
        enqueue(mask);
    }
};
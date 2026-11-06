#include "widget.h"

#if HAS_DISPLAY && HAS_FACE_WIDGET

#include "../binding_template.h"
#include "../log_manager.h"
#include <math.h>
#include <new>
#include <string.h>

enum class FaceEmotion : uint8_t { Content, Relaxed, Happy, Worried, Angry, Sad, Surprised, Sleepy };
enum class FaceStyle : uint8_t { Robot, Doodle };
enum class FaceLiveliness : uint8_t { Calm, Normal, Lively, Still };

static constexpr const char* face_emotions[] = {
    "content", "relaxed", "happy", "worried", "angry", "sad", "surprised", "sleepy"
};
static constexpr const char* face_styles[] = {"robot", "doodle"};
static constexpr const char* face_liveliness[] = {"calm", "normal", "lively", "still"};

struct FaceConfig {
    FaceStyle style;
    FaceEmotion emotion;
    FaceLiveliness liveliness;
};

struct FacePose {
    float width, height, lid, tilt, pupil, smile;
    float brow_tilt, brow_raise, mouth_curve, mouth_open, mouth_width;
};

static constexpr FacePose face_poses[] = {
    {49, 39, 0,    0,    1,    0,  -1, 0,  7, 0, 22},
    {49, 37, 0.48f,0,    0.9f, 0,  -1, 0,  5, 0, 22},
    {51, 45, 0,    0,    0.95f,0.35f,-3,3,12, 0, 27},
    {47, 45, 0.12f,0.44f,0.85f,0,  -9, 6, -3, 0, 19},
    {49, 44, 0.28f,-0.5f,0.8f,0,   9, 0, -9, 0, 23},
    {46, 41, 0.42f,0.36f,0.85f,0,  -8, 2,-10, 0, 23},
    {44, 65, 0,    0,    0.7f, 0,  -1,10,  0,18, 12},
    {49, 39, 0.58f,0,    0.85f,0,  -1,-4,  3, 0, 17},
};

struct FaceState {
    lv_obj_t* object;
    lv_obj_t* parent;
    FacePose pose, from;
    uint32_t transition_start, transition_ms, last_frame;
    uint32_t next_blink, blink_start, next_gaze, gaze_start, random;
    float eye_open, gaze_x, gaze_y, gaze_from_x, gaze_from_y, gaze_to_x, gaze_to_y;
    FaceStyle style;
    FaceEmotion emotion;
    FaceLiveliness liveliness;
    bool active, blinking, invalid_emotion, layout_dirty;
};

static_assert(sizeof(FaceConfig) <= WIDGET_CONFIG_MAX_BYTES, "Face config too large");
static_assert(sizeof(FaceState) <= WIDGET_STATE_MAX_BYTES, "Face state too large");

template<size_t N>
static int face_choice(const char* text, const char* const (&choices)[N]) {
    for (size_t index = 0; index < N; ++index) {
        if (strcmp(text, choices[index]) == 0) return static_cast<int>(index);
    }
    return -1;
}

static const char* face_validate(JsonObjectConst btn) {
    if (btn.containsKey("widget_face_style") &&
        (!btn["widget_face_style"].is<const char*>() ||
         face_choice(btn["widget_face_style"].as<const char*>(), face_styles) < 0))
        return "face style must be robot or doodle";
    if (btn.containsKey("widget_face_emotion") &&
        (!btn["widget_face_emotion"].is<const char*>() ||
         face_choice(btn["widget_face_emotion"].as<const char*>(), face_emotions) < 0))
        return "face emotion must be content, relaxed, happy, worried, angry, sad, surprised, or sleepy";
    if (btn.containsKey("widget_face_liveliness") &&
        (!btn["widget_face_liveliness"].is<const char*>() ||
         face_choice(btn["widget_face_liveliness"].as<const char*>(), face_liveliness) < 0))
        return "face liveliness must be still, calm, normal, or lively";
    if (btn.containsKey("widget_data_binding")) {
        if (!btn["widget_data_binding"].is<const char*>())
            return "face emotion source must be a string";
        const char* value = btn["widget_data_binding"];
        if (strlen(value) >= CONFIG_LABEL_MAX_LEN)
            return "face emotion source too long (max 191 bytes)";
        if (value[0] && face_choice(value, face_emotions) < 0 && !binding_template_has_bindings(value))
            return "face emotion source must be an emotion name or binding";
    }
    return nullptr;
}

static void face_parse(const JsonObject& btn, uint8_t* data) {
    auto* config = new (data) FaceConfig{FaceStyle::Robot, FaceEmotion::Content, FaceLiveliness::Normal};
    if (const char* error = face_validate(btn)) {
        LOGW("Face", "Invalid configuration: %s", error);
        return;
    }
    config->style = static_cast<FaceStyle>(face_choice(btn["widget_face_style"] | "robot", face_styles));
    config->emotion = static_cast<FaceEmotion>(face_choice(btn["widget_face_emotion"] | "content", face_emotions));
    config->liveliness = static_cast<FaceLiveliness>(face_choice(btn["widget_face_liveliness"] | "normal", face_liveliness));
}

static float face_mix(float from, float to, float amount) {
    return from + (to - from) * amount;
}

static float face_ease(float amount) {
    amount = clamp_val(amount, 0.0f, 1.0f);
    return amount * amount * (3.0f - 2.0f * amount);
}

static FacePose face_blend(const FacePose& from, const FacePose& to, float amount) {
    return {
        face_mix(from.width, to.width, amount), face_mix(from.height, to.height, amount),
        face_mix(from.lid, to.lid, amount), face_mix(from.tilt, to.tilt, amount),
        face_mix(from.pupil, to.pupil, amount), face_mix(from.smile, to.smile, amount),
        face_mix(from.brow_tilt, to.brow_tilt, amount), face_mix(from.brow_raise, to.brow_raise, amount),
        face_mix(from.mouth_curve, to.mouth_curve, amount), face_mix(from.mouth_open, to.mouth_open, amount),
        face_mix(from.mouth_width, to.mouth_width, amount)
    };
}

static uint32_t face_random(FaceState* state, uint32_t range) {
    state->random ^= state->random << 13;
    state->random ^= state->random >> 17;
    state->random ^= state->random << 5;
    return state->random % range;
}

static float face_energy(const FaceState* state) {
    const float temperament[] = {0.6f, 1.0f, 1.5f};
    const float mood[] = {1, 0.65f, 1.2f, 1.35f, 0.8f, 0.6f, 1.1f, 0.35f};
    return temperament[static_cast<unsigned>(state->liveliness)] * mood[static_cast<unsigned>(state->emotion)];
}

static uint32_t face_pause(FaceState* state) {
    return static_cast<uint32_t>((2400 + face_random(state, 3000)) / face_energy(state));
}

static void face_set_emotion(FaceState* state, FaceEmotion emotion, uint32_t now) {
    if (state->emotion == emotion) return;
    state->from = state->pose;
    state->emotion = emotion;
    state->transition_start = now;
    state->transition_ms = emotion == FaceEmotion::Surprised ? 180 :
        emotion == FaceEmotion::Relaxed || emotion == FaceEmotion::Sleepy ? 900 : 450;
    if (DISPLAY_DISABLE_ANIMATIONS && !HAS_LVGL_EPAPER) {
        state->pose = face_poses[static_cast<unsigned>(emotion)];
        state->transition_ms = 0;
    }
}

struct FaceDraw {
    lv_layer_t* layer;
    float x, y, scale;
    lv_color_t color, background;
};

static lv_point_precise_t face_point(const FaceDraw& draw, float x, float y) {
    return {static_cast<lv_value_precise_t>(lroundf(draw.x + x * draw.scale)),
            static_cast<lv_value_precise_t>(lroundf(draw.y + y * draw.scale))};
}

static void face_round_rect(const FaceDraw& draw, float x, float y, float width, float height, lv_color_t color) {
    if (width <= 0 || height <= 0) return;
    const auto first = face_point(draw, x - width / 2, y - height / 2);
    const auto last = face_point(draw, x + width / 2, y + height / 2);
    const lv_area_t bounds = {static_cast<int32_t>(first.x), static_cast<int32_t>(first.y),
                              static_cast<int32_t>(last.x), static_cast<int32_t>(last.y)};
    lv_draw_rect_dsc_t rectangle;
    lv_draw_rect_dsc_init(&rectangle);
    rectangle.bg_color = color;
    rectangle.radius = LV_RADIUS_CIRCLE;
    lv_draw_rect(draw.layer, &rectangle, &bounds);
}

static void face_curve(const FaceDraw& draw, float x, float y, float width,
                       float bend, float tilt, float thickness) {
    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.color = draw.color;
    line.width = static_cast<int32_t>(fmaxf(1, lroundf(thickness * draw.scale)));
    line.round_start = line.round_end = true;
    for (unsigned index = 0; index < 12; ++index) {
        const float first = index / 6.0f - 1;
        const float last = (index + 1) / 6.0f - 1;
        line.p1 = face_point(draw, x + first * width / 2, y + bend * (1 - first * first) + tilt * first);
        line.p2 = face_point(draw, x + last * width / 2, y + bend * (1 - last * last) + tilt * last);
        lv_draw_line(draw.layer, &line);
    }
}

static void face_eye(const FaceDraw& draw, const FaceState* state, float center, float direction) {
    const FacePose& pose = state->pose;
    const float height = pose.height * state->eye_open;
    const float gaze_x = state->gaze_x;
    const float gaze_y = state->gaze_y;
    const float cy = gaze_y + (state->style == FaceStyle::Doodle ? -3 : 0);
    const float cx = center + gaze_x * 0.35f;
    face_round_rect(draw, cx, cy, pose.width, height, draw.color);
    // The pupil is negative space, using the button's live background color.
    const float pupil_width = 18 * pose.pupil;
    const float smile_y = cy + face_mix(height + 1, 9 * state->eye_open, pose.smile);
    const float lid_slope = direction * pose.tilt * height / pose.width;
    const float top = fmaxf(cy - height / 2, cy - height / 2 + height * pose.lid +
                            lid_slope * gaze_x + fabsf(lid_slope) * pupil_width / 2);
    const float bottom = pose.smile > 0 ? fminf(cy + height / 2, smile_y - height / 2)
                                       : cy + height / 2;
    // Keep a visible rim around the pupil as either eyelid closes.
    const float rim = fmaxf(3, 2 / draw.scale);
    const float pupil_height = fminf(25 * pose.pupil * state->eye_open, fmaxf(0, bottom - top - 2 * rim));
    if (pupil_height > 0) {
        const float pupil_y = clamp_val(cy + gaze_y * 0.25f + 2,
                                        top + rim + pupil_height / 2, bottom - rim - pupil_height / 2);
        face_round_rect(draw, cx + gaze_x, pupil_y, pupil_width, pupil_height, draw.background);
    }
    if (pose.smile > 0) {
        face_round_rect(draw, cx, smile_y, pose.width + 4, height, draw.background);
    }
    if (pose.lid > 0 || pose.tilt != 0) {
        // Clip the eyelid's half-plane to this eye, not the surrounding face.
        const auto first = face_point(draw, cx - pose.width / 2 - 1, cy - height / 2 - 1);
        const auto last = face_point(draw, cx + pose.width / 2 + 1, cy + height / 2 + 1);
        const lv_area_t eye = {static_cast<int32_t>(first.x), static_cast<int32_t>(first.y),
                               static_cast<int32_t>(last.x), static_cast<int32_t>(last.y)};
        const lv_area_t saved_clip = draw.layer->_clip_area;
        draw.layer->_clip_area = {
            eye.x1 > saved_clip.x1 ? eye.x1 : saved_clip.x1,
            eye.y1 > saved_clip.y1 ? eye.y1 : saved_clip.y1,
            eye.x2 < saved_clip.x2 ? eye.x2 : saved_clip.x2,
            eye.y2 < saved_clip.y2 ? eye.y2 : saved_clip.y2
        };
        if (draw.layer->_clip_area.x1 <= draw.layer->_clip_area.x2 &&
            draw.layer->_clip_area.y1 <= draw.layer->_clip_area.y2) {
            const float lid_y = cy - height / 2 + height * pose.lid;
            const float tilt = direction * pose.tilt * height * 2;
            lv_draw_triangle_dsc_t lid;
            lv_draw_triangle_dsc_init(&lid);
            lid.color = draw.background;
            lid.p[0] = face_point(draw, cx - pose.width * 2, lid_y - tilt);
            lid.p[1] = face_point(draw, cx + pose.width * 2, lid_y + tilt);
            lid.p[2] = face_point(draw, cx, cy - height * 5 - 2);
            lv_draw_triangle(draw.layer, &lid);
        }
        draw.layer->_clip_area = saved_clip;
    }
}

static void face_draw(lv_event_t* event) {
    auto* state = static_cast<FaceState*>(lv_event_get_user_data(event));
    lv_obj_t* object = state->object;
    lv_area_t bounds;
    lv_obj_get_coords(object, &bounds);
    const float scale = fminf(lv_area_get_width(&bounds) / 180.0f, lv_area_get_height(&bounds) / 140.0f);
    if (scale <= 0) return;
    lv_obj_t* button = lv_obj_get_parent(object);
    const FaceDraw draw = {
        lv_event_get_layer(event), (bounds.x1 + bounds.x2) / 2.0f,
        (bounds.y1 + bounds.y2) / 2.0f, scale,
        lv_obj_get_style_text_color(button, LV_PART_MAIN),
        lv_obj_get_style_bg_color(button, LV_PART_MAIN)
    };
    face_eye(draw, state, -41, -1);
    face_eye(draw, state, 41, 1);
    if (state->style == FaceStyle::Doodle) {
        const FacePose& pose = state->pose;
        for (int direction : {-1, 1}) {
            face_curve(draw, direction * 41.0f, -44 - pose.brow_raise,
                        25, -2, -direction * pose.brow_tilt, 4);
        }
        face_curve(draw, 0, 41, pose.mouth_width, pose.mouth_curve, 0, 4);
        face_round_rect(draw, 0, 42, pose.mouth_width * pose.mouth_open / 20,
                        pose.mouth_open, draw.color);
    }
}

struct FaceRegion {
    float left, top, right, bottom;
};

static FaceRegion face_eye_region(const FaceState& state, float center) {
    const FacePose& pose = state.pose;
    const float height = pose.height * state.eye_open;
    const float x = center + state.gaze_x * 0.35f;
    const float y = state.gaze_y + (state.style == FaceStyle::Doodle ? -3 : 0);
    // Include the subtractive smile mask, which extends below the outer eye.
    const float mask_bottom = pose.smile > 0
        ? face_mix(height + 1, 9 * state.eye_open, pose.smile) + height / 2 : height / 2;
    return {x - pose.width / 2 - 2, y - height / 2 - 1,
            x + pose.width / 2 + 2, y + fmaxf(height / 2 + 1, mask_bottom)};
}

static FaceRegion face_brow_region(const FacePose& pose, float center) {
    const float y = -44 - pose.brow_raise;
    const float tilt = fabsf(pose.brow_tilt);
    return {center - 14.5f, y - tilt - 4, center + 14.5f, y + tilt + 2};
}

static FaceRegion face_mouth_region(const FacePose& pose) {
    const float width = fmaxf(pose.mouth_width, pose.mouth_width * pose.mouth_open / 20);
    return {-width / 2 - 2, fminf(39 + fminf(0, pose.mouth_curve), 42 - pose.mouth_open / 2),
            width / 2 + 2, fmaxf(43 + fmaxf(0, pose.mouth_curve), 42 + pose.mouth_open / 2)};
}

static void face_invalidate_region(lv_obj_t* object, const FaceRegion& before, const FaceRegion& after) {
    lv_area_t bounds;
    lv_obj_get_coords(object, &bounds);
    const float scale = fminf(lv_area_get_width(&bounds) / 180.0f, lv_area_get_height(&bounds) / 140.0f);
    if (scale <= 0) return;
    const float x = (bounds.x1 + bounds.x2) / 2.0f;
    const float y = (bounds.y1 + bounds.y2) / 2.0f;
    // Cover rounding and antialiasing at both the old and new edges.
    const lv_area_t dirty = {
        static_cast<int32_t>(floorf(x + fminf(before.left, after.left) * scale)) - 2,
        static_cast<int32_t>(floorf(y + fminf(before.top, after.top) * scale)) - 2,
        static_cast<int32_t>(ceilf(x + fmaxf(before.right, after.right) * scale)) + 2,
        static_cast<int32_t>(ceilf(y + fmaxf(before.bottom, after.bottom) * scale)) + 2
    };
    lv_obj_invalidate_area(object, &dirty);
}

static void face_invalidate_changes(const FaceState& before, const FaceState& after) {
    for (float center : {-41.0f, 41.0f})
        face_invalidate_region(after.object, face_eye_region(before, center), face_eye_region(after, center));
    if (after.style != FaceStyle::Doodle) return;
    if (before.pose.brow_tilt != after.pose.brow_tilt || before.pose.brow_raise != after.pose.brow_raise) {
        for (float center : {-41.0f, 41.0f})
            face_invalidate_region(after.object, face_brow_region(before.pose, center), face_brow_region(after.pose, center));
    }
    if (before.pose.mouth_curve != after.pose.mouth_curve || before.pose.mouth_open != after.pose.mouth_open ||
        before.pose.mouth_width != after.pose.mouth_width)
        face_invalidate_region(after.object, face_mouth_region(before.pose), face_mouth_region(after.pose));
}

static void face_hide(WidgetState* runtime) {
    reinterpret_cast<FaceState*>(runtime->data)->active = false;
}

static void face_show(WidgetState* runtime) {
    auto* state = reinterpret_cast<FaceState*>(runtime->data);
    const uint32_t now = lv_tick_get();
    state->active = true;
    state->last_frame = now;
    state->from = state->pose;
    state->transition_start = now;
    state->blinking = false;
    state->eye_open = 1;
    if (state->liveliness != FaceLiveliness::Still) {
        state->next_blink = now + face_pause(state);
        state->next_gaze = now + face_pause(state);
    }
    state->gaze_from_x = state->gaze_to_x = state->gaze_x;
    state->gaze_from_y = state->gaze_to_y = state->gaze_y;
    state->gaze_start = now;
}

static void face_deleted(lv_event_t* event) {
    auto* state = static_cast<FaceState*>(lv_event_get_user_data(event));
    state->object = nullptr;
    state->active = false;
}

static void face_parent_event(lv_event_t* event) {
    auto* state = static_cast<FaceState*>(lv_event_get_user_data(event));
    if (lv_event_get_target_obj(event) != state->parent) return;
    if (lv_event_get_code(event) == LV_EVENT_DELETE) state->parent = nullptr;
    else state->layout_dirty = true;
}

static void face_layout(FaceState* state) {
    if (!state->object || !state->parent) return;
    lv_obj_t* button = state->parent;
    lv_obj_update_layout(button);
    lv_obj_set_size(state->object, lv_obj_get_content_width(button), lv_obj_get_content_height(button));
    lv_obj_set_pos(state->object, 0, 0);
    state->layout_dirty = false;
}

static void face_update(lv_obj_t*, const WidgetConfig*, WidgetState* runtime, const char* value) {
    auto* state = reinterpret_cast<FaceState*>(runtime->data);
    if (!state->object) return;
    const int choice = value ? face_choice(value, face_emotions) : -1;
    if (choice < 0 && !state->invalid_emotion)
        LOGW("Face", "Emotion unavailable or unrecognized; using content");
    state->invalid_emotion = choice < 0;
    const FaceEmotion emotion = choice < 0 ? FaceEmotion::Content : static_cast<FaceEmotion>(choice);
    if (state->emotion == emotion) return;
    const FaceState before = *state;
    face_set_emotion(state, emotion, lv_tick_get());
    face_invalidate_changes(before, *state);
}

static void face_create(lv_obj_t* button, const WidgetConfig* config,
                         const ScreenButtonConfig*, const PadRect*, const UIScaleInfo*,
                         WidgetState* runtime) {
    const auto* settings = reinterpret_cast<const FaceConfig*>(config->data);
    auto* state = new (runtime->data) FaceState{};
    state->style = settings->style;
    state->liveliness = settings->liveliness;
    state->pose = state->from = face_poses[0];
    state->emotion = FaceEmotion::Content;
    state->random = lv_rand(1, 0x7fffffffu);
    state->parent = button;
    state->object = lv_obj_create(button);
    lv_obj_remove_style_all(state->object);
    lv_obj_remove_flag(state->object, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(state->object, LV_OBJ_FLAG_SCROLLABLE);
    face_layout(state);
    lv_obj_add_event_cb(state->object, face_draw, LV_EVENT_DRAW_MAIN, state);
    lv_obj_add_event_cb(state->object, face_deleted, LV_EVENT_DELETE, state);
    lv_obj_add_event_cb(button, face_parent_event, LV_EVENT_SIZE_CHANGED, state);
    lv_obj_add_event_cb(button, face_parent_event, LV_EVENT_DELETE, state);
    face_show(runtime);
    if (config->data_binding[0][0]) {
        char resolved[BINDING_TEMPLATE_MAX_LEN];
        binding_template_resolve(config->data_binding[0], resolved, sizeof(resolved));
        face_update(button, config, runtime, resolved);
    } else {
        face_set_emotion(state, settings->emotion, lv_tick_get());
    }
}

static void face_tick(lv_obj_t*, const WidgetConfig*, WidgetState* runtime) {
    auto* state = reinterpret_cast<FaceState*>(runtime->data);
    if (state->layout_dirty) face_layout(state);
    if (!state->object || !state->active) return;
    const uint32_t now = lv_tick_get();
    if ((DISPLAY_DISABLE_ANIMATIONS && !HAS_LVGL_EPAPER) || now - state->last_frame < 33) return;
    const FaceState before = *state;
    state->last_frame = now;
    bool changed = false;
    if (state->transition_ms) {
        const uint32_t elapsed = now - state->transition_start;
        state->pose = face_blend(state->from, face_poses[static_cast<unsigned>(state->emotion)],
                                 face_ease(static_cast<float>(elapsed) / state->transition_ms));
        if (elapsed >= state->transition_ms) state->transition_ms = 0;
        changed = true;
    }
    if (state->liveliness == FaceLiveliness::Still) {
        if (changed) face_invalidate_changes(before, *state);
        return;
    }
    if (!state->blinking && static_cast<int32_t>(now - state->next_blink) >= 0) {
        state->blinking = true;
        state->blink_start = now;
    }
    if (state->blinking) {
        const uint32_t elapsed = now - state->blink_start;
        // Give the existing slow e-paper polling a chance to present closed eyes.
        const uint32_t hold_ms = HAS_LVGL_EPAPER ? 1000 : 0;
        const float openness = elapsed < 90 ? 1 - elapsed / 90.0f :
            elapsed < 90 + hold_ms ? 0 : (elapsed - 90 - hold_ms) / 150.0f;
        state->eye_open = clamp_val(openness, 0.04f, 1.0f);
        if (elapsed >= 240 + hold_ms) {
            state->blinking = false;
            state->next_blink = now + face_pause(state);
        }
        changed = true;
    }
    if (static_cast<int32_t>(now - state->next_gaze) >= 0) {
        state->gaze_from_x = state->gaze_x;
        state->gaze_from_y = state->gaze_y;
        const float amplitude = fminf(face_energy(state), 1.5f);
        state->gaze_to_x = (static_cast<int>(face_random(state, 11)) - 5) * amplitude;
        state->gaze_to_y = (static_cast<int>(face_random(state, 7)) - 3) * amplitude;
        state->gaze_start = now;
        state->next_gaze = now + 500 + face_pause(state);
    }
    const float gaze_amount = face_ease((now - state->gaze_start) / 400.0f);
    const float x = face_mix(state->gaze_from_x, state->gaze_to_x, gaze_amount);
    const float y = face_mix(state->gaze_from_y, state->gaze_to_y, gaze_amount);
    if (x != state->gaze_x || y != state->gaze_y) {
        state->gaze_x = x;
        state->gaze_y = y;
        changed = true;
    }
    if (changed) face_invalidate_changes(before, *state);
}

static void face_destroy(WidgetState* runtime) {
    auto* state = reinterpret_cast<FaceState*>(runtime->data);
    if (state->parent) lv_obj_remove_event_cb_with_user_data(state->parent, face_parent_event, state);
    if (state->object) lv_obj_delete(state->object);
    state->active = false;
}

#if HAS_MCP
static void face_describe(JsonObject& out) {
    JsonArray fields = out.createNestedArray("config_fields");
    const auto add = [&](const char* name, const char* value, const auto& options) {
        JsonObject field = fields.createNestedObject();
        field["name"] = name;
        field["type"] = "string";
        field["default"] = value;
        JsonArray choices = field.createNestedArray("enum");
        for (const char* choice : options) choices.add(choice);
    };
    add("widget_face_style", "robot", face_styles);
    add("widget_face_emotion", "content", face_emotions);
    add("widget_face_liveliness", "normal", face_liveliness);
    JsonObject emotion = fields.createNestedObject();
    emotion["name"] = "widget_data_binding";
    emotion["type"] = "string";
    emotion["default"] = "content";
    emotion["bindable"] = true;
    emotion["max_length"] = CONFIG_LABEL_MAX_LEN - 1;
    emotion["description"] = "Emotion name or binding returning content, relaxed, happy, worried, angry, sad, surprised, or sleepy. Invalid or unavailable results use content.";
    out["note"] = "Procedural companion face. Uses button fg_color and bg_color. Emotion names are lowercase. Without an emotion source, widget_face_emotion supplies a fixed emotion (default content).";
}
#endif

static const WidgetPreview face_preview = {"Face", "face"};
REGISTER_WIDGET_SCHEMA_VALIDATED_LIFECYCLE(face, nullptr, false, face_validate);

#endif

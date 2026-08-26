#include "widget.h"

#if HAS_DISPLAY && HAS_CAMERA

#include "../camera_feed.h"
#include "../display_driver.h"
#include "../log_manager.h"
#include "../screen_saver_manager.h"

#include <string.h>

enum CameraPreviewScaleMode : uint8_t {
    CAMERA_PREVIEW_SCALE_COVER,
    CAMERA_PREVIEW_SCALE_LETTERBOX,
    CAMERA_PREVIEW_SCALE_CENTER_CROP,
};

struct CameraPreviewConfig {
    CameraPreviewScaleMode scale_mode;
};

static_assert(sizeof(CameraPreviewConfig) <= WIDGET_CONFIG_MAX_BYTES,
              "CameraPreviewConfig exceeds WIDGET_CONFIG_MAX_BYTES");

struct CameraPreviewWidgetState {
    lv_obj_t* image;
    lv_image_dsc_t descriptor;
    CameraFeedFrame frame;
    uint32_t generation;
    bool has_frame;
    bool has_demand;
};

static_assert(sizeof(CameraPreviewWidgetState) <= WIDGET_STATE_MAX_BYTES,
              "CameraPreviewWidgetState exceeds WIDGET_STATE_MAX_BYTES");

static void camera_preview_parse(const JsonObject& btn, uint8_t* data) {
    auto* cfg = reinterpret_cast<CameraPreviewConfig*>(data);
    cfg->scale_mode = CAMERA_PREVIEW_SCALE_COVER;
    const char* scale = btn["widget_camera_scale"] | "cover";
    if (strcmp(scale, "letterbox") == 0) {
        cfg->scale_mode = CAMERA_PREVIEW_SCALE_LETTERBOX;
    } else if (strcmp(scale, "center_crop") == 0) {
        cfg->scale_mode = CAMERA_PREVIEW_SCALE_CENTER_CROP;
    }
}

static void camera_preview_apply_scale(lv_obj_t* image, CameraPreviewScaleMode scale_mode,
                                       uint16_t source_width = 0, uint16_t source_height = 0) {
    if (scale_mode == CAMERA_PREVIEW_SCALE_CENTER_CROP && source_width && source_height) {
        lv_image_set_inner_align(image, LV_IMAGE_ALIGN_DEFAULT);
        lv_obj_set_size(image, source_width, source_height);
        lv_obj_center(image);
        return;
    }

    lv_obj_set_size(image, LV_PCT(100), LV_PCT(100));
    lv_obj_center(image);
    lv_image_set_inner_align(image, scale_mode == CAMERA_PREVIEW_SCALE_LETTERBOX
                                        ? LV_IMAGE_ALIGN_CONTAIN
                                        : LV_IMAGE_ALIGN_COVER);
}

static void camera_preview_create(lv_obj_t* tile, const WidgetConfig* cfg,
                                  const ScreenButtonConfig* btn,
                                  const PadRect* rect, const UIScaleInfo* scale,
                                  lv_obj_t* icon_img, lv_obj_t* center_label,
                                  WidgetState* state) {
    (void)btn;
    (void)rect;
    (void)scale;

    auto* preview = reinterpret_cast<CameraPreviewWidgetState*>(state->data);
    memset(preview, 0, sizeof(CameraPreviewWidgetState));
    if (icon_img) lv_obj_add_flag(icon_img, LV_OBJ_FLAG_HIDDEN);
    if (center_label) lv_obj_add_flag(center_label, LV_OBJ_FLAG_HIDDEN);

    preview->image = lv_image_create(tile);
    lv_obj_set_size(preview->image, LV_PCT(100), LV_PCT(100));
    lv_obj_center(preview->image);
    lv_obj_clear_flag(preview->image, LV_OBJ_FLAG_CLICKABLE);
    const auto* preview_config = reinterpret_cast<const CameraPreviewConfig*>(cfg->data);
    camera_preview_apply_scale(preview->image, preview_config->scale_mode);
    camera_feed_acquire_demand(CAMERA_FEED_OUTPUT_RGB565);
    preview->has_demand = true;
}

static void camera_preview_update(lv_obj_t* tile, const WidgetConfig* cfg,
                                  WidgetState* state, const char* raw_value) {
    (void)tile;
    (void)cfg;
    (void)state;
    (void)raw_value;
}

static void camera_preview_tick(lv_obj_t* tile, const WidgetConfig* cfg, WidgetState* state) {
    (void)tile;
    (void)cfg;
    auto* preview = reinterpret_cast<CameraPreviewWidgetState*>(state->data);
    if (!preview->image) return;
    if (screen_saver_manager_is_asleep()) {
        if (preview->has_demand) {
            camera_feed_release_demand(CAMERA_FEED_OUTPUT_RGB565);
            preview->has_demand = false;
        }
        return;
    }
    if (!preview->has_demand) {
        camera_feed_acquire_demand(CAMERA_FEED_OUTPUT_RGB565);
        preview->has_demand = true;
    }
    if (displayDriverIsFlushBusy()) return;

    CameraFeedFrame next = {};
    if (!camera_feed_acquire_frame(&next, CAMERA_FEED_OUTPUT_RGB565)) return;
    if (preview->has_frame && next.generation == preview->generation) {
        camera_feed_release_frame(&next);
        return;
    }

    const CameraRgb565Frame* rgb565 = next.rgb565;
    if (!rgb565 || !rgb565->data || !rgb565->width || !rgb565->height) {
        camera_feed_release_frame(&next);
        return;
    }

    preview->descriptor.header.magic = LV_IMAGE_HEADER_MAGIC;
    preview->descriptor.header.cf = LV_COLOR_FORMAT_RGB565;
    preview->descriptor.header.flags = 0;
    preview->descriptor.header.w = rgb565->width;
    preview->descriptor.header.h = rgb565->height;
    preview->descriptor.header.stride = rgb565->width * sizeof(uint16_t);
    preview->descriptor.data_size = rgb565->size;
    preview->descriptor.data = reinterpret_cast<const uint8_t*>(rgb565->data);
    const auto* preview_config = reinterpret_cast<const CameraPreviewConfig*>(cfg->data);
    camera_preview_apply_scale(preview->image, preview_config->scale_mode, rgb565->width, rgb565->height);
    lv_image_set_src(preview->image, &preview->descriptor);
    lv_obj_invalidate(preview->image);

    if (preview->has_frame) camera_feed_release_frame(&preview->frame);
    preview->frame = next;
    preview->generation = next.generation;
    preview->has_frame = true;
}

static void camera_preview_destroy(WidgetState* state) {
    auto* preview = reinterpret_cast<CameraPreviewWidgetState*>(state->data);
    if (preview->has_frame) camera_feed_release_frame(&preview->frame);
    preview->has_frame = false;
    if (preview->has_demand) camera_feed_release_demand(CAMERA_FEED_OUTPUT_RGB565);
    preview->has_demand = false;
}

static void camera_preview_show(WidgetState* state) {
    auto* preview = reinterpret_cast<CameraPreviewWidgetState*>(state->data);
    if (!preview->has_demand) {
        camera_feed_acquire_demand(CAMERA_FEED_OUTPUT_RGB565);
        preview->has_demand = true;
        LOGD("CameraPreview", "Visible: acquired RGB565 feed demand");
    }
}

static void camera_preview_hide(WidgetState* state) {
    auto* preview = reinterpret_cast<CameraPreviewWidgetState*>(state->data);
    if (preview->has_frame) camera_feed_release_frame(&preview->frame);
    preview->has_frame = false;
    if (preview->has_demand) {
        camera_feed_release_demand(CAMERA_FEED_OUTPUT_RGB565);
        LOGD("CameraPreview", "Hidden: released RGB565 feed demand");
    }
    preview->has_demand = false;
}

#if HAS_MCP
static void camera_preview_describe(JsonObject& out) {
    JsonArray fields = out.createNestedArray("config_fields");
    JsonObject scale = fields.createNestedObject();
    scale["name"] = "widget_camera_scale";
    scale["type"] = "string";
    scale["desc"] = "'cover' (default), 'letterbox' (fit without crop), or 'center_crop' (native-size clip)";
}
#endif

REGISTER_WIDGET_SCHEMA_LIFECYCLE(camera_preview, nullptr, false);

#endif
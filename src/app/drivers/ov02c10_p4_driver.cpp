#include "camera_driver.h"

#if HAS_CAMERA

#include "i2c_bus.h"
#include "jpeg_encoder_service.h"
#include "log_manager.h"

#include <Wire.h>
#include <algorithm>
#include <esp_cam_ctlr.h>
#include <esp_cam_ctlr_csi.h>
#include <esp_cache.h>
#include <esp_ldo_regulator.h>
#include <esp_private/esp_cache_private.h>
#include <esp_timer.h>
#include <driver/isp.h>
#include <soc/mipi_csi_host_struct.h>

static bool s_camera_detected = false;
static bool s_camera_raw10_configured = false;
// ESP32-P4 has one ISP processor. Keep the RAW bypass instance for the
// firmware lifetime and reuse it across short-lived CSI captures.
static isp_proc_handle_t s_isp_processor = nullptr;

// The P4 CSI DMA path stores RAW10 samples in 16-bit containers. The driver
// reports the packed RAW10 payload size in transaction->received_size.
static const size_t kCsiRaw10DmaBytesPerPixel = 2;
static const uint8_t kCsiBufferCount = 2;
static const size_t kRaw10RowBytes = (size_t)CAMERA_CAPTURE_WIDTH * 5 / 4;
static uint8_t s_raw10_row_scratch[kRaw10RowBytes * 2];
// OV02C10 sets 0x4800 without LINE_SYNC_ENABLE, so no line packets are sent.
static const bool kCsiLinePacketsEnabled = false;
static bool camera_read_register(uint16_t reg, uint8_t* value);

struct CameraRegister {
    uint16_t address;
    uint8_t value;
};

// OV02C10 MIPI 1-lane RAW10 1288x728@30fps mode from csvke/esp-video-components
// (feat/add-ov02c10-sensor, Apache-2.0).
// MIPI_CTRL00 value used by the reference driver: BUS_IDLE | CLOCK_LANE_GATE.
// LINE_SYNC_ENABLE (bit 4) is deliberately clear, so no line short packets are
// sent and the ISP must not be told to expect them.
static const uint8_t kOv02c10MipiCtrl00 = 0x64;
static const CameraRegister kOv02c10Raw10Mode[] = {
    {0x0301, 0x08}, {0x0303, 0x06}, {0x0304, 0x01}, {0x0305, 0x77},
    {0x0313, 0x40}, {0x031c, 0x4f}, {0x3016, 0x12}, {0x301b, 0xf0},
    {0x3020, 0x97}, {0x3021, 0x23}, {0x3022, 0x01}, {0x3026, 0xb4},
    {0x3027, 0xf1}, {0x303b, 0x00}, {0x303c, 0x4f}, {0x303d, 0xe6},
    {0x303e, 0x00}, {0x303f, 0x03}, {0x3501, 0x04}, {0x3502, 0x6c},
    {0x3504, 0x0c}, {0x3507, 0x00}, {0x3508, 0x08}, {0x3509, 0x00},
    {0x350a, 0x01}, {0x350b, 0x00}, {0x350c, 0x41}, {0x3600, 0x84},
    {0x3603, 0x08}, {0x3610, 0x57}, {0x3611, 0x1b}, {0x3613, 0x78},
    {0x3623, 0x00}, {0x3632, 0xa0}, {0x3642, 0xe8}, {0x364c, 0x70},
    {0x365d, 0x00}, {0x365f, 0x0f}, {0x3708, 0x30}, {0x3714, 0x24},
    {0x3725, 0x02}, {0x3737, 0x08}, {0x3739, 0x28}, {0x3749, 0x32},
    {0x374a, 0x32}, {0x374b, 0x32}, {0x374c, 0x32}, {0x374d, 0x81},
    {0x374e, 0x81}, {0x374f, 0x81}, {0x3752, 0x36}, {0x3753, 0x36},
    {0x3754, 0x36}, {0x3761, 0x00}, {0x376c, 0x81}, {0x3774, 0x18},
    {0x3776, 0x08}, {0x377c, 0x81}, {0x377d, 0x81}, {0x377e, 0x81},
    {0x37a0, 0x44}, {0x37a6, 0x44}, {0x37aa, 0x0d}, {0x37ae, 0x00},
    {0x37cb, 0x03}, {0x37cc, 0x01}, {0x37d8, 0x02}, {0x37d9, 0x10},
    {0x37e1, 0x10}, {0x37e2, 0x18}, {0x37e3, 0x08}, {0x37e4, 0x08},
    {0x37e5, 0x02}, {0x37e6, 0x08}, {0x3800, 0x01}, {0x3801, 0x40},
    {0x3802, 0x00}, {0x3803, 0xb4}, {0x3804, 0x06}, {0x3805, 0x4f},
    {0x3806, 0x03}, {0x3807, 0x8f}, {0x3808, 0x05}, {0x3809, 0x00},
    {0x380a, 0x02}, {0x380b, 0xd0}, {0x380c, 0x08}, {0x380d, 0xe8},
    {0x380e, 0x04}, {0x380f, 0x8c}, {0x3810, 0x00}, {0x3811, 0x07},
    {0x3812, 0x00}, {0x3813, 0x04}, {0x3814, 0x01}, {0x3815, 0x01},
    {0x3816, 0x01}, {0x3817, 0x01}, {0x3820, 0xa0}, {0x3821, 0x00},
    {0x3822, 0x80}, {0x3823, 0x08}, {0x3824, 0x00}, {0x3825, 0x20},
    {0x3826, 0x00}, {0x3827, 0x08}, {0x382a, 0x00}, {0x382b, 0x08},
    {0x382d, 0x00}, {0x382e, 0x00}, {0x382f, 0x23}, {0x3834, 0x00},
    {0x3839, 0x00}, {0x383a, 0xd1}, {0x383e, 0x03}, {0x393d, 0x29},
    {0x393f, 0x6e}, {0x394b, 0x06}, {0x394c, 0x06}, {0x394d, 0x08},
    {0x394e, 0x0a}, {0x394f, 0x01}, {0x3950, 0x01}, {0x3951, 0x01},
    {0x3952, 0x01}, {0x3953, 0x01}, {0x3954, 0x01}, {0x3955, 0x01},
    {0x3956, 0x01}, {0x3957, 0x0e}, {0x3958, 0x08}, {0x3959, 0x08},
    {0x395a, 0x08}, {0x395b, 0x13}, {0x395c, 0x09}, {0x395d, 0x05},
    {0x395e, 0x02}, {0x395f, 0x00}, {0x3960, 0x00}, {0x3961, 0x00},
    {0x3962, 0x00}, {0x3963, 0x00}, {0x3964, 0x00}, {0x3965, 0x00},
    {0x3966, 0x00}, {0x3967, 0x00}, {0x3968, 0x01}, {0x3969, 0x01},
    {0x396a, 0x01}, {0x396b, 0x01}, {0x396c, 0x10}, {0x396d, 0xf0},
    {0x396e, 0x11}, {0x396f, 0x00}, {0x3970, 0x37}, {0x3971, 0x37},
    {0x3972, 0x37}, {0x3973, 0x37}, {0x3974, 0x00}, {0x3975, 0x3c},
    {0x3976, 0x3c}, {0x3977, 0x3c}, {0x3978, 0x3c}, {0x3c00, 0x0f},
    {0x3c20, 0x01}, {0x3c21, 0x08}, {0x3f00, 0x8b}, {0x3f02, 0x0f},
    {0x4000, 0xc3}, {0x4001, 0xe0}, {0x4002, 0x00}, {0x4003, 0x40},
    {0x4008, 0x04}, {0x4009, 0x23}, {0x400a, 0x04}, {0x400b, 0x01},
    {0x4041, 0x20}, {0x4077, 0x06}, {0x4078, 0x00}, {0x4079, 0x1a},
    {0x407a, 0x7f}, {0x407b, 0x01}, {0x4080, 0x03}, {0x4081, 0x84},
    {0x4308, 0x03}, {0x4309, 0xff}, {0x430d, 0x00}, {0x4500, 0x07},
    {0x4501, 0x00}, {0x4503, 0x00}, {0x450a, 0x04}, {0x450e, 0x00},
    {0x450f, 0x00}, {0x4800, kOv02c10MipiCtrl00}, {0x4806, 0x00}, {0x4813, 0x00},
    {0x4815, 0x40}, {0x4816, 0x12}, {0x481f, 0x30}, {0x4837, 0x15},
    {0x4857, 0x05}, {0x4884, 0x04}, {0x4900, 0x00}, {0x4901, 0x00},
    {0x4902, 0x01}, {0x4d00, 0x03}, {0x4d01, 0xd8}, {0x4d02, 0xba},
    {0x4d03, 0xa0}, {0x4d04, 0xb7}, {0x4d05, 0x34}, {0x4d0d, 0x00},
    {0x5000, 0xfd}, {0x5001, 0x50}, {0x5006, 0x00}, {0x5080, 0x40},
    {0x5181, 0x2b}, {0x5202, 0xa3}, {0x5206, 0x01}, {0x5207, 0x00},
    {0x520a, 0x01}, {0x520b, 0x00}, {0x4f00, 0x01},
};

struct CameraCsiProbeContext {
    void* buffers[kCsiBufferCount];
    size_t buffer_size;
    volatile bool capture_armed;
    volatile bool frame_complete;
    volatile size_t received_size;
    volatile uint32_t transaction_requests;
    volatile uint32_t completed_transactions;
    uint8_t next_buffer_index;
    uint8_t completed_buffer_index;
};

static esp_ldo_channel_handle_t s_csi_ldo_handle = nullptr;
static esp_cam_ctlr_handle_t s_csi_controller = nullptr;
static CameraCsiProbeContext s_csi_context = {};
static bool s_csi_controller_enabled = false;
static bool s_csi_capture_started = false;
static bool s_camera_streaming = false;
// This buffer serves the continuous RGB565/JPEG feed and avoids a PSRAM
// allocation/free cycle for each frame. Raw snapshot callers still own theirs.
static CameraRawFrame s_rgb565_raw_staging = {};

static bool camera_csi_buffers_ready() {
    for (void* buffer : s_csi_context.buffers) {
        if (!buffer) return false;
    }
    return true;
}

static void camera_free_csi_buffers() {
    for (void*& buffer : s_csi_context.buffers) {
        if (buffer) free(buffer);
        buffer = nullptr;
    }
    s_csi_context.buffer_size = 0;
}

static void camera_stop_csi_capture() {
    s_csi_context.capture_armed = false;
    if (!s_csi_capture_started) return;
    const esp_err_t error = esp_cam_ctlr_stop(s_csi_controller);
    if (error != ESP_OK) {
        LOGW("Camera", "CSI stop after failed capture: %s", esp_err_to_name(error));
    }
    s_csi_capture_started = false;
}

static void camera_prepare_csi_capture() {
    s_csi_context.frame_complete = false;
    s_csi_context.received_size = 0;
    s_csi_context.transaction_requests = 0;
    s_csi_context.completed_transactions = 0;
    s_csi_context.next_buffer_index = 0;
    s_csi_context.completed_buffer_index = 0;
    s_csi_context.capture_armed = true;
}

static bool camera_on_csi_get_new_transaction(esp_cam_ctlr_handle_t controller,
                                              esp_cam_ctlr_trans_t* transaction,
                                              void* user_data) {
    (void)controller;
    (void)user_data;
    CameraCsiProbeContext* context = &s_csi_context;
    context->transaction_requests++;
    const uint8_t buffer_index = context->next_buffer_index;
    context->next_buffer_index = (buffer_index + 1) % kCsiBufferCount;
    transaction->buffer = context->buffers[buffer_index];
    transaction->buflen = context->buffer_size;
    return false;
}

static bool camera_on_csi_transaction_finished(esp_cam_ctlr_handle_t controller,
                                               esp_cam_ctlr_trans_t* transaction,
                                               void* user_data) {
    (void)controller;
    (void)user_data;
    CameraCsiProbeContext* context = &s_csi_context;
    context->completed_transactions++;
    if (context->capture_armed && !context->frame_complete) {
        context->received_size = transaction->received_size;
        for (uint8_t index = 0; index < kCsiBufferCount; ++index) {
            if (transaction->buffer == context->buffers[index]) {
                context->completed_buffer_index = index;
                break;
            }
        }
        context->frame_complete = true;
    }
    return false;
}

static bool camera_write_register(uint16_t reg, uint8_t value) {
    Wire.beginTransmission(CAMERA_SCCB_ADDR);
    Wire.write(static_cast<uint8_t>(reg >> 8));
    Wire.write(static_cast<uint8_t>(reg));
    Wire.write(value);
    return Wire.endTransmission() == 0;
}

// Confirms the sensor accepted the crop; a mismatch desynchronises the CSI bridge.
static void camera_log_output_size() {
    if (!i2c_bus_lock(pdMS_TO_TICKS(1000))) return;
    uint8_t w_high = 0, w_low = 0, h_high = 0, h_low = 0;
    const bool ok = camera_read_register(0x3808, &w_high) &&
                    camera_read_register(0x3809, &w_low) &&
                    camera_read_register(0x380a, &h_high) &&
                    camera_read_register(0x380b, &h_low);
    i2c_bus_unlock();
    if (!ok) return;

    const uint16_t width = (uint16_t)(w_high << 8 | w_low);
    const uint16_t height = (uint16_t)(h_high << 8 | h_low);
    const uint32_t stride = (uint32_t)width * 10 / 8;
    LOGI("Camera", "Sensor output: %ux%u (expected %ux%u), line %u bytes, 4-byte aligned: %s",
         width, height, CAMERA_CAPTURE_WIDTH, CAMERA_CAPTURE_HEIGHT,
         (unsigned)stride, (stride % 4 == 0) ? "yes" : "NO");
}

static bool camera_configure_raw10_mode() {
    if (!i2c_bus_lock(pdMS_TO_TICKS(1000))) {
        LOGW("Camera", "RAW10 mode setup skipped: I2C bus busy");
        return false;
    }

    bool configured = camera_write_register(0x0100, 0x00) &&
                      camera_write_register(0x0103, 0x01);
    delay(10);
    if (configured) configured = camera_write_register(0x4800, 0x01);
    for (size_t index = 0; configured && index < sizeof(kOv02c10Raw10Mode) / sizeof(kOv02c10Raw10Mode[0]); ++index) {
        configured = camera_write_register(kOv02c10Raw10Mode[index].address,
                                           kOv02c10Raw10Mode[index].value);
    }
    i2c_bus_unlock();

    if (!configured) LOGW("Camera", "OV02C10 RAW10 mode register write failed");
    else camera_log_output_size();
    return configured;
}

static bool camera_set_streaming(bool enabled, TickType_t lock_timeout = pdMS_TO_TICKS(1000)) {
    if (!i2c_bus_lock(lock_timeout)) return false;
    bool configured = camera_write_register(0x4800, enabled ? kOv02c10MipiCtrl00 : 0x21);
    if (configured && enabled) {
        configured = camera_write_register(0x3002, 0x01) &&
                     camera_write_register(0x3010, 0x01) &&
                     camera_write_register(0x300d, 0x01);
    }
    if (configured) configured = camera_write_register(0x0100, enabled ? 0x01 : 0x00);

    uint8_t stream_mode = 0;
    uint8_t mipi_control = 0;
    const bool read_back = configured && camera_read_register(0x0100, &stream_mode) &&
                           camera_read_register(0x4800, &mipi_control);
    i2c_bus_unlock();

    if (read_back) {
        LOGI("Camera", "Stream %s: 0x0100=0x%02X 0x4800=0x%02X",
             enabled ? "on" : "off", stream_mode, mipi_control);
    }
    return read_back;
}

// The Arduino 3.3.7 CSI driver exposes no on_error callback, so error groups are
// unmasked here and polled from the capture path instead.
static void camera_unmask_csi_errors() {
    MIPI_CSI_HOST.int_msk_phy_fatal.val = 0xFFFFFFFF;
    MIPI_CSI_HOST.int_msk_pkt_fatal.val = 0xFFFFFFFF;
    MIPI_CSI_HOST.int_msk_phy.val = 0xFFFFFFFF;
    MIPI_CSI_HOST.int_msk_bndry_frame_fatal.val = 0xFFFFFFFF;
    MIPI_CSI_HOST.int_msk_seq_frame_fatal.val = 0xFFFFFFFF;
    MIPI_CSI_HOST.int_msk_crc_frame_fatal.val = 0xFFFFFFFF;
    MIPI_CSI_HOST.int_msk_pld_crc_fatal.val = 0xFFFFFFFF;
    MIPI_CSI_HOST.int_msk_data_id.val = 0xFFFFFFFF;
    MIPI_CSI_HOST.int_msk_ecc_corrected.val = 0xFFFFFFFF;
}

// Status registers are read-clear, so each field is sampled exactly once.
static void camera_log_csi_errors() {
    const uint32_t main_status = MIPI_CSI_HOST.int_st_main.val;
    const uint32_t phy_fatal = MIPI_CSI_HOST.int_st_phy_fatal.val;
    const uint32_t pkt_fatal = MIPI_CSI_HOST.int_st_pkt_fatal.val;
    const uint32_t phy = MIPI_CSI_HOST.int_st_phy.val;
    const uint32_t bndry_frame = MIPI_CSI_HOST.int_st_bndry_frame_fatal.val;
    const uint32_t seq_frame = MIPI_CSI_HOST.int_st_seq_frame_fatal.val;
    const uint32_t crc_frame = MIPI_CSI_HOST.int_st_crc_frame_fatal.val;
    const uint32_t pld_crc = MIPI_CSI_HOST.int_st_pld_crc_fatal.val;
    const uint32_t data_id = MIPI_CSI_HOST.int_st_data_id.val;
    const uint32_t ecc_corrected = MIPI_CSI_HOST.int_st_ecc_corrected.val;
    const uint32_t stopstate = MIPI_CSI_HOST.phy_stopstate.val;

    LOGW("Camera", "CSI host status: main=0x%08X stopstate=0x%08X",
         static_cast<unsigned>(main_status), static_cast<unsigned>(stopstate));
    LOGW("Camera", "CSI errors: phy_fatal=0x%08X pkt_fatal=0x%08X phy=0x%08X",
         static_cast<unsigned>(phy_fatal), static_cast<unsigned>(pkt_fatal),
         static_cast<unsigned>(phy));
    LOGW("Camera", "CSI frame errors: bndry=0x%08X seq=0x%08X crc=0x%08X pld_crc=0x%08X",
         static_cast<unsigned>(bndry_frame), static_cast<unsigned>(seq_frame),
         static_cast<unsigned>(crc_frame), static_cast<unsigned>(pld_crc));
    LOGW("Camera", "CSI packet errors: data_id=0x%08X ecc_corrected=0x%08X",
         static_cast<unsigned>(data_id), static_cast<unsigned>(ecc_corrected));
}

static esp_err_t camera_create_isp_bypass(isp_proc_handle_t* processor) {
    const esp_isp_processor_cfg_t config = {
        .clk_src = ISP_CLK_SRC_PLL240,
        .clk_hz = 240 * 1000 * 1000,
        .input_data_source = ISP_INPUT_DATA_SOURCE_CSI,
        // Bypass forwards the OV02C10 RAW10 path without image processing.
        .input_data_color_type = ISP_COLOR_RAW10,
        .output_data_color_type = ISP_COLOR_RAW10,
        .yuv_range = ISP_COLOR_RANGE_FULL,
        .yuv_std = ISP_YUV_CONV_STD_BT601,
        .has_line_start_packet = kCsiLinePacketsEnabled,
        .has_line_end_packet = kCsiLinePacketsEnabled,
        .h_res = CAMERA_CAPTURE_WIDTH,
        .v_res = CAMERA_CAPTURE_HEIGHT,
        .bayer_order = COLOR_RAW_ELEMENT_ORDER_GBRG,
        .intr_priority = 0,
        .flags = {
            .bypass_isp = true,
            .byte_swap_en = false,
        },
    };
    return esp_isp_new_processor(&config, processor);
}

static bool camera_driver_capture_raw_into(CameraRawFrame* frame, bool retain_buffer_on_failure) {
    if (!retain_buffer_on_failure && frame) *frame = {};
    const esp_ldo_channel_config_t ldo_config = {
        .chan_id = 3,
        .voltage_mv = 2500,
    };
    esp_err_t error = ESP_OK;

    const esp_cam_ctlr_csi_config_t csi_config = {
        .ctlr_id = 0,
        .clk_src = MIPI_CSI_PHY_CLK_SRC_DEFAULT,
        .h_res = CAMERA_CAPTURE_WIDTH,
        .v_res = CAMERA_CAPTURE_HEIGHT,
        .data_lane_num = CAMERA_CSI_DATA_LANES,
        .lane_bit_rate_mbps = CAMERA_CSI_LANE_BIT_RATE_MBPS,
        .input_data_color_type = CAM_CTLR_COLOR_RAW10,
        .output_data_color_type = CAM_CTLR_COLOR_RAW10,
        .queue_items = 1,
        .byte_swap_en = false,
        .bk_buffer_dis = true,
    };

    if (!s_csi_ldo_handle) {
        error = esp_ldo_acquire_channel(&ldo_config, &s_csi_ldo_handle);
        if (error != ESP_OK) LOGW("Camera", "CSI LDO acquire failed: %s", esp_err_to_name(error));
    }
    if (error == ESP_OK && !s_csi_controller) {
        error = esp_cam_new_csi_ctlr(&csi_config, &s_csi_controller);
    }
    if (error == ESP_OK && !camera_csi_buffers_ready()) {
        // A prior allocation attempt may have left only one buffer populated.
        // Never start DMA until both buffers were allocated as a matched pair.
        camera_free_csi_buffers();
        size_t cache_alignment = 0;
        const uint32_t buffer_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA;
        error = esp_cache_get_alignment(buffer_caps, &cache_alignment);
        if (error == ESP_OK) {
            const size_t dma_frame_size = CAMERA_CAPTURE_WIDTH * CAMERA_CAPTURE_HEIGHT *
                                          kCsiRaw10DmaBytesPerPixel;
            s_csi_context.buffer_size = (dma_frame_size + cache_alignment - 1) /
                                        cache_alignment * cache_alignment;
            for (uint8_t index = 0; index < kCsiBufferCount; ++index) {
                s_csi_context.buffers[index] = esp_cam_ctlr_alloc_buffer(
                    s_csi_controller, s_csi_context.buffer_size, buffer_caps);
                if (!s_csi_context.buffers[index]) {
                    error = ESP_ERR_NO_MEM;
                    break;
                }
            }
            if (error != ESP_OK) camera_free_csi_buffers();
        }
    }
    if (error == ESP_OK && !s_csi_controller_enabled) {
        const esp_cam_ctlr_evt_cbs_t callbacks = {
            .on_get_new_trans = camera_on_csi_get_new_transaction,
            .on_trans_finished = camera_on_csi_transaction_finished,
        };
        error = esp_cam_ctlr_register_event_callbacks(s_csi_controller, &callbacks, &s_csi_context);
        // The ISP claims the shared MIPI CSI bridge. It must be created before
        // enabling CSI, otherwise the IDF driver leaks its sole ISP slot on failure.
        if (error == ESP_OK && !s_isp_processor) {
            error = camera_create_isp_bypass(&s_isp_processor);
            if (error != ESP_OK) {
                LOGE("Camera", "ISP bypass creation failed: %s", esp_err_to_name(error));
            }
        }
        if (error == ESP_OK) {
            error = esp_cam_ctlr_enable(s_csi_controller);
            s_csi_controller_enabled = error == ESP_OK;
        }
        if (error == ESP_OK) camera_unmask_csi_errors();
    }
    if (error == ESP_OK && !s_camera_raw10_configured) {
        if (!camera_configure_raw10_mode()) error = ESP_FAIL;
        else s_camera_raw10_configured = true;
    }
    if (error == ESP_OK) {
        camera_prepare_csi_capture();
    }
    if (error == ESP_OK && !s_csi_capture_started) {
        error = esp_cam_ctlr_start(s_csi_controller);
        s_csi_capture_started = error == ESP_OK;
    }
    if (error == ESP_OK && !s_camera_streaming) {
        if (!camera_set_streaming(true)) {
            error = ESP_FAIL;
        } else {
            s_camera_streaming = true;
        }
    }
    const unsigned long capture_started_ms = millis();
    while (error == ESP_OK && !s_csi_context.frame_complete && millis() - capture_started_ms < 1000) {
        delay(1);
    }
    if (error == ESP_OK && !s_csi_context.frame_complete) {
        error = ESP_ERR_TIMEOUT;
        // Stop the active transaction before the next capture arms its wait;
        // otherwise a late callback can complete the wrong request.
        camera_stop_csi_capture();
    }
    if (error == ESP_OK && frame) {
        if (frame->size < s_csi_context.received_size) {
            uint8_t* resized = static_cast<uint8_t*>(heap_caps_realloc(
                frame->data, s_csi_context.received_size, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM));
            if (!resized) {
                error = ESP_ERR_NO_MEM;
            } else {
                frame->data = resized;
            }
        }
        if (error == ESP_OK && !frame->data) {
            error = ESP_ERR_NO_MEM;
        } else if (error == ESP_OK) {
            void* completed_buffer = s_csi_context.buffers[s_csi_context.completed_buffer_index];
            error = esp_cache_msync(completed_buffer, s_csi_context.buffer_size,
                                    ESP_CACHE_MSYNC_FLAG_TYPE_DATA | ESP_CACHE_MSYNC_FLAG_DIR_M2C);
            if (error == ESP_OK) {
                memcpy(frame->data, completed_buffer, s_csi_context.received_size);
                frame->size = s_csi_context.received_size;
                frame->width = CAMERA_CAPTURE_WIDTH;
                frame->height = CAMERA_CAPTURE_HEIGHT;
            }
        }
    }
    s_csi_context.capture_armed = false;
    if (error != ESP_OK) {
        if (frame && !retain_buffer_on_failure) camera_driver_release_raw(frame);
        LOGW("Camera", "RAW10 frame capture failed: %s (requests=%u completed=%u bytes=%u)",
             esp_err_to_name(error), static_cast<unsigned>(s_csi_context.transaction_requests),
             static_cast<unsigned>(s_csi_context.completed_transactions),
             static_cast<unsigned>(s_csi_context.received_size));
        camera_log_csi_errors();
        return false;
    }

    return true;
}

bool camera_driver_capture_raw(CameraRawFrame* frame) {
    return camera_driver_capture_raw_into(frame, false);
}

void camera_driver_release_raw(CameraRawFrame* frame) {
    if (!frame) return;
    if (frame->data) free(frame->data);
    *frame = {};
}

static uint8_t camera_raw10_high_byte(const CameraRawFrame& raw, uint16_t x, uint16_t y) {
    const size_t raw_index = (size_t)y * kRaw10RowBytes + (size_t)(x / 4) * 5 + x % 4;
    return raw.data[raw_index];
}

static uint8_t camera_raw10_row_high_byte(const uint8_t* row, uint16_t x) {
    return row[(size_t)(x / 4) * 5 + x % 4];
}

// Exposure is expressed in lines. The verified mode has 1164 vertical timing
// lines, so the service constrains this setting to VTS-15 or below.
static uint16_t s_exposure_lines = 0;

static bool camera_write_exposure(uint16_t lines) {
    if (!i2c_bus_lock(pdMS_TO_TICKS(1000))) return false;
    const bool written = camera_write_register(0x3501, static_cast<uint8_t>(lines >> 8)) &&
                         camera_write_register(0x3502, static_cast<uint8_t>(lines & 0xFF));
    i2c_bus_unlock();
    return written;
}

struct CameraWhiteBalance {
    uint16_t red_gain_q8;
    uint16_t blue_gain_q8;
};

static const uint16_t kWhiteBalanceUnityQ8 = 256;
static const uint16_t kWhiteBalanceMinQ8 = 64;
static const uint16_t kWhiteBalanceMaxQ8 = 1024;
static const uint16_t kWhiteBalanceSampleStep = 8;

static uint16_t camera_clamp_white_balance(uint32_t gain_q8) {
    if (gain_q8 < kWhiteBalanceMinQ8) return kWhiteBalanceMinQ8;
    if (gain_q8 > kWhiteBalanceMaxQ8) return kWhiteBalanceMaxQ8;
    return static_cast<uint16_t>(gain_q8);
}

// Estimate the sensor's green cast from a sparse GBRG sample. User settings
// are applied as multipliers afterwards, so they remain useful for tuning.
static CameraWhiteBalance camera_estimate_white_balance(const CameraRawFrame& raw) {
    uint64_t red_sum = 0;
    uint64_t green_sum = 0;
    uint64_t blue_sum = 0;
    uint32_t samples = 0;

    for (uint16_t y = 0; y + 1 < raw.height; y += kWhiteBalanceSampleStep) {
        for (uint16_t x = 0; x + 1 < raw.width; x += kWhiteBalanceSampleStep) {
            green_sum += camera_raw10_high_byte(raw, x, y);
            green_sum += camera_raw10_high_byte(raw, x + 1, y + 1);
            blue_sum += camera_raw10_high_byte(raw, x + 1, y);
            red_sum += camera_raw10_high_byte(raw, x, y + 1);
            ++samples;
        }
    }

    if (!samples || !red_sum || !blue_sum) {
        return {kWhiteBalanceUnityQ8, kWhiteBalanceUnityQ8};
    }

    const uint64_t green_mean = green_sum / 2;
    return {
        camera_clamp_white_balance(green_mean * kWhiteBalanceUnityQ8 / red_sum),
        camera_clamp_white_balance(green_mean * kWhiteBalanceUnityQ8 / blue_sum),
    };
}

static uint8_t camera_apply_gain(uint8_t value, uint16_t gain_q8) {
    const uint32_t scaled = (static_cast<uint32_t>(value) * gain_q8) >> 8;
    return static_cast<uint8_t>(scaled > 255 ? 255 : scaled);
}

// Each 2x2 GBRG quad becomes one output pixel, which demosaics and downsamples
// in a single step and needs no interpolation between neighbouring quads.
//   (x, y) = G   (x+1, y)   = B
//   (x, y+1) = R (x+1, y+1) = G
static uint16_t camera_bayer_quad_to_rgb565(const uint8_t* top_row, const uint8_t* bottom_row, uint16_t x,
                                            const CameraWhiteBalance& balance) {
    const uint8_t green_top = camera_raw10_row_high_byte(top_row, x);
    const uint8_t blue = camera_apply_gain(camera_raw10_row_high_byte(top_row, x + 1), balance.blue_gain_q8);
    const uint8_t red = camera_apply_gain(camera_raw10_row_high_byte(bottom_row, x), balance.red_gain_q8);
    const uint8_t green_bottom = camera_raw10_row_high_byte(bottom_row, x + 1);
    const uint8_t green = static_cast<uint8_t>((green_top + green_bottom) / 2);

    return static_cast<uint16_t>(((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3));
}

bool camera_driver_capture_rgb565(CameraRgb565Frame* rgb565, CameraJpegFrame* frame,
                                  CameraCaptureTiming* timing,
                                  const CameraCaptureSettings& settings) {
    if (!rgb565 || !rgb565->data) return false;
    if (frame) *frame = {};
    if (timing) *timing = {};
    const int64_t capture_started_us = esp_timer_get_time();

    const bool exposure_changed = settings.exposure_lines != s_exposure_lines;
    if (exposure_changed) {
        if (!camera_write_exposure(settings.exposure_lines)) {
            LOGW("Camera", "Failed to apply exposure %u lines", settings.exposure_lines);
            return false;
        }
        s_exposure_lines = settings.exposure_lines;

        // The SCCB update can land during an active sensor frame. Discard its
        // completion so the returned JPEG always uses a full new exposure.
        CameraRawFrame stale_frame = {};
        if (!camera_driver_capture_raw(&stale_frame)) return false;
        camera_driver_release_raw(&stale_frame);
    }

    const int64_t raw_started_us = esp_timer_get_time();
    if (!camera_driver_capture_raw_into(&s_rgb565_raw_staging, true)) return false;
    if (timing) timing->raw_capture_us = static_cast<uint32_t>(esp_timer_get_time() - raw_started_us);

    const CameraRawFrame& raw = s_rgb565_raw_staging;

    const CameraWhiteBalance estimated_balance = camera_estimate_white_balance(raw);
    const CameraWhiteBalance balance = {
        camera_clamp_white_balance(static_cast<uint32_t>(estimated_balance.red_gain_q8) *
                                   settings.white_balance_red_q8 / kWhiteBalanceUnityQ8),
        camera_clamp_white_balance(static_cast<uint32_t>(estimated_balance.blue_gain_q8) *
                                   settings.white_balance_blue_q8 / kWhiteBalanceUnityQ8),
    };

    const size_t raw_pixel_count = (size_t)raw.width * raw.height;
    const size_t expected_raw_size = raw_pixel_count * 5 / 4;
    bool captured = false;
    const bool output_supported = raw.size == expected_raw_size &&
                                  raw.width / 2 >= settings.output_width &&
                                  raw.height / 2 >= settings.output_height &&
                                  rgb565->size >= (size_t)settings.output_width * settings.output_height * sizeof(uint16_t);
    if (!output_supported) {
        LOGW("Camera", "Unsupported preview: raw=%ux%u/%u bytes output=%ux%u",
             raw.width, raw.height, static_cast<unsigned>(raw.size),
             settings.output_width, settings.output_height);
    }
    if (output_supported) {
        const bool swap_dimensions = settings.rotation == CAMERA_ROTATION_90 ||
                                     settings.rotation == CAMERA_ROTATION_270;
        const uint16_t output_width = swap_dimensions ? settings.output_height : settings.output_width;
        const uint16_t output_height = swap_dimensions ? settings.output_width : settings.output_height;
        const int64_t convert_started_us = esp_timer_get_time();
        for (uint16_t y = 0; y < settings.output_height; ++y) {
            const uint16_t source_y = static_cast<uint16_t>(
                (static_cast<uint32_t>(y) * (raw.height - 2) / (settings.output_height - 1)) & ~1U);
            const uint8_t* source_top_row = raw.data + (size_t)source_y * kRaw10RowBytes;
            const uint8_t* source_bottom_row = source_top_row + kRaw10RowBytes;
            memcpy(s_raw10_row_scratch, source_top_row, kRaw10RowBytes);
            memcpy(s_raw10_row_scratch + kRaw10RowBytes, source_bottom_row, kRaw10RowBytes);
            uint16_t* destination = rgb565->data;
            ptrdiff_t destination_step = 1;
            switch (settings.rotation) {
                case CAMERA_ROTATION_180:
                    destination += (size_t)(settings.output_height - 1 - y) * output_width +
                                   settings.output_width - 1;
                    destination_step = -1;
                    break;
                case CAMERA_ROTATION_90:
                    destination += settings.output_height - 1 - y;
                    destination_step = output_width;
                    break;
                case CAMERA_ROTATION_270:
                    destination += (size_t)(settings.output_width - 1) * output_width + y;
                    destination_step = -static_cast<ptrdiff_t>(output_width);
                    break;
                case CAMERA_ROTATION_0:
                default:
                    destination += (size_t)y * output_width;
                    break;
            }
            for (uint16_t x = 0; x < settings.output_width; ++x) {
                const uint16_t source_x = static_cast<uint16_t>(
                    (static_cast<uint32_t>(x) * (raw.width - 2) / (settings.output_width - 1)) & ~1U);
                *destination = camera_bayer_quad_to_rgb565(s_raw10_row_scratch,
                                                            s_raw10_row_scratch + kRaw10RowBytes,
                                                            source_x, balance);
                destination += destination_step;
            }
        }
        if (timing) timing->rgb565_convert_us = static_cast<uint32_t>(esp_timer_get_time() - convert_started_us);
        captured = true;
        if (frame) {
            const int64_t jpeg_started_us = esp_timer_get_time();
            captured = jpeg_encode_rgb565(reinterpret_cast<const uint8_t*>(rgb565->data),
                                          output_width, output_height, settings.jpeg_quality,
                                          &frame->data, &frame->size);
            if (timing) timing->jpeg_encode_us = static_cast<uint32_t>(esp_timer_get_time() - jpeg_started_us);
            if (captured) {
                frame->width = output_width;
                frame->height = output_height;
            }
        }
        rgb565->width = output_width;
        rgb565->height = output_height;
    }
    if (timing) timing->total_us = static_cast<uint32_t>(esp_timer_get_time() - capture_started_us);

    if (!captured) {
        if (frame) camera_driver_release_jpeg(frame);
        LOGW("Camera", "RGB565 frame capture failed for %ux%u at quality %u",
             settings.output_width, settings.output_height, settings.jpeg_quality);
        return false;
    }

    return true;
}

bool camera_driver_capture_jpeg(CameraJpegFrame* frame, const CameraCaptureSettings& settings) {
    if (!frame) return false;
    const size_t rgb_size = (size_t)settings.output_width * settings.output_height * sizeof(uint16_t);
    uint16_t* rgb = static_cast<uint16_t*>(
        heap_caps_malloc(rgb_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!rgb) {
        LOGW("Camera", "Preview buffer allocation failed: %u bytes for %ux%u",
             static_cast<unsigned>(rgb_size), settings.output_width, settings.output_height);
        return false;
    }
    CameraRgb565Frame rgb565 = {.data = rgb, .size = rgb_size};
    const bool captured = camera_driver_capture_rgb565(&rgb565, frame, nullptr, settings);
    free(rgb);
    return captured;
}

void camera_driver_release_jpeg(CameraJpegFrame* frame) {
    if (!frame) return;
    if (frame->data) free(frame->data);
    *frame = {};
}

static bool camera_read_register(uint16_t reg, uint8_t* value) {
    Wire.beginTransmission(CAMERA_SCCB_ADDR);
    Wire.write(static_cast<uint8_t>(reg >> 8));
    Wire.write(static_cast<uint8_t>(reg));
    if (Wire.endTransmission(false) != 0) {
        return false;
    }

    if (Wire.requestFrom(CAMERA_SCCB_ADDR, static_cast<uint8_t>(1)) != 1) {
        return false;
    }

    *value = Wire.read();
    return true;
}

void camera_driver_init() {
    if (!i2c_bus_lock()) {
        LOGW("Camera", "SCCB probe skipped: I2C bus busy");
        return;
    }

    Wire.beginTransmission(CAMERA_SCCB_ADDR);
    const uint8_t error = Wire.endTransmission();

    if (error != 0) {
        i2c_bus_unlock();
        LOGW("Camera", "No sensor at SCCB address 0x%02X (I2C error %u)", CAMERA_SCCB_ADDR, error);
        return;
    }

    uint8_t id_high = 0;
    uint8_t id_low = 0;
    const bool id_read = camera_read_register(CAMERA_SENSOR_ID_REG_HIGH, &id_high) &&
                         camera_read_register(CAMERA_SENSOR_ID_REG_HIGH + 1, &id_low);
    i2c_bus_unlock();

    if (!id_read) {
        LOGW("Camera", "Sensor acknowledged at 0x%02X but product ID read failed", CAMERA_SCCB_ADDR);
        return;
    }

    const uint16_t sensor_id = (static_cast<uint16_t>(id_high) << 8) | id_low;
    s_camera_detected = sensor_id == CAMERA_SENSOR_EXPECTED_ID;
    if (s_camera_detected) {
        LOGI("Camera", "Detected sensor ID 0x%04X at SCCB address 0x%02X", sensor_id, CAMERA_SCCB_ADDR);
    } else {
        LOGW("Camera", "Unexpected sensor ID 0x%04X at SCCB address 0x%02X", sensor_id, CAMERA_SCCB_ADDR);
    }
}

bool camera_driver_deinit() {
    if (s_camera_streaming) {
        if (!camera_set_streaming(false, 0)) {
            LOGD("Camera", "Deferring idle cleanup: I2C bus busy");
            return false;
        }
        s_camera_streaming = false;
    }
    camera_stop_csi_capture();
    if (s_csi_controller_enabled && s_csi_controller) {
        const esp_err_t error = esp_cam_ctlr_disable(s_csi_controller);
        if (error != ESP_OK) {
            LOGW("Camera", "CSI disable during idle cleanup failed: %s", esp_err_to_name(error));
        }
    }
    s_csi_controller_enabled = false;
    camera_free_csi_buffers();
    if (s_isp_processor) {
        const esp_err_t error = esp_isp_del_processor(s_isp_processor);
        if (error != ESP_OK) {
            LOGW("Camera", "ISP cleanup failed: %s", esp_err_to_name(error));
        }
        s_isp_processor = nullptr;
    }
    if (s_csi_controller) {
        const esp_err_t error = esp_cam_ctlr_del(s_csi_controller);
        if (error != ESP_OK) {
            LOGW("Camera", "CSI cleanup failed: %s", esp_err_to_name(error));
        }
        s_csi_controller = nullptr;
    }
    if (s_csi_ldo_handle) {
        const esp_err_t error = esp_ldo_release_channel(s_csi_ldo_handle);
        if (error != ESP_OK) {
            LOGW("Camera", "CSI LDO cleanup failed: %s", esp_err_to_name(error));
        }
        s_csi_ldo_handle = nullptr;
    }
    camera_driver_release_raw(&s_rgb565_raw_staging);
    s_csi_context = {};
    s_camera_raw10_configured = false;
    s_exposure_lines = 0;
    return true;
}

bool camera_driver_is_detected() {
    return s_camera_detected;
}

#else

void camera_driver_init() {}

bool camera_driver_deinit() { return true; }

bool camera_driver_is_detected() {
    return false;
}

bool camera_driver_capture_raw(CameraRawFrame* frame) {
    if (frame) *frame = {};
    return false;
}

void camera_driver_release_raw(CameraRawFrame* frame) {
    if (frame) *frame = {};
}

bool camera_driver_capture_jpeg(CameraJpegFrame* frame, const CameraCaptureSettings& settings) {
    if (frame) *frame = {};
    (void)settings;
    return false;
}

bool camera_driver_capture_rgb565(CameraRgb565Frame* rgb565, CameraJpegFrame* jpeg,
                                  CameraCaptureTiming* timing,
                                  const CameraCaptureSettings& settings) {
    if (jpeg) *jpeg = {};
    if (rgb565) *rgb565 = {};
    if (timing) *timing = {};
    (void)settings;
    return false;
}

void camera_driver_release_jpeg(CameraJpegFrame* frame) {
    if (frame) *frame = {};
}

#endif
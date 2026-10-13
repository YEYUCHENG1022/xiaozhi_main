#include "lcd_display.h"
#include "assets/lang_config.h"
#include "gif/lvgl_gif.h"
#include "lvgl_theme.h"
#include "settings.h"

#include <esp_err.h>
#include <esp_log.h>
#include <esp_lvgl_port.h>
#include <esp_psram.h>
#include <font_awesome.h>
#include <src/misc/cache/lv_cache.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <utility>
#include <vector>

#include "board.h"

#define TAG "LcdDisplay"

LV_FONT_DECLARE(BUILTIN_TEXT_FONT);
LV_FONT_DECLARE(BUILTIN_ICON_FONT);
LV_FONT_DECLARE(font_awesome_30_4);

extern const uint8_t story_expression_1_png_start[] asm("_binary_story_expression_1_png_start");
extern const uint8_t story_expression_1_png_end[] asm("_binary_story_expression_1_png_end");
extern const uint8_t story_expression_2_png_start[] asm("_binary_story_expression_2_png_start");
extern const uint8_t story_expression_2_png_end[] asm("_binary_story_expression_2_png_end");
extern const uint8_t story_expression_3_png_start[] asm("_binary_story_expression_3_png_start");
extern const uint8_t story_expression_3_png_end[] asm("_binary_story_expression_3_png_end");
extern const uint8_t story_expression_5_png_start[] asm("_binary_story_expression_5_png_start");
extern const uint8_t story_expression_5_png_end[] asm("_binary_story_expression_5_png_end");
extern const uint8_t story_expression_calm_1_png_start[] asm(
    "_binary_story_expression_calm_1_png_start");
extern const uint8_t story_expression_calm_1_png_end[] asm(
    "_binary_story_expression_calm_1_png_end");
extern const uint8_t story_expression_calm_2_png_start[] asm(
    "_binary_story_expression_calm_2_png_start");
extern const uint8_t story_expression_calm_2_png_end[] asm(
    "_binary_story_expression_calm_2_png_end");
extern const uint8_t story_expression_calm_3_png_start[] asm(
    "_binary_story_expression_calm_3_png_start");
extern const uint8_t story_expression_calm_3_png_end[] asm(
    "_binary_story_expression_calm_3_png_end");
extern const uint8_t story_expression_calm_5_png_start[] asm(
    "_binary_story_expression_calm_5_png_start");
extern const uint8_t story_expression_calm_5_png_end[] asm(
    "_binary_story_expression_calm_5_png_end");
extern const uint8_t story_expression_negative_1_png_start[] asm(
    "_binary_story_expression_negative_1_png_start");
extern const uint8_t story_expression_negative_1_png_end[] asm(
    "_binary_story_expression_negative_1_png_end");
extern const uint8_t story_expression_negative_2_png_start[] asm(
    "_binary_story_expression_negative_2_png_start");
extern const uint8_t story_expression_negative_2_png_end[] asm(
    "_binary_story_expression_negative_2_png_end");
extern const uint8_t story_expression_negative_3_png_start[] asm(
    "_binary_story_expression_negative_3_png_start");
extern const uint8_t story_expression_negative_3_png_end[] asm(
    "_binary_story_expression_negative_3_png_end");
extern const uint8_t story_expression_negative_4_png_start[] asm(
    "_binary_story_expression_negative_4_png_start");
extern const uint8_t story_expression_negative_4_png_end[] asm(
    "_binary_story_expression_negative_4_png_end");
extern const uint8_t story_expression_negative_5_png_start[] asm(
    "_binary_story_expression_negative_5_png_start");
extern const uint8_t story_expression_negative_5_png_end[] asm(
    "_binary_story_expression_negative_5_png_end");

namespace {

constexpr size_t kStoryExpressionFrameCount = 6;
constexpr uint32_t kStoryExpressionFrameDurationMs = 500;
constexpr uint32_t kStoryExpressionDurationMs =
    kStoryExpressionFrameCount * kStoryExpressionFrameDurationMs;

lv_image_dsc_t story_expression_frames[kStoryExpressionFrameCount] = {};
const void* story_expression_sources[kStoryExpressionFrameCount] = {};

constexpr size_t kCalmExpressionFrameCount = 6;
constexpr uint32_t kCalmExpressionFrameDurationMs = 500;
constexpr uint32_t kCalmExpressionDurationMs =
    kCalmExpressionFrameCount * kCalmExpressionFrameDurationMs;

lv_image_dsc_t calm_expression_frames[kCalmExpressionFrameCount] = {};
const void* calm_expression_sources[kCalmExpressionFrameCount] = {};

constexpr size_t kNegativeExpressionFrameCount = 6;
constexpr uint32_t kNegativeExpressionFrameDurationMs = 500;
constexpr uint32_t kNegativeExpressionDurationMs =
    kNegativeExpressionFrameCount * kNegativeExpressionFrameDurationMs;

lv_image_dsc_t negative_expression_frames[kNegativeExpressionFrameCount] = {};
const void* negative_expression_sources[kNegativeExpressionFrameCount] = {};

enum class ConversationExpressionKind {
    Positive = 0,
    Calm,
    Negative,
};

// 将后端情绪标签归并为板端已有的积极、平静和消极三套动画。
ConversationExpressionKind ResolveConversationExpression(const char* emotion) {
    if (emotion == nullptr) {
        return ConversationExpressionKind::Positive;
    }

    const std::string original(emotion);
    if (original == "😔" || original == "😰") {
        return ConversationExpressionKind::Calm;
    }
    if (original == "😡" || original == "🤢") {
        return ConversationExpressionKind::Negative;
    }

    std::string normalized = original;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
    if (normalized == "SAD" || normalized == "FEARFUL" || normalized == "FEAR") {
        return ConversationExpressionKind::Calm;
    }
    if (normalized == "ANGRY" || normalized == "DISGUSTED" || normalized == "DISGUST") {
        return ConversationExpressionKind::Negative;
    }
    return ConversationExpressionKind::Positive;
}

struct ExpressionAnimationState {
    const void* const* sources = nullptr;
    size_t frame_count = 0;
    size_t current_index = 0;
    bool initialized = false;
};

lv_obj_t* CreateExpressionAnimation(lv_obj_t* parent, int width, int height, lv_align_t align,
                                    int x_offset, int y_offset, const void* const* sources,
                                    size_t frame_count, uint32_t duration_ms) {
    auto* image = lv_image_create(parent);
    lv_obj_set_size(image, width, height);
    lv_image_set_inner_align(image, LV_IMAGE_ALIGN_CONTAIN);
    lv_obj_remove_flag(image, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(image, align, x_offset, y_offset);

    auto* state = new ExpressionAnimationState;
    state->sources = sources;
    state->frame_count = frame_count;
    lv_image_set_src(image, sources[0]);
    lv_obj_set_user_data(image, state);
    lv_obj_add_event_cb(
        image,
        [](lv_event_t* event) {
            auto* deleted_state =
                static_cast<ExpressionAnimationState*>(lv_event_get_user_data(event));
            delete deleted_state;
        },
        LV_EVENT_DELETE, state);

    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, image);
    lv_anim_set_exec_cb(&animation, [](void* target, int32_t animation_value) {
        auto* image = static_cast<lv_obj_t*>(target);
        auto* state = static_cast<ExpressionAnimationState*>(lv_obj_get_user_data(image));
        if (state == nullptr || state->frame_count == 0) {
            return;
        }

        const size_t frame_index = static_cast<size_t>(animation_value) % state->frame_count;
        if (!state->initialized || frame_index != state->current_index) {
            state->current_index = frame_index;
            state->initialized = true;
            lv_image_set_src(image, state->sources[frame_index]);
        }
    });
    lv_anim_set_values(&animation, 0, static_cast<int32_t>(frame_count));
    lv_anim_set_duration(&animation, duration_ms);
    lv_anim_set_repeat_count(&animation, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&animation);
    return image;
}

void InitializeStoryExpressionFrames() {
    static bool initialized = false;
    if (initialized) {
        return;
    }

    const uint8_t* starts[kStoryExpressionFrameCount] = {
        story_expression_1_png_start, story_expression_2_png_start, story_expression_3_png_start,
        story_expression_2_png_start, story_expression_5_png_start, story_expression_2_png_start,
    };
    const uint8_t* ends[kStoryExpressionFrameCount] = {
        story_expression_1_png_end, story_expression_2_png_end, story_expression_3_png_end,
        story_expression_2_png_end, story_expression_5_png_end, story_expression_2_png_end,
    };

    for (size_t i = 0; i < kStoryExpressionFrameCount; ++i) {
        auto& frame = story_expression_frames[i];
        frame.header.magic = LV_IMAGE_HEADER_MAGIC;
        frame.header.cf = LV_COLOR_FORMAT_RAW_ALPHA;
        frame.data_size = static_cast<uint32_t>(ends[i] - starts[i]);
        frame.data = starts[i];
        story_expression_sources[i] = &frame;
    }
    initialized = true;
}

lv_obj_t* CreateStoryExpressionAnimation(lv_obj_t* parent, int width, int height, lv_align_t align,
                                         int x_offset, int y_offset) {
    InitializeStoryExpressionFrames();
    return CreateExpressionAnimation(parent, width, height, align, x_offset, y_offset,
                                     story_expression_sources, kStoryExpressionFrameCount,
                                     kStoryExpressionDurationMs);
}

// 按用户提供的图一至图六顺序初始化“平静表情”；完全相同的闭眼帧复用资源以节省 Flash。
void InitializeCalmExpressionFrames() {
    static bool initialized = false;
    if (initialized) {
        return;
    }

    const uint8_t* starts[kCalmExpressionFrameCount] = {
        story_expression_calm_1_png_start, story_expression_calm_2_png_start,
        story_expression_calm_3_png_start, story_expression_calm_2_png_start,
        story_expression_calm_5_png_start, story_expression_calm_2_png_start,
    };
    const uint8_t* ends[kCalmExpressionFrameCount] = {
        story_expression_calm_1_png_end, story_expression_calm_2_png_end,
        story_expression_calm_3_png_end, story_expression_calm_2_png_end,
        story_expression_calm_5_png_end, story_expression_calm_2_png_end,
    };

    for (size_t i = 0; i < kCalmExpressionFrameCount; ++i) {
        auto& frame = calm_expression_frames[i];
        frame.header.magic = LV_IMAGE_HEADER_MAGIC;
        frame.header.cf = LV_COLOR_FORMAT_RAW_ALPHA;
        frame.data_size = static_cast<uint32_t>(ends[i] - starts[i]);
        frame.data = starts[i];
        calm_expression_sources[i] = &frame;
    }
    initialized = true;
}

// 创建“平静表情”循环动画，每张独立帧固定显示 0.5 秒。
[[maybe_unused]] lv_obj_t* CreateCalmExpressionAnimation(lv_obj_t* parent, int width, int height,
                                                         lv_align_t align, int x_offset,
                                                         int y_offset) {
    InitializeCalmExpressionFrames();
    return CreateExpressionAnimation(parent, width, height, align, x_offset, y_offset,
                                     calm_expression_sources, kCalmExpressionFrameCount,
                                     kCalmExpressionDurationMs);
}

// 初始化“消极表情”的六帧图片；完全相同的第 2、6 帧复用资源以节省 Flash。
void InitializeNegativeExpressionFrames() {
    static bool initialized = false;
    if (initialized) {
        return;
    }

    const uint8_t* starts[kNegativeExpressionFrameCount] = {
        story_expression_negative_1_png_start, story_expression_negative_2_png_start,
        story_expression_negative_3_png_start, story_expression_negative_4_png_start,
        story_expression_negative_5_png_start, story_expression_negative_2_png_start,
    };
    const uint8_t* ends[kNegativeExpressionFrameCount] = {
        story_expression_negative_1_png_end, story_expression_negative_2_png_end,
        story_expression_negative_3_png_end, story_expression_negative_4_png_end,
        story_expression_negative_5_png_end, story_expression_negative_2_png_end,
    };

    for (size_t i = 0; i < kNegativeExpressionFrameCount; ++i) {
        auto& frame = negative_expression_frames[i];
        frame.header.magic = LV_IMAGE_HEADER_MAGIC;
        frame.header.cf = LV_COLOR_FORMAT_RAW_ALPHA;
        frame.data_size = static_cast<uint32_t>(ends[i] - starts[i]);
        frame.data = starts[i];
        negative_expression_sources[i] = &frame;
    }
    initialized = true;
}

// 创建“消极表情”循环动画，每张独立帧固定显示 0.5 秒。
[[maybe_unused]] lv_obj_t* CreateNegativeExpressionAnimation(lv_obj_t* parent, int width,
                                                             int height, lv_align_t align,
                                                             int x_offset, int y_offset) {
    InitializeNegativeExpressionFrames();
    return CreateExpressionAnimation(parent, width, height, align, x_offset, y_offset,
                                     negative_expression_sources, kNegativeExpressionFrameCount,
                                     kNegativeExpressionDurationMs);
}

}  // namespace

void LcdDisplay::InitializeLcdThemes() {
    auto text_font = std::make_shared<LvglBuiltInFont>(&BUILTIN_TEXT_FONT);
    auto icon_font = std::make_shared<LvglBuiltInFont>(&BUILTIN_ICON_FONT);
    auto large_icon_font = std::make_shared<LvglBuiltInFont>(&font_awesome_30_4);

    // light theme
    auto light_theme = new LvglTheme("light");
    light_theme->set_background_color(lv_color_hex(0xFFFFFF));
    light_theme->set_text_color(lv_color_hex(0x000000));
    light_theme->set_chat_background_color(lv_color_hex(0xE0E0E0));
    light_theme->set_user_bubble_color(lv_color_hex(0x00FF00));
    light_theme->set_assistant_bubble_color(lv_color_hex(0xDDDDDD));
    light_theme->set_system_bubble_color(lv_color_hex(0xFFFFFF));
    light_theme->set_system_text_color(lv_color_hex(0x000000));
    light_theme->set_border_color(lv_color_hex(0x000000));
    light_theme->set_low_battery_color(lv_color_hex(0x000000));
    light_theme->set_text_font(text_font);
    light_theme->set_icon_font(icon_font);
    light_theme->set_large_icon_font(large_icon_font);

    // dark theme
    auto dark_theme = new LvglTheme("dark");
    dark_theme->set_background_color(lv_color_hex(0x000000));
    dark_theme->set_text_color(lv_color_hex(0xFFFFFF));
    dark_theme->set_chat_background_color(lv_color_hex(0x1F1F1F));
    dark_theme->set_user_bubble_color(lv_color_hex(0x00FF00));
    dark_theme->set_assistant_bubble_color(lv_color_hex(0x222222));
    dark_theme->set_system_bubble_color(lv_color_hex(0x000000));
    dark_theme->set_system_text_color(lv_color_hex(0xFFFFFF));
    dark_theme->set_border_color(lv_color_hex(0xFFFFFF));
    dark_theme->set_low_battery_color(lv_color_hex(0xFF0000));
    dark_theme->set_text_font(text_font);
    dark_theme->set_icon_font(icon_font);
    dark_theme->set_large_icon_font(large_icon_font);

    auto& theme_manager = LvglThemeManager::GetInstance();
    theme_manager.RegisterTheme("light", light_theme);
    theme_manager.RegisterTheme("dark", dark_theme);
}

LcdDisplay::LcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width,
                       int height)
    : panel_io_(panel_io), panel_(panel) {
    width_ = width;
    height_ = height;

    // Initialize LCD themes
    InitializeLcdThemes();

    // Load theme from settings
    Settings settings("display", false);
    std::string theme_name = settings.GetString("theme", "light");
    current_theme_ = LvglThemeManager::GetInstance().GetTheme(theme_name);

    // Create a timer to hide the preview image
    esp_timer_create_args_t preview_timer_args = {
        .callback =
            [](void* arg) {
                LcdDisplay* display = static_cast<LcdDisplay*>(arg);
                display->SetPreviewImage(nullptr);
            },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "preview_timer",
        .skip_unhandled_events = false,
    };
    esp_timer_create(&preview_timer_args, &preview_timer_);
}

SpiLcdDisplay::SpiLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                             int width, int height, int offset_x, int offset_y, bool mirror_x,
                             bool mirror_y, bool swap_xy)
    : LcdDisplay(panel_io, panel, width, height) {
    // draw white
    std::vector<uint16_t> buffer(width_, 0xFFFF);
    for (int y = 0; y < height_; y++) {
        esp_lcd_panel_draw_bitmap(panel_, 0, y, width_, y + 1, buffer.data());
    }

    // Set the display to on
    ESP_LOGI(TAG, "Turning display on");
    {
        esp_err_t __err = esp_lcd_panel_disp_on_off(panel_, true);
        if (__err == ESP_ERR_NOT_SUPPORTED) {
            ESP_LOGW(TAG, "Panel does not support disp_on_off; assuming ON");
        } else {
            ESP_ERROR_CHECK(__err);
        }
    }

    ESP_LOGI(TAG, "Initialize LVGL library");
    lv_init();

#if CONFIG_SPIRAM
    // lv image cache, currently only PNG is supported
    size_t psram_size_mb = esp_psram_get_size() / 1024 / 1024;
    if (psram_size_mb >= 8) {
        lv_image_cache_resize(2 * 1024 * 1024, true);
        ESP_LOGI(TAG, "Use 2MB of PSRAM for image cache");
    } else if (psram_size_mb >= 2) {
        lv_image_cache_resize(512 * 1024, true);
        ESP_LOGI(TAG, "Use 512KB of PSRAM for image cache");
    }
#endif

    ESP_LOGI(TAG, "Initialize LVGL port");
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_priority = 1;
#if CONFIG_SOC_CPU_CORES_NUM > 1
    port_cfg.task_affinity = 1;
#endif
    lvgl_port_init(&port_cfg);

    ESP_LOGI(TAG, "Adding LCD display");
    const lvgl_port_display_cfg_t display_cfg = {
        .io_handle = panel_io_,
        .panel_handle = panel_,
        .control_handle = nullptr,
        .buffer_size = static_cast<uint32_t>(width_ * 20),
        .double_buffer = false,
        .trans_size = 0,
        .hres = static_cast<uint32_t>(width_),
        .vres = static_cast<uint32_t>(height_),
        .monochrome = false,
        .rotation =
            {
                .swap_xy = swap_xy,
                .mirror_x = mirror_x,
                .mirror_y = mirror_y,
            },
        .color_format = LV_COLOR_FORMAT_RGB565,
        .flags =
            {
                .buff_dma = 1,
                .buff_spiram = 0,
                .sw_rotate = 0,
                .swap_bytes = 1,
                .full_refresh = 0,
                .direct_mode = 0,
            },
    };

    display_ = lvgl_port_add_disp(&display_cfg);
    if (display_ == nullptr) {
        ESP_LOGE(TAG, "Failed to add display");
        return;
    }

    if (offset_x != 0 || offset_y != 0) {
        lv_display_set_offset(display_, offset_x, offset_y);
    }
}

// RGB LCD implementation
RgbLcdDisplay::RgbLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                             int width, int height, int offset_x, int offset_y, bool mirror_x,
                             bool mirror_y, bool swap_xy)
    : LcdDisplay(panel_io, panel, width, height) {
    // draw white
    std::vector<uint16_t> buffer(width_, 0xFFFF);
    for (int y = 0; y < height_; y++) {
        esp_lcd_panel_draw_bitmap(panel_, 0, y, width_, y + 1, buffer.data());
    }

    ESP_LOGI(TAG, "Initialize LVGL library");
    lv_init();

    ESP_LOGI(TAG, "Initialize LVGL port");
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_priority = 1;
    port_cfg.timer_period_ms = 50;
    lvgl_port_init(&port_cfg);

    ESP_LOGI(TAG, "Adding LCD display");
    const lvgl_port_display_cfg_t display_cfg = {
        .io_handle = panel_io_,
        .panel_handle = panel_,
        .buffer_size = static_cast<uint32_t>(width_ * 20),
        .double_buffer = true,
        .hres = static_cast<uint32_t>(width_),
        .vres = static_cast<uint32_t>(height_),
        .rotation =
            {
                .swap_xy = swap_xy,
                .mirror_x = mirror_x,
                .mirror_y = mirror_y,
            },
        .flags =
            {
                .buff_dma = 1,
                .swap_bytes = 0,
                .full_refresh = 1,
                .direct_mode = 1,
            },
    };

    const lvgl_port_display_rgb_cfg_t rgb_cfg = {.flags = {
                                                     .bb_mode = true,
                                                     .avoid_tearing = true,
                                                 }};

    display_ = lvgl_port_add_disp_rgb(&display_cfg, &rgb_cfg);
    if (display_ == nullptr) {
        ESP_LOGE(TAG, "Failed to add RGB display");
        return;
    }

    if (offset_x != 0 || offset_y != 0) {
        lv_display_set_offset(display_, offset_x, offset_y);
    }
}

MipiLcdDisplay::MipiLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                               int width, int height, int offset_x, int offset_y, bool mirror_x,
                               bool mirror_y, bool swap_xy)
    : LcdDisplay(panel_io, panel, width, height) {
    ESP_LOGI(TAG, "Initialize LVGL library");
    lv_init();

    ESP_LOGI(TAG, "Initialize LVGL port");
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    lvgl_port_init(&port_cfg);

    ESP_LOGI(TAG, "Adding LCD display");
    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = panel_io,
        .panel_handle = panel,
        .control_handle = nullptr,
        .buffer_size = static_cast<uint32_t>(width_ * 50),
        .double_buffer = false,
        .hres = static_cast<uint32_t>(width_),
        .vres = static_cast<uint32_t>(height_),
        .monochrome = false,
        /* Rotation values must be same as used in esp_lcd for initial settings of the screen */
        .rotation =
            {
                .swap_xy = swap_xy,
                .mirror_x = mirror_x,
                .mirror_y = mirror_y,
            },
        .flags =
            {
                .buff_dma = true,
                .buff_spiram = false,
                .sw_rotate = true,
            },
    };

    const lvgl_port_display_dsi_cfg_t dpi_cfg = {.flags = {
                                                     .avoid_tearing = false,
                                                 }};
    display_ = lvgl_port_add_disp_dsi(&disp_cfg, &dpi_cfg);
    if (display_ == nullptr) {
        ESP_LOGE(TAG, "Failed to add display");
        return;
    }

    if (offset_x != 0 || offset_y != 0) {
        lv_display_set_offset(display_, offset_x, offset_y);
    }
}

LcdDisplay::~LcdDisplay() {
    SetPreviewImage(nullptr);
    HideStoryPlaybackAnimation();

    // Clean up GIF controller
    if (gif_controller_) {
        gif_controller_->Stop();
        gif_controller_.reset();
    }

    if (preview_timer_ != nullptr) {
        esp_timer_stop(preview_timer_);
        esp_timer_delete(preview_timer_);
    }

    if (preview_image_ != nullptr) {
        lv_obj_del(preview_image_);
    }
    if (chat_message_label_ != nullptr) {
        lv_obj_del(chat_message_label_);
    }
    if (emoji_label_ != nullptr) {
        lv_obj_del(emoji_label_);
    }
    if (emoji_image_ != nullptr) {
        lv_obj_del(emoji_image_);
    }
    if (emoji_box_ != nullptr) {
        lv_obj_del(emoji_box_);
    }
    if (content_ != nullptr) {
        lv_obj_del(content_);
    }
    if (bottom_bar_ != nullptr) {
        lv_obj_del(bottom_bar_);
    }
    if (status_bar_ != nullptr) {
        lv_obj_del(status_bar_);
    }
    if (top_bar_ != nullptr) {
        lv_obj_del(top_bar_);
    }
    if (side_bar_ != nullptr) {
        lv_obj_del(side_bar_);
    }
    if (container_ != nullptr) {
        lv_obj_del(container_);
    }
    if (display_ != nullptr) {
        lv_display_delete(display_);
    }

    if (panel_ != nullptr) {
        esp_lcd_panel_del(panel_);
    }
    if (panel_io_ != nullptr) {
        esp_lcd_panel_io_del(panel_io_);
    }
}

bool LcdDisplay::Lock(int timeout_ms) { return lvgl_port_lock(timeout_ms); }

void LcdDisplay::Unlock() { lvgl_port_unlock(); }

#if CONFIG_USE_WECHAT_MESSAGE_STYLE
void LcdDisplay::SetupUI() {
    // Prevent duplicate calls - if already called, return early
    if (setup_ui_called_) {
        ESP_LOGW(TAG, "SetupUI() called multiple times, skipping duplicate call");
        return;
    }

    Display::SetupUI();  // Mark SetupUI as called
    DisplayLockGuard lock(this);

    auto lvgl_theme = static_cast<LvglTheme*>(current_theme_);
    auto text_font = lvgl_theme->text_font()->font();
    auto icon_font = lvgl_theme->icon_font()->font();
    auto large_icon_font = lvgl_theme->large_icon_font()->font();

    auto screen = lv_screen_active();
    lv_obj_set_style_text_font(screen, text_font, 0);
    lv_obj_set_style_text_color(screen, lvgl_theme->text_color(), 0);
    lv_obj_set_style_bg_color(screen, lvgl_theme->background_color(), 0);

    /* Container */
    container_ = lv_obj_create(screen);
    lv_obj_set_size(container_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_radius(container_, 0, 0);
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(container_, 0, 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_set_style_pad_row(container_, 0, 0);
    lv_obj_set_style_bg_color(container_, lvgl_theme->background_color(), 0);
    lv_obj_set_style_border_color(container_, lvgl_theme->border_color(), 0);

    /* Layer 1: Top bar - for status icons */
    top_bar_ = lv_obj_create(container_);
    lv_obj_set_size(top_bar_, LV_HOR_RES, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(top_bar_, 0, 0);
    lv_obj_set_style_bg_opa(top_bar_, LV_OPA_50, 0);  // 50% opacity background
    lv_obj_set_style_bg_color(top_bar_, lvgl_theme->background_color(), 0);
    lv_obj_set_style_border_width(top_bar_, 0, 0);
    lv_obj_set_style_pad_all(top_bar_, 0, 0);
    lv_obj_set_style_pad_top(top_bar_, lvgl_theme->spacing(2), 0);
    lv_obj_set_style_pad_bottom(top_bar_, lvgl_theme->spacing(2), 0);
    lv_obj_set_style_pad_left(top_bar_, lvgl_theme->spacing(4), 0);
    lv_obj_set_style_pad_right(top_bar_, lvgl_theme->spacing(4), 0);
    lv_obj_set_flex_flow(top_bar_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top_bar_, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(top_bar_, LV_SCROLLBAR_MODE_OFF);

    // Left icon
    network_label_ = lv_label_create(top_bar_);
    lv_label_set_text(network_label_, "");
    lv_obj_set_style_text_font(network_label_, icon_font, 0);
    lv_obj_set_style_text_color(network_label_, lvgl_theme->text_color(), 0);

    // Right icons container
    lv_obj_t* right_icons = lv_obj_create(top_bar_);
    lv_obj_set_size(right_icons, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(right_icons, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(right_icons, 0, 0);
    lv_obj_set_style_pad_all(right_icons, 0, 0);
    lv_obj_set_flex_flow(right_icons, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(right_icons, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    mute_label_ = lv_label_create(right_icons);
    lv_label_set_text(mute_label_, "");
    lv_obj_set_style_text_font(mute_label_, icon_font, 0);
    lv_obj_set_style_text_color(mute_label_, lvgl_theme->text_color(), 0);

    battery_label_ = lv_label_create(right_icons);
    lv_label_set_text(battery_label_, "");
    lv_obj_set_style_text_font(battery_label_, icon_font, 0);
    lv_obj_set_style_text_color(battery_label_, lvgl_theme->text_color(), 0);
    lv_obj_set_style_margin_left(battery_label_, lvgl_theme->spacing(2), 0);

    /* Layer 2: Status bar - for center text labels */
    status_bar_ = lv_obj_create(screen);
    lv_obj_set_size(status_bar_, LV_HOR_RES, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(status_bar_, 0, 0);
    lv_obj_set_style_bg_opa(status_bar_, LV_OPA_TRANSP, 0);  // Transparent background
    lv_obj_set_style_border_width(status_bar_, 0, 0);
    lv_obj_set_style_pad_all(status_bar_, 0, 0);
    lv_obj_set_style_pad_top(status_bar_, lvgl_theme->spacing(2), 0);
    lv_obj_set_style_pad_bottom(status_bar_, lvgl_theme->spacing(2), 0);
    lv_obj_set_scrollbar_mode(status_bar_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_layout(status_bar_, LV_LAYOUT_NONE, 0);  // Use absolute positioning
    lv_obj_align(status_bar_, LV_ALIGN_TOP_MID, 0, 0);        // Overlap with top_bar_

    notification_label_ = lv_label_create(status_bar_);
    lv_obj_set_width(notification_label_, LV_HOR_RES * 0.8);
    lv_obj_set_style_text_align(notification_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(notification_label_, lvgl_theme->text_color(), 0);
    lv_label_set_text(notification_label_, "");
    lv_obj_align(notification_label_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);

    status_label_ = lv_label_create(status_bar_);
    lv_obj_set_width(status_label_, LV_HOR_RES * 0.8);
    lv_label_set_long_mode(status_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(status_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(status_label_, lvgl_theme->text_color(), 0);
    lv_label_set_text(status_label_, Lang::Strings::INITIALIZING);
    lv_obj_align(status_label_, LV_ALIGN_CENTER, 0, 0);

    /* Content - Chat area */
    content_ = lv_obj_create(container_);
    lv_obj_set_style_radius(content_, 0, 0);
    lv_obj_set_width(content_, LV_HOR_RES);
    lv_obj_set_flex_grow(content_, 1);
    lv_obj_set_style_pad_all(content_, lvgl_theme->spacing(4), 0);
    lv_obj_set_style_border_width(content_, 0, 0);
    lv_obj_set_style_bg_color(content_, lvgl_theme->chat_background_color(),
                              0);  // Background for chat area

    // Enable scrolling for chat content
    lv_obj_set_scrollbar_mode(content_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(content_, LV_DIR_VER);

    // Create a flex container for chat messages
    lv_obj_set_flex_flow(content_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(content_, lvgl_theme->spacing(4), 0);  // Space between messages

    // We'll create chat messages dynamically in SetChatMessage
    chat_message_label_ = nullptr;

    low_battery_popup_ = lv_obj_create(screen);
    lv_obj_set_scrollbar_mode(low_battery_popup_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(low_battery_popup_, LV_HOR_RES * 0.9, text_font->line_height * 2);
    lv_obj_align(low_battery_popup_, LV_ALIGN_BOTTOM_MID, 0, -lvgl_theme->spacing(4));
    lv_obj_set_style_bg_color(low_battery_popup_, lvgl_theme->low_battery_color(), 0);
    lv_obj_set_style_radius(low_battery_popup_, lvgl_theme->spacing(4), 0);
    low_battery_label_ = lv_label_create(low_battery_popup_);
    lv_label_set_text(low_battery_label_, Lang::Strings::BATTERY_NEED_CHARGE);
    lv_obj_set_style_text_color(low_battery_label_, lv_color_white(), 0);
    lv_obj_center(low_battery_label_);
    lv_obj_add_flag(low_battery_popup_, LV_OBJ_FLAG_HIDDEN);

    emoji_image_ = lv_img_create(screen);
    lv_obj_align(emoji_image_, LV_ALIGN_TOP_MID, 0,
                 text_font->line_height + lvgl_theme->spacing(8));

    // Display AI logo while booting
    emoji_label_ = lv_label_create(screen);
    lv_obj_center(emoji_label_);
    lv_obj_set_style_text_font(emoji_label_, large_icon_font, 0);
    lv_obj_set_style_text_color(emoji_label_, lvgl_theme->text_color(), 0);
    lv_label_set_text(emoji_label_, FONT_AWESOME_MICROCHIP_AI);
}
#if CONFIG_IDF_TARGET_ESP32P4
#define MAX_MESSAGES 40
#else
#define MAX_MESSAGES 20
#endif
void LcdDisplay::SetChatMessage(const char* role, const char* content) {
    if (!setup_ui_called_) {
        ESP_LOGW(TAG, "SetChatMessage('%s', '%s') called before SetupUI() - message will be lost!",
                 role, content);
    }
    DisplayLockGuard lock(this);
    if (content_ == nullptr) {
        if (setup_ui_called_) {
            ESP_LOGW(TAG,
                     "SetChatMessage('%s', '%s') failed: content_ is nullptr (SetupUI() was called "
                     "but container not created)",
                     role, content);
        }
        return;
    }

    // Check if message count exceeds limit
    uint32_t child_count = lv_obj_get_child_cnt(content_);
    if (child_count >= MAX_MESSAGES) {
        // Delete the oldest message (first child object)
        lv_obj_t* first_child = lv_obj_get_child(content_, 0);
        if (first_child != nullptr) {
            lv_obj_del(first_child);
            // Refresh child count after deletion
            child_count = lv_obj_get_child_cnt(content_);
        }
        // Scroll to the last message immediately (get last_child after deletion)
        if (child_count > 0) {
            lv_obj_t* last_child = lv_obj_get_child(content_, child_count - 1);
            if (last_child != nullptr && lv_obj_is_valid(last_child)) {
                lv_obj_scroll_to_view_recursive(last_child, LV_ANIM_OFF);
            }
        }
    }

    // Collapse system messages (if it's a system message, check if the last message is also a
    // system message)
    if (strcmp(role, "system") == 0) {
        // Refresh child count to get accurate count after potential deletion above
        child_count = lv_obj_get_child_cnt(content_);
        if (child_count > 0) {
            // Get the last message container
            lv_obj_t* last_container = lv_obj_get_child(content_, child_count - 1);
            if (last_container != nullptr && lv_obj_is_valid(last_container) &&
                lv_obj_get_child_cnt(last_container) > 0) {
                // Get the bubble inside the container
                lv_obj_t* last_bubble = lv_obj_get_child(last_container, 0);
                if (last_bubble != nullptr && lv_obj_is_valid(last_bubble)) {
                    // Check if bubble type is system message
                    void* bubble_type_ptr = lv_obj_get_user_data(last_bubble);
                    if (bubble_type_ptr != nullptr &&
                        strcmp((const char*)bubble_type_ptr, "system") == 0) {
                        // If the last message is also a system message, delete it
                        lv_obj_del(last_container);
                    }
                }
            }
        }
    } else {
        // Hide the centered AI logo
        lv_obj_add_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);
    }

    // Avoid empty message boxes
    if (strlen(content) == 0) {
        return;
    }

    // Create a message bubble
    lv_obj_t* msg_bubble = lv_obj_create(content_);
    lv_obj_set_style_radius(msg_bubble, 8, 0);
    lv_obj_set_scrollbar_mode(msg_bubble, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_border_width(msg_bubble, 0, 0);
    lv_obj_set_style_pad_all(msg_bubble, lvgl_theme->spacing(4), 0);

    // Create the message text
    lv_obj_t* msg_text = lv_label_create(msg_bubble);
    lv_label_set_text(msg_text, content);

    // Calculate bubble width constraints
    lv_coord_t max_width = LV_HOR_RES * 85 / 100 - 16;  // 85% of screen width
    lv_coord_t min_width = 20;

    // Let LVGL calculate the natural text width first
    lv_obj_set_width(msg_text, LV_SIZE_CONTENT);
    lv_obj_update_layout(msg_text);
    lv_coord_t text_width = lv_obj_get_width(msg_text);

    // Ensure text width is not less than minimum width
    if (text_width < min_width) {
        text_width = min_width;
    }

    // Constrain to max width
    lv_coord_t bubble_width = (text_width < max_width) ? text_width : max_width;

    // Set message text width
    lv_obj_set_width(msg_text, bubble_width);
    lv_label_set_long_mode(msg_text, LV_LABEL_LONG_WRAP);

    // Set bubble width
    lv_obj_set_width(msg_bubble, bubble_width);
    lv_obj_set_height(msg_bubble, LV_SIZE_CONTENT);

    // Set alignment and style based on message role
    if (strcmp(role, "user") == 0) {
        // User messages are right-aligned with green background
        lv_obj_set_style_bg_color(msg_bubble, lvgl_theme->user_bubble_color(), 0);
        lv_obj_set_style_bg_opa(msg_bubble, LV_OPA_70, 0);
        // Set text color for contrast
        lv_obj_set_style_text_color(msg_text, lvgl_theme->text_color(), 0);

        // Set custom attribute to mark bubble type
        lv_obj_set_user_data(msg_bubble, (void*)"user");

        // Set appropriate width for content
        lv_obj_set_width(msg_bubble, LV_SIZE_CONTENT);
        lv_obj_set_height(msg_bubble, LV_SIZE_CONTENT);

        // Don't grow
        lv_obj_set_style_flex_grow(msg_bubble, 0, 0);
    } else if (strcmp(role, "assistant") == 0) {
        // Assistant messages are left-aligned with white background
        lv_obj_set_style_bg_color(msg_bubble, lvgl_theme->assistant_bubble_color(), 0);
        lv_obj_set_style_bg_opa(msg_bubble, LV_OPA_70, 0);
        // Set text color for contrast
        lv_obj_set_style_text_color(msg_text, lvgl_theme->text_color(), 0);

        // Set custom attribute to mark bubble type
        lv_obj_set_user_data(msg_bubble, (void*)"assistant");

        // Set appropriate width for content
        lv_obj_set_width(msg_bubble, LV_SIZE_CONTENT);
        lv_obj_set_height(msg_bubble, LV_SIZE_CONTENT);

        // Don't grow
        lv_obj_set_style_flex_grow(msg_bubble, 0, 0);
    } else if (strcmp(role, "system") == 0) {
        // System messages are center-aligned with light gray background
        lv_obj_set_style_bg_color(msg_bubble, lvgl_theme->system_bubble_color(), 0);
        lv_obj_set_style_bg_opa(msg_bubble, LV_OPA_70, 0);
        // Set text color for contrast
        lv_obj_set_style_text_color(msg_text, lvgl_theme->system_text_color(), 0);

        // Set custom attribute to mark bubble type
        lv_obj_set_user_data(msg_bubble, (void*)"system");

        // Set appropriate width for content
        lv_obj_set_width(msg_bubble, LV_SIZE_CONTENT);
        lv_obj_set_height(msg_bubble, LV_SIZE_CONTENT);

        // Don't grow
        lv_obj_set_style_flex_grow(msg_bubble, 0, 0);
    }

    // Create a full-width container for user messages to ensure right alignment
    if (strcmp(role, "user") == 0) {
        // Create a full-width container
        lv_obj_t* container = lv_obj_create(content_);
        lv_obj_set_width(container, LV_HOR_RES);
        lv_obj_set_height(container, LV_SIZE_CONTENT);

        // Make container transparent and borderless
        lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(container, 0, 0);
        lv_obj_set_style_pad_all(container, 0, 0);

        // Move the message bubble into this container
        lv_obj_set_parent(msg_bubble, container);

        // Right align the bubble in the container
        lv_obj_align(msg_bubble, LV_ALIGN_RIGHT_MID, -25, 0);

        // Auto-scroll to this container
        lv_obj_scroll_to_view_recursive(container, LV_ANIM_ON);
    } else if (strcmp(role, "system") == 0) {
        // Create full-width container for system messages to ensure center alignment
        lv_obj_t* container = lv_obj_create(content_);
        lv_obj_set_width(container, LV_HOR_RES);
        lv_obj_set_height(container, LV_SIZE_CONTENT);

        lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(container, 0, 0);
        lv_obj_set_style_pad_all(container, 0, 0);

        lv_obj_set_parent(msg_bubble, container);
        lv_obj_align(msg_bubble, LV_ALIGN_CENTER, 0, 0);
        lv_obj_scroll_to_view_recursive(container, LV_ANIM_ON);
    } else {
        // For assistant messages
        // Left align assistant messages
        lv_obj_align(msg_bubble, LV_ALIGN_LEFT_MID, 0, 0);

        // Auto-scroll to the message bubble
        lv_obj_scroll_to_view_recursive(msg_bubble, LV_ANIM_ON);
    }

    // Store reference to the latest message label
    chat_message_label_ = msg_text;
}

void LcdDisplay::SetPreviewImage(std::unique_ptr<LvglImage> image) {
    DisplayLockGuard lock(this);
    if (content_ == nullptr) {
        return;
    }

    if (image == nullptr) {
        return;
    }

    auto lvgl_theme = static_cast<LvglTheme*>(current_theme_);
    // Create a message bubble for image preview
    lv_obj_t* img_bubble = lv_obj_create(content_);
    lv_obj_set_style_radius(img_bubble, 8, 0);
    lv_obj_set_scrollbar_mode(img_bubble, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_border_width(img_bubble, 0, 0);
    lv_obj_set_style_pad_all(img_bubble, lvgl_theme->spacing(4), 0);

    // Set image bubble background color (similar to system message)
    lv_obj_set_style_bg_color(img_bubble, lvgl_theme->assistant_bubble_color(), 0);
    lv_obj_set_style_bg_opa(img_bubble, LV_OPA_70, 0);

    // Set custom attribute to mark bubble type
    lv_obj_set_user_data(img_bubble, (void*)"image");

    // Create the image object inside the bubble
    lv_obj_t* preview_image = lv_image_create(img_bubble);

    // Calculate appropriate size for the image
    lv_coord_t max_width = LV_HOR_RES * 70 / 100;   // 70% of screen width
    lv_coord_t max_height = LV_VER_RES * 50 / 100;  // 50% of screen height

    // Calculate zoom factor to fit within maximum dimensions
    auto img_dsc = image->image_dsc();
    lv_coord_t img_width = img_dsc->header.w;
    lv_coord_t img_height = img_dsc->header.h;
    if (img_width == 0 || img_height == 0) {
        img_width = max_width;
        img_height = max_height;
        ESP_LOGW(TAG, "Invalid image dimensions: %ld x %ld, using default dimensions: %ld x %ld",
                 img_width, img_height, max_width, max_height);
    }

    lv_coord_t zoom_w = (max_width * 256) / img_width;
    lv_coord_t zoom_h = (max_height * 256) / img_height;
    lv_coord_t zoom = (zoom_w < zoom_h) ? zoom_w : zoom_h;

    // Ensure zoom doesn't exceed 256 (100%)
    if (zoom > 256)
        zoom = 256;

    // Set image properties
    lv_image_set_src(preview_image, img_dsc);
    lv_image_set_scale(preview_image, zoom);

    // Add event handler to clean up LvglImage when image is deleted
    // We need to transfer ownership of the unique_ptr to the event callback
    LvglImage* raw_image = image.release();  // Release ownership of smart pointer
    lv_obj_add_event_cb(
        preview_image,
        [](lv_event_t* e) {
            LvglImage* img = (LvglImage*)lv_event_get_user_data(e);
            if (img != nullptr) {
                delete img;  // Properly release memory by deleting LvglImage object
            }
        },
        LV_EVENT_DELETE, (void*)raw_image);

    // Calculate actual scaled image dimensions
    lv_coord_t scaled_width = (img_width * zoom) / 256;
    lv_coord_t scaled_height = (img_height * zoom) / 256;

    // Set bubble size to be 16 pixels larger than the image (8 pixels on each side)
    lv_obj_set_width(img_bubble, scaled_width + 16);
    lv_obj_set_height(img_bubble, scaled_height + 16);

    // Don't grow in flex layout
    lv_obj_set_style_flex_grow(img_bubble, 0, 0);

    // Center the image within the bubble
    lv_obj_center(preview_image);

    // Left align the image bubble like assistant messages
    lv_obj_align(img_bubble, LV_ALIGN_LEFT_MID, 0, 0);

    // Auto-scroll to the image bubble
    lv_obj_scroll_to_view_recursive(img_bubble, LV_ANIM_ON);
}

void LcdDisplay::ClearChatMessages() {
    DisplayLockGuard lock(this);
    if (content_ == nullptr) {
        return;
    }

    // Use lv_obj_clean to delete all children of content_ (chat message bubbles)
    lv_obj_clean(content_);

    // Reset chat_message_label_ as it has been deleted
    chat_message_label_ = nullptr;

    // Show the centered AI logo (emoji_label_) again
    if (emoji_label_ != nullptr) {
        lv_obj_remove_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);
    }

    ESP_LOGI(TAG, "Chat messages cleared");
}
#else
void LcdDisplay::SetupUI() {
    // Prevent duplicate calls - if already called, return early
    if (setup_ui_called_) {
        ESP_LOGW(TAG, "SetupUI() called multiple times, skipping duplicate call");
        return;
    }

    Display::SetupUI();  // Mark SetupUI as called
    DisplayLockGuard lock(this);
    LvglTheme* lvgl_theme = static_cast<LvglTheme*>(current_theme_);
    auto text_font = lvgl_theme->text_font()->font();
    auto icon_font = lvgl_theme->icon_font()->font();
    auto large_icon_font = lvgl_theme->large_icon_font()->font();

    auto screen = lv_screen_active();
    lv_obj_set_style_text_font(screen, text_font, 0);
    lv_obj_set_style_text_color(screen, lvgl_theme->text_color(), 0);
    lv_obj_set_style_bg_color(screen, lvgl_theme->background_color(), 0);

    /* Container - used as background */
    container_ = lv_obj_create(screen);
    lv_obj_set_size(container_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_radius(container_, 0, 0);
    lv_obj_set_style_pad_all(container_, 0, 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_set_style_bg_color(container_, lvgl_theme->background_color(), 0);
    lv_obj_set_style_border_color(container_, lvgl_theme->border_color(), 0);

    /* Bottom layer: emoji_box_ - centered display */
    emoji_box_ = lv_obj_create(screen);
    lv_obj_set_size(emoji_box_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(emoji_box_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(emoji_box_, 0, 0);
    lv_obj_set_style_border_width(emoji_box_, 0, 0);
    lv_obj_align(emoji_box_, LV_ALIGN_CENTER, 0, 0);

    emoji_label_ = lv_label_create(emoji_box_);
    lv_obj_set_style_text_font(emoji_label_, large_icon_font, 0);
    lv_obj_set_style_text_color(emoji_label_, lvgl_theme->text_color(), 0);
    lv_label_set_text(emoji_label_, FONT_AWESOME_MICROCHIP_AI);

    emoji_image_ = lv_img_create(emoji_box_);
    lv_obj_center(emoji_image_);
    lv_obj_add_flag(emoji_image_, LV_OBJ_FLAG_HIDDEN);

    /* Middle layer: preview_image_ - centered display */
    preview_image_ = lv_image_create(screen);
    lv_obj_set_size(preview_image_, width_ / 2, height_ / 2);
    lv_obj_align(preview_image_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);

    /* Layer 1: Top bar - for status icons */
    top_bar_ = lv_obj_create(screen);
    lv_obj_set_size(top_bar_, LV_HOR_RES, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(top_bar_, 0, 0);
    lv_obj_set_style_bg_opa(top_bar_, LV_OPA_50, 0);  // 50% opacity background
    lv_obj_set_style_bg_color(top_bar_, lvgl_theme->background_color(), 0);
    lv_obj_set_style_border_width(top_bar_, 0, 0);
    lv_obj_set_style_pad_all(top_bar_, 0, 0);
    lv_obj_set_style_pad_top(top_bar_, lvgl_theme->spacing(2), 0);
    lv_obj_set_style_pad_bottom(top_bar_, lvgl_theme->spacing(2), 0);
    lv_obj_set_style_pad_left(top_bar_, lvgl_theme->spacing(4), 0);
    lv_obj_set_style_pad_right(top_bar_, lvgl_theme->spacing(4), 0);
    lv_obj_set_flex_flow(top_bar_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top_bar_, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(top_bar_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_align(top_bar_, LV_ALIGN_TOP_MID, 0, 0);

    // Left icon
    network_label_ = lv_label_create(top_bar_);
    lv_label_set_text(network_label_, "");
    lv_obj_set_style_text_font(network_label_, icon_font, 0);
    lv_obj_set_style_text_color(network_label_, lvgl_theme->text_color(), 0);

    // Right icons container
    lv_obj_t* right_icons = lv_obj_create(top_bar_);
    lv_obj_set_size(right_icons, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(right_icons, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(right_icons, 0, 0);
    lv_obj_set_style_pad_all(right_icons, 0, 0);
    lv_obj_set_flex_flow(right_icons, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(right_icons, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    mute_label_ = lv_label_create(right_icons);
    lv_label_set_text(mute_label_, "");
    lv_obj_set_style_text_font(mute_label_, icon_font, 0);
    lv_obj_set_style_text_color(mute_label_, lvgl_theme->text_color(), 0);

    battery_label_ = lv_label_create(right_icons);
    lv_label_set_text(battery_label_, "");
    lv_obj_set_style_text_font(battery_label_, icon_font, 0);
    lv_obj_set_style_text_color(battery_label_, lvgl_theme->text_color(), 0);
    lv_obj_set_style_margin_left(battery_label_, lvgl_theme->spacing(2), 0);

    /* Layer 2: Status bar - for center text labels */
    status_bar_ = lv_obj_create(screen);
    lv_obj_set_size(status_bar_, LV_HOR_RES, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(status_bar_, 0, 0);
    lv_obj_set_style_bg_opa(status_bar_, LV_OPA_TRANSP, 0);  // Transparent background
    lv_obj_set_style_border_width(status_bar_, 0, 0);
    lv_obj_set_style_pad_all(status_bar_, 0, 0);
    lv_obj_set_style_pad_top(status_bar_, lvgl_theme->spacing(2), 0);
    lv_obj_set_style_pad_bottom(status_bar_, lvgl_theme->spacing(2), 0);
    lv_obj_set_scrollbar_mode(status_bar_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_layout(status_bar_, LV_LAYOUT_NONE, 0);  // Use absolute positioning
    lv_obj_align(status_bar_, LV_ALIGN_TOP_MID, 0, 0);        // Overlap with top_bar_

    notification_label_ = lv_label_create(status_bar_);
    lv_obj_set_width(notification_label_, LV_HOR_RES * 0.75);
    lv_obj_set_style_text_align(notification_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(notification_label_, lvgl_theme->text_color(), 0);
    lv_label_set_text(notification_label_, "");
    lv_obj_align(notification_label_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);

    status_label_ = lv_label_create(status_bar_);
    lv_obj_set_width(status_label_, LV_HOR_RES * 0.75);
    lv_label_set_long_mode(status_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(status_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(status_label_, lvgl_theme->text_color(), 0);
    lv_label_set_text(status_label_, Lang::Strings::INITIALIZING);
    lv_obj_align(status_label_, LV_ALIGN_CENTER, 0, 0);

#if CONFIG_USE_MULTILINE_CHAT_MESSAGE
    /* Bottom bar - auto height, grows upward with wrapped text */
    bottom_bar_ = lv_obj_create(screen);
    lv_obj_set_width(bottom_bar_, LV_HOR_RES);
    lv_obj_set_height(bottom_bar_, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(bottom_bar_, 0, 0);
    lv_obj_set_style_bg_color(bottom_bar_, lvgl_theme->background_color(), 0);
    lv_obj_set_style_bg_opa(bottom_bar_, LV_OPA_50, 0);
    lv_obj_set_style_text_color(bottom_bar_, lvgl_theme->text_color(), 0);
    lv_obj_set_style_pad_all(bottom_bar_, lvgl_theme->spacing(4), 0);
    lv_obj_set_style_border_width(bottom_bar_, 0, 0);
    lv_obj_set_scrollbar_mode(bottom_bar_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_align(bottom_bar_, LV_ALIGN_BOTTOM_MID, 0, 0);

    /* chat_message_label_ placed in bottom_bar_, multiline wrapped display */
    chat_message_label_ = lv_label_create(bottom_bar_);
    lv_label_set_text(chat_message_label_, "");
    lv_obj_set_width(chat_message_label_, LV_HOR_RES - lvgl_theme->spacing(8));
    lv_label_set_long_mode(chat_message_label_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(chat_message_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(chat_message_label_, lvgl_theme->text_color(), 0);
    lv_obj_align(chat_message_label_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(bottom_bar_, LV_OBJ_FLAG_HIDDEN);  // Hide until there is content
#else
    /* Top layer: Bottom bar - fixed height at bottom */
    bottom_bar_ = lv_obj_create(screen);
    lv_obj_set_size(bottom_bar_, LV_HOR_RES, text_font->line_height + lvgl_theme->spacing(8));
    lv_obj_set_style_radius(bottom_bar_, 0, 0);
    lv_obj_set_style_bg_color(bottom_bar_, lvgl_theme->background_color(), 0);
    lv_obj_set_style_text_color(bottom_bar_, lvgl_theme->text_color(), 0);
    lv_obj_set_style_pad_all(bottom_bar_, 0, 0);
    lv_obj_set_style_pad_left(bottom_bar_, lvgl_theme->spacing(4), 0);
    lv_obj_set_style_pad_right(bottom_bar_, lvgl_theme->spacing(4), 0);
    lv_obj_set_style_border_width(bottom_bar_, 0, 0);
    lv_obj_set_scrollbar_mode(bottom_bar_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_align(bottom_bar_, LV_ALIGN_BOTTOM_MID, 0, 0);

    /* chat_message_label_ placed in bottom_bar_, single-line horizontal scroll */
    chat_message_label_ = lv_label_create(bottom_bar_);
    lv_label_set_text(chat_message_label_, "");
    lv_obj_set_width(chat_message_label_, LV_HOR_RES - lvgl_theme->spacing(8));
    lv_label_set_long_mode(chat_message_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(chat_message_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(chat_message_label_, lvgl_theme->text_color(), 0);
    lv_obj_align(chat_message_label_, LV_ALIGN_CENTER, 0, 0);

    // Start scrolling after a delay (short text won't scroll)
    static lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_delay(&a, 1000);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_obj_set_style_anim(chat_message_label_, &a, LV_PART_MAIN);
    lv_obj_set_style_anim_duration(chat_message_label_, lv_anim_speed_clamped(60, 300, 60000),
                                   LV_PART_MAIN);
    lv_obj_add_flag(bottom_bar_, LV_OBJ_FLAG_HIDDEN);  // Hide until there is content
#endif

    low_battery_popup_ = lv_obj_create(screen);
    lv_obj_set_scrollbar_mode(low_battery_popup_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(low_battery_popup_, LV_HOR_RES * 0.9, text_font->line_height * 2);
    lv_obj_align(low_battery_popup_, LV_ALIGN_BOTTOM_MID, 0, -lvgl_theme->spacing(4));
    lv_obj_set_style_bg_color(low_battery_popup_, lvgl_theme->low_battery_color(), 0);
    lv_obj_set_style_radius(low_battery_popup_, lvgl_theme->spacing(4), 0);

    low_battery_label_ = lv_label_create(low_battery_popup_);
    lv_label_set_text(low_battery_label_, Lang::Strings::BATTERY_NEED_CHARGE);
    lv_obj_set_style_text_color(low_battery_label_, lv_color_white(), 0);
    lv_obj_center(low_battery_label_);
    lv_obj_add_flag(low_battery_popup_, LV_OBJ_FLAG_HIDDEN);
}

void LcdDisplay::SetPreviewImage(std::unique_ptr<LvglImage> image) {
    DisplayLockGuard lock(this);
    if (preview_image_ == nullptr) {
        ESP_LOGE(TAG, "Preview image is not initialized");
        return;
    }

    if (image == nullptr) {
        esp_timer_stop(preview_timer_);
        lv_obj_remove_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);
        preview_image_cached_.reset();
        if (gif_controller_) {
            gif_controller_->Start();
        }
        return;
    }

    preview_image_cached_ = std::move(image);
    auto img_dsc = preview_image_cached_->image_dsc();
    lv_image_set_src(preview_image_, img_dsc);
    if (img_dsc->header.w > 0 && img_dsc->header.h > 0) {
        // zoom factor 0.5
        lv_image_set_scale(preview_image_, 128 * width_ / img_dsc->header.w);
    }

    // Hide emoji_box_
    if (gif_controller_) {
        gif_controller_->Stop();
    }
    lv_obj_add_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);
    esp_timer_stop(preview_timer_);
    ESP_ERROR_CHECK(esp_timer_start_once(preview_timer_, PREVIEW_IMAGE_DURATION_MS * 1000));
}

void LcdDisplay::SetChatMessage(const char* role, const char* content) {
    if (!setup_ui_called_) {
        ESP_LOGW(TAG, "SetChatMessage('%s', '%s') called before SetupUI() - message will be lost!",
                 role, content);
    }
    DisplayLockGuard lock(this);
    if (chat_message_label_ == nullptr) {
        if (setup_ui_called_) {
            ESP_LOGW(TAG,
                     "SetChatMessage('%s', '%s') failed: chat_message_label_ is nullptr (SetupUI() "
                     "was called but label not created)",
                     role, content);
        }
        return;
    }
    lv_label_set_text(chat_message_label_, content);
    // Show bottom_bar_ only when there is content (and subtitle is not globally hidden)
    if (bottom_bar_ != nullptr) {
        if (content == nullptr || content[0] == '\0') {
            lv_obj_add_flag(bottom_bar_, LV_OBJ_FLAG_HIDDEN);
        } else if (!hide_subtitle_) {
            lv_obj_remove_flag(bottom_bar_, LV_OBJ_FLAG_HIDDEN);
        }
    }
#if CONFIG_USE_MULTILINE_CHAT_MESSAGE
    // Re-align bottom_bar_ after text change so it stays anchored to the bottom
    // as its height adapts to the wrapped content.
    if (bottom_bar_ != nullptr) {
        lv_obj_align(bottom_bar_, LV_ALIGN_BOTTOM_MID, 0, 0);
    }
#endif
}

void LcdDisplay::ClearChatMessages() {
    DisplayLockGuard lock(this);
    // In non-wechat mode, just clear the chat message label and hide the bar
    if (chat_message_label_ != nullptr) {
        lv_label_set_text(chat_message_label_, "");
    }
    if (bottom_bar_ != nullptr) {
        lv_obj_add_flag(bottom_bar_, LV_OBJ_FLAG_HIDDEN);
    }
}
#endif

// 更新“聆听中/说话中”后把状态栏放到全屏业务页面上方。
void LcdDisplay::SetStatus(const char* status) {
    LvglDisplay::SetStatus(status);
    if (!setup_ui_called_) {
        return;
    }
    DisplayLockGuard lock(this);
    if (status_bar_ != nullptr) {
        lv_obj_move_foreground(status_bar_);
    }
}

void LcdDisplay::ShowStoryPlaybackAnimation() {
    if (story_playback_animation_ == nullptr && container_ != nullptr) {
        // 播放表情整体上移 5 像素，给底部字幕留出更清晰的间距。
        story_playback_animation_ = CreateStoryExpressionAnimation(
            container_, LV_HOR_RES, LV_VER_RES - 30, LV_ALIGN_CENTER, 0, -8);
    }
    if (emoji_box_ != nullptr) {
        lv_obj_add_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
    }
}

void LcdDisplay::HideStoryPlaybackAnimation() {
    if (story_playback_animation_ != nullptr) {
        lv_obj_del(story_playback_animation_);
        story_playback_animation_ = nullptr;
    }
    if (emoji_box_ != nullptr) {
        lv_obj_remove_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
    }
}

void LcdDisplay::ShowStoryNavigation(std::function<void()> back_callback,
                                     std::function<void()> home_callback, bool back_enabled) {
    if (!setup_ui_called_) {
        return;
    }
    DisplayLockGuard lock(this);
    if (story_nav_container_ != nullptr) {
        lv_obj_del(story_nav_container_);
    }
    story_back_callback_ = std::move(back_callback);
    story_home_callback_ = std::move(home_callback);

    story_nav_container_ = lv_obj_create(lv_screen_active());
    lv_obj_set_size(story_nav_container_, 114, 48);
    // Leave a little more space at the left edge for easier touch access.
    lv_obj_set_pos(story_nav_container_, 12, 0);
    lv_obj_set_style_bg_opa(story_nav_container_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(story_nav_container_, 0, 0);
    lv_obj_set_style_pad_all(story_nav_container_, 0, 0);
    lv_obj_set_scrollbar_mode(story_nav_container_, LV_SCROLLBAR_MODE_OFF);

    auto create_nav_button = [this](int x, const char* icon_text, bool is_home, bool enabled) {
        auto* button = lv_btn_create(story_nav_container_);
        // Enlarge the transparent touch target without changing the icon font size.
        lv_obj_set_size(button, 54, 46);
        lv_obj_set_pos(button, x, 1);
        lv_obj_set_style_radius(button, 6, 0);
        lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, 0);
        lv_obj_set_style_shadow_width(button, 0, 0);
        lv_obj_set_style_pad_all(button, 0, 0);
        if (!enabled) {
            lv_obj_add_state(button, LV_STATE_DISABLED);
        }
        auto* label = lv_label_create(button);
        lv_label_set_text(label, icon_text);
        lv_obj_set_style_text_font(
            label, static_cast<LvglTheme*>(current_theme_)->large_icon_font()->font(), 0);
        lv_obj_set_style_text_color(label, lv_color_hex(0x333333), 0);
        lv_obj_center(label);
        if (is_home) {
            lv_obj_add_event_cb(
                button,
                [](lv_event_t* event) {
                    auto* display = static_cast<LcdDisplay*>(lv_event_get_user_data(event));
                    if (lv_event_get_code(event) == LV_EVENT_CLICKED && display != nullptr &&
                        display->story_home_callback_) {
                        display->story_home_callback_();
                    }
                },
                LV_EVENT_CLICKED, this);
        } else {
            lv_obj_add_event_cb(
                button,
                [](lv_event_t* event) {
                    auto* display = static_cast<LcdDisplay*>(lv_event_get_user_data(event));
                    if (lv_event_get_code(event) == LV_EVENT_CLICKED && display != nullptr &&
                        display->story_back_callback_) {
                        display->story_back_callback_();
                    }
                },
                LV_EVENT_CLICKED, this);
        }
    };

    create_nav_button(1, FONT_AWESOME_ANGLE_LEFT, false, back_enabled);
    create_nav_button(58, FONT_AWESOME_HOUSE, true, true);
    lv_obj_move_foreground(story_nav_container_);
    // 每个故事页面配置导航后都把通用状态栏置顶，避免被全屏容器遮住。
    if (status_bar_ != nullptr) {
        lv_obj_move_foreground(status_bar_);
    }
}

void LcdDisplay::HideStoryNavigation() {
    if (!setup_ui_called_) {
        return;
    }
    DisplayLockGuard lock(this);
    if (story_nav_container_ != nullptr) {
        lv_obj_del(story_nav_container_);
        story_nav_container_ = nullptr;
    }
    story_back_callback_ = nullptr;
    story_home_callback_ = nullptr;
}

void LcdDisplay::ShowStoryHome(std::function<void()> enter_callback,
                               std::function<void()> theater_callback) {
    if (!setup_ui_called_) {
        return;
    }
    DisplayLockGuard lock(this);
    HideStoryPlaybackAnimation();
    if (story_menu_container_ != nullptr) {
        conversation_expression_animation_ = nullptr;
        lv_obj_del(story_menu_container_);
    }
    story_menu_buttons_.clear();
    story_start_callback_ = nullptr;
    story_resume_callback_ = nullptr;
    story_history_callback_ = nullptr;
    story_confirmation_callback_ = nullptr;
    story_enter_callback_ = std::move(enter_callback);
    story_theater_callback_ = std::move(theater_callback);
    story_menu_container_ = lv_obj_create(lv_screen_active());
    lv_obj_set_size(story_menu_container_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_pos(story_menu_container_, 0, 0);
    lv_obj_set_style_radius(story_menu_container_, 0, 0);
    lv_obj_set_style_border_width(story_menu_container_, 0, 0);
    lv_obj_set_style_pad_all(story_menu_container_, 0, 0);
    lv_obj_set_style_bg_color(story_menu_container_, lv_color_hex(0xF2F1FC), 0);
    lv_obj_set_style_bg_opa(story_menu_container_, LV_OPA_COVER, 0);
    lv_obj_set_scrollbar_mode(story_menu_container_, LV_SCROLLBAR_MODE_OFF);

    conversation_expression_animation_ = CreateStoryExpressionAnimation(
        story_menu_container_, LV_HOR_RES, LV_VER_RES, LV_ALIGN_CENTER, 0, 0);
    conversation_expression_kind_ = static_cast<int>(ConversationExpressionKind::Positive);

    auto* enter = lv_btn_create(story_menu_container_);
    lv_obj_set_size(enter, 78, 28);
    lv_obj_align(enter, LV_ALIGN_TOP_MID, -52, 164);
    lv_obj_set_style_radius(enter, 14, 0);
    lv_obj_set_style_border_width(enter, 0, 0);
    lv_obj_set_style_bg_color(enter, lv_color_hex(0xC9BCF5), 0);
    lv_obj_set_style_bg_grad_color(enter, lv_color_hex(0xE6E0FF), 0);
    lv_obj_set_style_bg_grad_dir(enter, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_shadow_color(enter, lv_color_hex(0xB9ABE8), 0);
    lv_obj_set_style_shadow_width(enter, 8, 0);
    lv_obj_set_style_shadow_opa(enter, LV_OPA_20, 0);
    lv_obj_set_style_pad_all(enter, 0, 0);
    auto* label = lv_label_create(enter);
    lv_label_set_text(label, "AI\xE5\x90\xAC\xE4\xB9\xA6");
    lv_obj_set_style_text_font(label, static_cast<LvglTheme*>(current_theme_)->text_font()->font(),
                               0);
    lv_obj_set_style_text_color(label, lv_color_hex(0x39334F), 0);
    lv_obj_center(label);
    lv_obj_add_event_cb(
        enter,
        [](lv_event_t* event) {
            auto* display = static_cast<LcdDisplay*>(lv_event_get_user_data(event));
            if (lv_event_get_code(event) == LV_EVENT_CLICKED && display != nullptr &&
                display->story_enter_callback_) {
                display->story_enter_callback_();
            }
        },
        LV_EVENT_CLICKED, this);

    auto* theater = lv_btn_create(story_menu_container_);
    lv_obj_set_size(theater, 78, 28);
    lv_obj_align(theater, LV_ALIGN_TOP_MID, 52, 164);
    lv_obj_set_style_radius(theater, 14, 0);
    lv_obj_set_style_border_width(theater, 0, 0);
    lv_obj_set_style_bg_color(theater, lv_color_hex(0xC9BCF5), 0);
    lv_obj_set_style_bg_grad_color(theater, lv_color_hex(0xE6E0FF), 0);
    lv_obj_set_style_bg_grad_dir(theater, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_shadow_color(theater, lv_color_hex(0xB9ABE8), 0);
    lv_obj_set_style_shadow_width(theater, 8, 0);
    lv_obj_set_style_shadow_opa(theater, LV_OPA_20, 0);
    lv_obj_set_style_pad_all(theater, 0, 0);
    auto* theater_label = lv_label_create(theater);
    lv_label_set_text(theater_label, "\xE5\xB0\x8F\xE5\x89\xA7\xE5\x9C\xBA");
    lv_obj_set_style_text_font(theater_label,
                               static_cast<LvglTheme*>(current_theme_)->text_font()->font(), 0);
    lv_obj_set_style_text_color(theater_label, lv_color_hex(0x39334F), 0);
    lv_obj_center(theater_label);
    lv_obj_add_event_cb(
        theater,
        [](lv_event_t* event) {
            auto* display = static_cast<LcdDisplay*>(lv_event_get_user_data(event));
            if (lv_event_get_code(event) == LV_EVENT_CLICKED && display != nullptr &&
                display->story_theater_callback_) {
                display->story_theater_callback_();
            }
        },
        LV_EVENT_CLICKED, this);
    lv_obj_move_foreground(story_menu_container_);
    if (bottom_bar_ != nullptr) {
        lv_obj_move_foreground(bottom_bar_);
    }
    // 功能首页覆盖整个屏幕时，仍把通用状态栏置顶，显示“聆听中/说话中”。
    if (status_bar_ != nullptr) {
        lv_obj_move_foreground(status_bar_);
    }
}

void LcdDisplay::ShowStoryLibrary(const std::vector<StoryHistoryItem>& stories) {
    if (!setup_ui_called_) {
        return;
    }
    DisplayLockGuard lock(this);
    HideStoryPlaybackAnimation();
    if (story_menu_container_ != nullptr) {
        lv_obj_del(story_menu_container_);
    }
    story_menu_buttons_.clear();
    story_start_callback_ = nullptr;
    story_resume_callback_ = nullptr;
    story_history_callback_ = nullptr;
    story_confirmation_callback_ = nullptr;
    story_menu_container_ = lv_obj_create(lv_screen_active());
    lv_obj_set_size(story_menu_container_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_pos(story_menu_container_, 0, 0);
    lv_obj_set_style_radius(story_menu_container_, 0, 0);
    lv_obj_set_style_border_width(story_menu_container_, 0, 0);
    lv_obj_set_style_pad_all(story_menu_container_, 0, 0);
    lv_obj_set_style_bg_color(story_menu_container_, lv_color_hex(0xF8F8FC), 0);
    lv_obj_set_scrollbar_mode(story_menu_container_, LV_SCROLLBAR_MODE_OFF);

    auto* title = lv_label_create(story_menu_container_);
    lv_label_set_text(title,
                      "\xE6\x95\x85\xE4\xBA\x8B\xE9\x87\x8D\xE6\x92\xAD"
                      "\xE9\x80\x89\xE6\x8B\xA9");
    lv_obj_set_style_text_font(title, static_cast<LvglTheme*>(current_theme_)->text_font()->font(),
                               0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x29243D), 0);
    lv_obj_set_pos(title, 12, 51);

    auto* list = lv_obj_create(story_menu_container_);
    lv_obj_set_pos(list, 10, 72);
    lv_obj_set_size(list, 146, 142);
    lv_obj_set_style_radius(list, 0, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_style_pad_row(list, 8, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_width(list, 3, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(list, lv_color_hex(0x8A68FF), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(list, LV_OPA_50, LV_PART_SCROLLBAR);

    if (stories.empty()) {
        auto* empty = lv_label_create(list);
        lv_label_set_text(empty,
                          "\xE6\x9A\x82\xE6\x97\xA0\xE5\x8E\x86\xE5\x8F\xB2"
                          "\xE6\x95\x85\xE4\xBA\x8B");
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(empty, lv_color_hex(0x888888), 0);
        lv_obj_set_width(empty, LV_PCT(100));
        lv_obj_set_style_margin_top(empty, 42, 0);
    } else {
        for (size_t i = 0; i < stories.size(); ++i) {
            auto* button = lv_btn_create(list);
            lv_obj_set_size(button, LV_PCT(100), 76);
            lv_obj_set_flex_grow(button, 0);
            lv_obj_set_style_radius(button, 18, 0);
            lv_obj_set_style_bg_color(button, lv_color_white(), 0);
            lv_obj_set_style_shadow_width(button, 0, 0);
            lv_obj_set_style_pad_all(button, 0, 0);
            lv_obj_set_style_border_color(button, lv_color_hex(0x7B57FF), 0);
            lv_obj_set_style_border_width(button, i == 0 ? 2 : 0, 0);
            lv_obj_set_user_data(button, reinterpret_cast<void*>(i + 1));

            auto* story_title = lv_label_create(button);
            std::string title_text = "\xE3\x80\x8A" + stories[i].title + "\xE3\x80\x8B";
            lv_label_set_text(story_title, title_text.c_str());
            lv_obj_set_pos(story_title, 10, 9);
            lv_obj_set_width(story_title, 92);
            lv_label_set_long_mode(story_title, LV_LABEL_LONG_DOT);
            lv_obj_set_style_text_color(story_title, lv_color_hex(0x29243D), 0);

            if (!stories[i].updated_at.empty()) {
                auto* date = lv_label_create(button);
                lv_label_set_text(date, stories[i].updated_at.c_str());
                lv_obj_align(date, LV_ALIGN_TOP_RIGHT, -8, 10);
                lv_obj_set_style_text_color(
                    date, i == 0 ? lv_color_hex(0x7554FF) : lv_color_hex(0x8C8998), 0);
            }

            auto* summary = lv_label_create(button);
            lv_label_set_text(summary, stories[i].summary.c_str());
            lv_obj_set_pos(summary, 10, 33);
            lv_obj_set_size(summary, 122, 34);
            lv_label_set_long_mode(summary, LV_LABEL_LONG_DOT);
            lv_obj_set_style_text_color(summary, lv_color_hex(0x777382), 0);
            lv_obj_remove_flag(button, LV_OBJ_FLAG_CLICKABLE);
        }
    }

    auto* action_panel = lv_obj_create(story_menu_container_);
    lv_obj_set_pos(action_panel, 166, 16);
    lv_obj_set_size(action_panel, 143, 198);
    lv_obj_set_style_radius(action_panel, 30, 0);
    lv_obj_set_style_border_width(action_panel, 0, 0);
    lv_obj_set_style_pad_all(action_panel, 0, 0);
    lv_obj_set_style_bg_color(action_panel, lv_color_hex(0xEFEBFF), 0);
    lv_obj_set_scrollbar_mode(action_panel, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(action_panel, LV_OBJ_FLAG_SCROLLABLE);

    CreateStoryExpressionAnimation(action_panel, 137, 104, LV_ALIGN_TOP_MID, 0, 8);

    auto* hint = lv_label_create(action_panel);
    lv_label_set_text(hint,
                      "\xE5\x8E\x86\xE5\x8F\xB2\xE6\x95\x85\xE4\xBA\x8B\n"
                      "\xE4\xBB\x85\xE4\xBE\x9B\xE6\x9F\xA5\xE7\x9C\x8B");
    lv_obj_set_width(hint, 130);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x7654FF), 0);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 111);

    lv_obj_move_foreground(story_menu_container_);
    if (bottom_bar_ != nullptr) {
        lv_obj_move_foreground(bottom_bar_);
    }
}

// 显示点击 AI 听书后等待后端 home_result 的加载页面，不触发任何业务动作。
void LcdDisplay::ShowStoryLoading(const std::string& message) {
    if (!setup_ui_called_) {
        return;
    }
    DisplayLockGuard lock(this);
    HideStoryPlaybackAnimation();
    if (story_menu_container_ != nullptr) {
        lv_obj_del(story_menu_container_);
    }
    story_menu_buttons_.clear();
    story_start_callback_ = nullptr;
    story_resume_callback_ = nullptr;
    story_history_callback_ = nullptr;
    story_confirmation_callback_ = nullptr;

    story_menu_container_ = lv_obj_create(lv_screen_active());
    lv_obj_set_size(story_menu_container_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_pos(story_menu_container_, 0, 0);
    lv_obj_set_style_radius(story_menu_container_, 0, 0);
    lv_obj_set_style_border_width(story_menu_container_, 0, 0);
    lv_obj_set_style_pad_all(story_menu_container_, 0, 0);
    lv_obj_set_style_bg_color(story_menu_container_, lv_color_hex(0xEEE8FF), 0);
    lv_obj_set_style_bg_grad_color(story_menu_container_, lv_color_hex(0xF9E5F3), 0);
    lv_obj_set_style_bg_grad_dir(story_menu_container_, LV_GRAD_DIR_VER, 0);
    lv_obj_remove_flag(story_menu_container_, LV_OBJ_FLAG_SCROLLABLE);

    CreateStoryExpressionAnimation(story_menu_container_, 286, 150, LV_ALIGN_CENTER, 0, -18);

    auto* label = lv_label_create(story_menu_container_);
    lv_label_set_text(label, message.empty() ? "正在打开互动听书…" : message.c_str());
    lv_obj_set_width(label, 280);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0x6F52D5), 0);
    lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -30);

    lv_obj_move_foreground(story_menu_container_);
    if (story_nav_container_ != nullptr) {
        lv_obj_move_foreground(story_nav_container_);
    }
    if (bottom_bar_ != nullptr) {
        lv_obj_move_foreground(bottom_bar_);
    }
}

// 根据后端 home_result 渲染故事首页，并把按钮点击交给 Controller 发送上行消息。
void LcdDisplay::ShowStoryPicker(const std::vector<StoryHistoryItem>& stories,
                                 bool last_story_unfinished, const std::string& last_story_title,
                                 std::function<void()> start_callback,
                                 std::function<void()> resume_callback,
                                 std::function<void(size_t)> history_callback) {
    if (!setup_ui_called_) {
        return;
    }
    DisplayLockGuard lock(this);
    HideStoryPlaybackAnimation();
    if (story_menu_container_ != nullptr) {
        lv_obj_del(story_menu_container_);
    }
    story_menu_buttons_.clear();
    story_start_callback_ = std::move(start_callback);
    story_resume_callback_ = std::move(resume_callback);
    story_history_callback_ = std::move(history_callback);
    story_confirmation_callback_ = nullptr;

    auto* text_font = static_cast<LvglTheme*>(current_theme_)->text_font()->font();
    story_menu_container_ = lv_obj_create(lv_screen_active());
    lv_obj_set_size(story_menu_container_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_pos(story_menu_container_, 0, 0);
    lv_obj_set_style_radius(story_menu_container_, 0, 0);
    lv_obj_set_style_border_width(story_menu_container_, 0, 0);
    lv_obj_set_style_pad_all(story_menu_container_, 0, 0);
    lv_obj_set_style_bg_color(story_menu_container_, lv_color_hex(0xF3EFFF), 0);
    lv_obj_set_style_bg_grad_color(story_menu_container_, lv_color_hex(0xFBEAF7), 0);
    lv_obj_set_style_bg_grad_dir(story_menu_container_, LV_GRAD_DIR_VER, 0);
    lv_obj_set_scrollbar_mode(story_menu_container_, LV_SCROLLBAR_MODE_OFF);

    auto* accent = lv_obj_create(story_menu_container_);
    lv_obj_set_pos(accent, 10, 51);
    lv_obj_set_size(accent, 4, 17);
    lv_obj_set_style_radius(accent, 2, 0);
    lv_obj_set_style_border_width(accent, 0, 0);
    lv_obj_set_style_bg_color(accent, lv_color_hex(0x7B5CFA), 0);
    lv_obj_remove_flag(accent, LV_OBJ_FLAG_SCROLLABLE);

    auto* title = lv_label_create(story_menu_container_);
    lv_label_set_text(title, "故事选择");
    lv_obj_set_style_text_font(title, text_font, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x29213F), 0);
    lv_obj_set_pos(title, 19, 51);

    auto* list = lv_obj_create(story_menu_container_);
    lv_obj_set_pos(list, 9, 71);
    lv_obj_set_size(list, 149, 149);
    lv_obj_set_style_radius(list, 0, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_style_pad_row(list, 7, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_width(list, 3, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(list, lv_color_hex(0x8A68FF), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(list, LV_OPA_50, LV_PART_SCROLLBAR);

    if (stories.empty()) {
        auto* empty = lv_label_create(list);
        lv_label_set_text(empty, "暂无历史故事");
        lv_obj_set_width(empty, LV_PCT(100));
        lv_obj_set_style_margin_top(empty, 46, 0);
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(empty, lv_color_hex(0x8B819C), 0);
    } else {
        const size_t count = std::min<size_t>(stories.size(), 10);
        for (size_t i = 0; i < count; ++i) {
            const auto& story = stories[i];
            auto* card = lv_obj_create(list);
            lv_obj_set_size(card, LV_PCT(100), 72);
            lv_obj_set_flex_grow(card, 0);
            lv_obj_set_style_radius(card, 17, 0);
            lv_obj_set_style_border_width(card, 1, 0);
            lv_obj_set_style_border_color(card, lv_color_hex(0xDDD2FF), 0);
            lv_obj_set_style_bg_color(card, lv_color_white(), 0);
            lv_obj_set_style_shadow_color(card, lv_color_hex(0xBBAEFF), 0);
            lv_obj_set_style_shadow_width(card, 7, 0);
            lv_obj_set_style_shadow_opa(card, LV_OPA_10, 0);
            lv_obj_set_style_pad_all(card, 0, 0);
            lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_user_data(card, reinterpret_cast<void*>(i + 1));
            lv_obj_add_event_cb(
                card,
                [](lv_event_t* event) {
                    auto* display = static_cast<LcdDisplay*>(lv_event_get_user_data(event));
                    auto* target = static_cast<lv_obj_t*>(lv_event_get_target(event));
                    const size_t encoded = reinterpret_cast<size_t>(lv_obj_get_user_data(target));
                    if (display != nullptr && encoded > 0 && display->story_history_callback_) {
                        for (auto* button : display->story_menu_buttons_) {
                            lv_obj_add_state(button, LV_STATE_DISABLED);
                        }
                        display->story_history_callback_(encoded - 1);
                    }
                },
                LV_EVENT_CLICKED, this);
            story_menu_buttons_.push_back(card);

            auto* story_title = lv_label_create(card);
            std::string title_text = "《" + story.title + "》";
            lv_label_set_text(story_title, title_text.c_str());
            lv_obj_set_pos(story_title, 9, 8);
            lv_obj_set_width(story_title, 85);
            lv_label_set_long_mode(story_title, LV_LABEL_LONG_DOT);
            lv_obj_set_style_text_color(story_title, lv_color_hex(0x29213F), 0);

            auto* badge = lv_obj_create(card);
            lv_obj_set_size(badge, 45, 18);
            lv_obj_align(badge, LV_ALIGN_TOP_RIGHT, -6, 6);
            lv_obj_set_style_radius(badge, 9, 0);
            lv_obj_set_style_border_width(badge, 0, 0);
            lv_obj_set_style_pad_all(badge, 0, 0);
            lv_obj_remove_flag(badge, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_remove_flag(badge, LV_OBJ_FLAG_CLICKABLE);

            const char* status_text = "待确认";
            lv_color_t badge_color = lv_color_hex(0xE8E0FF);
            lv_color_t badge_text_color = lv_color_hex(0x6E55C7);
            if (story.status == StoryHistoryStatus::Completed) {
                status_text = "已完成";
                badge_color = lv_color_hex(0xCCFF31);
                badge_text_color = lv_color_hex(0x29310D);
            } else if (story.status == StoryHistoryStatus::Unfinished) {
                status_text = "未完成";
                badge_color = lv_color_hex(0xF3C3E8);
                badge_text_color = lv_color_hex(0x713D68);
            }
            lv_obj_set_style_bg_color(badge, badge_color, 0);
            auto* badge_label = lv_label_create(badge);
            lv_label_set_text(badge_label, status_text);
            lv_obj_set_style_text_color(badge_label, badge_text_color, 0);
            lv_obj_center(badge_label);

            auto* summary = lv_label_create(card);
            lv_label_set_text(summary, story.summary.c_str());
            lv_obj_set_pos(summary, 9, 34);
            lv_obj_set_size(summary, 128, 27);
            lv_label_set_long_mode(summary, LV_LABEL_LONG_DOT);
            lv_obj_set_style_text_color(summary, lv_color_hex(0x777083), 0);

            if (!story.updated_at.empty()) {
                auto* date = lv_label_create(card);
                lv_label_set_text(date, story.updated_at.c_str());
                lv_obj_align(date, LV_ALIGN_BOTTOM_RIGHT, -7, -4);
                lv_obj_set_style_text_color(date, lv_color_hex(0x9C91AA), 0);
            }
        }
    }

    auto* action_panel = lv_obj_create(story_menu_container_);
    lv_obj_set_pos(action_panel, 166, 16);
    lv_obj_set_size(action_panel, 144, 204);
    lv_obj_set_style_radius(action_panel, 28, 0);
    lv_obj_set_style_border_width(action_panel, 1, 0);
    lv_obj_set_style_border_color(action_panel, lv_color_hex(0xE1D4FF), 0);
    lv_obj_set_style_pad_all(action_panel, 0, 0);
    lv_obj_set_style_bg_color(action_panel, lv_color_hex(0xECE5FF), 0);
    lv_obj_set_style_bg_grad_color(action_panel, lv_color_hex(0xF8E3F3), 0);
    lv_obj_set_style_bg_grad_dir(action_panel, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_shadow_color(action_panel, lv_color_hex(0xA88DF8), 0);
    lv_obj_set_style_shadow_width(action_panel, 10, 0);
    lv_obj_set_style_shadow_opa(action_panel, LV_OPA_10, 0);
    lv_obj_remove_flag(action_panel, LV_OBJ_FLAG_SCROLLABLE);

    CreateStoryExpressionAnimation(action_panel, 134, 79, LV_ALIGN_TOP_MID, 0, 7);

    auto* prompt = lv_label_create(action_panel);
    lv_label_set_text(prompt, last_story_unfinished ? "上次故事还没听完\n请选择接下来要听什么"
                                                    : "上次故事已经结束\n要开启新故事吗？");
    lv_obj_set_pos(prompt, 8, 88);
    lv_obj_set_width(prompt, 128);
    lv_label_set_long_mode(prompt, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(prompt, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(prompt, lv_color_hex(0x7455DD), 0);

    if (last_story_unfinished && !last_story_title.empty()) {
        auto* last_title = lv_label_create(action_panel);
        std::string last_title_text = "《" + last_story_title + "》";
        lv_label_set_text(last_title, last_title_text.c_str());
        lv_obj_set_pos(last_title, 14, 123);
        lv_obj_set_width(last_title, 116);
        lv_label_set_long_mode(last_title, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(last_title, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(last_title, lv_color_hex(0x44365D), 0);
    }

    auto create_visual_button = [this, action_panel, text_font](
                                    int y, const char* text, lv_color_t color, lv_color_t gradient,
                                    lv_color_t text_color, bool resume_action) {
        auto* button = lv_btn_create(action_panel);
        lv_obj_set_pos(button, 13, y);
        lv_obj_set_size(button, 118, 28);
        lv_obj_set_style_radius(button, 14, 0);
        lv_obj_set_style_border_width(button, 0, 0);
        lv_obj_set_style_bg_color(button, color, 0);
        lv_obj_set_style_bg_grad_color(button, gradient, 0);
        lv_obj_set_style_bg_grad_dir(button, LV_GRAD_DIR_HOR, 0);
        lv_obj_set_style_shadow_width(button, 0, 0);
        lv_obj_set_style_pad_all(button, 0, 0);
        lv_obj_set_user_data(button, reinterpret_cast<void*>(resume_action ? 1 : 2));
        lv_obj_add_event_cb(
            button,
            [](lv_event_t* event) {
                auto* display = static_cast<LcdDisplay*>(lv_event_get_user_data(event));
                auto* target = static_cast<lv_obj_t*>(lv_event_get_target(event));
                const size_t action = reinterpret_cast<size_t>(lv_obj_get_user_data(target));
                if (display == nullptr) {
                    return;
                }
                for (auto* menu_button : display->story_menu_buttons_) {
                    lv_obj_add_state(menu_button, LV_STATE_DISABLED);
                }
                if (action == 1 && display->story_resume_callback_) {
                    display->story_resume_callback_();
                } else if (action == 2 && display->story_start_callback_) {
                    display->story_start_callback_();
                }
            },
            LV_EVENT_CLICKED, this);
        story_menu_buttons_.push_back(button);
        auto* label = lv_label_create(button);
        lv_label_set_text(label, text);
        lv_obj_set_style_text_font(label, text_font, 0);
        lv_obj_set_style_text_color(label, text_color, 0);
        lv_obj_center(label);
    };

    if (last_story_unfinished) {
        create_visual_button(142, "继续上次故事", lv_color_hex(0x7656F5), lv_color_hex(0xA37DFF),
                             lv_color_white(), true);
        create_visual_button(176, "开启新故事", lv_color_hex(0xC9FF22), lv_color_hex(0xE2FF70),
                             lv_color_hex(0x252C12), false);
    } else {
        create_visual_button(165, "开启新故事", lv_color_hex(0xC9FF22), lv_color_hex(0xE2FF70),
                             lv_color_hex(0x252C12), false);
    }

    lv_obj_move_foreground(story_menu_container_);
    if (story_nav_container_ != nullptr) {
        lv_obj_move_foreground(story_nav_container_);
    }
    if (bottom_bar_ != nullptr) {
        lv_obj_move_foreground(bottom_bar_);
    }
}

// 根据后端 show_confirmation 的 title/prompt 显示通用“是/否”确认弹窗。
void LcdDisplay::ShowStoryConfirmation(const std::string& title, const std::string& prompt,
                                       std::function<void(bool)> confirmation_callback) {
    if (!setup_ui_called_) {
        return;
    }
    DisplayLockGuard lock(this);
    HideStoryPlaybackAnimation();
    if (story_menu_container_ != nullptr) {
        lv_obj_del(story_menu_container_);
    }
    story_menu_buttons_.clear();
    story_start_callback_ = nullptr;
    story_resume_callback_ = nullptr;
    story_history_callback_ = nullptr;
    story_confirmation_callback_ = std::move(confirmation_callback);

    auto* text_font = static_cast<LvglTheme*>(current_theme_)->text_font()->font();
    story_menu_container_ = lv_obj_create(lv_screen_active());
    lv_obj_set_size(story_menu_container_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_pos(story_menu_container_, 0, 0);
    lv_obj_set_style_radius(story_menu_container_, 0, 0);
    lv_obj_set_style_border_width(story_menu_container_, 0, 0);
    lv_obj_set_style_pad_all(story_menu_container_, 0, 0);
    lv_obj_set_style_bg_color(story_menu_container_, lv_color_hex(0xEEE8FF), 0);
    lv_obj_set_style_bg_grad_color(story_menu_container_, lv_color_hex(0xF9E5F3), 0);
    lv_obj_set_style_bg_grad_dir(story_menu_container_, LV_GRAD_DIR_VER, 0);
    lv_obj_remove_flag(story_menu_container_, LV_OBJ_FLAG_SCROLLABLE);

    auto* dialog = lv_obj_create(story_menu_container_);
    // “是/否”确认卡片顶部下收 10px、底部上收 10px，横向尺寸保持不变。
    lv_obj_set_pos(dialog, 20, 53);
    lv_obj_set_size(dialog, 280, 154);
    lv_obj_set_style_radius(dialog, 27, 0);
    lv_obj_set_style_border_width(dialog, 2, 0);
    lv_obj_set_style_border_color(dialog, lv_color_hex(0x8B6CFA), 0);
    lv_obj_set_style_pad_all(dialog, 0, 0);
    lv_obj_set_style_bg_color(dialog, lv_color_hex(0xF8F5FF), 0);
    lv_obj_set_style_bg_grad_color(dialog, lv_color_hex(0xFBE8F5), 0);
    lv_obj_set_style_bg_grad_dir(dialog, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_shadow_color(dialog, lv_color_hex(0x9D7CF7), 0);
    lv_obj_set_style_shadow_width(dialog, 14, 0);
    lv_obj_set_style_shadow_opa(dialog, LV_OPA_20, 0);
    lv_obj_remove_flag(dialog, LV_OBJ_FLAG_SCROLLABLE);

    CreateStoryExpressionAnimation(dialog, 262, 60, LV_ALIGN_TOP_MID, 0, 4);

    auto* prompt_label = lv_label_create(dialog);
    lv_label_set_text(prompt_label, prompt.c_str());
    lv_obj_set_pos(prompt_label, 20, 64);
    lv_obj_set_size(prompt_label, 240, 44);
    lv_label_set_long_mode(prompt_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(prompt_label, text_font, 0);
    lv_obj_set_style_text_align(prompt_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(prompt_label, lv_color_hex(0x6547CE), 0);

    auto* story_pill = lv_obj_create(dialog);
    lv_obj_set_pos(story_pill, 47, 93);
    lv_obj_set_size(story_pill, 186, 27);
    lv_obj_set_style_radius(story_pill, 14, 0);
    lv_obj_set_style_border_width(story_pill, 0, 0);
    lv_obj_set_style_pad_all(story_pill, 0, 0);
    lv_obj_set_style_bg_color(story_pill, lv_color_hex(0xE9DFFF), 0);
    lv_obj_remove_flag(story_pill, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(story_pill, LV_OBJ_FLAG_CLICKABLE);

    auto* story_label = lv_label_create(story_pill);
    std::string story_text = title.empty() ? "互动听书" : "《" + title + "》";
    lv_label_set_text(story_label, story_text.c_str());
    lv_obj_set_width(story_label, 166);
    lv_label_set_long_mode(story_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(story_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(story_label, lv_color_hex(0x3D3152), 0);
    lv_obj_center(story_label);

    auto create_dialog_button = [this, dialog, text_font](int x, const char* text, lv_color_t color,
                                                          lv_color_t gradient,
                                                          lv_color_t text_color, bool confirmed) {
        auto* button = lv_btn_create(dialog);
        lv_obj_set_pos(button, x, 123);
        lv_obj_set_size(button, 108, 31);
        lv_obj_set_style_radius(button, 16, 0);
        lv_obj_set_style_border_width(button, 0, 0);
        lv_obj_set_style_bg_color(button, color, 0);
        lv_obj_set_style_bg_grad_color(button, gradient, 0);
        lv_obj_set_style_bg_grad_dir(button, LV_GRAD_DIR_HOR, 0);
        lv_obj_set_style_shadow_width(button, 0, 0);
        lv_obj_set_style_pad_all(button, 0, 0);
        lv_obj_set_user_data(button, reinterpret_cast<void*>(confirmed ? 1 : 2));
        lv_obj_add_event_cb(
            button,
            [](lv_event_t* event) {
                auto* display = static_cast<LcdDisplay*>(lv_event_get_user_data(event));
                auto* target = static_cast<lv_obj_t*>(lv_event_get_target(event));
                const bool confirmed = reinterpret_cast<size_t>(lv_obj_get_user_data(target)) == 1;
                if (display != nullptr && display->story_confirmation_callback_) {
                    for (auto* menu_button : display->story_menu_buttons_) {
                        lv_obj_add_state(menu_button, LV_STATE_DISABLED);
                    }
                    display->story_confirmation_callback_(confirmed);
                }
            },
            LV_EVENT_CLICKED, this);
        story_menu_buttons_.push_back(button);
        auto* label = lv_label_create(button);
        lv_label_set_text(label, text);
        lv_obj_set_style_text_font(label, text_font, 0);
        lv_obj_set_style_text_color(label, text_color, 0);
        lv_obj_center(label);
    };

    create_dialog_button(24, "是", lv_color_hex(0xC9FF22), lv_color_hex(0xE3FF73),
                         lv_color_hex(0x252C12), true);
    create_dialog_button(148, "否", lv_color_hex(0x7656F5), lv_color_hex(0xA37DFF),
                         lv_color_white(), false);

    lv_obj_move_foreground(story_menu_container_);
    if (story_nav_container_ != nullptr) {
        lv_obj_move_foreground(story_nav_container_);
    }
    if (bottom_bar_ != nullptr) {
        lv_obj_move_foreground(bottom_bar_);
    }
}

// 统一启用或禁用故事菜单按钮，用于等待服务端 clear_* 消息期间防止连点。
void LcdDisplay::SetStoryMenuButtonsEnabled(bool enabled) {
    if (!setup_ui_called_) {
        return;
    }
    DisplayLockGuard lock(this);
    for (auto* button : story_menu_buttons_) {
        if (button == nullptr) {
            continue;
        }
        if (enabled) {
            lv_obj_remove_state(button, LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(button, LV_STATE_DISABLED);
        }
    }
}

void LcdDisplay::HideStoryMenu() {
    if (!setup_ui_called_) {
        return;
    }
    DisplayLockGuard lock(this);
    if (story_menu_container_ != nullptr) {
        lv_obj_del(story_menu_container_);
        story_menu_container_ = nullptr;
    }
    story_enter_callback_ = nullptr;
    story_theater_callback_ = nullptr;
    story_start_callback_ = nullptr;
    story_resume_callback_ = nullptr;
    story_history_callback_ = nullptr;
    story_confirmation_callback_ = nullptr;
    story_menu_buttons_.clear();
    ShowStoryPlaybackAnimation();
}

void LcdDisplay::ShowStory(const std::string& page_title, const std::string& question,
                           const std::string& option_a, const std::string& option_b,
                           std::function<void(int)> choice_callback) {
    if (!setup_ui_called_) {
        ESP_LOGW(TAG, "ShowStory called before SetupUI()");
        return;
    }

    DisplayLockGuard lock(this);
    HideStoryPlaybackAnimation();

    lv_obj_t* story_parent = content_;

    if (story_parent == nullptr) {
        story_parent = lv_screen_active();
    }

    if (story_parent == nullptr) {
        ESP_LOGE(TAG, "ShowStory failed: no valid LVGL parent");
        return;
    }

    if (story_container_ != nullptr) {
        lv_obj_del(story_container_);
        story_container_ = nullptr;
        story_button_a_ = nullptr;
        story_button_b_ = nullptr;
    }
    if (story_menu_container_ != nullptr) {
        lv_obj_del(story_menu_container_);
        story_menu_container_ = nullptr;
    }

    story_choice_callback_ = std::move(choice_callback);

    // Story branch choice page.
    lv_obj_t* story_container = lv_obj_create(story_parent);
    story_container_ = story_container;
    lv_obj_set_size(story_container, LV_HOR_RES, LV_VER_RES - 22);
    lv_obj_align(story_container, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_set_style_bg_color(story_container, lv_color_hex(0xF2EEFF), 0);
    lv_obj_set_style_bg_opa(story_container, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(story_container, 0, 0);
    lv_obj_set_style_pad_all(story_container, 0, 0);
    lv_obj_set_style_layout(story_container, LV_LAYOUT_NONE, 0);
    lv_obj_move_foreground(story_container);

    lv_obj_t* title = lv_label_create(story_container);
    lv_label_set_text(title, question.c_str());
    // 分支故事内容横跨屏幕可用宽度，顶部高度和纵向位置保持不变。
    lv_obj_set_pos(title, 12, 64);
    lv_obj_set_width(title, LV_HOR_RES - 24);
    lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x8067D9), 0);

    lv_obj_t* prompt = lv_label_create(story_container);
    lv_label_set_text(prompt, page_title.c_str());
    lv_obj_set_pos(prompt, 12, 43);
    lv_obj_set_width(prompt, LV_HOR_RES - 24);
    lv_obj_set_style_text_color(prompt, lv_color_hex(0x7154E8), 0);
    lv_obj_set_style_text_align(prompt, LV_TEXT_ALIGN_LEFT, 0);

    // 故事标题

    // 故事正文气泡

    // 选项 A 按钮
    lv_obj_t* button_a = lv_btn_create(story_container);
    story_button_a_ = button_a;
    // 选项卡片顶部下移、底部上收；宽度保持不变，避免压住字幕。
    lv_obj_set_size(button_a, 140, 106);
    lv_obj_set_pos(button_a, 12, 105);
    lv_obj_set_style_radius(button_a, 18, 0);
    lv_obj_set_style_bg_color(button_a, lv_color_hex(0x7A5CF4), 0);
    lv_obj_set_style_bg_color(button_a, lv_color_hex(0x6847E8), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(button_a, 0, 0);
    lv_obj_set_style_pad_all(button_a, 0, 0);
    lv_obj_set_style_shadow_width(button_a, 12, 0);
    lv_obj_set_style_shadow_opa(button_a, LV_OPA_20, 0);
    lv_obj_set_style_shadow_color(button_a, lv_color_hex(0x6F52DD), 0);
    lv_obj_set_style_shadow_ofs_y(button_a, 5, 0);

    lv_obj_t* badge_a = lv_obj_create(button_a);
    lv_obj_set_size(badge_a, 23, 23);
    lv_obj_set_pos(badge_a, 12, 13);
    lv_obj_set_style_radius(badge_a, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(badge_a, lv_color_hex(0xCFFF23), 0);
    lv_obj_set_style_border_width(badge_a, 0, 0);
    lv_obj_set_style_pad_all(badge_a, 0, 0);
    lv_obj_clear_flag(badge_a, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* badge_a_label = lv_label_create(badge_a);
    lv_label_set_text(badge_a_label, "A");
    lv_obj_set_style_text_color(badge_a_label, lv_color_hex(0x4C35A3), 0);
    lv_obj_center(badge_a_label);

    lv_obj_t* button_a_label = lv_label_create(button_a);
    lv_label_set_text(button_a_label, option_a.c_str());
    lv_obj_set_pos(button_a_label, 12, 43);
    lv_obj_set_width(button_a_label, 116);
    lv_label_set_long_mode(button_a_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(button_a_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_color(button_a_label, lv_color_white(), 0);

    lv_obj_t* line_a = lv_obj_create(button_a);
    lv_obj_set_size(line_a, 104, 1);
    lv_obj_set_pos(line_a, 12, 89);
    lv_obj_set_style_bg_color(line_a, lv_color_hex(0xB5A7FA), 0);
    lv_obj_set_style_bg_opa(line_a, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(line_a, 0, 0);
    lv_obj_set_style_pad_all(line_a, 0, 0);
    lv_obj_t* dots_a = lv_label_create(button_a);
    lv_label_set_text(dots_a, "\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2");
    lv_obj_set_pos(dots_a, 111, 86);
    lv_obj_set_style_text_color(dots_a, lv_color_hex(0xCFFF23), 0);

    lv_obj_add_event_cb(
        button_a,
        [](lv_event_t* event) {
            if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
                return;
            }

            auto* display = static_cast<LcdDisplay*>(lv_event_get_user_data(event));

            if (display != nullptr && display->story_choice_callback_) {
                ESP_LOGI(TAG, "Story choice A clicked");
                lv_obj_add_state(display->story_button_a_, LV_STATE_DISABLED);
                lv_obj_add_state(display->story_button_b_, LV_STATE_DISABLED);
                display->story_choice_callback_(0);
            }
        },
        LV_EVENT_CLICKED, this);

    // 选项 B 按钮
    lv_obj_t* button_b = lv_btn_create(story_container);
    story_button_b_ = button_b;
    lv_obj_set_size(button_b, 146, 106);
    lv_obj_set_pos(button_b, 162, 105);
    lv_obj_set_style_radius(button_b, 18, 0);
    lv_obj_set_style_bg_color(button_b, lv_color_hex(0xCFFF23), 0);
    lv_obj_set_style_bg_color(button_b, lv_color_hex(0xBDEB12), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(button_b, 0, 0);
    lv_obj_set_style_pad_all(button_b, 0, 0);
    lv_obj_set_style_shadow_width(button_b, 12, 0);
    lv_obj_set_style_shadow_opa(button_b, LV_OPA_20, 0);
    lv_obj_set_style_shadow_color(button_b, lv_color_hex(0x93BC00), 0);
    lv_obj_set_style_shadow_ofs_y(button_b, 5, 0);

    lv_obj_t* badge_b = lv_obj_create(button_b);
    lv_obj_set_size(badge_b, 23, 23);
    lv_obj_set_pos(badge_b, 12, 13);
    lv_obj_set_style_radius(badge_b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(badge_b, lv_color_hex(0x7A5CF4), 0);
    lv_obj_set_style_border_width(badge_b, 0, 0);
    lv_obj_set_style_pad_all(badge_b, 0, 0);
    lv_obj_clear_flag(badge_b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* badge_b_label = lv_label_create(badge_b);
    lv_label_set_text(badge_b_label, "B");
    lv_obj_set_style_text_color(badge_b_label, lv_color_white(), 0);
    lv_obj_center(badge_b_label);

    lv_obj_t* button_b_label = lv_label_create(button_b);
    lv_label_set_text(button_b_label, option_b.c_str());
    lv_obj_set_pos(button_b_label, 12, 43);
    lv_obj_set_width(button_b_label, 122);
    lv_label_set_long_mode(button_b_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(button_b_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_color(button_b_label, lv_color_hex(0x171024), 0);

    lv_obj_t* line_b = lv_obj_create(button_b);
    lv_obj_set_size(line_b, 110, 1);
    lv_obj_set_pos(line_b, 12, 89);
    lv_obj_set_style_bg_color(line_b, lv_color_hex(0x9FC400), 0);
    lv_obj_set_style_bg_opa(line_b, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(line_b, 0, 0);
    lv_obj_set_style_pad_all(line_b, 0, 0);
    lv_obj_t* dots_b = lv_label_create(button_b);
    lv_label_set_text(dots_b, "\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2");
    lv_obj_set_pos(dots_b, 117, 86);
    lv_obj_set_style_text_color(dots_b, lv_color_hex(0x8467F5), 0);

    lv_obj_add_event_cb(
        button_b,
        [](lv_event_t* event) {
            if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
                return;
            }

            auto* display = static_cast<LcdDisplay*>(lv_event_get_user_data(event));

            if (display != nullptr && display->story_choice_callback_) {
                ESP_LOGI(TAG, "Story choice B clicked");
                lv_obj_add_state(display->story_button_a_, LV_STATE_DISABLED);
                lv_obj_add_state(display->story_button_b_, LV_STATE_DISABLED);
                display->story_choice_callback_(1);
            }
        },
        LV_EVENT_CLICKED, this);

    // 自动滚动到故事选项
    if (bottom_bar_ != nullptr) {
        lv_obj_move_foreground(bottom_bar_);
    }
}

void LcdDisplay::HideStory() {
    if (!setup_ui_called_) {
        return;
    }

    DisplayLockGuard lock(this);
    if (story_container_ != nullptr) {
        lv_obj_del(story_container_);
        story_container_ = nullptr;
        story_button_a_ = nullptr;
        story_button_b_ = nullptr;
    }
    story_choice_callback_ = nullptr;
    ShowStoryPlaybackAnimation();
}

void LcdDisplay::SetStoryButtonsEnabled(bool enabled) {
    DisplayLockGuard lock(this);
    lv_obj_t* buttons[] = {story_button_a_, story_button_b_};
    for (auto* button : buttons) {
        if (button == nullptr) {
            continue;
        }
        if (enabled) {
            lv_obj_remove_state(button, LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(button, LV_STATE_DISABLED);
        }
    }
}

// 根据日常对话情绪替换功能首页中的动画；业务页面不存在该动画时直接忽略。
void LcdDisplay::SetConversationEmotion(const char* emotion) {
    if (!setup_ui_called_) {
        return;
    }

    DisplayLockGuard lock(this);
    if (conversation_expression_animation_ == nullptr ||
        !lv_obj_is_valid(conversation_expression_animation_) || story_menu_container_ == nullptr ||
        !lv_obj_is_valid(story_menu_container_)) {
        conversation_expression_animation_ = nullptr;
        return;
    }

    const auto kind = ResolveConversationExpression(emotion);
    if (conversation_expression_kind_ == static_cast<int>(kind)) {
        return;
    }

    lv_obj_del(conversation_expression_animation_);
    conversation_expression_animation_ = nullptr;
    switch (kind) {
        case ConversationExpressionKind::Calm:
            conversation_expression_animation_ = CreateCalmExpressionAnimation(
                story_menu_container_, LV_HOR_RES, LV_VER_RES, LV_ALIGN_CENTER, 0, 0);
            break;
        case ConversationExpressionKind::Negative:
            conversation_expression_animation_ = CreateNegativeExpressionAnimation(
                story_menu_container_, LV_HOR_RES, LV_VER_RES, LV_ALIGN_CENTER, 0, 0);
            break;
        case ConversationExpressionKind::Positive:
        default:
            conversation_expression_animation_ = CreateStoryExpressionAnimation(
                story_menu_container_, LV_HOR_RES, LV_VER_RES, LV_ALIGN_CENTER, 0, 0);
            break;
    }
    conversation_expression_kind_ = static_cast<int>(kind);
    lv_obj_move_background(conversation_expression_animation_);
    ESP_LOGI(TAG, "Conversation emotion '%s' mapped to expression %d",
             emotion != nullptr ? emotion : "", conversation_expression_kind_);
}

void LcdDisplay::SetEmotion(const char* emotion) {
    if (!setup_ui_called_) {
        ESP_LOGW(TAG, "SetEmotion('%s') called before SetupUI() - emotion will not be displayed!",
                 emotion);
    }
    // Stop any running GIF animation
    if (gif_controller_) {
        DisplayLockGuard lock(this);
        gif_controller_->Stop();
        // Hide image before destroying GIF controller to prevent LVGL from
        // accessing freed image data during rendering between lock scopes
        if (emoji_image_) {
            lv_obj_add_flag(emoji_image_, LV_OBJ_FLAG_HIDDEN);
        }
        gif_controller_.reset();
    }

    if (emoji_image_ == nullptr) {
        if (setup_ui_called_) {
            ESP_LOGW(TAG,
                     "SetEmotion('%s') failed: emoji_image_ is nullptr (SetupUI() was called but "
                     "emoji image not created)",
                     emotion);
        }
        return;
    }

    auto emoji_collection = static_cast<LvglTheme*>(current_theme_)->emoji_collection();
    auto image = emoji_collection != nullptr ? emoji_collection->GetEmojiImage(emotion) : nullptr;
    if (image == nullptr) {
        const char* utf8 = font_awesome_get_utf8(emotion);
        if (utf8 != nullptr && emoji_label_ != nullptr) {
            DisplayLockGuard lock(this);
            lv_label_set_text(emoji_label_, utf8);
            lv_obj_add_flag(emoji_image_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    DisplayLockGuard lock(this);
    if (image->IsGif()) {
        // Create new GIF controller
        gif_controller_ = std::make_unique<LvglGif>(image->image_dsc());

        if (gif_controller_->IsLoaded()) {
            // Set up frame update callback
            gif_controller_->SetFrameCallback(
                [this]() { lv_image_set_src(emoji_image_, gif_controller_->image_dsc()); });

            // Set initial frame and start animation
            lv_image_set_src(emoji_image_, gif_controller_->image_dsc());
            gif_controller_->Start();

            // Show GIF, hide others
            lv_obj_add_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(emoji_image_, LV_OBJ_FLAG_HIDDEN);
        } else {
            ESP_LOGE(TAG, "Failed to load GIF for emotion: %s", emotion);
            gif_controller_.reset();
        }
    } else {
        lv_image_set_src(emoji_image_, image->image_dsc());
        lv_obj_add_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(emoji_image_, LV_OBJ_FLAG_HIDDEN);
    }

#if CONFIG_USE_WECHAT_MESSAGE_STYLE
    // In WeChat message style, if emotion is neutral, don't display it
    uint32_t child_count = lv_obj_get_child_cnt(content_);
    if (strcmp(emotion, "neutral") == 0 && child_count > 0) {
        // Stop GIF animation if running
        if (gif_controller_) {
            gif_controller_->Stop();
            gif_controller_.reset();
        }

        lv_obj_add_flag(emoji_image_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);
    }
#endif
}

void LcdDisplay::SetTheme(Theme* theme) {
    DisplayLockGuard lock(this);

    auto lvgl_theme = static_cast<LvglTheme*>(theme);

    // Get the active screen
    lv_obj_t* screen = lv_screen_active();

    // Set font
    auto text_font = lvgl_theme->text_font()->font();
    auto icon_font = lvgl_theme->icon_font()->font();
    auto large_icon_font = lvgl_theme->large_icon_font()->font();

    if (text_font->line_height >= 40) {
        lv_obj_set_style_text_font(mute_label_, large_icon_font, 0);
        lv_obj_set_style_text_font(battery_label_, large_icon_font, 0);
        lv_obj_set_style_text_font(network_label_, large_icon_font, 0);
    } else {
        lv_obj_set_style_text_font(mute_label_, icon_font, 0);
        lv_obj_set_style_text_font(battery_label_, icon_font, 0);
        lv_obj_set_style_text_font(network_label_, icon_font, 0);
    }

    // Set parent text color
    lv_obj_set_style_text_font(screen, text_font, 0);
    lv_obj_set_style_text_color(screen, lvgl_theme->text_color(), 0);

    // Set background image
    if (lvgl_theme->background_image() != nullptr) {
        lv_obj_set_style_bg_image_src(container_, lvgl_theme->background_image()->image_dsc(), 0);
    } else {
        lv_obj_set_style_bg_image_src(container_, nullptr, 0);
        lv_obj_set_style_bg_color(container_, lvgl_theme->background_color(), 0);
    }

    // Update top bar background color with 50% opacity
    if (top_bar_ != nullptr) {
        lv_obj_set_style_bg_opa(top_bar_, LV_OPA_50, 0);
        lv_obj_set_style_bg_color(top_bar_, lvgl_theme->background_color(), 0);
    }

    // Update status bar elements
    lv_obj_set_style_text_color(network_label_, lvgl_theme->text_color(), 0);
    lv_obj_set_style_text_color(status_label_, lvgl_theme->text_color(), 0);
    lv_obj_set_style_text_color(notification_label_, lvgl_theme->text_color(), 0);
    lv_obj_set_style_text_color(mute_label_, lvgl_theme->text_color(), 0);
    lv_obj_set_style_text_color(battery_label_, lvgl_theme->text_color(), 0);
    lv_obj_set_style_text_color(emoji_label_, lvgl_theme->text_color(), 0);

    // If we have the chat message style, update all message bubbles
#if CONFIG_USE_WECHAT_MESSAGE_STYLE
    // Set content background opacity
    lv_obj_set_style_bg_opa(content_, LV_OPA_TRANSP, 0);

    // Iterate through all children of content (message containers or bubbles)
    uint32_t child_count = lv_obj_get_child_cnt(content_);
    for (uint32_t i = 0; i < child_count; i++) {
        lv_obj_t* obj = lv_obj_get_child(content_, i);
        if (obj == nullptr)
            continue;

        lv_obj_t* bubble = nullptr;

        // Check if this object is a container or bubble
        // If it's a container (user or system message), get its child as bubble
        // If it's a bubble (assistant message), use it directly
        if (lv_obj_get_child_cnt(obj) > 0) {
            // Might be a container, check if it's a user or system message container
            // User and system message containers are transparent
            lv_opa_t bg_opa = lv_obj_get_style_bg_opa(obj, LV_PART_MAIN);
            if (bg_opa == LV_OPA_TRANSP) {
                // This is a user or system message container
                bubble = lv_obj_get_child(obj, 0);
            } else {
                // This might be an assistant message bubble itself
                bubble = obj;
            }
        } else {
            // No child elements, might be other UI elements, skip
            continue;
        }

        if (bubble == nullptr)
            continue;

        // Use saved user data to identify bubble type
        void* bubble_type_ptr = lv_obj_get_user_data(bubble);
        if (bubble_type_ptr != nullptr) {
            const char* bubble_type = static_cast<const char*>(bubble_type_ptr);

            // Apply correct color based on bubble type
            if (strcmp(bubble_type, "user") == 0) {
                lv_obj_set_style_bg_color(bubble, lvgl_theme->user_bubble_color(), 0);
            } else if (strcmp(bubble_type, "assistant") == 0) {
                lv_obj_set_style_bg_color(bubble, lvgl_theme->assistant_bubble_color(), 0);
            } else if (strcmp(bubble_type, "system") == 0) {
                lv_obj_set_style_bg_color(bubble, lvgl_theme->system_bubble_color(), 0);
            } else if (strcmp(bubble_type, "image") == 0) {
                lv_obj_set_style_bg_color(bubble, lvgl_theme->system_bubble_color(), 0);
            }

            // Update border color
            lv_obj_set_style_border_color(bubble, lvgl_theme->border_color(), 0);

            // Update text color for the message
            if (lv_obj_get_child_cnt(bubble) > 0) {
                lv_obj_t* text = lv_obj_get_child(bubble, 0);
                if (text != nullptr) {
                    // Set text color based on bubble type
                    if (strcmp(bubble_type, "system") == 0) {
                        lv_obj_set_style_text_color(text, lvgl_theme->system_text_color(), 0);
                    } else {
                        lv_obj_set_style_text_color(text, lvgl_theme->text_color(), 0);
                    }
                }
            }
        } else {
            ESP_LOGW(TAG, "child[%lu] Bubble type is not found", i);
        }
    }
#else
    // Simple UI mode - just update the main chat message
    if (chat_message_label_ != nullptr) {
        lv_obj_set_style_text_color(chat_message_label_, lvgl_theme->text_color(), 0);
    }

    if (emoji_label_ != nullptr) {
        lv_obj_set_style_text_color(emoji_label_, lvgl_theme->text_color(), 0);
    }

    // Update bottom bar background color with 50% opacity
    if (bottom_bar_ != nullptr) {
        lv_obj_set_style_bg_opa(bottom_bar_, LV_OPA_50, 0);
        lv_obj_set_style_bg_color(bottom_bar_, lvgl_theme->background_color(), 0);
    }
#endif

    // Update low battery popup
    lv_obj_set_style_bg_color(low_battery_popup_, lvgl_theme->low_battery_color(), 0);

    // No errors occurred. Save theme to settings
    Display::SetTheme(lvgl_theme);
}

void LcdDisplay::SetHideSubtitle(bool hide) {
    DisplayLockGuard lock(this);
    hide_subtitle_ = hide;

    // Immediately update UI visibility based on the setting
    if (bottom_bar_ != nullptr) {
        if (hide) {
            lv_obj_add_flag(bottom_bar_, LV_OBJ_FLAG_HIDDEN);
        } else {
            // Only show if there is actual content to display
            const char* text =
                (chat_message_label_ != nullptr) ? lv_label_get_text(chat_message_label_) : nullptr;
            if (text != nullptr && text[0] != '\0') {
                lv_obj_remove_flag(bottom_bar_, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
}

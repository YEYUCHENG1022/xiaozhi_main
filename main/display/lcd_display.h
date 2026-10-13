#ifndef LCD_DISPLAY_H
#define LCD_DISPLAY_H

#include "gif/lvgl_gif.h"
#include "lvgl_display.h"

#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <font_emoji.h>
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#define PREVIEW_IMAGE_DURATION_MS 5000

enum class StoryHistoryStatus {
    Unknown,
    Completed,
    Unfinished,
};

struct StoryHistoryItem {
    std::string story_id;
    std::string title;
    std::string summary;
    std::string updated_at;
    int scene_no = 0;
    StoryHistoryStatus status = StoryHistoryStatus::Unknown;
};

class LcdDisplay : public LvglDisplay {
protected:
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;

    lv_draw_buf_t draw_buf_;
    lv_obj_t* top_bar_ = nullptr;
    lv_obj_t* status_bar_ = nullptr;
    lv_obj_t* content_ = nullptr;
    lv_obj_t* container_ = nullptr;
    lv_obj_t* side_bar_ = nullptr;
    lv_obj_t* bottom_bar_ = nullptr;
    lv_obj_t* preview_image_ = nullptr;
    lv_obj_t* emoji_label_ = nullptr;
    lv_obj_t* emoji_image_ = nullptr;
    std::unique_ptr<LvglGif> gif_controller_ = nullptr;
    lv_obj_t* emoji_box_ = nullptr;
    lv_obj_t* chat_message_label_ = nullptr;
    esp_timer_handle_t preview_timer_ = nullptr;
    std::unique_ptr<LvglImage> preview_image_cached_ = nullptr;
    bool hide_subtitle_ = false;  // Control whether to hide chat messages/subtitles
    std::function<void(int)> story_choice_callback_;
    lv_obj_t* story_container_ = nullptr;
    lv_obj_t* story_button_a_ = nullptr;
    lv_obj_t* story_button_b_ = nullptr;
    lv_obj_t* story_menu_container_ = nullptr;
    lv_obj_t* conversation_expression_animation_ = nullptr;
    int conversation_expression_kind_ = 0;
    lv_obj_t* story_nav_container_ = nullptr;
    lv_obj_t* story_playback_animation_ = nullptr;
    std::function<void()> story_enter_callback_;
    std::function<void()> story_theater_callback_;
    std::function<void()> story_back_callback_;
    std::function<void()> story_home_callback_;
    std::function<void()> story_start_callback_;
    std::function<void()> story_resume_callback_;
    std::function<void(size_t)> story_history_callback_;
    std::function<void(bool)> story_confirmation_callback_;
    std::vector<lv_obj_t*> story_menu_buttons_;

    void InitializeLcdThemes();
    void ShowStoryPlaybackAnimation();
    void HideStoryPlaybackAnimation();
    virtual bool Lock(int timeout_ms = 0) override;
    virtual void Unlock() override;

protected:
    // Add protected constructor
    LcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width,
               int height);

public:
    ~LcdDisplay();
    virtual void SetStatus(const char* status) override;
    virtual void SetEmotion(const char* emotion) override;
    // 仅更新日常对话首页的积极、平静或消极动画，不影响听书和反诈播放页。
    virtual void SetConversationEmotion(const char* emotion) override;
    virtual void SetChatMessage(const char* role, const char* content) override;
    virtual void ClearChatMessages() override;
    virtual void SetPreviewImage(std::unique_ptr<LvglImage> image) override;
    virtual void SetupUI() override;
    // Add theme switching function
    virtual void SetTheme(Theme* theme) override;

    void ShowStory(const std::string& page_title, const std::string& question,
                   const std::string& option_a, const std::string& option_b,
                   std::function<void(int)> choice_callback);
    void HideStory();
    void SetStoryButtonsEnabled(bool enabled);
    void ShowStoryHome(std::function<void()> enter_callback,
                       std::function<void()> theater_callback);
    void ShowStoryLibrary(const std::vector<StoryHistoryItem>& stories);

    // 显示点击 AI 听书后、等待后端 home_result 的加载页面。
    void ShowStoryLoading(const std::string& message);

    // 根据后端 home_result 显示故事首页，并绑定新故事、续听和历史记录按钮。
    void ShowStoryPicker(const std::vector<StoryHistoryItem>& stories, bool last_story_unfinished,
                         const std::string& last_story_title, std::function<void()> start_callback,
                         std::function<void()> resume_callback,
                         std::function<void(size_t)> history_callback);

    // 根据后端 show_confirmation 显示“是/否”确认弹窗。
    void ShowStoryConfirmation(const std::string& title, const std::string& prompt,
                               std::function<void(bool)> confirmation_callback);

    // 启用或禁用故事首页、确认弹窗中的业务按钮，防止重复点击。
    void SetStoryMenuButtonsEnabled(bool enabled);

    void ShowStoryNavigation(std::function<void()> back_callback,
                             std::function<void()> home_callback, bool back_enabled = true);
    void HideStoryNavigation();
    void HideStoryMenu();

    // Set whether to hide chat messages/subtitles
    void SetHideSubtitle(bool hide);
};

// SPI LCD display
class SpiLcdDisplay : public LcdDisplay {
public:
    SpiLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width,
                  int height, int offset_x, int offset_y, bool mirror_x, bool mirror_y,
                  bool swap_xy);
};

// RGB LCD display
class RgbLcdDisplay : public LcdDisplay {
public:
    RgbLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width,
                  int height, int offset_x, int offset_y, bool mirror_x, bool mirror_y,
                  bool swap_xy);
};

// MIPI LCD display
class MipiLcdDisplay : public LcdDisplay {
public:
    MipiLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width,
                   int height, int offset_x, int offset_y, bool mirror_x, bool mirror_y,
                   bool swap_xy);
};

#endif  // LCD_DISPLAY_H

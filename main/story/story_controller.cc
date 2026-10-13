#include "story_controller.h"

#include <esp_log.h>
#include <esp_random.h>
#include <cJSON.h>

#include <algorithm>

#include "application.h"
#include "board.h"
#include "display/lcd_display.h"

#define TAG "StoryController"

StoryController& StoryController::GetInstance() {
    static StoryController instance;
    return instance;
}

StoryController::StoryController() { ESP_LOGI(TAG, "StoryController created"); }

void StoryController::Initialize() {
    if (initialized_) {
        return;
    }

    story_ui_ = std::make_unique<StoryUi>();
    story_ui_->SetChoiceCallback([this](int choice_index) { OnChoiceSelected(choice_index); });
    initialized_ = true;
    ESP_LOGI(TAG, "StoryController initialized");
}

void StoryController::ShowStoryOptions(const std::string& request_id, const std::string& session_id,
                                       const std::string& node_id, int step_no, int step_count,
                                       const std::string& title, const std::string& prompt,
                                       const std::string& option_a, const std::string& option_b) {
    if (!story_mode_active_) {
        ESP_LOGW(TAG, "Ignoring show_options after leaving interactive story");
        return;
    }
    if (active_story_session_id_.empty() || session_id != active_story_session_id_) {
        ESP_LOGW(TAG, "Ignoring show_options from another session: %s", session_id.c_str());
        return;
    }
    // show_options 是场景播放完成后由服务端主动推送的页面事件。
    // request_id 只在它恰好对应当前本地操作时收敛 busy，页面身份仍以 session_id + node_id 为准。
    if (interaction_busy_ && request_id == interaction_request_id_) {
        MarkInteractionReady(request_id);
    }
    ShowOptions(Experience::Story, request_id, session_id, node_id, step_no, step_count, title,
                prompt, option_a, option_b);
}

void StoryController::ShowAntiFraudOptions(const std::string& session_id,
                                           const std::string& node_id, int step_no, int step_count,
                                           const std::string& title, const std::string& prompt,
                                           const std::string& option_a,
                                           const std::string& option_b) {
    if (!initialized_ || current_experience_ != Experience::AntiFraud ||
        current_page_ == Page::Home) {
        ESP_LOGW(TAG, "Ignoring show_options outside active anti-fraud theater");
        return;
    }
    if (!active_story_session_id_.empty() && session_id != active_story_session_id_) {
        ESP_LOGW(TAG, "Ignoring anti-fraud show_options from another session: %s",
                 session_id.c_str());
        return;
    }
    ShowOptions(Experience::AntiFraud, "", session_id, node_id, step_no, step_count, title, prompt,
                option_a, option_b);
}

void StoryController::HandleAntiFraudCompleted(const std::string& request_id,
                                               const std::string& session_id) {
    if (!initialized_ || current_experience_ != Experience::AntiFraud ||
        current_page_ == Page::Home) {
        ESP_LOGW(TAG, "Ignoring anti_fraud completed outside active theater");
        return;
    }
    if (!active_story_session_id_.empty() && active_story_session_id_ != session_id) {
        ESP_LOGW(TAG, "Ignoring anti_fraud completed from another session: %s", session_id.c_str());
        return;
    }

    active_story_session_id_ = session_id;
    anti_fraud_completion_pending_ = true;
    anti_fraud_completion_session_id_ = session_id;
    ESP_LOGI(TAG, "Anti-fraud completed: request_id=%s session_id=%s", request_id.c_str(),
             session_id.c_str());

    // completed 可能晚于最后一次 TTS stop；此时立即停止已经启动的自动聆听。
    auto& application = Application::GetInstance();
    if (application.GetDeviceState() != kDeviceStateSpeaking) {
        if (application.GetDeviceState() == kDeviceStateListening) {
            application.StopListening();
        } else {
            application.SetDeviceState(kDeviceStateIdle);
        }
        FinishAntiFraudAndReturnHome();
    }
}

void StoryController::ShowOptions(Experience experience, const std::string& request_id,
                                  const std::string& session_id, const std::string& node_id,
                                  int step_no, int step_count, const std::string& title,
                                  const std::string& prompt, const std::string& option_a,
                                  const std::string& option_b) {
    if (!initialized_ || story_ui_ == nullptr) {
        ESP_LOGE(TAG, "ShowOptions failed: controller is not initialized");
        return;
    }
    if (experience == Experience::Story && !story_mode_active_) {
        ESP_LOGW(TAG, "Ignoring show_options after leaving interactive story");
        return;
    }
    if (experience == Experience::AntiFraud &&
        (current_experience_ != Experience::AntiFraud || current_page_ == Page::Home)) {
        ESP_LOGW(TAG, "Ignoring show_options after leaving anti-fraud theater");
        return;
    }
    if (session_id.empty() || node_id.empty() || option_a.empty() || option_b.empty()) {
        ESP_LOGE(TAG, "ShowOptions failed: required field is empty");
        return;
    }
    if ((session_id == completed_session_id_ && node_id == completed_node_id_) ||
        (session_id == current_session_id_ && node_id == current_node_id_ && choices_visible_)) {
        ESP_LOGW(TAG, "Ignoring duplicate show_options: session_id=%s node_id=%s",
                 session_id.c_str(), node_id.c_str());
        return;
    }

    current_experience_ = experience;
    current_session_id_ = session_id;
    active_story_session_id_ = session_id;
    current_node_id_ = node_id;
    current_step_no_ = step_no;
    choice_pending_ = true;
    choices_visible_ = false;

    std::string display_title = title;
    if (step_count > 0) {
        display_title += "  " + std::to_string(step_no) + "/" + std::to_string(step_count);
    }
    story_ui_->SetStory(session_id, node_id, display_title, prompt, option_a, option_b);
    if (Application::GetInstance().GetDeviceState() == kDeviceStateSpeaking) {
        ESP_LOGI(TAG, "Deferring options until the current TTS round stops");
        return;
    }
    DisplayPendingChoices();
}

void StoryController::ShowPendingChoices() { DisplayPendingChoices(); }

void StoryController::DisplayPendingChoices() {
    if (!choice_pending_ || choices_visible_ || story_ui_ == nullptr) {
        return;
    }

    auto* display = dynamic_cast<LcdDisplay*>(Board::GetInstance().GetDisplay());
    if (display == nullptr) {
        ESP_LOGW(TAG, "The current display does not support option pages");
        return;
    }

    display->ShowStory(story_ui_->GetTitle(), story_ui_->GetContent(), story_ui_->GetChoiceA(),
                       story_ui_->GetChoiceB(), [](int choice_index) {
                           Application::GetInstance().Schedule([choice_index]() {
                               StoryController::GetInstance().SelectChoice(choice_index);
                           });
                       });
    current_page_ = Page::Choice;
    ConfigureNavigation(true);
    choices_visible_ = true;
    ESP_LOGI(TAG, "Options displayed: session_id=%s node_id=%s step_no=%d",
             current_session_id_.c_str(), current_node_id_.c_str(), current_step_no_);
}

void StoryController::ClearChoices(const std::string& request_id, const std::string& session_id,
                                   const std::string& node_id) {
    if (!initialized_) {
        return;
    }
    // clear_options 可能由按钮或语音选择触发，只按当前页面复合键清理。
    if ((!session_id.empty() && session_id != current_session_id_) ||
        (!node_id.empty() && node_id != current_node_id_)) {
        ESP_LOGW(TAG, "Ignoring stale clear_options message");
        return;
    }

    completed_session_id_ = current_session_id_;
    completed_node_id_ = current_node_id_;
    // 匹配节点已被服务端清理，说明点击选择已生效，立即释放音频互斥门。
    if (interaction_busy_ && interaction_action_ == "select_option") {
        CompleteInteraction(interaction_request_id_);
    }
    ClearChoices();
}

void StoryController::ClearChoices() {
    if (current_page_ == Page::Choice) {
        current_page_ = Page::Playing;
    }
    choice_pending_ = false;
    choices_visible_ = false;
    current_session_id_.clear();
    current_node_id_.clear();
    current_step_no_ = 0;

    auto* display = dynamic_cast<LcdDisplay*>(Board::GetInstance().GetDisplay());
    if (display != nullptr) {
        display->HideStory();
    }
}

void StoryController::OnChoiceSelected(int choice_index) {
    if (!initialized_ || story_ui_ == nullptr || !choice_pending_) {
        ESP_LOGW(TAG, "No pending option");
        return;
    }
    if (choice_index != 0 && choice_index != 1) {
        ESP_LOGE(TAG, "Invalid choice index: %d", choice_index);
        return;
    }

    choice_pending_ = false;
    completed_session_id_ = current_session_id_;
    completed_node_id_ = current_node_id_;
    if (auto* display = dynamic_cast<LcdDisplay*>(Board::GetInstance().GetDisplay())) {
        display->SetStoryButtonsEnabled(false);
    }

    cJSON* data = cJSON_CreateObject();
    if (data == nullptr) {
        choice_pending_ = true;
        if (auto* display = dynamic_cast<LcdDisplay*>(Board::GetInstance().GetDisplay())) {
            display->SetStoryButtonsEnabled(true);
        }
        ESP_LOGE(TAG, "Failed to allocate select_option JSON");
        return;
    }
    cJSON_AddStringToObject(data, "session_id", current_session_id_.c_str());
    cJSON_AddStringToObject(data, "node_id", current_node_id_.c_str());
    cJSON_AddStringToObject(data, "option_id", choice_index == 0 ? "A" : "B");

    if (current_experience_ == Experience::AntiFraud) {
        cJSON* root = cJSON_CreateObject();
        if (root == nullptr) {
            cJSON_Delete(data);
            choice_pending_ = true;
            return;
        }
        cJSON_AddStringToObject(root, "type", "anti_fraud");
        cJSON_AddNumberToObject(root, "version", 2);
        cJSON_AddStringToObject(root, "action", "select_option");
        cJSON_AddItemToObject(root, "data", data);
        char* payload = cJSON_PrintUnformatted(root);
        if (payload != nullptr) {
            Application::GetInstance().SendTextMessage(payload);
            cJSON_free(payload);
        }
        cJSON_Delete(root);
        return;
    }

    const std::string request_id = NextRequestId();
    const bool begun = BeginInteraction("select_option", request_id);
    const bool sent = begun && SendStoryMessage("select_option", request_id, data);
    if (!sent) {
        if (!begun) {
            cJSON_Delete(data);
            return;
        } else {
            HandleInteractionInterrupted();
        }
        choice_pending_ = true;
        completed_session_id_.clear();
        completed_node_id_.clear();
        if (auto* display = dynamic_cast<LcdDisplay*>(Board::GetInstance().GetDisplay())) {
            display->SetStoryButtonsEnabled(true);
        }
    }
}

void StoryController::SelectChoice(int choice_index) {
    if (!initialized_ || story_ui_ == nullptr) {
        ESP_LOGE(TAG, "SelectChoice failed: controller is not initialized");
        return;
    }
    story_ui_->SelectChoice(choice_index);
}

void StoryController::ShowHome() {
    if (!initialized_) {
        return;
    }
    auto* display = dynamic_cast<LcdDisplay*>(Board::GetInstance().GetDisplay());
    if (display == nullptr) {
        return;
    }
    display->ShowStoryHome(
        []() {
            Application::GetInstance().Schedule(
                []() { StoryController::GetInstance().EnterStoryMode(); });
        },
        []() {
            Application::GetInstance().Schedule(
                []() { StoryController::GetInstance().EnterAntiFraudMode(); });
        });
    story_mode_active_ = false;
    current_page_ = Page::Home;
    ConfigureNavigation(false);
}

void StoryController::ShowLibrary(const std::vector<StoryHistoryItem>& stories) {
    if (!initialized_) {
        return;
    }
    current_experience_ = Experience::Story;
    story_history_ = stories;
    auto* display = dynamic_cast<LcdDisplay*>(Board::GetInstance().GetDisplay());
    if (display == nullptr) {
        return;
    }
    display->ShowStoryLibrary(story_history_);
    current_page_ = Page::Library;
    ConfigureNavigation(true);
}

// 处理新版 home_result，并用后端返回的历史和续听状态更新页面。
void StoryController::ShowHomeResult(const std::string& request_id,
                                     const std::vector<StoryHistoryItem>& stories,
                                     bool continue_available,
                                     const std::string& continue_session_id,
                                     const std::string& continue_title) {
    if (!pending_enter_request_id_.empty() && request_id != pending_enter_request_id_) {
        ESP_LOGW(TAG, "Ignoring stale home_result: request_id=%s", request_id.c_str());
        return;
    }
    if (request_id.empty()) {
        ESP_LOGE(TAG, "home_result requires request_id");
        return;
    }
    if (!AcceptInteractionResponse(request_id) || !MarkInteractionReady(request_id)) {
        ESP_LOGW(TAG, "Ignoring completed home_result: request_id=%s", request_id.c_str());
        return;
    }
    pending_enter_request_id_.clear();
    current_experience_ = Experience::Story;
    story_mode_active_ = true;
    story_history_ = stories;
    continue_available_ = continue_available;
    continue_session_id_ = continue_session_id;
    continue_title_ = continue_title;
    RenderStoryPicker();
}

// 处理旧流程 history_result，保留兼容但新版入口不会主动发送对应查询。
void StoryController::ShowHistoryResult(const std::string& request_id,
                                        const std::vector<StoryHistoryItem>& stories) {
    if (request_id.empty() || request_id != pending_history_request_id_) {
        ESP_LOGW(TAG, "Ignoring stale history_result: request_id=%s", request_id.c_str());
        return;
    }
    pending_history_request_id_.clear();
    story_history_ = stories;
    if (current_page_ == Page::Library && current_experience_ == Experience::Story) {
        ShowLibrary(story_history_);
    }
}

// 处理旧流程 resume_result，保留兼容但不绕过新版确认流程。
void StoryController::ShowResumeResult(const std::string& request_id, bool available,
                                       const std::string& session_id) {
    if (request_id.empty() || request_id != pending_resume_request_id_) {
        ESP_LOGW(TAG, "Ignoring stale resume_result: request_id=%s", request_id.c_str());
        return;
    }
    pending_resume_request_id_.clear();
    ESP_LOGI(TAG, "Compatibility resume_result: available=%d session_id=%s", available,
             session_id.c_str());
}

// 按后端给出的复合键和文案显示确认弹窗，按钮只负责回传结果。
void StoryController::ShowConfirmation(const std::string& request_id, const std::string& session_id,
                                       const std::string& confirmation_id,
                                       const std::string& purpose, const std::string& title,
                                       const std::string& prompt) {
    if (session_id.empty() || confirmation_id.empty()) {
        ESP_LOGE(TAG, "show_confirmation requires composite key");
        return;
    }
    if (!active_story_session_id_.empty() && session_id != "interactive_story_home" &&
        session_id != active_story_session_id_) {
        ESP_LOGW(TAG, "Ignoring show_confirmation from another session: %s", session_id.c_str());
        return;
    }
    if (!story_mode_active_ && !force_home_exit_pending_) {
        ESP_LOGW(TAG, "Ignoring show_confirmation outside interactive story");
        return;
    }
    // 确认页也可能由服务端状态机主动推送；仅匹配当前本地请求时更新请求门。
    if (interaction_busy_ && request_id == interaction_request_id_) {
        MarkInteractionReady(request_id);
    }

    // Home 已经立即返回首页时，后台自动确认服务端随后下发的退出弹窗。
    if (force_home_exit_pending_ && purpose == "exit_confirm" &&
        (force_home_exit_session_id_.empty() || session_id == force_home_exit_session_id_)) {
        confirmation_session_id_ = session_id;
        confirmation_id_ = confirmation_id;
        confirmation_purpose_ = purpose;
        confirmation_response_sent_ = false;
        confirmation_response_value_ = true;

        cJSON* data = cJSON_CreateObject();
        if (data != nullptr) {
            cJSON_AddStringToObject(data, "session_id", session_id.c_str());
            cJSON_AddStringToObject(data, "confirmation_id", confirmation_id.c_str());
            cJSON_AddBoolToObject(data, "confirmed", true);
        }
        const std::string confirm_request_id = NextRequestId();
        const bool begun = BeginInteraction("confirm", confirm_request_id);
        const bool sent = begun && SendStoryMessage("confirm", confirm_request_id, data);
        if (sent) {
            confirmation_response_sent_ = true;
        } else {
            if (!begun) {
                cJSON_Delete(data);
                return;
            }
            HandleInteractionInterrupted();
        }
        return;
    }
    if (current_page_ != Page::Confirmation) {
        page_before_confirmation_ = current_page_;
    }
    confirmation_session_id_ = session_id;
    confirmation_id_ = confirmation_id;
    confirmation_purpose_ = purpose;
    confirmation_response_sent_ = false;
    confirmation_response_value_ = false;
    if (session_id != "interactive_story_home") {
        active_story_session_id_ = session_id;
    }

    auto* display = dynamic_cast<LcdDisplay*>(Board::GetInstance().GetDisplay());
    if (display == nullptr) {
        return;
    }
    display->ShowStoryConfirmation(title, prompt, [](bool confirmed) {
        Application::GetInstance().Schedule(
            [confirmed]() { StoryController::GetInstance().ConfirmCurrent(confirmed); });
    });
    current_page_ = Page::Confirmation;
    ConfigureNavigation(false);
}

// 仅清理当前匹配的确认弹窗，并按本次本地点击结果恢复对应页面。
void StoryController::ClearConfirmation(const std::string& request_id,
                                        const std::string& session_id,
                                        const std::string& confirmation_id) {
    // clear_confirmation 的页面归属由 session_id + confirmation_id 决定，兼容语音确认。
    if (force_home_exit_pending_) {
        if (session_id != confirmation_session_id_ || confirmation_id != confirmation_id_) {
            ESP_LOGW(TAG, "Ignoring stale forced-home clear_confirmation");
            return;
        }
        force_home_exit_pending_ = false;
        force_home_exit_session_id_.clear();
        confirmation_session_id_.clear();
        confirmation_id_.clear();
        confirmation_purpose_.clear();
        confirmation_response_sent_ = false;
        confirmation_response_value_ = false;
        ESP_LOGI(TAG, "Forced Home exit synchronized with server");
        return;
    }
    if (current_page_ != Page::Confirmation || session_id != confirmation_session_id_ ||
        confirmation_id != confirmation_id_) {
        ESP_LOGW(TAG, "Ignoring stale clear_confirmation");
        return;
    }

    // 匹配确认框已被服务端清理，说明 confirm 已生效，不再等待缺少请求号的 done。
    if (interaction_busy_ && interaction_action_ == "confirm") {
        CompleteInteraction(interaction_request_id_);
    }

    const std::string purpose = confirmation_purpose_;
    const bool response_sent = confirmation_response_sent_;
    const bool confirmed = confirmation_response_value_;
    const Page restore_page = page_before_confirmation_;
    confirmation_session_id_.clear();
    confirmation_id_.clear();
    confirmation_purpose_.clear();
    confirmation_response_sent_ = false;

    if (response_sent && confirmed) {
        if (purpose == "exit_confirm") {
            ClearChoices();
            active_story_session_id_.clear();
            Application::GetInstance().SetDeviceState(kDeviceStateIdle);
            ShowHome();
        } else {
            EnterStoryPlayback();
        }
        return;
    }

    if (restore_page == Page::Loading) {
        if (auto* display = dynamic_cast<LcdDisplay*>(Board::GetInstance().GetDisplay())) {
            display->ShowStoryLoading("正在打开互动听书…");
        }
        current_page_ = Page::Loading;
        ConfigureNavigation(true);
    } else if (restore_page == Page::Picker || restore_page == Page::Library) {
        RenderStoryPicker();
    } else if (restore_page == Page::Choice && choice_pending_) {
        choices_visible_ = false;
        DisplayPendingChoices();
    } else {
        EnterStoryPlayback();
    }
    if (interaction_busy_) {
        if (auto* display = dynamic_cast<LcdDisplay*>(Board::GetInstance().GetDisplay())) {
            if (current_page_ == Page::Choice) {
                display->SetStoryButtonsEnabled(false);
            } else if (current_page_ == Page::Picker || current_page_ == Page::Library) {
                display->SetStoryMenuButtonsEnabled(false);
            }
        }
    }
}

void StoryController::HideMenu() {
    auto* display = dynamic_cast<LcdDisplay*>(Board::GetInstance().GetDisplay());
    if (display != nullptr) {
        display->HideStoryMenu();
    }
}

// 点击 AI 听书后只发送 enter；页面等待对应 home_result 后再展示内容。
void StoryController::EnterStoryMode() {
    if (interaction_busy_) {
        return;
    }
    Application::GetInstance().PrepareInteractiveStoryConversation();
    current_experience_ = Experience::Story;
    story_mode_active_ = true;
    // 新一轮入口尚未由后端分配故事会话，不能沿用上一次的 session_id。
    active_story_session_id_.clear();
    ClearChoices();
    auto* display = dynamic_cast<LcdDisplay*>(Board::GetInstance().GetDisplay());
    if (display != nullptr) {
        display->ShowStoryLoading("正在打开互动听书…");
    }
    current_page_ = Page::Loading;
    ConfigureNavigation(true);
    SendStoryEnter();
}

void StoryController::EnterAntiFraudMode() {
    if (interaction_busy_) {
        return;
    }
    current_experience_ = Experience::AntiFraud;
    story_mode_active_ = false;
    // 小剧场首个 show_options 建立 session；入口先清掉上一场景的归属。
    active_story_session_id_.clear();
    ClearChoices();
    HideMenu();
    current_page_ = Page::Playing;
    ConfigureNavigation(true);
    SendAntiFraudStart();
}

// 使用最近一次 home_result 的快照渲染可点击的故事首页。
void StoryController::RenderStoryPicker() {
    auto* display = dynamic_cast<LcdDisplay*>(Board::GetInstance().GetDisplay());
    if (display == nullptr) {
        return;
    }
    display->ShowStoryPicker(
        story_history_, continue_available_, continue_title_,
        []() {
            Application::GetInstance().Schedule(
                []() { StoryController::GetInstance().StartNewStory(); });
        },
        []() {
            Application::GetInstance().Schedule(
                []() { StoryController::GetInstance().ResumeLastStory(); });
        },
        [](size_t index) {
            Application::GetInstance().Schedule(
                [index]() { StoryController::GetInstance().OpenHistory(index); });
        });
    current_page_ = Page::Picker;
    ConfigureNavigation(true);
}

// 点击新故事只发送 start，等待后端 TTS 和 show_confirmation 推进业务。
void StoryController::StartNewStory() {
    if (current_page_ != Page::Picker) {
        return;
    }
    SendStoryStart();
}

// 点击续听只发送 home_result 中 continue_play 对应的 session_id。
void StoryController::ResumeLastStory() {
    if (current_page_ != Page::Picker || !continue_available_ || continue_session_id_.empty()) {
        if (auto* display = dynamic_cast<LcdDisplay*>(Board::GetInstance().GetDisplay())) {
            display->SetStoryMenuButtonsEnabled(true);
        }
        return;
    }
    SendStoryResume(continue_session_id_);
}

// 点击历史条目只发送 open_history，确认弹窗必须由后端下行创建。
void StoryController::OpenHistory(size_t index) {
    if (current_page_ != Page::Picker || index >= story_history_.size() ||
        story_history_[index].story_id.empty()) {
        if (auto* display = dynamic_cast<LcdDisplay*>(Board::GetInstance().GetDisplay())) {
            display->SetStoryMenuButtonsEnabled(true);
        }
        return;
    }
    SendStoryOpenHistory(story_history_[index].story_id);
}

// 点击“是/否”时发送布尔 confirmed，并保持弹窗直到后端清理。
void StoryController::ConfirmCurrent(bool confirmed) {
    if (current_page_ != Page::Confirmation || confirmation_id_.empty() ||
        confirmation_response_sent_) {
        return;
    }
    cJSON* data = cJSON_CreateObject();
    if (data != nullptr) {
        cJSON_AddStringToObject(data, "session_id", confirmation_session_id_.c_str());
        cJSON_AddStringToObject(data, "confirmation_id", confirmation_id_.c_str());
        cJSON_AddBoolToObject(data, "confirmed", confirmed);
    }
    const std::string request_id = NextRequestId();
    const bool begun = BeginInteraction("confirm", request_id);
    const bool sent = begun && SendStoryMessage("confirm", request_id, data);
    if (sent) {
        confirmation_response_sent_ = true;
        confirmation_response_value_ = confirmed;
    } else {
        if (!begun) {
            cJSON_Delete(data);
            return;
        }
        HandleInteractionInterrupted();
        if (auto* display = dynamic_cast<LcdDisplay*>(Board::GetInstance().GetDisplay())) {
            display->SetStoryMenuButtonsEnabled(true);
        }
    }
}

// 故事模块内返回和 Home 都统一请求 exit，不在本地提前销毁页面状态。
void StoryController::RequestStoryExit() {
    if (current_page_ == Page::Home || current_page_ == Page::Confirmation) {
        return;
    }
    SendStoryExit();
}

// Home 键立即停止播报并回功能首页，互动听书退出确认在后台自动完成。
void StoryController::ForceReturnToFeatureHome() {
    if (current_page_ == Page::Home) {
        return;
    }

    Application::GetInstance().AbortSpeaking(kAbortReasonNone);
    Application::GetInstance().SetDeviceState(kDeviceStateIdle);
    if (current_experience_ == Experience::Story && story_mode_active_) {
        force_home_exit_pending_ = true;
        force_home_exit_session_id_ =
            active_story_session_id_.empty() ? "interactive_story_home" : active_story_session_id_;
        SendStoryExit();
        // Home 已经本地返回功能首页，后台 exit 不能继续占用入口交互门。
        HandleInteractionInterrupted();
    } else if (current_experience_ == Experience::AntiFraud) {
        if (!active_story_session_id_.empty()) {
            SendAntiFraudExit(active_story_session_id_);
        } else {
            ESP_LOGW(TAG, "Cannot send anti_fraud exit before receiving a session_id");
        }
    }
    ResetStoryRuntimeState();
    ShowHome();
}

// 清理活动故事的页面复合键和待处理请求，保留历史列表供下次进入复用。
void StoryController::ResetStoryRuntimeState() {
    choice_pending_ = false;
    choices_visible_ = false;
    current_session_id_.clear();
    active_story_session_id_.clear();
    current_node_id_.clear();
    completed_session_id_.clear();
    completed_node_id_.clear();
    current_step_no_ = 0;
    pending_enter_request_id_.clear();
    pending_history_request_id_.clear();
    pending_resume_request_id_.clear();
    confirmation_session_id_.clear();
    confirmation_id_.clear();
    confirmation_purpose_.clear();
    confirmation_response_sent_ = false;
    confirmation_response_value_ = false;
    story_mode_active_ = false;
    anti_fraud_completion_pending_ = false;
    anti_fraud_completion_session_id_.clear();

    if (auto* display = dynamic_cast<LcdDisplay*>(Board::GetInstance().GetDisplay())) {
        display->HideStory();
    }
}

void StoryController::FinishAntiFraudAndReturnHome() {
    ESP_LOGI(TAG, "Anti-fraud playback finished; returning to feature home");
    ResetStoryRuntimeState();
    ShowHome();
}

// 切换到播放外观；正文仍由既有 TTS/音频链路驱动。
void StoryController::EnterStoryPlayback() {
    ClearChoices();
    HideMenu();
    current_page_ = Page::Playing;
    ConfigureNavigation(true);
}

void StoryController::ConfigureNavigation(bool back_enabled) {
    auto* display = dynamic_cast<LcdDisplay*>(Board::GetInstance().GetDisplay());
    if (display == nullptr) {
        return;
    }
    display->ShowStoryNavigation(
        []() {
            Application::GetInstance().Schedule(
                []() { StoryController::GetInstance().NavigateBack(); });
        },
        []() {
            Application::GetInstance().Schedule(
                []() { StoryController::GetInstance().NavigateHome(); });
        },
        back_enabled);
}

void StoryController::NavigateBack() {
    if (current_page_ == Page::Home) {
        return;
    }
    if (current_experience_ == Experience::AntiFraud) {
        Application::GetInstance().AbortSpeaking(kAbortReasonNone);
        Application::GetInstance().SetDeviceState(kDeviceStateIdle);
        ClearChoices();
        ShowHome();
        return;
    }
    RequestStoryExit();
}

void StoryController::NavigateHome() { ForceReturnToFeatureHome(); }

std::string StoryController::NextRequestId() {
    uint8_t random_bytes[16];
    esp_fill_random(random_bytes, sizeof(random_bytes));
    static constexpr char kHex[] = "0123456789abcdef";
    std::string request_id(32, '0');
    for (size_t i = 0; i < sizeof(random_bytes); ++i) {
        request_id[i * 2] = kHex[random_bytes[i] >> 4];
        request_id[i * 2 + 1] = kHex[random_bytes[i] & 0x0f];
    }
    return request_id;
}

// 统一组装 interactive_story V2 信封；函数接管 data 的生命周期。
bool StoryController::SendStoryMessage(const char* action, const std::string& request_id,
                                       cJSON* data) {
    if (data == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate %s data", action);
        return false;
    }
    cJSON* root = cJSON_CreateObject();
    if (root == nullptr) {
        cJSON_Delete(data);
        return false;
    }
    cJSON_AddStringToObject(root, "type", "interactive_story");
    cJSON_AddNumberToObject(root, "version", 2);
    cJSON_AddStringToObject(root, "action", action);
    cJSON_AddStringToObject(root, "request_id", request_id.c_str());
    cJSON_AddItemToObject(root, "data", data);

    bool sent = false;
    char* payload = cJSON_PrintUnformatted(root);
    if (payload != nullptr) {
        Application::GetInstance().SendTextMessage(payload);
        cJSON_free(payload);
        sent = true;
    } else {
        ESP_LOGE(TAG, "Failed to serialize %s JSON", action);
    }
    cJSON_Delete(root);
    return sent;
}

// 为按钮业务请求建立互斥门，并丢弃点击前尚未完成的录音。
bool StoryController::BeginInteraction(const char* action, const std::string& request_id) {
    if (interaction_busy_) {
        ESP_LOGW(TAG, "Ignoring local %s while request %s is pending", action,
                 interaction_request_id_.c_str());
        return false;
    }
    interaction_busy_ = true;
    interaction_request_id_ = request_id;
    interaction_action_ = action;
    interaction_origin_page_ = current_page_;
    RemoveReadyRequest(request_id);
    completed_request_ids_.erase(
        std::remove(completed_request_ids_.begin(), completed_request_ids_.end(), request_id),
        completed_request_ids_.end());
    Application::GetInstance().SetInteractiveStoryAudioBlocked(true);
    return true;
}

// 校验下行是否属于当前请求，并拒绝已经完成请求的迟到消息。
bool StoryController::AcceptInteractionResponse(const std::string& request_id) const {
    if (request_id.empty() ||
        std::find(completed_request_ids_.begin(), completed_request_ids_.end(), request_id) !=
            completed_request_ids_.end()) {
        return false;
    }
    return !interaction_busy_ || request_id == interaction_request_id_;
}

// 完成指定 request_id；只在它正处于 busy 时解除音频与按钮互斥门。
bool StoryController::CompleteInteraction(const std::string& request_id) {
    const bool is_busy_request = interaction_busy_ && request_id == interaction_request_id_;
    const bool is_ready_request = IsReadyRequest(request_id);
    if (!is_busy_request && !is_ready_request) {
        return false;
    }
    if (is_busy_request) {
        interaction_busy_ = false;
        interaction_request_id_.clear();
        interaction_action_.clear();
        Application::GetInstance().SetInteractiveStoryAudioBlocked(false);
    }
    RemoveReadyRequest(request_id);
    RememberCompletedRequest(request_id);
    return true;
}

// 收到对应业务页面时进入 ready；允许下一次操作，但继续等待同 ID 的 done。
bool StoryController::MarkInteractionReady(const std::string& request_id) {
    if (!AcceptInteractionResponse(request_id)) {
        return false;
    }
    std::string action;
    Page origin_page = current_page_;
    if (interaction_busy_) {
        action = interaction_action_;
        origin_page = interaction_origin_page_;
        interaction_busy_ = false;
        interaction_request_id_.clear();
        interaction_action_.clear();
        Application::GetInstance().SetInteractiveStoryAudioBlocked(false);
    }
    if (!IsReadyRequest(request_id)) {
        if (ready_requests_.size() >= 8) {
            RememberCompletedRequest(ready_requests_.front().request_id);
            ready_requests_.erase(ready_requests_.begin());
        }
        ready_requests_.push_back({request_id, action, origin_page});
    }
    return true;
}

// 记录完成请求，最近八个请求的迟到消息都会被丢弃。
void StoryController::RememberCompletedRequest(const std::string& request_id) {
    if (request_id.empty()) {
        return;
    }
    if (std::find(completed_request_ids_.begin(), completed_request_ids_.end(), request_id) ==
        completed_request_ids_.end()) {
        if (completed_request_ids_.size() >= 8) {
            completed_request_ids_.erase(completed_request_ids_.begin());
        }
        completed_request_ids_.push_back(request_id);
    }
}

// 判断请求是否已经收到业务页面、正在等待 done。
bool StoryController::IsReadyRequest(const std::string& request_id) const {
    return std::any_of(ready_requests_.begin(), ready_requests_.end(),
                       [&request_id](const ReadyInteraction& request) {
                           return request.request_id == request_id;
                       });
}

// 查询 ready 请求原来的 action 和页面，供定向恢复使用。
bool StoryController::GetReadyRequest(const std::string& request_id, std::string* action,
                                      Page* origin_page) const {
    const auto it = std::find_if(ready_requests_.begin(), ready_requests_.end(),
                                 [&request_id](const ReadyInteraction& request) {
                                     return request.request_id == request_id;
                                 });
    if (it == ready_requests_.end()) {
        return false;
    }
    if (action != nullptr) {
        *action = it->action;
    }
    if (origin_page != nullptr) {
        *origin_page = it->origin_page;
    }
    return true;
}

// 从 ready 集合删除指定请求，不影响其他请求状态。
void StoryController::RemoveReadyRequest(const std::string& request_id) {
    ready_requests_.erase(std::remove_if(ready_requests_.begin(), ready_requests_.end(),
                                         [&request_id](const ReadyInteraction& request) {
                                             return request.request_id == request_id;
                                         }),
                          ready_requests_.end());
}

// 按业务请求发起前的页面恢复可点击状态。
void StoryController::RestoreInteractionButtons(Page origin_page) {
    auto* display = dynamic_cast<LcdDisplay*>(Board::GetInstance().GetDisplay());
    if (display == nullptr) {
        return;
    }
    if (origin_page == Page::Choice) {
        choice_pending_ = true;
        completed_session_id_.clear();
        completed_node_id_.clear();
        if (current_page_ != Page::Choice) {
            choices_visible_ = false;
            DisplayPendingChoices();
        }
        display->SetStoryButtonsEnabled(true);
    } else if (origin_page == Page::Picker) {
        RenderStoryPicker();
        display->SetStoryMenuButtonsEnabled(true);
    } else if (origin_page == Page::Library) {
        ShowLibrary(story_history_);
        display->SetStoryMenuButtonsEnabled(true);
    } else if (origin_page == Page::Confirmation) {
        confirmation_response_sent_ = false;
        display->SetStoryMenuButtonsEnabled(true);
    }
}

// 通用 abort 或断线会立即释放本地交互门。
void StoryController::HandleInteractionInterrupted() {
    if (interaction_busy_) {
        RestoreInteractionButtons(interaction_origin_page_);
        RememberCompletedRequest(interaction_request_id_);
    }
    interaction_busy_ = false;
    interaction_request_id_.clear();
    interaction_action_.clear();
    for (const auto& request : ready_requests_) {
        RememberCompletedRequest(request.request_id);
    }
    ready_requests_.clear();
    Application::GetInstance().SetInteractiveStoryAudioBlocked(false);
}

void StoryController::HandleConnectionClosed() {
    HandleInteractionInterrupted();
    ResetStoryRuntimeState();
    force_home_exit_pending_ = false;
    force_home_exit_session_id_.clear();
    completed_request_ids_.clear();
    ShowHome();
}

// 发送链路失败时释放交互门；入口请求失败则退出无法完成的加载页。
void StoryController::HandleTransportSendFailed() {
    HandleInteractionInterrupted();
    if (current_page_ == Page::Loading && current_experience_ == Experience::Story) {
        pending_enter_request_id_.clear();
        ResetStoryRuntimeState();
        ShowHome();
    } else if (current_page_ == Page::Playing && current_experience_ == Experience::AntiFraud) {
        ClearChoices();
        ShowHome();
    }
}

// 互动听书正文开始播报时兜底恢复播放页；选择页和确认页必须继续保留。
void StoryController::HandleStoryTtsStarted() {
    if (!initialized_ || !story_mode_active_ || current_experience_ != Experience::Story ||
        active_story_session_id_.empty() || force_home_exit_pending_) {
        return;
    }
    if (current_page_ != Page::Loading && current_page_ != Page::Playing) {
        return;
    }

    ESP_LOGI(TAG, "Story TTS started, ensuring playback page is visible");
    EnterStoryPlayback();
}

bool StoryController::FinishAntiFraudAfterTtsStop() {
    if (!anti_fraud_completion_pending_ || current_experience_ != Experience::AntiFraud ||
        current_page_ == Page::Home) {
        return false;
    }
    FinishAntiFraudAndReturnHome();
    return true;
}

// done 只表示本轮交互结束，不在本地推断故事是否结束。
void StoryController::HandleInteractionDone(const std::string& request_id) {
    const bool is_busy_request = interaction_busy_ && request_id == interaction_request_id_;
    const bool is_ready_request = IsReadyRequest(request_id);
    if (!is_busy_request && !is_ready_request) {
        ESP_LOGW(TAG, "Ignoring stale done: request_id=%s", request_id.c_str());
        return;
    }
    const Page origin = interaction_origin_page_;
    CompleteInteraction(request_id);
    if (is_busy_request && current_page_ == origin) {
        RestoreInteractionButtons(origin);
    }
}

// request_rejected 保留原页面并恢复本次操作锁定的按钮。
void StoryController::HandleRequestRejected(const std::string& request_id,
                                            const std::string& reason, const std::string& message) {
    const bool is_busy_request = interaction_busy_ && request_id == interaction_request_id_;
    std::string rejected_action;
    Page origin = Page::Home;
    const bool is_ready_request = GetReadyRequest(request_id, &rejected_action, &origin);
    if (!is_busy_request && !is_ready_request) {
        ESP_LOGW(TAG, "Ignoring stale request_rejected: request_id=%s", request_id.c_str());
        return;
    }
    ESP_LOGW(TAG, "Request rejected: action=%s reason=%s message=%s", interaction_action_.c_str(),
             reason.c_str(), message.c_str());
    if (is_busy_request) {
        rejected_action = interaction_action_;
        origin = interaction_origin_page_;
    }
    CompleteInteraction(request_id);
    if (rejected_action == "enter") {
        pending_enter_request_id_.clear();
        ResetStoryRuntimeState();
        ShowHome();
    } else {
        RestoreInteractionButtons(origin);
    }
    if (!message.empty()) {
        Board::GetInstance().GetDisplay()->ShowNotification(message.c_str(), 5000);
    }
}

// error 是终止事件：停止活动音频，清空全部活动投影，并回功能首页。
void StoryController::HandleError(const std::string& request_id, const std::string& code,
                                  const std::string& message) {
    ESP_LOGE(TAG, "Interactive story error: request_id=%s code=%s message=%s", request_id.c_str(),
             code.c_str(), message.c_str());

    auto& application = Application::GetInstance();
    if (application.GetDeviceState() == kDeviceStateListening) {
        application.StopListening();
    }
    application.AbortSpeaking(kAbortReasonNone);
    HandleInteractionInterrupted();

    ResetStoryRuntimeState();
    story_history_.clear();
    continue_available_ = false;
    continue_session_id_.clear();
    continue_title_.clear();
    force_home_exit_pending_ = false;
    force_home_exit_session_id_.clear();
    completed_request_ids_.clear();
    page_before_confirmation_ = Page::Home;

    application.SetDeviceState(kDeviceStateIdle);
    ShowHome();
}

// clear_home 只清理首页投影，不结束请求，也不据此认定故事已经创建成功。
void StoryController::HandleClearHome(const std::string& request_id, const std::string& session_id,
                                      const std::string& reason) {
    if (reason == "disconnected") {
        Application::GetInstance().SetDeviceState(kDeviceStateIdle);
        HandleConnectionClosed();
        return;
    }
    if (reason == "exit" || reason == "interrupted") {
        const bool normal_exit_pending = interaction_busy_ && interaction_action_ == "exit";
        if (normal_exit_pending) {
            HandleInteractionInterrupted();
        }

        if (force_home_exit_pending_ &&
            (session_id.empty() || force_home_exit_session_id_ == "interactive_story_home" ||
             session_id == force_home_exit_session_id_)) {
            force_home_exit_pending_ = false;
            force_home_exit_session_id_.clear();
        }

        // 普通返回键等待服务端退出；Home 已经本地返回，不再用迟到消息覆盖当前新页面。
        if (normal_exit_pending) {
            Application::GetInstance().SetDeviceState(kDeviceStateIdle);
            ResetStoryRuntimeState();
            ShowHome();
        }
        return;
    }

    if (reason == "started") {
        story_mode_active_ = true;
        current_experience_ = Experience::Story;
        if (!session_id.empty()) {
            if (!active_story_session_id_.empty() && current_page_ != Page::Picker &&
                current_page_ != Page::Loading && session_id != active_story_session_id_) {
                ESP_LOGW(TAG, "Ignoring clear_home from another session: %s", session_id.c_str());
                return;
            }
            active_story_session_id_ = session_id;
        }
        HideMenu();
        if (auto* display = dynamic_cast<LcdDisplay*>(Board::GetInstance().GetDisplay())) {
            display->ShowStoryLoading("正在准备互动听书…");
        }
        current_page_ = Page::Loading;
        ConfigureNavigation(true);
        return;
    }

    // 恢复或重听确认完成后，服务端已经进入 playing，板端同步显示播放页。
    if (reason == "resumed" || reason == "replayed" || reason == "opened") {
        story_mode_active_ = true;
        current_experience_ = Experience::Story;
        if (!session_id.empty()) {
            active_story_session_id_ = session_id;
        }
        EnterStoryPlayback();
        return;
    }

    if (reason == "handoff") {
        // handoff 表示最终正文已经真实播放完成；停止可能已经启动的自动聆听并回到待机首页。
        auto& application = Application::GetInstance();
        if (application.GetDeviceState() == kDeviceStateListening) {
            application.StopListening();
        } else {
            application.SetDeviceState(kDeviceStateIdle);
        }
        force_home_exit_pending_ = false;
        force_home_exit_session_id_.clear();
        ResetStoryRuntimeState();
        ShowHome();
    }
}

// 发送 enter 并保存 request_id，供 home_result 严格匹配。
void StoryController::SendStoryEnter() {
    pending_enter_request_id_ = NextRequestId();
    cJSON* data = cJSON_CreateObject();
    if (data != nullptr) {
        cJSON_AddStringToObject(data, "request_id", pending_enter_request_id_.c_str());
    }
    const bool begun = BeginInteraction("enter", pending_enter_request_id_);
    const bool sent = begun && SendStoryMessage("enter", pending_enter_request_id_, data);
    if (!sent) {
        if (!begun) {
            cJSON_Delete(data);
            pending_enter_request_id_.clear();
            return;
        }
        HandleInteractionInterrupted();
        pending_enter_request_id_.clear();
        ResetStoryRuntimeState();
        ShowHome();
    }
}

// 发送无主题的 start；题材由后端后续语音引导收集。
void StoryController::SendStoryStart() {
    const std::string request_id = NextRequestId();
    cJSON* data = cJSON_CreateObject();
    const bool begun = BeginInteraction("start", request_id);
    const bool sent = begun && SendStoryMessage("start", request_id, data);
    if (!sent) {
        if (!begun) {
            cJSON_Delete(data);
            return;
        }
        HandleInteractionInterrupted();
        if (auto* display = dynamic_cast<LcdDisplay*>(Board::GetInstance().GetDisplay())) {
            display->SetStoryMenuButtonsEnabled(true);
        }
    }
}

// 发送 resume，并原样使用 continue_play.session_id。
void StoryController::SendStoryResume(const std::string& session_id) {
    cJSON* data = cJSON_CreateObject();
    if (data != nullptr) {
        cJSON_AddStringToObject(data, "session_id", session_id.c_str());
    }
    const std::string request_id = NextRequestId();
    const bool begun = BeginInteraction("resume", request_id);
    const bool sent = begun && SendStoryMessage("resume", request_id, data);
    if (!sent) {
        if (!begun) {
            cJSON_Delete(data);
            return;
        }
        HandleInteractionInterrupted();
        if (auto* display = dynamic_cast<LcdDisplay*>(Board::GetInstance().GetDisplay())) {
            display->SetStoryMenuButtonsEnabled(true);
        }
    }
}

// 发送 open_history，并原样使用 history item 的 session_id。
void StoryController::SendStoryOpenHistory(const std::string& session_id) {
    cJSON* data = cJSON_CreateObject();
    if (data != nullptr) {
        cJSON_AddStringToObject(data, "session_id", session_id.c_str());
    }
    const std::string request_id = NextRequestId();
    const bool begun = BeginInteraction("open_history", request_id);
    const bool sent = begun && SendStoryMessage("open_history", request_id, data);
    if (!sent) {
        if (!begun) {
            cJSON_Delete(data);
            return;
        }
        HandleInteractionInterrupted();
        if (auto* display = dynamic_cast<LcdDisplay*>(Board::GetInstance().GetDisplay())) {
            display->SetStoryMenuButtonsEnabled(true);
        }
    }
}

// 发送固定 exit 空 data，等待 show_confirmation(exit_confirm)。
void StoryController::SendStoryExit() {
    cJSON* data = cJSON_CreateObject();
    if (data != nullptr && !active_story_session_id_.empty()) {
        cJSON_AddStringToObject(data, "session_id", active_story_session_id_.c_str());
    }
    const std::string request_id = NextRequestId();
    const bool begun = BeginInteraction("exit", request_id);
    const bool sent = begun && SendStoryMessage("exit", request_id, data);
    if (!sent) {
        if (!begun) {
            cJSON_Delete(data);
            return;
        }
        HandleInteractionInterrupted();
    }
}

void StoryController::SendAntiFraudStart() {
    cJSON* root = cJSON_CreateObject();
    cJSON* data = cJSON_CreateObject();
    if (root == nullptr || data == nullptr) {
        cJSON_Delete(root);
        cJSON_Delete(data);
        ESP_LOGE(TAG, "Failed to allocate anti_fraud start JSON");
        return;
    }

    cJSON_AddStringToObject(root, "type", "anti_fraud");
    cJSON_AddNumberToObject(root, "version", 2);
    cJSON_AddStringToObject(root, "action", "start");
    cJSON_AddItemToObject(root, "data", data);

    char* payload = cJSON_PrintUnformatted(root);
    if (payload != nullptr) {
        Application::GetInstance().SendTextMessage(payload);
        cJSON_free(payload);
    } else {
        ESP_LOGE(TAG, "Failed to serialize anti_fraud start JSON");
    }
    cJSON_Delete(root);
}

void StoryController::SendAntiFraudExit(const std::string& session_id) {
    if (session_id.empty()) {
        ESP_LOGW(TAG, "Skipping anti_fraud exit without session_id");
        return;
    }

    cJSON* root = cJSON_CreateObject();
    cJSON* data = cJSON_CreateObject();
    if (root == nullptr || data == nullptr) {
        cJSON_Delete(root);
        cJSON_Delete(data);
        ESP_LOGE(TAG, "Failed to allocate anti_fraud exit JSON");
        return;
    }

    const std::string request_id = NextRequestId();
    cJSON_AddStringToObject(root, "type", "anti_fraud");
    cJSON_AddNumberToObject(root, "version", 2);
    cJSON_AddStringToObject(root, "action", "exit");
    cJSON_AddStringToObject(root, "request_id", request_id.c_str());
    cJSON_AddStringToObject(data, "session_id", session_id.c_str());
    cJSON_AddItemToObject(root, "data", data);

    char* payload = cJSON_PrintUnformatted(root);
    if (payload != nullptr) {
        ESP_LOGI(TAG, "Sending anti_fraud exit: request_id=%s session_id=%s", request_id.c_str(),
                 session_id.c_str());
        Application::GetInstance().SendTextMessage(payload);
        cJSON_free(payload);
    } else {
        ESP_LOGE(TAG, "Failed to serialize anti_fraud exit JSON");
    }
    cJSON_Delete(root);
}

StoryUi* StoryController::GetUi() { return story_ui_.get(); }

#ifndef STORY_CONTROLLER_H
#define STORY_CONTROLLER_H

#include "display/lcd_display.h"
#include "story_ui.h"

#include <cJSON.h>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class StoryController {
public:
    static StoryController& GetInstance();

    void Initialize();

    void ShowStoryOptions(const std::string& request_id, const std::string& session_id,
                          const std::string& node_id, int step_no, int step_count,
                          const std::string& title, const std::string& prompt,
                          const std::string& option_a, const std::string& option_b);
    void ShowAntiFraudOptions(const std::string& session_id, const std::string& node_id,
                              int step_no, int step_count, const std::string& title,
                              const std::string& prompt, const std::string& option_a,
                              const std::string& option_b);

    // 处理服务端下发的反诈自然结束事件；若仍在播音则等待最后一次 TTS stop。
    void HandleAntiFraudCompleted(const std::string& request_id, const std::string& session_id);
    void ClearChoices(const std::string& request_id, const std::string& session_id,
                      const std::string& node_id);
    void ClearChoices();
    void ShowPendingChoices();
    void SelectChoice(int choice_index);

    void ShowHome();
    void ShowLibrary(const std::vector<StoryHistoryItem>& stories);

    // 处理新版 home_result，并用后端快照渲染互动听书首页。
    void ShowHomeResult(const std::string& request_id, const std::vector<StoryHistoryItem>& stories,
                        bool continue_available, const std::string& continue_session_id,
                        const std::string& continue_title);

    // 处理兼容协议的历史列表结果。
    void ShowHistoryResult(const std::string& request_id,
                           const std::vector<StoryHistoryItem>& stories);

    // 处理兼容协议的最近故事结果。
    void ShowResumeResult(const std::string& request_id, bool available,
                          const std::string& session_id);

    // 显示后端指定的确认弹窗，并保存复合键用于回传和清理。
    void ShowConfirmation(const std::string& request_id, const std::string& session_id,
                          const std::string& confirmation_id, const std::string& purpose,
                          const std::string& title, const std::string& prompt);

    // 仅在复合键匹配时关闭当前确认弹窗。
    void ClearConfirmation(const std::string& request_id, const std::string& session_id,
                           const std::string& confirmation_id);

    // 处理服务端清理活动首页的指令，并依据 reason 切换到播放页或功能首页。
    void HandleClearHome(const std::string& request_id, const std::string& session_id,
                         const std::string& reason);

    // 处理本轮请求完成，只释放交互门，不把 done 当作故事结束。
    void HandleInteractionDone(const std::string& request_id);

    // 处理服务端拒绝请求，恢复发起请求前页面的按钮。
    void HandleRequestRejected(const std::string& request_id, const std::string& reason,
                               const std::string& message);

    // 处理不可恢复 error，停止活动音频、清理全部活动投影并回到功能首页。
    void HandleError(const std::string& request_id, const std::string& code,
                     const std::string& message);

    // 通用 abort、断线时清理本地交互门，避免按钮永久锁死。
    void HandleInteractionInterrupted();

    // 网络或协议连接关闭时清空运行状态和 session 投影，并返回功能首页。
    void HandleConnectionClosed();

    // WebSocket 建连或文本发送失败时恢复可操作页面，避免停在加载状态。
    void HandleTransportSendFailed();

    // 互动听书语音开始播放时，确保加载页或播放页切换到故事播放外观。
    void HandleStoryTtsStarted();

    // 在最后一次 TTS stop 消费反诈结束标记，返回 true 表示本次不得进入聆听。
    bool FinishAntiFraudAfterTtsStop();

    void HideMenu();
    void NavigateBack();
    void NavigateHome();

    StoryUi* GetUi();

private:
    StoryController();
    ~StoryController() = default;

    StoryController(const StoryController&) = delete;
    StoryController& operator=(const StoryController&) = delete;

    enum class Page {
        Home,
        Loading,
        Picker,
        Library,
        Playing,
        Choice,
        Confirmation,
    };

    enum class Experience {
        Story,
        AntiFraud,
    };

    // 记录已经显示业务页面、但仍在等待同 request_id 的 done/rejected 的请求。
    struct ReadyInteraction {
        std::string request_id;
        std::string action;
        Page origin_page = Page::Home;
    };

    void ShowOptions(Experience experience, const std::string& request_id,
                     const std::string& session_id, const std::string& node_id, int step_no,
                     int step_count, const std::string& title, const std::string& prompt,
                     const std::string& option_a, const std::string& option_b);
    void OnChoiceSelected(int choice_index);
    void DisplayPendingChoices();

    // 点击 AI 听书后发送 enter，并显示等待 home_result 的加载页。
    void EnterStoryMode();
    void EnterAntiFraudMode();

    // 使用缓存的 home_result 内容重新渲染互动听书首页。
    void RenderStoryPicker();

    // 点击“开启新故事”时发送 start，不在本地直接切换播放页。
    void StartNewStory();

    // 点击“继续上次”时发送 resume，不在本地直接切换播放页。
    void ResumeLastStory();

    // 点击历史故事时发送 open_history，不在本地直接弹出确认框。
    void OpenHistory(size_t index);

    // 点击确认弹窗按钮时回传 confirm，并等待 clear_confirmation。
    void ConfirmCurrent(bool confirmed);

    // 点击返回或 Home 时发送统一的 exit，并等待后端退出确认。
    void RequestStoryExit();

    // Home 键立即打断语音并返回功能首页，同时在后台完成退出确认闭环。
    void ForceReturnToFeatureHome();

    // 清理互动听书的临时页面和复合键，历史列表缓存继续保留。
    void ResetStoryRuntimeState();

    // 清理反诈运行状态并返回“AI 听书 / 小剧场”功能首页。
    void FinishAntiFraudAndReturnHome();

    // 在确认完成或选项清理后恢复故事播放外观。
    void EnterStoryPlayback();

    // 发送仅含 data 对象的互动听书 V2 消息。
    bool SendStoryMessage(const char* action, const std::string& request_id, cJSON* data);

    // 为业务操作建立本地互斥门，同时停止当前未完成录音。
    bool BeginInteraction(const char* action, const std::string& request_id);

    // 仅允许与当前业务请求匹配的下行消息解除互斥门。
    bool CompleteInteraction(const std::string& request_id);

    // 收到业务页面后把请求从 busy 转为 ready，保留 request_id 等待 done。
    bool MarkInteractionReady(const std::string& request_id);

    // 判断下行 request_id 是否属于当前业务请求；无本地请求时允许服务端主动事件。
    bool AcceptInteractionResponse(const std::string& request_id) const;

    // 请求失败或被拒绝时，按发起请求前的页面恢复按钮状态。
    void RestoreInteractionButtons(Page origin_page);

    // 记录近期已完成请求，拦截同一 request_id 的迟到页面消息。
    void RememberCompletedRequest(const std::string& request_id);

    // 查询并维护仍在等待 done 的 ready 请求。
    bool IsReadyRequest(const std::string& request_id) const;
    bool GetReadyRequest(const std::string& request_id, std::string* action,
                         Page* origin_page) const;
    void RemoveReadyRequest(const std::string& request_id);

    // 发送 enter，并保存用于匹配 home_result 的 request_id。
    void SendStoryEnter();

    // 发送 start 新故事动作。
    void SendStoryStart();

    // 发送 resume 继续故事动作。
    void SendStoryResume(const std::string& session_id);

    // 发送 open_history 打开历史故事动作。
    void SendStoryOpenHistory(const std::string& session_id);

    // 发送固定 exit 退出动作。
    void SendStoryExit();

    void SendAntiFraudStart();

    // Home 键主动退出小剧场时，发送带 session_id 和唯一 request_id 的 exit。
    void SendAntiFraudExit(const std::string& session_id);
    std::string NextRequestId();
    void ConfigureNavigation(bool back_enabled);

    std::unique_ptr<StoryUi> story_ui_;

    std::string current_session_id_;
    std::string active_story_session_id_;
    std::string current_node_id_;
    std::string completed_session_id_;
    std::string completed_node_id_;
    int current_step_no_ = 0;

    bool initialized_ = false;
    bool choice_pending_ = false;
    bool choices_visible_ = false;
    std::vector<StoryHistoryItem> story_history_;
    bool continue_available_ = false;
    std::string continue_session_id_;
    std::string continue_title_;
    std::string pending_enter_request_id_;
    std::string pending_history_request_id_;
    std::string pending_resume_request_id_;

    std::string confirmation_session_id_;
    std::string confirmation_id_;
    std::string confirmation_purpose_;
    bool confirmation_response_sent_ = false;
    bool confirmation_response_value_ = false;
    bool story_mode_active_ = false;
    bool anti_fraud_completion_pending_ = false;
    std::string anti_fraud_completion_session_id_;
    bool force_home_exit_pending_ = false;
    std::string force_home_exit_session_id_;
    Page page_before_confirmation_ = Page::Home;

    bool interaction_busy_ = false;
    std::string interaction_request_id_;
    std::string interaction_action_;
    Page interaction_origin_page_ = Page::Home;
    std::vector<ReadyInteraction> ready_requests_;
    std::vector<std::string> completed_request_ids_;

    uint32_t request_sequence_ = 0;
    Page current_page_ = Page::Home;
    Experience current_experience_ = Experience::Story;
};

#endif

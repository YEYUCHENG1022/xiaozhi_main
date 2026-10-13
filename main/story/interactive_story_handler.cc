#include "interactive_story_handler.h"

#include <esp_log.h>
#include <algorithm>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "application.h"
#include "story_controller.h"

#define TAG "InteractiveStory"

namespace {

// 校验 interactive_story V2 的公共信封。
bool ValidateV2Envelope(const cJSON* root, const cJSON** action, const cJSON** request_id,
                        const cJSON** data, bool require_request_id) {
    auto version = cJSON_GetObjectItem(root, "version");
    *action = cJSON_GetObjectItem(root, "action");
    *request_id = cJSON_GetObjectItem(root, "request_id");
    *data = cJSON_GetObjectItem(root, "data");
    if (!cJSON_IsNumber(version) || version->valueint != 2) {
        ESP_LOGE(TAG, "Unsupported version; only V2 is accepted");
        return false;
    }
    if (!cJSON_IsString(*action) || !cJSON_IsObject(*data)) {
        ESP_LOGE(TAG, "Missing action or data");
        return false;
    }
    if (require_request_id &&
        (!cJSON_IsString(*request_id) || (*request_id)->valuestring[0] == '\0')) {
        ESP_LOGE(TAG, "Missing top-level request_id");
        return false;
    }
    return true;
}

// 兼容回包若同时携带 data.request_id，必须与顶层 request_id 完全一致。
bool ValidateMirroredRequestId(const std::string& request_id, const cJSON* data) {
    const cJSON* mirrored_request_id = cJSON_GetObjectItem(data, "request_id");
    if (mirrored_request_id == nullptr) {
        return true;
    }
    if (!cJSON_IsString(mirrored_request_id) || request_id != mirrored_request_id->valuestring) {
        ESP_LOGE(TAG, "Mismatched top-level and data request_id");
        return false;
    }
    return true;
}

// 将 ISO 8601 时间压缩为列表中使用的 MM/DD。
std::string FormatStoryDate(const cJSON* value) {
    if (!cJSON_IsString(value)) {
        return "";
    }
    const std::string timestamp = value->valuestring;
    return timestamp.size() >= 10 ? timestamp.substr(5, 2) + "/" + timestamp.substr(8, 2)
                                  : timestamp;
}

// 解析 home_result/history_result 共用的历史故事数组。
std::vector<StoryHistoryItem> ParseHistoryItems(const cJSON* items_json) {
    std::vector<StoryHistoryItem> items;
    if (!cJSON_IsArray(items_json)) {
        return items;
    }
    const int count = std::min(cJSON_GetArraySize(items_json), 10);
    items.reserve(count);
    for (int i = 0; i < count; ++i) {
        auto item = cJSON_GetArrayItem(items_json, i);
        if (!cJSON_IsObject(item)) {
            continue;
        }
        auto session_id = cJSON_GetObjectItem(item, "session_id");
        auto title = cJSON_GetObjectItem(item, "title");
        if (!cJSON_IsString(session_id) || !cJSON_IsString(title)) {
            continue;
        }

        StoryHistoryItem history;
        history.story_id = session_id->valuestring;
        history.title = title->valuestring;
        auto summary = cJSON_GetObjectItem(item, "summary");
        auto completion_status = cJSON_GetObjectItem(item, "completion_status");
        auto updated_at = cJSON_GetObjectItem(item, "updated_at");
        if (cJSON_IsString(summary)) {
            history.summary = summary->valuestring;
        }
        if (cJSON_IsString(completion_status)) {
            if (strcmp(completion_status->valuestring, "completed") == 0) {
                history.status = StoryHistoryStatus::Completed;
            } else if (strcmp(completion_status->valuestring, "incomplete") == 0) {
                history.status = StoryHistoryStatus::Unfinished;
            }
        }
        history.updated_at = FormatStoryDate(updated_at);
        items.emplace_back(std::move(history));
    }
    return items;
}

}  // namespace

InteractiveStoryHandler& InteractiveStoryHandler::GetInstance() {
    static InteractiveStoryHandler instance;
    return instance;
}

void InteractiveStoryHandler::HandleMessage(const cJSON* root) {
    const cJSON* action = nullptr;
    const cJSON* request_id = nullptr;
    const cJSON* data = nullptr;
    // 页面事件可由场景播放完成或语音选择主动产生，不强制要求顶层 request_id。
    if (!ValidateV2Envelope(root, &action, &request_id, &data, false)) {
        return;
    }
    const std::string request_id_value = cJSON_IsString(request_id) ? request_id->valuestring : "";

    const bool request_id_required = strcmp(action->valuestring, "home_result") == 0 ||
                                     strcmp(action->valuestring, "history_result") == 0 ||
                                     strcmp(action->valuestring, "resume_result") == 0 ||
                                     strcmp(action->valuestring, "error") == 0 ||
                                     strcmp(action->valuestring, "request_rejected") == 0;
    if (request_id_required && request_id_value.empty()) {
        ESP_LOGE(TAG, "%s requires top-level request_id", action->valuestring);
        return;
    }

    if (strcmp(action->valuestring, "home_result") == 0) {
        HandleHomeResult(request_id_value, data);
    } else if (strcmp(action->valuestring, "show_options") == 0) {
        HandleShowOptions(request_id_value, data, false);
    } else if (strcmp(action->valuestring, "clear_options") == 0) {
        HandleClearOptions(request_id_value, data, false);
    } else if (strcmp(action->valuestring, "show_confirmation") == 0) {
        HandleShowConfirmation(request_id_value, data);
    } else if (strcmp(action->valuestring, "clear_confirmation") == 0) {
        HandleClearConfirmation(request_id_value, data);
    } else if (strcmp(action->valuestring, "history_result") == 0) {
        HandleHistoryResult(request_id_value, data);
    } else if (strcmp(action->valuestring, "resume_result") == 0) {
        HandleResumeResult(request_id_value, data);
    } else if (strcmp(action->valuestring, "clear_home") == 0) {
        HandleClearHome(request_id_value, data);
    } else if (strcmp(action->valuestring, "done") == 0) {
        if (!request_id_value.empty()) {
            Application::GetInstance().Schedule([request_id_value]() {
                StoryController::GetInstance().HandleInteractionDone(request_id_value);
            });
        }
    } else if (strcmp(action->valuestring, "error") == 0) {
        HandleError(request_id_value, data);
    } else if (strcmp(action->valuestring, "request_rejected") == 0) {
        HandleRequestRejected(request_id_value, data);
    } else {
        ESP_LOGW(TAG, "Unknown interactive_story V2 action: %s", action->valuestring);
    }
}

// 解析 enter 对应的 home_result，并校验续听入口所需字段。
void InteractiveStoryHandler::HandleHomeResult(const std::string& request_id, const cJSON* data) {
    if (!ValidateMirroredRequestId(request_id, data)) {
        return;
    }
    auto items_json = cJSON_GetObjectItem(data, "items");
    auto continue_available = cJSON_GetObjectItem(data, "continue_available");
    auto continue_play = cJSON_GetObjectItem(data, "continue_play");
    if (!cJSON_IsArray(items_json) || !cJSON_IsBool(continue_available) ||
        (continue_play != nullptr && !cJSON_IsObject(continue_play) &&
         !cJSON_IsNull(continue_play))) {
        ESP_LOGE(TAG, "Invalid home_result data");
        return;
    }

    const bool available = cJSON_IsTrue(continue_available);
    std::string continue_session_id;
    std::string continue_title;
    if (available) {
        if (!cJSON_IsObject(continue_play)) {
            ESP_LOGE(TAG, "home_result continue_available=true requires continue_play");
            return;
        }
        auto session_id = cJSON_GetObjectItem(continue_play, "session_id");
        auto title = cJSON_GetObjectItem(continue_play, "title");
        if (!cJSON_IsString(session_id) || !cJSON_IsString(title)) {
            ESP_LOGE(TAG, "home_result continue_play is incomplete");
            return;
        }
        continue_session_id = session_id->valuestring;
        continue_title = title->valuestring;
    }

    auto items = ParseHistoryItems(items_json);
    Application::GetInstance().Schedule(
        [request_id, items = std::move(items), available, continue_session_id, continue_title]() {
            StoryController::GetInstance().ShowHomeResult(request_id, items, available,
                                                          continue_session_id, continue_title);
        });
}

void InteractiveStoryHandler::HandleAntiFraudMessage(const cJSON* root) {
    const cJSON* action = nullptr;
    const cJSON* request_id = nullptr;
    const cJSON* data = nullptr;
    if (!ValidateV2Envelope(root, &action, &request_id, &data, false)) {
        return;
    }

    if (strcmp(action->valuestring, "show_options") == 0) {
        HandleShowOptions("", data, true);
    } else if (strcmp(action->valuestring, "clear_options") == 0) {
        HandleClearOptions("", data, true);
    } else if (strcmp(action->valuestring, "completed") == 0) {
        auto session_id = cJSON_GetObjectItem(data, "session_id");
        if (!cJSON_IsString(session_id) || session_id->valuestring[0] == '\0') {
            ESP_LOGE(TAG, "Invalid anti_fraud completed data");
            return;
        }
        const std::string request_id_value =
            cJSON_IsString(request_id) ? request_id->valuestring : "";
        const std::string session_id_value = session_id->valuestring;
        Application::GetInstance().Schedule([request_id_value, session_id_value]() {
            StoryController::GetInstance().HandleAntiFraudCompleted(request_id_value,
                                                                    session_id_value);
        });
    } else {
        ESP_LOGW(TAG, "Unknown anti_fraud V2 action: %s", action->valuestring);
    }
}

void InteractiveStoryHandler::HandleShowOptions(const std::string& request_id, const cJSON* data,
                                                bool anti_fraud) {
    auto session_id = cJSON_GetObjectItem(data, "session_id");
    auto node_id = cJSON_GetObjectItem(data, "node_id");
    auto step_no = cJSON_GetObjectItem(data, "step_no");
    auto step_count = cJSON_GetObjectItem(data, "step_count");
    auto title = cJSON_GetObjectItem(data, "title");
    auto prompt = cJSON_GetObjectItem(data, "prompt");
    auto options = cJSON_GetObjectItem(data, "options");
    if (!cJSON_IsString(session_id) || !cJSON_IsString(node_id) || !cJSON_IsNumber(step_no) ||
        !cJSON_IsNumber(step_count) || !cJSON_IsString(title) || !cJSON_IsString(prompt) ||
        !cJSON_IsArray(options) || cJSON_GetArraySize(options) != 2) {
        ESP_LOGE(TAG, "Invalid show_options data");
        return;
    }

    std::string option_a;
    std::string option_b;
    for (int i = 0; i < 2; ++i) {
        auto option = cJSON_GetArrayItem(options, i);
        if (!cJSON_IsObject(option)) {
            ESP_LOGE(TAG, "Invalid show_options option");
            return;
        }
        auto id = cJSON_GetObjectItem(option, "id");
        auto label = cJSON_GetObjectItem(option, "label");
        if (!cJSON_IsString(id) || !cJSON_IsString(label)) {
            ESP_LOGE(TAG, "Invalid show_options option");
            return;
        }
        if (strcmp(id->valuestring, "A") == 0) {
            option_a = label->valuestring;
        } else if (strcmp(id->valuestring, "B") == 0) {
            option_b = label->valuestring;
        }
    }
    if (option_a.empty() || option_b.empty()) {
        ESP_LOGE(TAG, "show_options requires option ids A and B");
        return;
    }

    const std::string session_id_value = session_id->valuestring;
    const std::string node_id_value = node_id->valuestring;
    const int step_no_value = step_no->valueint;
    const int step_count_value = step_count->valueint;
    const std::string title_value = title->valuestring;
    const std::string prompt_value = prompt->valuestring;
    Application::GetInstance().Schedule([request_id, anti_fraud, session_id_value, node_id_value,
                                         step_no_value, step_count_value, title_value, prompt_value,
                                         option_a, option_b]() {
        if (anti_fraud) {
            StoryController::GetInstance().ShowAntiFraudOptions(
                session_id_value, node_id_value, step_no_value, step_count_value, title_value,
                prompt_value, option_a, option_b);
        } else {
            StoryController::GetInstance().ShowStoryOptions(
                request_id, session_id_value, node_id_value, step_no_value, step_count_value,
                title_value, prompt_value, option_a, option_b);
        }
    });
}

void InteractiveStoryHandler::HandleClearOptions(const std::string& request_id, const cJSON* data,
                                                 bool anti_fraud) {
    auto session_id = cJSON_GetObjectItem(data, "session_id");
    auto node_id = cJSON_GetObjectItem(data, "node_id");
    auto reason = cJSON_GetObjectItem(data, "reason");
    if (!cJSON_IsString(session_id) || !cJSON_IsString(node_id) || !cJSON_IsString(reason)) {
        ESP_LOGE(TAG, "Invalid clear_options data");
        return;
    }

    const std::string session_id_value = session_id->valuestring;
    const std::string node_id_value = node_id->valuestring;
    ESP_LOGI(TAG, "clear_options: session_id=%s node_id=%s reason=%s", session_id_value.c_str(),
             node_id_value.c_str(), reason->valuestring);
    Application::GetInstance().Schedule(
        [request_id, session_id_value, node_id_value, anti_fraud]() {
            if (anti_fraud) {
                StoryController::GetInstance().ClearChoices();
            } else {
                StoryController::GetInstance().ClearChoices(request_id, session_id_value,
                                                            node_id_value);
            }
        });
}

// 解析 show_confirmation，后端文案和标识全部原样交给控制器。
void InteractiveStoryHandler::HandleShowConfirmation(const std::string& request_id,
                                                     const cJSON* data) {
    auto session_id = cJSON_GetObjectItem(data, "session_id");
    auto confirmation_id = cJSON_GetObjectItem(data, "confirmation_id");
    auto purpose = cJSON_GetObjectItem(data, "purpose");
    auto title = cJSON_GetObjectItem(data, "title");
    auto prompt = cJSON_GetObjectItem(data, "prompt");
    if (!cJSON_IsString(session_id) || !cJSON_IsString(confirmation_id) ||
        !cJSON_IsString(purpose) || !cJSON_IsString(title) || !cJSON_IsString(prompt)) {
        ESP_LOGE(TAG, "Invalid show_confirmation data");
        return;
    }

    const std::string session_id_value = session_id->valuestring;
    const std::string confirmation_id_value = confirmation_id->valuestring;
    const std::string purpose_value = purpose->valuestring;
    const std::string title_value = title->valuestring;
    const std::string prompt_value = prompt->valuestring;
    Application::GetInstance().Schedule([request_id, session_id_value, confirmation_id_value,
                                         purpose_value, title_value, prompt_value]() {
        StoryController::GetInstance().ShowConfirmation(request_id, session_id_value,
                                                        confirmation_id_value, purpose_value,
                                                        title_value, prompt_value);
    });
}

// 解析 clear_confirmation，并只按 session_id + confirmation_id 清理。
void InteractiveStoryHandler::HandleClearConfirmation(const std::string& request_id,
                                                      const cJSON* data) {
    auto session_id = cJSON_GetObjectItem(data, "session_id");
    auto confirmation_id = cJSON_GetObjectItem(data, "confirmation_id");
    auto reason = cJSON_GetObjectItem(data, "reason");
    if (!cJSON_IsString(session_id) || !cJSON_IsString(confirmation_id) ||
        !cJSON_IsString(reason)) {
        ESP_LOGE(TAG, "Invalid clear_confirmation data");
        return;
    }

    const std::string session_id_value = session_id->valuestring;
    const std::string confirmation_id_value = confirmation_id->valuestring;
    ESP_LOGI(TAG, "clear_confirmation: session_id=%s confirmation_id=%s reason=%s",
             session_id_value.c_str(), confirmation_id_value.c_str(), reason->valuestring);
    Application::GetInstance().Schedule([request_id, session_id_value, confirmation_id_value]() {
        StoryController::GetInstance().ClearConfirmation(request_id, session_id_value,
                                                         confirmation_id_value);
    });
}

void InteractiveStoryHandler::HandleHistoryResult(const std::string& request_id,
                                                  const cJSON* data) {
    if (!ValidateMirroredRequestId(request_id, data)) {
        return;
    }
    auto items_json = cJSON_GetObjectItem(data, "items");
    if (!cJSON_IsArray(items_json)) {
        ESP_LOGE(TAG, "Invalid history_result data");
        return;
    }

    auto items = ParseHistoryItems(items_json);

    Application::GetInstance().Schedule([request_id, items = std::move(items)]() {
        StoryController::GetInstance().ShowHistoryResult(request_id, items);
    });
}

void InteractiveStoryHandler::HandleResumeResult(const std::string& request_id, const cJSON* data) {
    if (!ValidateMirroredRequestId(request_id, data)) {
        return;
    }
    auto available = cJSON_GetObjectItem(data, "available");
    auto session_id = cJSON_GetObjectItem(data, "session_id");
    if (!cJSON_IsBool(available)) {
        ESP_LOGE(TAG, "Invalid resume_result data");
        return;
    }

    const bool available_value = cJSON_IsTrue(available);
    const std::string session_id_value = cJSON_IsString(session_id) ? session_id->valuestring : "";
    if (available_value && session_id_value.empty()) {
        ESP_LOGE(TAG, "resume_result available=true requires session_id");
        return;
    }
    Application::GetInstance().Schedule([request_id, available_value, session_id_value]() {
        StoryController::GetInstance().ShowResumeResult(request_id, available_value,
                                                        session_id_value);
    });
}

// 解析服务端对活动首页的清理指令。
void InteractiveStoryHandler::HandleClearHome(const std::string& request_id, const cJSON* data) {
    auto reason = cJSON_GetObjectItem(data, "reason");
    auto session_id = cJSON_GetObjectItem(data, "session_id");
    if (!cJSON_IsString(reason) ||
        (!cJSON_IsString(session_id) && !cJSON_IsNull(session_id) && session_id != nullptr)) {
        ESP_LOGE(TAG, "Invalid clear_home data");
        return;
    }
    const std::string reason_value = reason->valuestring;
    const std::string session_id_value = cJSON_IsString(session_id) ? session_id->valuestring : "";
    Application::GetInstance().Schedule([request_id, session_id_value, reason_value]() {
        StoryController::GetInstance().HandleClearHome(request_id, session_id_value, reason_value);
    });
}

// 解析服务端拒绝原因，并由控制器恢复原页面按钮。
void InteractiveStoryHandler::HandleRequestRejected(const std::string& request_id,
                                                    const cJSON* data) {
    auto reason = cJSON_GetObjectItem(data, "reason");
    auto message = cJSON_GetObjectItem(data, "message");
    if (!cJSON_IsString(reason) || (message != nullptr && !cJSON_IsString(message))) {
        ESP_LOGE(TAG, "Invalid request_rejected data");
        return;
    }
    const std::string reason_value = reason->valuestring;
    const std::string message_value = cJSON_IsString(message) ? message->valuestring : "";
    Application::GetInstance().Schedule([request_id, reason_value, message_value]() {
        StoryController::GetInstance().HandleRequestRejected(request_id, reason_value,
                                                             message_value);
    });
}

// 解析不可恢复 error，并由控制器停止活动、清理页面和返回功能首页。
void InteractiveStoryHandler::HandleError(const std::string& request_id, const cJSON* data) {
    auto code = cJSON_GetObjectItem(data, "code");
    auto reason = cJSON_GetObjectItem(data, "reason");
    auto message = cJSON_GetObjectItem(data, "message");
    if ((code != nullptr && !cJSON_IsString(code)) ||
        (reason != nullptr && !cJSON_IsString(reason)) ||
        (message != nullptr && !cJSON_IsString(message))) {
        ESP_LOGE(TAG, "Invalid error data");
        return;
    }
    const std::string code_value = cJSON_IsString(code)
                                       ? code->valuestring
                                       : (cJSON_IsString(reason) ? reason->valuestring : "error");
    const std::string message_value = cJSON_IsString(message) ? message->valuestring : "";
    Application::GetInstance().Schedule([request_id, code_value, message_value]() {
        StoryController::GetInstance().HandleError(request_id, code_value, message_value);
    });
}

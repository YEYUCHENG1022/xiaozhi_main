#include "story_ui.h"

#include <esp_log.h>
#include <utility>

#define TAG "StoryUi"

void StoryUi::SetStory(
    const std::string& story_id,
    const std::string& node_id,
    const std::string& title,
    const std::string& content,
    const std::string& choice_a,
    const std::string& choice_b
) {
    story_id_ = story_id;
    node_id_ = node_id;
    title_ = title;
    content_ = content;
    choice_a_ = choice_a;
    choice_b_ = choice_b;

    ESP_LOGI(
        TAG,
        "Story updated: story_id=%s, node_id=%s, title=%s",
        story_id_.c_str(),
        node_id_.c_str(),
        title_.c_str()
    );
}

void StoryUi::SetChoiceCallback(ChoiceCallback callback) {
    choice_callback_ = std::move(callback);
}

void StoryUi::SelectChoice(int choice_index) {
    if (choice_index != 0 && choice_index != 1) {
        ESP_LOGW(TAG, "Invalid choice index: %d", choice_index);
        return;
    }

    const char* choice_text =
        choice_index == 0 ? choice_a_.c_str() : choice_b_.c_str();

    ESP_LOGI(
        TAG,
        "Choice selected: story_id=%s, node_id=%s, choice=%d, text=%s",
        story_id_.c_str(),
        node_id_.c_str(),
        choice_index,
        choice_text
    );

    if (choice_callback_) {
        choice_callback_(choice_index);
    } else {
        ESP_LOGW(TAG, "Choice callback is not set");
    }
}

const std::string& StoryUi::GetStoryId() const {
    return story_id_;
}

const std::string& StoryUi::GetNodeId() const {
    return node_id_;
}

const std::string& StoryUi::GetTitle() const {
    return title_;
}

const std::string& StoryUi::GetContent() const {
    return content_;
}

const std::string& StoryUi::GetChoiceA() const {
    return choice_a_;
}

const std::string& StoryUi::GetChoiceB() const {
    return choice_b_;
}

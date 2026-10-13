#ifndef STORY_UI_H
#define STORY_UI_H

#include <functional>
#include <string>

class StoryUi {
public:
    using ChoiceCallback = std::function<void(int)>;

    StoryUi() = default;
    ~StoryUi() = default;

    void SetStory(
        const std::string& story_id,
        const std::string& node_id,
        const std::string& title,
        const std::string& content,
        const std::string& choice_a,
        const std::string& choice_b
    );

    void SetChoiceCallback(ChoiceCallback callback);
    void SelectChoice(int choice_index);

    const std::string& GetStoryId() const;
    const std::string& GetNodeId() const;
    const std::string& GetTitle() const;
    const std::string& GetContent() const;
    const std::string& GetChoiceA() const;
    const std::string& GetChoiceB() const;

private:
    std::string story_id_;
    std::string node_id_;
    std::string title_;
    std::string content_;
    std::string choice_a_;
    std::string choice_b_;

    ChoiceCallback choice_callback_;
};

#endif

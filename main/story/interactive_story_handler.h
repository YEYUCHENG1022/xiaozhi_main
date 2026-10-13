#pragma once

#include <cJSON.h>
#include <string>

class InteractiveStoryHandler {
public:
    static InteractiveStoryHandler& GetInstance();

    void HandleMessage(const cJSON* root);
    void HandleAntiFraudMessage(const cJSON* root);

private:
    InteractiveStoryHandler() = default;

    // 解析后端首页快照并交给控制器渲染。
    void HandleHomeResult(const std::string& request_id, const cJSON* data);
    void HandleShowOptions(const std::string& request_id, const cJSON* data, bool anti_fraud);
    void HandleClearOptions(const std::string& request_id, const cJSON* data, bool anti_fraud);

    // 解析并显示后端下发的通用确认弹窗。
    void HandleShowConfirmation(const std::string& request_id, const cJSON* data);

    // 按复合键清理当前确认弹窗。
    void HandleClearConfirmation(const std::string& request_id, const cJSON* data);

    // 处理旧固件兼容的历史结果。
    void HandleHistoryResult(const std::string& request_id, const cJSON* data);

    // 处理旧固件兼容的续听结果。
    void HandleResumeResult(const std::string& request_id, const cJSON* data);

    // 解析 clear_home，并把清理原因交给控制器执行页面切换。
    void HandleClearHome(const std::string& request_id, const cJSON* data);

    // 解析 request_rejected，恢复被本次请求锁定的页面。
    void HandleRequestRejected(const std::string& request_id, const cJSON* data);

    // 解析 error，终止当前活动并回到功能首页。
    void HandleError(const std::string& request_id, const cJSON* data);
};

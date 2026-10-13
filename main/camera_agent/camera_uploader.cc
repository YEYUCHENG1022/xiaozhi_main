#include "camera_uploader.h"

#include "board.h"
#include "camera.h"

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <exception>
#include <string>
#include <cJSON.h>

#define TAG "CameraUploader"

namespace {

constexpr TickType_t kUploadInterval = pdMS_TO_TICKS(10000);
constexpr uint32_t kTaskStackSize = 12 * 1024;
constexpr UBaseType_t kTaskPriority = 4;

}  // namespace

CameraUploader& CameraUploader::GetInstance() {
    static CameraUploader instance;
    return instance;
}

void CameraUploader::Start() {
    if (running_.exchange(true)) {
        ESP_LOGW(TAG, "Camera uploader is already running");
        return;
    }

    BaseType_t result = xTaskCreate(
        UploadTask,
        "camera_uploader",
        kTaskStackSize,
        this,
        kTaskPriority,
        nullptr
    );

    if (result != pdPASS) {
        running_ = false;
        ESP_LOGE(TAG, "Failed to create camera uploader task");
        return;
    }

    ESP_LOGI(TAG, "Camera uploader started, interval=10 seconds");
}

void CameraUploader::Stop() {
    running_ = false;
    ESP_LOGI(TAG, "Camera uploader stop requested");
}

void CameraUploader::UploadTask(void* parameter) {
    auto* uploader = static_cast<CameraUploader*>(parameter);

    /*
     * 先等待10秒，让Wi-Fi、协议和后台配置完成初始化。
     */
    vTaskDelay(kUploadInterval);

    while (uploader->running_) {
        Camera* camera = Board::GetInstance().GetCamera();

        if (camera == nullptr) {
            ESP_LOGE(TAG, "Camera is not available");
            vTaskDelay(kUploadInterval);
            continue;
        }

        /*
         * 定时人脸情绪识别只使用独立地址，禁止回退到 vision.url/VLLM。
         */
        if (!camera->IsFaceEmotionUploadConfigured()) {
            ESP_LOGW(TAG, "Waiting for Server face emotion upload configuration");
            vTaskDelay(kUploadInterval);
            continue;
        }

        try {
            ESP_LOGI(TAG, "Capturing image");

            if (!camera->CaptureForUpload()) {
                ESP_LOGE(TAG, "Camera capture failed");
                vTaskDelay(kUploadInterval);
                continue;
            }

            /*
             * 上传到 Server 下发的 face_emotion_url。
             */
            std::string response = camera->UploadFaceEmotion();

            if (response.empty()) {
                ESP_LOGE(TAG, "Face emotion upload failed; retrying in 10 seconds");
                vTaskDelay(kUploadInterval);
                continue;
            }

            cJSON* root = cJSON_Parse(response.c_str());
            cJSON* success = root ? cJSON_GetObjectItem(root, "success") : nullptr;
            if (!cJSON_IsTrue(success)) {
                cJSON* message = root ? cJSON_GetObjectItem(root, "message") : nullptr;
                ESP_LOGE(TAG, "Face emotion upload rejected: %s",
                         cJSON_IsString(message) ? message->valuestring : "invalid response");
                if (root) {
                    cJSON_Delete(root);
                }
                vTaskDelay(kUploadInterval);
                continue;
            }
            cJSON_Delete(root);

            ESP_LOGI(
                TAG,
                "Face emotion image accepted, response length=%u",
                static_cast<unsigned>(response.size())
            );

        } catch (const std::exception& error) {
            ESP_LOGE(
                TAG,
                "Camera upload failed: %s",
                error.what()
            );

        } catch (...) {
            ESP_LOGE(TAG, "Camera upload failed: unknown exception");
        }

        vTaskDelay(kUploadInterval);
    }

    uploader->running_ = false;
    ESP_LOGI(TAG, "Camera uploader task stopped");

    vTaskDelete(nullptr);
}

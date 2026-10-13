#pragma once
#include "sdkconfig.h"

#include <lvgl.h>
#include <thread>
#include <memory>
#include <vector>
#include <atomic>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "camera.h"
#include "esp_camera.h"
#include "jpg/image_to_jpeg.h"

struct JpegChunk
{
    uint8_t *data;
    size_t len;
};

class Esp32Camera : public Camera
{
private:
    bool streaming_on_ = false;
    bool swap_bytes_enabled_ = true;  // Swap pixel byte order for RGB565, enabled by default
    std::string explain_url_;
    std::string explain_token_;
    std::atomic<bool> explain_configured_{false};
    std::string face_emotion_url_;
    std::string face_emotion_token_;
    std::atomic<bool> face_emotion_configured_{false};
    std::thread encoder_thread_;
    camera_fb_t *current_fb_ = nullptr;
    uint8_t *encode_buf_ = nullptr;  // Buffer for JPEG encoding (with optional byte swap)
    size_t encode_buf_size_ = 0;

    // 执行一次拍照，show_preview=false 时仅保留上传数据，不刷新屏幕。
    bool CaptureInternal(bool show_preview);
    std::string UploadImage(
        const std::string& url,
        const std::string& token,
        const std::vector<std::pair<std::string, std::string>>& fields,
        const char* operation_name);

public:
    Esp32Camera(const camera_config_t &config);
    ~Esp32Camera();

    virtual void SetExplainUrl(const std::string &url, const std::string &token) override;
    // 查询 MCP vision.url 是否已经配置完成。
    virtual bool IsUploadConfigured() const override;
    virtual void SetFaceEmotionUrl(const std::string& url, const std::string& token) override;
    virtual bool IsFaceEmotionUploadConfigured() const override;
    virtual bool Capture() override;
    // 定时上传使用的静默拍照，避免每10秒重建屏幕预览。
    virtual bool CaptureForUpload() override;
    virtual bool SetHMirror(bool enabled) override;
    virtual bool SetVFlip(bool enabled) override;
    virtual bool SetSwapBytes(bool enabled) override;
    virtual std::string Explain(const std::string &question) override;
    virtual std::string UploadFaceEmotion() override;
};

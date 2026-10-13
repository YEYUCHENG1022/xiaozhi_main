#pragma once

#include <atomic>

class CameraUploader {
public:
    static CameraUploader& GetInstance();

    // 启动每10秒拍照上传任务
    void Start();

    // 停止任务
    void Stop();

private:
    CameraUploader() = default;
    ~CameraUploader() = default;

    CameraUploader(const CameraUploader&) = delete;
    CameraUploader& operator=(const CameraUploader&) = delete;

    static void UploadTask(void* parameter);

    std::atomic<bool> running_{false};
};
#ifndef CAMERA_H
#define CAMERA_H

#include <string>

class Camera {
public:
    virtual void SetExplainUrl(const std::string& url, const std::string& token) = 0;
    // 判断 Server 是否已下发照片上传地址。
    virtual bool IsUploadConfigured() const { return false; }
    virtual void SetFaceEmotionUrl(const std::string& url, const std::string& token) {}
    virtual bool IsFaceEmotionUploadConfigured() const { return false; }
    virtual bool Capture() = 0;
    // 后台上传专用拍照：默认沿用普通拍照，支持的相机可关闭屏幕预览。
    virtual bool CaptureForUpload() { return Capture(); }
    virtual bool SetHMirror(bool enabled) = 0;
    virtual bool SetVFlip(bool enabled) = 0;
    virtual bool SetSwapBytes(bool enabled) { return false; }  // Optional, default no-op
    virtual std::string Explain(const std::string& question) = 0;
    virtual std::string UploadFaceEmotion() { return {}; }
};

#endif // CAMERA_H

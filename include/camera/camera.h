#pragma once
#include "buffer/buffer.h"
#include <opencv2/opencv.hpp>
#include <thread>
#include <atomic>
#include <string>

class Camera {
public:
    Camera(Buffer& rawBuffer, Buffer& previewBuffer, int cameraId, int intervalMs);
    ~Camera();

    void start();
    void stop();
    void setRecording(bool recording);
    void setPaused(bool paused);
    void setInterval(int intervalMs);
    void setResolution(int width, int height);
    void requestPropertiesDialog();
    void setCameraIndex(int index, bool isVirtual = false, bool isGoPro = false, const std::string& ip = "");
    void setGoProLiveView(bool live);

private:
    void captureLoop();
    std::string httpGet(const std::string& url);

    Buffer& raw_buffer_;
    Buffer& preview_buffer_;
    std::atomic<int> camera_id_;
    std::atomic<int> interval_ms_;
    std::atomic<int> target_width_;
    std::atomic<int> target_height_;
    std::atomic<bool> running_;
    std::atomic<bool> recording_;
    std::atomic<bool> paused_;
    std::atomic<bool> request_props_;
    std::atomic<bool> resolution_changed_;
    std::atomic<bool> camera_changed_;
    std::atomic<bool> is_virtual_;
    std::atomic<bool> is_gopro_api_;
    std::atomic<bool> gopro_live_view_;
    std::string gopro_ip_;

    std::thread worker_thread_;
    cv::VideoCapture cap_;
};

#pragma once
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <opencv2/opencv.hpp>
#include "buffer/buffer.h"

class VideoEncoder {
public:
    VideoEncoder(Buffer& inputBuffer, const std::string& outputPath, int fps, cv::Size resolution);
    ~VideoEncoder();

    void start();
    void stop();
    void setKeepImages(bool keep);
    void archiveSession(const std::string& destPath); 
    
    // NEW: Allow the UI to update the target encoding resolution dynamically
    void setResolution(int width, int height); 

private:
    void encodeLoop();

    Buffer& input_buffer_;
    std::string output_path_;
    int fps_;
    cv::Size resolution_;
    std::atomic<bool> running_;
    std::atomic<bool> keep_images_;
    
    std::atomic<bool> archive_pending_;
    std::string archive_dest_;
    std::mutex archive_mutex_;
    
    std::thread worker_thread_;
};

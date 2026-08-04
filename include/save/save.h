#pragma once
#include <string>
#include <thread>
#include <atomic>
#include "buffer/buffer.h"

class Save {
public:
    Save(Buffer& inputBuffer, Buffer& outputBuffer, const std::string& defaultFolder);
    ~Save();

    void start();
    void stop();
    void setOutputFolder(const std::string& path);
    
    // NEW: Listens to the UI to determine if SSD writing is even necessary
    void setKeepImages(bool keep);

private:
    void saveLoop();

    Buffer& input_buffer_;
    Buffer& output_buffer_;
    std::string output_folder_;
    std::atomic<bool> running_;
    std::atomic<bool> keep_images_;
    std::thread worker_thread_;
};

#pragma once
#include <thread>
#include <atomic>
#include <string>
#include <opencv2/opencv.hpp>
#include "buffer/buffer.h"

class Process {
public:
    // Takes the raw buffer, the output save buffer, and the dark frame threshold (0-255)
    Process(Buffer& inputBuffer, Buffer& outputBuffer, double darkThreshold);
    ~Process();

    void start();
    void stop();

private:
    void processLoop();
    std::string getCurrentTimestamp();

    Buffer& input_buffer_;
    Buffer& output_buffer_;
    double dark_threshold_;
    std::atomic<bool> running_;
    std::thread worker_thread_;
};

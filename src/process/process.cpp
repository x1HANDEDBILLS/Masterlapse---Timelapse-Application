#include "process/process.h"
#include "process/timestamp_config.h"
#include <iostream>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <algorithm>

extern std::atomic<bool> keep_running;

// Global shared memory instance
TimestampConfig g_ts_config;
std::mutex g_ts_mutex;

Process::Process(Buffer& inputBuffer, Buffer& outputBuffer, double darkThreshold)
    : input_buffer_(inputBuffer), output_buffer_(outputBuffer), dark_threshold_(darkThreshold), running_(false) {}

Process::~Process() { stop(); }

void Process::start() {
    if (running_) return;
    running_ = true;
    worker_thread_ = std::thread(&Process::processLoop, this);
    std::cout << "[Process] Image processing thread started." << std::endl;
}

void Process::stop() {
    if (!running_) return;
    running_ = false;
    if (worker_thread_.joinable()) worker_thread_.join();
}

void Process::processLoop() {
    while (running_ && keep_running) {
        FrameData fd;
        if (input_buffer_.pop(fd)) {
            if (!fd.image.empty()) {
                
                cv::Scalar m = cv::mean(fd.image);
                double avg_brightness = (m[0] + m[1] + m[2]) / 3.0;
                
                if (avg_brightness <= dark_threshold_) {
                    std::cout << "[Process] Frame " << fd.id << " dropped (Too Dark)." << std::endl;
                    continue; 
                }

                // Synchronize with the UI's interactive overlay
                double rel_x, rel_y, scale;
                bool enabled;
                {
                    std::lock_guard<std::mutex> lock(g_ts_mutex);
                    rel_x = g_ts_config.rel_x;
                    rel_y = g_ts_config.rel_y;
                    scale = g_ts_config.scale;
                    enabled = g_ts_config.enabled;
                }

                if (enabled) {
                    double base_width = 1280.0;
                    double scale_factor = (fd.image.cols / base_width) * scale;
                    
                    double font_scale = 1.0 * scale_factor; 
                    int thickness = std::max(2, static_cast<int>(3 * scale_factor));
                    int shadow_thickness = thickness + std::max(1, static_cast<int>(2 * scale_factor));
                    
                    auto now = std::chrono::system_clock::now();
                    std::time_t now_time = std::chrono::system_clock::to_time_t(now);
                    std::tm* local_time = std::localtime(&now_time);
                    
                    std::ostringstream timeStream;
                    timeStream << std::put_time(local_time, "%I:%M %p %m/%d/%y");
                    std::string timeStr = timeStream.str();
                    
                    int pos_x = static_cast<int>(fd.image.cols * rel_x);
                    int pos_y = static_cast<int>(fd.image.rows * rel_y);
                    cv::Point textPos(pos_x, pos_y);
                    
                    cv::putText(fd.image, timeStr, textPos, cv::FONT_HERSHEY_DUPLEX, font_scale, cv::Scalar(0, 0, 0), shadow_thickness, cv::LINE_AA);
                    cv::putText(fd.image, timeStr, textPos, cv::FONT_HERSHEY_DUPLEX, font_scale, cv::Scalar(0, 0, 255), thickness, cv::LINE_AA);
                }

                std::cout << "[Process] Frame " << fd.id << " passed inspection & stamped. Queued for saving." << std::endl;
                output_buffer_.push(fd);
            }
        }
    }
}


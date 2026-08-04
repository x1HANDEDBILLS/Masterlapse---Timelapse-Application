#include "save/save.h"
#include <iostream>
#include <opencv2/opencv.hpp>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <vector>

extern std::atomic<bool> keep_running;

Save::Save(Buffer& inputBuffer, Buffer& outputBuffer, const std::string& defaultFolder)
    : input_buffer_(inputBuffer), output_buffer_(outputBuffer), output_folder_(defaultFolder), running_(false), keep_images_(false) {}

Save::~Save() { stop(); }

void Save::start() {
    if (running_) return;
    running_ = true;
    worker_thread_ = std::thread(&Save::saveLoop, this);
    std::cout << "[Save] Disk writing thread started." << std::endl;
}

void Save::stop() {
    if (!running_) return;
    running_ = false;
    if (worker_thread_.joinable()) worker_thread_.join();
}

void Save::setOutputFolder(const std::string& path) {
    output_folder_ = path;
    std::filesystem::create_directories(output_folder_);
}

void Save::setKeepImages(bool keep) {
    keep_images_ = keep;
}

void Save::saveLoop() {
    std::filesystem::create_directories(output_folder_);
    
    // THE FIX: Switch to 100% Quality JPEG. Visually lossless, but exponentially faster to write than PNG.
    std::vector<int> compression_params;
    compression_params.push_back(cv::IMWRITE_JPEG_QUALITY);
    compression_params.push_back(100); 
    
    while (running_ && keep_running) {
        FrameData fd;
        if (input_buffer_.pop(fd)) {
            if (!fd.image.empty()) {
                
                // THE FIX: Only hit the SSD if the user explicitly checked the box. 
                // Otherwise, pass it entirely in high-speed RAM.
                if (keep_images_) {
                    std::ostringstream ss;
                    ss << output_folder_ << "/capture_" << std::setw(6) << std::setfill('0') << fd.id << ".jpg";
                    fd.filepath = ss.str();
                    
                    cv::imwrite(fd.filepath, fd.image, compression_params);
                    std::cout << "[Save] Securely written to SSD: " << fd.filepath << std::endl;
                } else {
                    fd.filepath = ""; // Flag as RAM-only
                }
                
                output_buffer_.push(fd);
            }
        }
    }
}

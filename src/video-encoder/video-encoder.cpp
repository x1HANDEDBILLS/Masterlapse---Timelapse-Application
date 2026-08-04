#include "video-encoder/video-encoder.h"
#include <iostream>
#include <cstdio> 
#include <filesystem>
#include <chrono>

extern std::atomic<bool> keep_running;

VideoEncoder::VideoEncoder(Buffer& inputBuffer, const std::string& outputPath, int fps, cv::Size resolution)
    : input_buffer_(inputBuffer), output_path_(outputPath), fps_(fps), resolution_(resolution), 
      running_(false), keep_images_(false), archive_pending_(false) {}

VideoEncoder::~VideoEncoder() { stop(); }

void VideoEncoder::start() {
    if (running_) return;
    running_ = true;
    worker_thread_ = std::thread(&VideoEncoder::encodeLoop, this);
    std::cout << "[VideoEncoder] Live video encoding thread started." << std::endl;
}

void VideoEncoder::stop() {
    if (!running_) return;
    running_ = false;
    if (worker_thread_.joinable()) worker_thread_.join();
    std::cout << "[VideoEncoder] Video encoding thread stopped safely. File finalized." << std::endl;
}

void VideoEncoder::setKeepImages(bool keep) {
    keep_images_ = keep;
}

void VideoEncoder::setResolution(int width, int height) {
    std::lock_guard<std::mutex> lock(archive_mutex_);
    resolution_ = cv::Size(width, height);
}

void VideoEncoder::archiveSession(const std::string& destPath) {
    std::lock_guard<std::mutex> lock(archive_mutex_);
    archive_dest_ = destPath;
    archive_pending_ = true;
}

void VideoEncoder::encodeLoop() {
    cv::VideoWriter writer;
    bool isOpened = false;

    while (running_ && keep_running) {
        FrameData fd;
        
        if (input_buffer_.try_pop(fd)) {
            if (!isOpened) {
                int fourcc = cv::VideoWriter::fourcc('M', 'J', 'P', 'G');
                
                cv::Size current_res;
                {
                    std::lock_guard<std::mutex> lock(archive_mutex_);
                    current_res = resolution_;
                }
                
                writer.open(output_path_, fourcc, fps_, current_res, true);
                
                if (!writer.isOpened()) {
                     std::cerr << "[VideoEncoder] ERROR: Failed to open live encoder stream!" << std::endl;
                     continue;
                }
                
                // THE FIX: Force maximum quality for the live MJPG buffer
                writer.set(cv::VIDEOWRITER_PROP_QUALITY, 100);
                isOpened = true;
            }

            if (!fd.image.empty()) {
                
                // THE FIX: Diagnostic Intercept to prove if the Process thread is ruining the frame
                if (fd.id == 1) {
                    std::cout << "[VideoEncoder] DIAGNOSTIC: Received Frame 1 from the Process thread at " 
                              << fd.image.cols << "x" << fd.image.rows << " pixels." << std::endl;
                }

                cv::Mat resized;
                cv::Size current_res;
                {
                    std::lock_guard<std::mutex> lock(archive_mutex_);
                    current_res = resolution_;
                }
                
                cv::resize(fd.image, resized, current_res);
                writer.write(resized);
                
                if (!keep_images_ && !fd.filepath.empty()) {
                    std::remove(fd.filepath.c_str());
                } else if (keep_images_) {
                    std::cout << "[VideoEncoder] PRESERVED raw image on SSD: " << fd.filepath << std::endl;
                }
            }
        } else {
            if (archive_pending_) {
                if (isOpened) {
                    writer.release(); 
                    isOpened = false;
                    
                    std::lock_guard<std::mutex> lock(archive_mutex_);
                    std::error_code ec;
                    
                    std::filesystem::rename(output_path_, archive_dest_, ec);
                    if (ec) {
                        std::cout << "[VideoEncoder] Cross-drive transfer detected. Moving file in background..." << std::endl;
                        std::filesystem::copy(output_path_, archive_dest_, std::filesystem::copy_options::overwrite_existing, ec);
                        if (!ec) std::filesystem::remove(output_path_);
                    }
                    std::cout << "[VideoEncoder] Video safely finalized and archived to: " << archive_dest_ << std::endl;
                }
                archive_pending_ = false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    if (isOpened) writer.release();
}

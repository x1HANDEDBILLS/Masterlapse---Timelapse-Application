#include "camera/camera.h"
#include "json.hpp"
#include <curl/curl.h>
#include <iostream>
#include <chrono>
#include <vector>

using json = nlohmann::json;

static size_t StringWriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

static size_t MemoryWriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    auto& mem = *static_cast<std::vector<uchar>*>(userp);
    auto* data = static_cast<uchar*>(contents);
    mem.insert(mem.end(), data, data + realsize);
    return realsize;
}

std::string Camera::httpGet(const std::string& url) {
    CURL* curl = curl_easy_init();
    std::string readBuffer;
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, StringWriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
        curl_easy_perform(curl);
        curl_easy_cleanup(curl);
    }
    return readBuffer;
}

Camera::Camera(Buffer& rawBuffer, Buffer& previewBuffer, int cameraId, int intervalMs)
    : raw_buffer_(rawBuffer), preview_buffer_(previewBuffer), camera_id_(cameraId), interval_ms_(intervalMs),
      target_width_(0), target_height_(0), running_(false), recording_(false), paused_(false), request_props_(false), resolution_changed_(false), camera_changed_(false), is_virtual_(false), is_gopro_api_(false), gopro_live_view_(false) {}

Camera::~Camera() { stop(); }

void Camera::start() {
    if (running_) return;
    running_ = true;
    worker_thread_ = std::thread(&Camera::captureLoop, this);
    std::cout << "[Camera] Background capture thread started." << std::endl;
}

void Camera::stop() {
    if (!running_) return;
    running_ = false;
    if (worker_thread_.joinable()) worker_thread_.join();
    std::cout << "[Camera] Capture thread stopped safely." << std::endl;
}

void Camera::setRecording(bool recording) { recording_ = recording; }
void Camera::setPaused(bool paused) { paused_ = paused; }
void Camera::setInterval(int intervalMs) { interval_ms_ = intervalMs; }
void Camera::setGoProLiveView(bool live) { gopro_live_view_ = live; }
void Camera::setResolution(int width, int height) {
    target_width_ = width;
    target_height_ = height;
    resolution_changed_ = true;
}
void Camera::requestPropertiesDialog() { request_props_ = true; }

void Camera::setCameraIndex(int index, bool isVirtual, bool isGoPro, const std::string& ip) {
    camera_id_ = index;
    is_virtual_ = isVirtual;
    is_gopro_api_ = isGoPro;
    gopro_ip_ = ip;
    camera_changed_ = true;
}

void Camera::captureLoop() {
    int current_hw_width = 0;
    int current_hw_height = 0;
    int frame_id = 1;
    bool was_recording = false;
    auto last_capture_time = std::chrono::steady_clock::now();
    auto warmup_until = std::chrono::steady_clock::now();
    bool hardware_fault = false;
    int network_strikes = 0; // [FIX] 3-Strike Counter

    if (camera_id_ != -1) { camera_changed_ = true; }

    while (running_) {
        // --- IDLE / OFFLINE STATE ---
        if (camera_id_ == -1) {
            cv::Mat offlineFrame(720, 1280, CV_8UC3, cv::Scalar(30, 30, 30));
            cv::putText(offlineFrame, "STANDBY - SELECT A CAMERA", cv::Point(250, 360), cv::FONT_HERSHEY_SIMPLEX, 1.5, cv::Scalar(255, 255, 255), 3);
            FrameData previewData; previewData.image = offlineFrame.clone(); preview_buffer_.push(previewData);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        // --- GOPRO NETWORK API PIPELINE ---
        if (is_gopro_api_) {
            if (camera_changed_) {
                if (cap_.isOpened()) cap_.release();
                std::cout << "[Camera] Connecting to GoPro Hero API at " << gopro_ip_ << "..." << std::endl;
                httpGet("http://" + gopro_ip_ + "/gopro/camera/control/wired_usb?p=1");
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                std::cout << "[Camera] Killing rogue recordings and forcing Photo Mode..." << std::endl;
                httpGet("http://" + gopro_ip_ + "/gopro/camera/shutter/stop");
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                httpGet("http://" + gopro_ip_ + "/gopro/camera/presets/set_group?id=1001");
                camera_changed_ = false;
                hardware_fault = false;
                network_strikes = 0;
            }

            bool is_preview_only = (!recording_ || paused_) && gopro_live_view_;

            if (!recording_ && !is_preview_only) {
                cv::Mat standby(720, 1280, CV_8UC3, cv::Scalar(40, 100, 40));
                cv::putText(standby, "GOPRO API READY - WAITING FOR CAPTURE", cv::Point(150, 360), cv::FONT_HERSHEY_SIMPLEX, 1.2, cv::Scalar(255, 255, 255), 3);
                cv::putText(standby, "Check 'API Live Framing' (Webcam Mode) to preview alignment.", cv::Point(120, 420), cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(200, 200, 200), 2);
                FrameData previewData; previewData.image = standby.clone(); preview_buffer_.push(previewData);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }

            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_capture_time).count();

            long current_interval = is_preview_only ? 3000 : interval_ms_.load();
            if (!is_preview_only && current_interval < 6000) {
                current_interval = 6000; // [FIX] Hard-limit GoPro to 6s minimum interval
            }

            if (elapsed >= current_interval) {
                if (is_preview_only) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                    continue; 
                }
                
                std::cout << "[Camera] Triggering GoPro REST API Shutter..." << std::endl;
                httpGet("http://" + gopro_ip_ + "/gopro/camera/shutter/start");
                
                // Allow sensor time to capture and write file
                for(int s=0; s<25 && running_; ++s) std::this_thread::sleep_for(std::chrono::milliseconds(100));
                
                std::string mediaList = httpGet("http://" + gopro_ip_ + "/gopro/media/list");
                
                if (mediaList.empty()) {
                    network_strikes++;
                    std::cerr << "[Camera] Warning: GoPro API timeout. Strike " << network_strikes << "/3." << std::endl;
                    if (network_strikes >= 3) {
                        std::cerr << "[Camera] CRITICAL: GoPro Connection Lost! Arming Auto-Reconnect..." << std::endl;
                        camera_changed_ = true;
                        network_strikes = 0;
                    }
                    last_capture_time = std::chrono::steady_clock::now();
                    continue;
                }

                try {
                    json j = json::parse(mediaList);
                    if (j.contains("media") && !j["media"].empty()) {
                        auto lastDir = j["media"].back();
                        std::string dirName = lastDir["d"];
                        if (lastDir.contains("fs") && !lastDir["fs"].empty()) {
                            auto lastFile = lastDir["fs"].back();
                            std::string fileName = lastFile["n"];

                            std::string downloadUrl = "http://" + gopro_ip_ + ":8080/videos/DCIM/" + dirName + "/" + fileName;
                            
                            std::vector<uchar> image_buffer;
                            CURL* curl = curl_easy_init();
                            if (curl) {
                                curl_easy_setopt(curl, CURLOPT_URL, downloadUrl.c_str());
                                curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, MemoryWriteCallback);
                                curl_easy_setopt(curl, CURLOPT_WRITEDATA, &image_buffer);
                                curl_easy_perform(curl);
                                curl_easy_cleanup(curl);

                                cv::Mat frame = cv::imdecode(image_buffer, cv::IMREAD_COLOR);
                                if (!frame.empty()) {
                                    if (target_width_ > 0 && target_height_ > 0 && (frame.cols != target_width_ || frame.rows != target_height_)) {
                                        double target_aspect = (double)target_width_ / target_height_;
                                        double frame_aspect = (double)frame.cols / frame.rows;
                                        
                                        if (std::abs(target_aspect - frame_aspect) > 0.01) {
                                            int crop_w = frame.cols; int crop_h = frame.rows;
                                            if (frame_aspect > target_aspect) crop_w = frame.rows * target_aspect;
                                            else crop_h = frame.cols / target_aspect;
                                            frame = frame(cv::Rect((frame.cols - crop_w) / 2, (frame.rows - crop_h) / 2, crop_w, crop_h));
                                        }
                                        cv::resize(frame, frame, cv::Size(target_width_, target_height_), 0, 0, cv::INTER_AREA);
                                    }
                                    
                                    FrameData pData; pData.image = frame.clone(); preview_buffer_.push(pData);
                                    
                                    if (!is_preview_only) {
                                        FrameData rData; rData.id = frame_id++; rData.image = frame.clone(); raw_buffer_.push(rData);
                                        std::cout << "[Camera] Network-to-Matrix Illusion Complete. Frame Pushed." << std::endl;
                                    }
                                }
                            }
                            httpGet("http://" + gopro_ip_ + "/gopro/media/delete/file?path=" + dirName + "/" + fileName);
                        }
                    }
                    network_strikes = 0; // Success resets strikes
                } catch (...) { 
                    network_strikes++;
                    std::cerr << "[Camera] Warning: Corrupt JSON from GoPro. Strike " << network_strikes << "/3." << std::endl;
                    if (network_strikes >= 3) {
                        std::cerr << "[Camera] CRITICAL: GoPro Connection Lost! Arming Auto-Reconnect..." << std::endl;
                        camera_changed_ = true;
                        network_strikes = 0;
                    }
                }
                last_capture_time = std::chrono::steady_clock::now();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        // --- STANDARD DIRECTSHOW & VIRTUAL CAMERA PIPELINE ---
        if (camera_changed_) {
            if (cap_.isOpened()) cap_.release();
            std::cout << "[Camera] Connecting to hardware index: " << camera_id_ << (is_virtual_ ? " (VIRTUAL MODE)" : " (HARDWARE MODE)") << "..." << std::endl;
            
            // Explicit MSMF fallback bypasses GStreamer probe spam
            int api = is_virtual_ ? cv::CAP_MSMF : cv::CAP_DSHOW;
            cap_.open(camera_id_, api);
            if (!cap_.isOpened() && is_virtual_) cap_.open(camera_id_, cv::CAP_DSHOW); 
            if (!cap_.isOpened()) cap_.open(camera_id_, cv::CAP_MSMF);
            
            camera_changed_ = false; resolution_changed_ = true; hardware_fault = false;
        }

        if (recording_ && !was_recording) {
            std::cout << "[Camera] Recording triggered. Stabilizing Auto-Focus/Exposure for 5 seconds..." << std::endl;
            frame_id = 1;
            warmup_until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            last_capture_time = warmup_until - std::chrono::milliseconds(interval_ms_);
        }
        was_recording = recording_;

        if (resolution_changed_ && target_width_ > 0 && target_height_ > 0) {
            std::cout << "[Camera] Applying dynamic resolution: " << target_width_ << "x" << target_height_ << "..." << std::endl;
            if (cap_.isOpened()) cap_.release();
            std::this_thread::sleep_for(std::chrono::milliseconds(500));

            // Explicit MSMF fallback during resolution re-opening
            int api = is_virtual_ ? cv::CAP_MSMF : cv::CAP_DSHOW;
            cap_.open(camera_id_, api);
            if (!cap_.isOpened()) cap_.open(camera_id_, cv::CAP_MSMF);

            if (!is_virtual_) cap_.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));
            
            cap_.set(cv::CAP_PROP_FRAME_WIDTH, target_width_);
            cap_.set(cv::CAP_PROP_FRAME_HEIGHT, target_height_);

            current_hw_width = cap_.get(cv::CAP_PROP_FRAME_WIDTH);
            current_hw_height = cap_.get(cv::CAP_PROP_FRAME_HEIGHT);
            
            for(int i = 0; i < 15; i++) { cv::Mat trash; cap_.read(trash); }
            std::this_thread::sleep_for(std::chrono::seconds(3));
            std::cout << "[Camera] Sensor ready." << std::endl;

            resolution_changed_ = false; hardware_fault = false; last_capture_time = std::chrono::steady_clock::now();
            continue;
        }

        if (hardware_fault) {
            cv::Mat offlineFrame(720, 1280, CV_8UC3, cv::Scalar(0, 0, 150));
            cv::putText(offlineFrame, "CAMERA DISCONNECTED - MANUAL RESTART REQUIRED", cv::Point(50, 360), cv::FONT_HERSHEY_SIMPLEX, 1.2, cv::Scalar(255, 255, 255), 3);
            FrameData pData; pData.image = offlineFrame.clone(); preview_buffer_.push(pData);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        if (request_props_ && cap_.isOpened() && !is_virtual_) {
            cap_.set(cv::CAP_PROP_SETTINGS, 1);
            request_props_ = false;
        }

        cv::Mat frame;
        if (cap_.isOpened() && cap_.read(frame) && !frame.empty()) {
            if (frame.cols != current_hw_width || frame.rows != current_hw_height) continue;

            FrameData pData; pData.image = frame.clone(); preview_buffer_.push(pData);

            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_capture_time).count();

            if (recording_ && !paused_ && now >= warmup_until && elapsed >= interval_ms_) {
                FrameData rData; rData.id = frame_id++; rData.image = frame.clone(); raw_buffer_.push(rData);
                last_capture_time = now;
            }
        } else {
            std::cerr << "[Camera] FATAL: Camera signal lost!" << std::endl;
            if (cap_.isOpened()) cap_.release();
            hardware_fault = true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    if (cap_.isOpened()) cap_.release();
}

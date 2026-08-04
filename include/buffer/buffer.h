#pragma once
#include <queue>
#include <mutex>
#include <string>
#include <condition_variable>
#include <opencv2/opencv.hpp>

struct FrameData {
    int id;
    cv::Mat image;
    std::string filepath;
};

class Buffer {
public:
    Buffer(size_t maxSize = 100);
    ~Buffer();

    bool push(const FrameData& frame);
    
    // Blocking pop (Used by background threads that need to sleep to save CPU)
    bool pop(FrameData& frame);
    
    // Non-blocking pop (Used by UI threads that need to keep refreshing instantly)
    bool try_pop(FrameData& frame);
    
    void stop(); 

private:
    std::queue<FrameData> queue_;
    std::mutex mutex_;
    std::condition_variable cond_full_;
    std::condition_variable cond_empty_;
    size_t max_size_;
    bool stopped_;
};

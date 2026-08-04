#include "buffer/buffer.h"

Buffer::Buffer(size_t maxSize) : max_size_(maxSize), stopped_(false) {}

Buffer::~Buffer() {
    stop();
}

bool Buffer::push(const FrameData& frame) {
    std::unique_lock<std::mutex> lock(mutex_);
    cond_full_.wait(lock, [this]() { return queue_.size() < max_size_ || stopped_; });
    
    if (stopped_) return false;

    queue_.push(frame);
    lock.unlock();
    cond_empty_.notify_one(); 
    return true;
}

bool Buffer::pop(FrameData& frame) {
    std::unique_lock<std::mutex> lock(mutex_);
    cond_empty_.wait(lock, [this]() { return !queue_.empty() || stopped_; });
    
    if (stopped_ && queue_.empty()) return false;

    frame = queue_.front();
    queue_.pop();
    lock.unlock();
    cond_full_.notify_one(); 
    return true;
}

// NEW: Non-blocking pop for high-speed UI loops
bool Buffer::try_pop(FrameData& frame) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (queue_.empty() || stopped_) return false;
    
    frame = queue_.front();
    queue_.pop();
    cond_full_.notify_one();
    return true;
}

void Buffer::stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    stopped_ = true;
    cond_empty_.notify_all();
    cond_full_.notify_all();
}

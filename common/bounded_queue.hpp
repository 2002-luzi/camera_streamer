#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>

namespace camera_streamer {

template <typename T>
class BoundedQueue {
public:
    explicit BoundedQueue(std::size_t capacity) : capacity_(capacity) {}

    bool Push(T value) {
        std::unique_lock<std::mutex> lock(mutex_);
        not_full_cv_.wait(lock, [&]() { return closed_ || queue_.size() < capacity_; });
        if (closed_) {
            return false;
        }
        queue_.push_back(std::move(value));
        not_empty_cv_.notify_one();
        return true;
    }

    std::optional<T> Pop() {
        std::unique_lock<std::mutex> lock(mutex_);
        not_empty_cv_.wait(lock, [&]() { return closed_ || !queue_.empty(); });
        if (queue_.empty()) {
            return std::nullopt;
        }
        T value = std::move(queue_.front());
        queue_.pop_front();
        not_full_cv_.notify_one();
        return value;
    }

    void Close() {
        std::lock_guard<std::mutex> lock(mutex_);
        closed_ = true;
        not_empty_cv_.notify_all();
        not_full_cv_.notify_all();
    }

    std::size_t Size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

private:
    const std::size_t capacity_;
    mutable std::mutex mutex_;
    std::condition_variable not_empty_cv_;
    std::condition_variable not_full_cv_;
    std::deque<T> queue_;
    bool closed_{false};
};

}  // namespace camera_streamer


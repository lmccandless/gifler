#pragma once

#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <optional>
#include <queue>

namespace gifler::record {

template <typename T>
class BoundedFrameQueue {
public:
    explicit BoundedFrameQueue(std::size_t capacity) : capacity_(capacity == 0 ? 1 : capacity) {}

    BoundedFrameQueue(const BoundedFrameQueue&) = delete;
    BoundedFrameQueue& operator=(const BoundedFrameQueue&) = delete;

    bool push(T value) {
        std::unique_lock lock(mutex_);
        notFull_.wait(lock, [&] { return closed_ || queue_.size() < capacity_; });
        if (closed_) {
            return false;
        }
        queue_.push(std::move(value));
        notEmpty_.notify_one();
        return true;
    }

    bool try_push(T value) {
        {
            std::lock_guard lock(mutex_);
            if (closed_ || queue_.size() >= capacity_) {
                return false;
            }
            queue_.push(std::move(value));
        }
        notEmpty_.notify_one();
        return true;
    }

    std::optional<T> pop() {
        std::unique_lock lock(mutex_);
        notEmpty_.wait(lock, [&] { return closed_ || !queue_.empty(); });
        if (queue_.empty()) {
            return std::nullopt;
        }
        T value = std::move(queue_.front());
        queue_.pop();
        notFull_.notify_one();
        return value;
    }

    void close() {
        {
            std::lock_guard lock(mutex_);
            closed_ = true;
        }
        notEmpty_.notify_all();
        notFull_.notify_all();
    }

    [[nodiscard]] std::size_t size() const {
        std::lock_guard lock(mutex_);
        return queue_.size();
    }

    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }

    [[nodiscard]] bool closed() const {
        std::lock_guard lock(mutex_);
        return closed_;
    }

private:
    std::size_t capacity_ = 1;
    mutable std::mutex mutex_{};
    std::condition_variable notEmpty_{};
    std::condition_variable notFull_{};
    bool closed_ = false;
    std::queue<T> queue_{};
};

} // namespace gifler::record

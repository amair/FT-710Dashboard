#pragma once
#include <vector>
#include <mutex>
#include <deque>

namespace audio {

class AudioTap {
public:
    void push_sample(float sample) {
        std::lock_guard lock(mutex_);
        buffer_.push_back(sample);
        if (buffer_.size() > 4800) {
            buffer_.erase(buffer_.begin(), buffer_.begin() + (buffer_.size() - 4800));
        }
    }

    std::vector<float> pull_samples(size_t count) {
        std::lock_guard lock(mutex_);
        std::vector<float> samples;
        size_t to_pull = std::min(count, buffer_.size());
        samples.reserve(to_pull);
        for (size_t i = 0; i < to_pull; ++i) {
            samples.push_back(buffer_.front());
            buffer_.pop_front();
        }
        return samples;
    }

    void clear() {
        std::lock_guard lock(mutex_);
        buffer_.clear();
    }

private:
    std::deque<float> buffer_;
    std::mutex mutex_;
};

}

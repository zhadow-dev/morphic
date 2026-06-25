#pragma once

#include <chrono>
#include <cstdint>

namespace morphic {

// Single monotonic time source for the entire system.
// ALL animations, transforms, and transitions read from this clock.
// No Flutter timers. No Win32 timers. No per-window timers.
class CompositionClock {
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    using Duration = std::chrono::duration<double>;

    void start() {
        startTime_ = Clock::now();
        lastTick_ = startTime_;
        frameCount_ = 0;
        running_ = true;
    }

    void stop() { running_ = false; }
    bool isRunning() const { return running_; }

    void tick() {
        auto now = Clock::now();
        delta_ = std::chrono::duration<double>(now - lastTick_).count();
        lastTick_ = now;
        frameCount_++;
    }

    // Seconds since start
    double now() const {
        return std::chrono::duration<double>(Clock::now() - startTime_).count();
    }

    // Seconds since last tick
    double delta() const { return delta_; }

    uint64_t frameCount() const { return frameCount_; }

    TimePoint lastTickTime() const { return lastTick_; }

private:
    TimePoint startTime_;
    TimePoint lastTick_;
    double delta_ = 0.0;
    uint64_t frameCount_ = 0;
    bool running_ = false;
};

}  // namespace morphic

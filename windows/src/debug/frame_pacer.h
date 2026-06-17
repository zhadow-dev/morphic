#pragma once

#include <chrono>
#include <vector>
#include <algorithm>
#include <cmath>
#include <windows.h>

namespace morphic {

// Phase 2A.3 — Frame Pacing Instrumentation.
//
// FPS is misleading. 258 stable FPS can feel smoother than 3000 unstable FPS.
// What matters is PERCEIVED COHERENCE — how stable are frame intervals?
//
// This tracker replaces FPS obsession with:
//   - Frame interval variance (σ of consecutive frame deltas)
//   - Pacing stability (% of frames within ±20% of rolling average interval)
//   - P99 frame duration (worst-case single frame cost)
//   - Compositor jitter (max deviation from rolling average cadence)
//   - Worst convergence duration (longest time from dirty→visual stable)
//
// THREAD: UI thread only (called from processFrame).
class FramePacer {
public:
    static constexpr size_t kHistorySize = 600;  // 10 seconds at 60fps
    static constexpr double kPacingTolerance = 0.20;  // ±20% of target interval

    struct PacingDistribution {
        double intervalMeanMs = 0.0;     // Average frame interval
        double intervalVarianceMs = 0.0; // Variance of frame intervals
        double intervalStdDevMs = 0.0;   // Std dev of frame intervals
        double pacingStability = 0.0;    // % of frames within ±20% of mean (0-100)
        double frameP99Ms = 0.0;         // 99th percentile frame duration
        double frameMaxMs = 0.0;         // Worst single frame
        double jitterMs = 0.0;           // Max deviation from rolling mean
        int totalFrames = 0;             // Frames in history
    };

    FramePacer() = default;

    // Call at the START of every processFrame().
    void onFrameBegin() {
        frameStart_ = std::chrono::high_resolution_clock::now();
    }

    // Call at the END of every processFrame().
    void onFrameEnd() {
        auto now = std::chrono::high_resolution_clock::now();

        // Frame duration (how long this frame took to process)
        double durationMs = std::chrono::duration<double, std::milli>(
            now - frameStart_).count();
        frameDurations_.push_back(durationMs);
        if (frameDurations_.size() > kHistorySize) {
            frameDurations_.erase(frameDurations_.begin());
        }

        // Frame interval (time between consecutive frame starts)
        if (lastFrameStart_.time_since_epoch().count() != 0) {
            double intervalMs = std::chrono::duration<double, std::milli>(
                frameStart_ - lastFrameStart_).count();
            frameIntervals_.push_back(intervalMs);
            if (frameIntervals_.size() > kHistorySize) {
                frameIntervals_.erase(frameIntervals_.begin());
            }
        }

        lastFrameStart_ = frameStart_;
        totalFramesEver_++;
    }

    // Record convergence event — time from dirty mark to visual stability.
    void recordConvergence(double durationMs) {
        if (durationMs > worstConvergenceMs_) {
            worstConvergenceMs_ = durationMs;
        }
        convergenceHistory_.push_back(durationMs);
        if (convergenceHistory_.size() > kHistorySize) {
            convergenceHistory_.erase(convergenceHistory_.begin());
        }
    }

    // Compute full pacing distribution from rolling history.
    PacingDistribution computeDistribution() const {
        PacingDistribution dist;

        if (frameIntervals_.empty()) return dist;

        dist.totalFrames = static_cast<int>(frameIntervals_.size());

        // Mean interval
        double sum = 0.0;
        for (double v : frameIntervals_) sum += v;
        dist.intervalMeanMs = sum / frameIntervals_.size();

        // Variance and StdDev
        double varianceSum = 0.0;
        double maxDeviation = 0.0;
        int withinTolerance = 0;

        for (double v : frameIntervals_) {
            double diff = v - dist.intervalMeanMs;
            varianceSum += diff * diff;

            double deviation = std::abs(diff);
            if (deviation > maxDeviation) maxDeviation = deviation;

            // Pacing: within ±20% of mean?
            if (dist.intervalMeanMs > 0.0) {
                double ratio = std::abs(v - dist.intervalMeanMs) / dist.intervalMeanMs;
                if (ratio <= kPacingTolerance) withinTolerance++;
            }
        }

        dist.intervalVarianceMs = varianceSum / frameIntervals_.size();
        dist.intervalStdDevMs = std::sqrt(dist.intervalVarianceMs);
        dist.jitterMs = maxDeviation;
        dist.pacingStability = (static_cast<double>(withinTolerance) /
                                frameIntervals_.size()) * 100.0;

        // P99 and max frame duration
        if (!frameDurations_.empty()) {
            std::vector<double> sorted = frameDurations_;
            std::sort(sorted.begin(), sorted.end());

            size_t p99Idx = static_cast<size_t>(sorted.size() * 0.99);
            if (p99Idx >= sorted.size()) p99Idx = sorted.size() - 1;
            dist.frameP99Ms = sorted[p99Idx];
            dist.frameMaxMs = sorted.back();
        }

        return dist;
    }

    double worstConvergenceMs() const { return worstConvergenceMs_; }
    int totalFramesEver() const { return totalFramesEver_; }

    // Reset (for test isolation).
    void reset() {
        frameIntervals_.clear();
        frameDurations_.clear();
        convergenceHistory_.clear();
        worstConvergenceMs_ = 0.0;
        totalFramesEver_ = 0;
        lastFrameStart_ = {};
        frameStart_ = {};
    }

private:
    std::vector<double> frameIntervals_;      // ms between frame starts
    std::vector<double> frameDurations_;       // ms per frame processing
    std::vector<double> convergenceHistory_;   // ms per convergence event

    std::chrono::high_resolution_clock::time_point lastFrameStart_{};
    std::chrono::high_resolution_clock::time_point frameStart_{};
    double worstConvergenceMs_ = 0.0;
    int totalFramesEver_ = 0;
};

}  // namespace morphic

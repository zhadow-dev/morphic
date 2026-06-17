#pragma once

#include "../core/types.h"
#include <chrono>
#include <vector>
#include <algorithm>
#include <cmath>

namespace morphic {

// Phase 2A.2 — Input-to-photon latency instrumentation.
//
// Tracks the full pipeline from user input to visual response:
//   WM_MOUSEMOVE → compositor transaction → renderer present → convergence
//
// This is the most important perceived-quality metric.
// Users feel latency, not FPS.
//
// Distributions tracked: avg, P50, P95, P99, max, worst-case persistence.
// "Persistence" = how many consecutive frames exceeded budget.
class InputPhotonTracker {
public:
    // A single input-to-visual measurement.
    struct LatencySample {
        double inputToCompositorMs = 0.0;   // Input event → compositor processes
        double compositorToRenderMs = 0.0;  // Compositor commit → renderer present
        double totalMs = 0.0;               // Input → visual convergence
        uint64_t compositorFrame = 0;
        std::chrono::high_resolution_clock::time_point inputTime;
        std::chrono::high_resolution_clock::time_point compositorTime;
        std::chrono::high_resolution_clock::time_point renderTime;
    };

    struct LatencyDistribution {
        double p50 = 0.0;
        double p95 = 0.0;
        double p99 = 0.0;
        double max = 0.0;
        double avg = 0.0;
        size_t sampleCount = 0;
    };

    // Jank persistence: how many consecutive frames exceeded budget.
    struct JankPersistence {
        int currentStreak = 0;      // Current consecutive jank frames
        int maxStreak = 0;          // Worst streak observed
        int totalJankFrames = 0;    // Total frames exceeding budget
        double budgetMs = 16.67;    // Frame budget (default 60fps)
    };

    static constexpr size_t kMaxSamples = 600;
    static constexpr double kDefaultBudgetMs = 16.67;

    InputPhotonTracker() = default;

    // --- Recording ---

    // Called when WM_MOUSEMOVE (or other input) arrives in WndProc.
    void recordInputEvent() {
        pendingInputTime_ = std::chrono::high_resolution_clock::now();
        hasPendingInput_ = true;
    }

    // Called when compositor processes the input into a transaction.
    void recordCompositorProcess(uint64_t compositorFrame) {
        if (!hasPendingInput_) return;

        pendingCompositorTime_ = std::chrono::high_resolution_clock::now();
        pendingFrame_ = compositorFrame;
    }

    // Called when renderer presents after the compositor commit.
    void recordRenderPresent() {
        if (!hasPendingInput_) return;

        auto now = std::chrono::high_resolution_clock::now();

        LatencySample sample;
        sample.inputTime = pendingInputTime_;
        sample.compositorTime = pendingCompositorTime_;
        sample.renderTime = now;
        sample.compositorFrame = pendingFrame_;

        using Ms = std::chrono::duration<double, std::milli>;
        sample.inputToCompositorMs = std::chrono::duration_cast<Ms>(
            pendingCompositorTime_ - pendingInputTime_).count();
        sample.compositorToRenderMs = std::chrono::duration_cast<Ms>(
            now - pendingCompositorTime_).count();
        sample.totalMs = std::chrono::duration_cast<Ms>(
            now - pendingInputTime_).count();

        samples_.push_back(sample);
        if (samples_.size() > kMaxSamples) {
            samples_.erase(samples_.begin());
        }

        // Update jank persistence
        if (sample.totalMs > jank_.budgetMs) {
            jank_.currentStreak++;
            jank_.totalJankFrames++;
            jank_.maxStreak = (std::max)(jank_.maxStreak, jank_.currentStreak);
        } else {
            jank_.currentStreak = 0;
        }

        hasPendingInput_ = false;
    }

    // --- Queries ---

    LatencyDistribution computeDistribution() const {
        LatencyDistribution dist;
        if (samples_.empty()) return dist;

        std::vector<double> values;
        values.reserve(samples_.size());
        for (const auto& s : samples_) {
            values.push_back(s.totalMs);
        }
        std::sort(values.begin(), values.end());

        dist.sampleCount = values.size();
        dist.avg = 0.0;
        for (double v : values) dist.avg += v;
        dist.avg /= values.size();

        dist.p50 = percentile(values, 50);
        dist.p95 = percentile(values, 95);
        dist.p99 = percentile(values, 99);
        dist.max = values.back();

        return dist;
    }

    const JankPersistence& jankPersistence() const { return jank_; }

    // Most recent latency.
    double lastTotalMs() const {
        return samples_.empty() ? 0.0 : samples_.back().totalMs;
    }

    size_t sampleCount() const { return samples_.size(); }

    void setJankBudget(double budgetMs) { jank_.budgetMs = budgetMs; }

    void reset() {
        samples_.clear();
        jank_ = JankPersistence{};
        hasPendingInput_ = false;
    }

private:
    static double percentile(const std::vector<double>& sorted, int pct) {
        if (sorted.empty()) return 0.0;
        double idx = (pct / 100.0) * (sorted.size() - 1);
        size_t lo = static_cast<size_t>(idx);
        size_t hi = (std::min)(lo + 1, sorted.size() - 1);
        double frac = idx - lo;
        return sorted[lo] * (1.0 - frac) + sorted[hi] * frac;
    }

    std::vector<LatencySample> samples_;
    JankPersistence jank_;

    bool hasPendingInput_ = false;
    std::chrono::high_resolution_clock::time_point pendingInputTime_;
    std::chrono::high_resolution_clock::time_point pendingCompositorTime_;
    uint64_t pendingFrame_ = 0;
};

}  // namespace morphic

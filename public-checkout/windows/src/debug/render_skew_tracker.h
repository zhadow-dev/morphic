#pragma once

#include "../core/types.h"
#include "../core/thread_affinity.h"
#include <chrono>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <cmath>
#include <string>

namespace morphic {

// Phase 2A.2 — Cross-surface render skew instrumentation.
//
// Tracks temporal coherence between multiple renderer surfaces.
// "Skew" is the frame-time difference between the fastest and slowest
// renderer in a given compositor frame. High skew = visual desync.
//
// Distributions are tracked (P50/P95/P99/max), NOT averages.
// Users notice worst-case persistence, not average timing.
//
// Also tracks convergence duration: how many consecutive frames
// a renderer stays behind after a compositor commit (resize, move).
class RenderSkewTracker {
public:
    // Per-renderer frame record.
    struct RendererFrameStamp {
        RenderId rendererId = 0;
        NodeId surfaceId = 0;
        uint64_t compositorFrame = 0;    // Compositor frame that triggered this
        uint64_t rendererFrame = 0;      // Renderer's own frame counter
        double presentLatencyMs = 0.0;   // Time from compositor commit to present
        std::chrono::high_resolution_clock::time_point timestamp;
    };

    // Skew snapshot for a single compositor frame.
    struct FrameSkew {
        uint64_t compositorFrame = 0;
        double maxSkewMs = 0.0;          // Max pairwise skew across renderers
        double minPresentMs = 0.0;       // Fastest renderer present
        double maxPresentMs = 0.0;       // Slowest renderer present
        int rendererCount = 0;           // How many renderers were active
        bool allConverged = true;        // Did all renderers present this frame?
    };

    // Distribution stats.
    struct SkewDistribution {
        double p50 = 0.0;
        double p95 = 0.0;
        double p99 = 0.0;
        double max = 0.0;
        double avg = 0.0;
        size_t sampleCount = 0;
    };

    // Per-renderer convergence tracking.
    struct ConvergenceRecord {
        RenderId rendererId = 0;
        uint64_t lastCommitFrame = 0;     // Last compositor frame that moved/resized this
        uint64_t lastPresentFrame = 0;    // Last frame renderer actually presented
        int framesBehin = 0;              // How many frames behind compositor
        int maxFramesBehind = 0;          // Worst-case lag observed
        double convergenceMs = 0.0;       // Time from commit to visual convergence
        double worstConvergenceMs = 0.0;  // Worst convergence time observed
    };

    static constexpr size_t kMaxSamples = 600;  // ~10 seconds at 60fps

    RenderSkewTracker() = default;

    // --- Recording ---

    // Called by compositor at start of processFrame.
    void beginFrame(uint64_t compositorFrame) {
        currentFrame_ = compositorFrame;
        frameStamps_.clear();
    }

    // Record a renderer's frame presentation within this compositor frame.
    void recordPresent(RenderId rendererId, NodeId surfaceId,
                       uint64_t rendererFrame, double presentLatencyMs) {
        RendererFrameStamp stamp;
        stamp.rendererId = rendererId;
        stamp.surfaceId = surfaceId;
        stamp.compositorFrame = currentFrame_;
        stamp.rendererFrame = rendererFrame;
        stamp.presentLatencyMs = presentLatencyMs;
        stamp.timestamp = std::chrono::high_resolution_clock::now();
        frameStamps_.push_back(stamp);

        // Update convergence
        auto& conv = convergence_[rendererId];
        conv.rendererId = rendererId;
        conv.lastPresentFrame = rendererFrame;
    }

    // Record that compositor committed a layout change for a renderer.
    void recordCommit(RenderId rendererId, uint64_t compositorFrame) {
        auto& conv = convergence_[rendererId];
        conv.rendererId = rendererId;
        conv.lastCommitFrame = compositorFrame;
    }

    // Called at end of processFrame — compute skew for this frame.
    void endFrame() {
        if (frameStamps_.size() < 2) return;  // Need 2+ renderers for skew

        FrameSkew skew;
        skew.compositorFrame = currentFrame_;
        skew.rendererCount = static_cast<int>(frameStamps_.size());

        double minMs = 1e9, maxMs = 0.0;
        for (const auto& s : frameStamps_) {
            minMs = (std::min)(minMs, s.presentLatencyMs);
            maxMs = (std::max)(maxMs, s.presentLatencyMs);
        }
        skew.minPresentMs = minMs;
        skew.maxPresentMs = maxMs;
        skew.maxSkewMs = maxMs - minMs;

        // Check convergence
        for (auto& [id, conv] : convergence_) {
            if (conv.lastCommitFrame > conv.lastPresentFrame) {
                conv.framesBehin++;
                conv.maxFramesBehind = (std::max)(conv.maxFramesBehind, conv.framesBehin);
                skew.allConverged = false;
            } else {
                conv.framesBehin = 0;
            }
        }

        // Store in rolling buffer
        skewHistory_.push_back(skew);
        if (skewHistory_.size() > kMaxSamples) {
            skewHistory_.erase(skewHistory_.begin());
        }
    }

    // --- Queries ---

    // Compute skew distribution over recent history.
    SkewDistribution computeSkewDistribution() const {
        SkewDistribution dist;
        if (skewHistory_.empty()) return dist;

        std::vector<double> values;
        values.reserve(skewHistory_.size());
        for (const auto& s : skewHistory_) {
            values.push_back(s.maxSkewMs);
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

    // Get the most recent frame skew.
    FrameSkew lastSkew() const {
        return skewHistory_.empty() ? FrameSkew{} : skewHistory_.back();
    }

    // Get convergence records for all tracked renderers.
    const std::unordered_map<RenderId, ConvergenceRecord>& convergence() const {
        return convergence_;
    }

    // Total frames with skew data.
    size_t sampleCount() const { return skewHistory_.size(); }

    // Reset all tracking.
    void reset() {
        skewHistory_.clear();
        convergence_.clear();
        frameStamps_.clear();
        currentFrame_ = 0;
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

    uint64_t currentFrame_ = 0;
    std::vector<RendererFrameStamp> frameStamps_;
    std::vector<FrameSkew> skewHistory_;
    std::unordered_map<RenderId, ConvergenceRecord> convergence_;
};

}  // namespace morphic

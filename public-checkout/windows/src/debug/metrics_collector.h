#pragma once

#include <deque>
#include <chrono>
#include <string>
#include <cmath>
#include <algorithm>
#include <numeric>

namespace morphic {

// --- Per-frame metrics ---
struct FrameMetrics {
    double frameTimeMs = 0.0;
    double dwmCommitCostMs = 0.0;
    int dirtyNodeCount = 0;
    int hwndCount = 0;
    int deferWindowPosCount = 0;
    bool dropped = false;
};

// --- Synchronization error (THE primary quality metric) ---
struct SyncMetrics {
    double maxPositionalDivergence = 0.0;   // px — scene graph vs GetWindowRect
    double maxGroupDesync = 0.0;            // px — between group members during drag
    double accumulatedDrift = 0.0;          // px — total position error over session
    int divergenceEvents = 0;               // count of frames where divergence > 0.5px
};

// --- Per-stage time budgets ---
// Calibrated from Phase 1B testing. During drag/resize, scene and DWM
// stages do real work (dirty nodes, batch flush). These budgets allow
// headroom for interactive operations while catching genuine regressions.
struct StageTiming {
    double sceneResolveMs = 0.0;        // budget: < 1.0ms
    double dwmBatchFlushMs = 0.0;       // budget: < 2.0ms
    double elevationResolveMs = 0.0;    // budget: < 1.0ms
    double invariantCheckMs = 0.0;      // budget: < 2.0ms (debug only)
    double debugOverlayMs = 0.0;        // budget: < 2.0ms
    double totalFrameMs = 0.0;          // budget: < 4.0ms (excl. overlay + invariants)
    int budgetViolations = 0;

    // Core = scene + dwm + elevation (excludes debug-only work)
    double coreFrameMs() const {
        return sceneResolveMs + dwmBatchFlushMs + elevationResolveMs;
    }

    bool withinBudget() const {
        return sceneResolveMs < 1.0 &&
               dwmBatchFlushMs < 2.0 &&
               elevationResolveMs < 1.0 &&
               coreFrameMs() < 4.0;
    }
};

// --- Scheduler health ---
struct SchedulerMetrics {
    int frameRequestCount = 0;          // total requestFrame() calls
    int actualFrameCount = 0;           // total processFrame() calls
    double coalescingRatio = 0.0;       // requests / actual (> 1.0 = coalescing works)
    double maxFrameGapMs = 0.0;         // longest gap between frames
    int starvationEvents = 0;           // gaps > 3x target frame time
    int consecutiveDrops = 0;           // current streak
    int maxConsecutiveDrops = 0;        // worst streak
    // Queue pressure — detects scheduler pressure before drops appear
    int pendingTransactionDepth = 0;    // transactions queued but not committed
    double maxQueueResidenceMs = 0.0;   // longest a request waited
};

// --- Scaling curves ---
// Cost-per-unit metrics via exponential moving average (alpha=0.1).
// Answers: "which subsystem scales poorly as surface count grows?"
struct ScalingMetrics {
    double costPerSurface = 0.0;        // coreFrameMs / hwndCount
    double costPerDirtyNode = 0.0;      // sceneResolveMs / dirtyNodeCount
    double costPerHostUpdate = 0.0;     // dwmBatchFlushMs / deferWindowPosCount
    int sampleCount = 0;

    void update(double coreMs, int hwnds, double sceneMs, int dirty, double dwmMs, int defers) {
        const double alpha = 0.1;
        if (hwnds > 0)
            costPerSurface = costPerSurface * (1.0 - alpha) + (coreMs / hwnds) * alpha;
        if (dirty > 0)
            costPerDirtyNode = costPerDirtyNode * (1.0 - alpha) + (sceneMs / dirty) * alpha;
        if (defers > 0)
            costPerHostUpdate = costPerHostUpdate * (1.0 - alpha) + (dwmMs / defers) * alpha;
        sampleCount++;
    }
};

// --- Frame drop forensics ---
// When a drop occurs, capture WHY so drops can be classified as
// OS noise vs pathological.
struct FrameDropForensics {
    uint64_t frameNumber = 0;
    double frameTimeMs = 0.0;
    double sceneMs = 0.0;
    double dwmMs = 0.0;
    double elevMs = 0.0;
    double invariantMs = 0.0;
    double overlayMs = 0.0;
    int dirtyNodes = 0;
    int deferCalls = 0;
    bool debugOverlayActive = false;
    double gapSinceLastFrameMs = 0.0;  // If large: OS scheduling noise

    std::string classify() const {
        if (gapSinceLastFrameMs > 50.0) return "OS_SCHEDULING_NOISE";
        if (overlayMs > frameTimeMs * 0.5) return "DEBUG_OVERLAY_OVERHEAD";
        if (invariantMs > frameTimeMs * 0.3) return "INVARIANT_CHECK_OVERHEAD";
        if (dwmMs > 2.0) return "DWM_STALL";
        if (sceneMs > 1.0) return "SCENE_RESOLVE_SLOW";
        return "UNKNOWN";
    }
};

// Performance telemetry collector.
// Rolling window of frame metrics for monitoring compositor health.
class MetricsCollector {
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    void beginFrame() {
        current_ = {};
        stageTiming_ = {};  // Reset per-frame — only stages that run this frame are timed
        frameStart_ = Clock::now();

        // Track frame gap (time since last frame ended)
        if (lastFrameEnd_.time_since_epoch().count() > 0) {
            double gapMs = std::chrono::duration<double, std::milli>(
                frameStart_ - lastFrameEnd_).count();
            if (gapMs > scheduler_.maxFrameGapMs) {
                scheduler_.maxFrameGapMs = gapMs;
            }
            if (targetFrameTimeMs_ > 0 && gapMs > targetFrameTimeMs_ * 3.0) {
                scheduler_.starvationEvents++;
            }
        }

        scheduler_.actualFrameCount++;
    }

    void endFrame() {
        auto end = Clock::now();
        current_.frameTimeMs = std::chrono::duration<double, std::milli>(end - frameStart_).count();
        stageTiming_.totalFrameMs = current_.frameTimeMs;

        // Frame gap for forensics
        double gapMs = 0.0;
        if (lastFrameEnd_.time_since_epoch().count() > 0) {
            gapMs = std::chrono::duration<double, std::milli>(
                frameStart_ - lastFrameEnd_).count();
        }

        // Dropped frame detection
        if (targetFrameTimeMs_ > 0 && current_.frameTimeMs > targetFrameTimeMs_ * 1.5) {
            current_.dropped = true;
            totalDropped_++;
            scheduler_.consecutiveDrops++;
            if (scheduler_.consecutiveDrops > scheduler_.maxConsecutiveDrops) {
                scheduler_.maxConsecutiveDrops = scheduler_.consecutiveDrops;
            }

            // Capture forensics — classify WHY this drop happened
            FrameDropForensics forensics;
            forensics.frameNumber = scheduler_.actualFrameCount;
            forensics.frameTimeMs = current_.frameTimeMs;
            forensics.sceneMs = stageTiming_.sceneResolveMs;
            forensics.dwmMs = stageTiming_.dwmBatchFlushMs;
            forensics.elevMs = stageTiming_.elevationResolveMs;
            forensics.invariantMs = stageTiming_.invariantCheckMs;
            forensics.overlayMs = stageTiming_.debugOverlayMs;
            forensics.dirtyNodes = current_.dirtyNodeCount;
            forensics.deferCalls = current_.deferWindowPosCount;
            forensics.gapSinceLastFrameMs = gapMs;
            dropForensics_.push_back(forensics);
            if (dropForensics_.size() > 100) dropForensics_.erase(dropForensics_.begin());

            // Log classification to debug output
            std::string msg = "FRAME DROP #" + std::to_string(totalDropped_) +
                " [" + forensics.classify() + "] " +
                std::to_string(current_.frameTimeMs) + "ms" +
                " gap=" + std::to_string(gapMs) + "ms" +
                " scene=" + std::to_string(stageTiming_.sceneResolveMs) +
                " dwm=" + std::to_string(stageTiming_.dwmBatchFlushMs) +
                " overlay=" + std::to_string(stageTiming_.debugOverlayMs) + "\n";
            OutputDebugStringA(msg.c_str());
        } else {
            scheduler_.consecutiveDrops = 0;
        }

        // Budget violation tracking
        if (!stageTiming_.withinBudget()) {
            stageTiming_.budgetViolations++;
            totalBudgetViolations_++;
        }

        // Coalescing ratio
        if (scheduler_.actualFrameCount > 0) {
            scheduler_.coalescingRatio =
                static_cast<double>(scheduler_.frameRequestCount) /
                static_cast<double>(scheduler_.actualFrameCount);
        }

        // Scaling curves (EMA update)
        scaling_.update(
            stageTiming_.coreFrameMs(), current_.hwndCount,
            stageTiming_.sceneResolveMs, current_.dirtyNodeCount,
            stageTiming_.dwmBatchFlushMs, current_.deferWindowPosCount);

        history_.push_back(current_);
        if (history_.size() > maxHistory_) {
            history_.pop_front();
        }

        lastFrameEnd_ = end;
    }

    void setTargetFPS(int fps) {
        targetFrameTimeMs_ = 1000.0 / fps;
    }

    // --- Basic recorders ---
    void recordDirtyNodes(int count) { current_.dirtyNodeCount = count; }
    void recordHwndCount(int count) { current_.hwndCount = count; }
    void recordDeferCount(int count) { current_.deferWindowPosCount = count; }
    void recordFrameRequest() { scheduler_.frameRequestCount++; }

    void recordDwmCommitStart() {
        dwmStart_ = Clock::now();
    }

    void recordDwmCommitEnd() {
        auto end = Clock::now();
        current_.dwmCommitCostMs = std::chrono::duration<double, std::milli>(end - dwmStart_).count();
    }

    // --- Stage timing recorders ---
    TimePoint stageTimer() const { return Clock::now(); }

    void recordStageSceneResolve(TimePoint start) {
        stageTiming_.sceneResolveMs = msFrom(start);
    }
    void recordStageDwmFlush(TimePoint start) {
        stageTiming_.dwmBatchFlushMs = msFrom(start);
    }
    void recordStageElevation(TimePoint start) {
        stageTiming_.elevationResolveMs = msFrom(start);
    }
    void recordStageInvariantCheck(TimePoint start) {
        stageTiming_.invariantCheckMs = msFrom(start);
    }
    void recordStageDebugOverlay(TimePoint start) {
        stageTiming_.debugOverlayMs = msFrom(start);
    }

    // --- Sync error recorders ---
    void recordPositionalDivergence(double maxDivergence) {
        if (maxDivergence > sync_.maxPositionalDivergence) {
            sync_.maxPositionalDivergence = maxDivergence;
        }
        sync_.accumulatedDrift += maxDivergence;
        if (maxDivergence > 0.5) {
            sync_.divergenceEvents++;
        }
    }

    void recordGroupDesync(double maxDesync) {
        if (maxDesync > sync_.maxGroupDesync) {
            sync_.maxGroupDesync = maxDesync;
        }
    }

    // --- Queries ---
    double avgFrameTime(size_t window = 60) const {
        return rollingAvg(window, [](const FrameMetrics& m) { return m.frameTimeMs; });
    }

    double avgDwmCost(size_t window = 60) const {
        return rollingAvg(window, [](const FrameMetrics& m) { return m.dwmCommitCostMs; });
    }

    double frameTimeP99() const {
        if (history_.empty()) return 0.0;
        std::vector<double> times;
        times.reserve(history_.size());
        for (const auto& m : history_) times.push_back(m.frameTimeMs);
        std::sort(times.begin(), times.end());
        size_t idx = static_cast<size_t>(times.size() * 0.99);
        if (idx >= times.size()) idx = times.size() - 1;
        return times[idx];
    }

    double frameTimeP95() const {
        if (history_.empty()) return 0.0;
        std::vector<double> times;
        times.reserve(history_.size());
        for (const auto& m : history_) times.push_back(m.frameTimeMs);
        std::sort(times.begin(), times.end());
        size_t idx = static_cast<size_t>(times.size() * 0.95);
        if (idx >= times.size()) idx = times.size() - 1;
        return times[idx];
    }

    double frameTimeStdDev(size_t window = 60) const {
        if (history_.empty()) return 0.0;
        size_t n = (std::min)(window, history_.size());
        double avg = avgFrameTime(window);
        double sumSq = 0.0;
        for (size_t i = history_.size() - n; i < history_.size(); i++) {
            double d = history_[i].frameTimeMs - avg;
            sumSq += d * d;
        }
        return std::sqrt(sumSq / n);
    }

    int totalDroppedFrames() const { return totalDropped_; }
    int totalBudgetViolations() const { return totalBudgetViolations_; }

    const FrameMetrics& lastFrame() const {
        static FrameMetrics empty;
        return history_.empty() ? empty : history_.back();
    }

    double currentFPS() const {
        double avg = avgFrameTime(30);
        return avg > 0 ? 1000.0 / avg : 0.0;
    }

    const SyncMetrics& syncMetrics() const { return sync_; }
    const StageTiming& stageTiming() const { return stageTiming_; }
    const SchedulerMetrics& schedulerMetrics() const { return scheduler_; }
    const ScalingMetrics& scalingMetrics() const { return scaling_; }
    const std::vector<FrameDropForensics>& dropForensics() const { return dropForensics_; }

    void resetSync() { sync_ = {}; }
    void resetScheduler() { scheduler_ = {}; }

private:
    double msFrom(TimePoint start) const {
        return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    }

    template<typename Fn>
    double rollingAvg(size_t window, Fn extractor) const {
        if (history_.empty()) return 0.0;
        size_t n = (std::min)(window, history_.size());
        double sum = 0.0;
        for (size_t i = history_.size() - n; i < history_.size(); i++) {
            sum += extractor(history_[i]);
        }
        return sum / n;
    }

    std::deque<FrameMetrics> history_;
    FrameMetrics current_;
    TimePoint frameStart_;
    TimePoint lastFrameEnd_;
    TimePoint dwmStart_;
    double targetFrameTimeMs_ = 16.67;  // 60fps default
    int totalDropped_ = 0;
    int totalBudgetViolations_ = 0;
    size_t maxHistory_ = 300;  // ~5 seconds at 60fps

    SyncMetrics sync_;
    StageTiming stageTiming_;
    SchedulerMetrics scheduler_;
    ScalingMetrics scaling_;
    std::vector<FrameDropForensics> dropForensics_; // last N drops
};

}  // namespace morphic

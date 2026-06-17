#pragma once

#include <windows.h>
#include <chrono>
#include <vector>
#include <map>
#include <mutex>
#include <string>
#include <algorithm>
#include <cmath>

namespace morphic {

// Phase 2A.5 — Frame Cadence Monitor.
//
// Shared static storage for cross-engine frame telemetry.
// Secondary Flutter engines call frameProduced() which updates this.
// Main engine queries it via queryFrameCadence().
//
// Stores:
//   - Total frame count per engine
//   - Rolling timestamp buffer (last 200 frames)
//   - Interval histogram (bucketed by ms)
//   - Activity state classification
class FrameCadenceMonitor {
public:

    // Renderer Activity States
    enum class ActivityState {
        ActiveVisible,     // Producing visible frames
        ActiveHidden,      // Frame cadence continues while hidden
        TimerOnly,         // No frame cadence, but timers active
        Dormant,           // No meaningful wake activity
        Decaying,          // Activity reducing over time
        Runaway            // Sustained hidden frame cadence
    };

    static const char* stateName(ActivityState s) {
        switch (s) {
            case ActivityState::ActiveVisible: return "ActiveVisible";
            case ActivityState::ActiveHidden: return "ActiveHidden";
            case ActivityState::TimerOnly: return "TimerOnly";
            case ActivityState::Dormant: return "Dormant";
            case ActivityState::Decaying: return "Decaying";
            case ActivityState::Runaway: return "Runaway";
            default: return "Unknown";
        }
    }

    // Per-engine frame telemetry
    struct EngineFrameData {
        int engineId = 0;
        int64_t totalFrames = 0;
        bool isHidden = false;
        bool animationsPaused = false;

        // Rolling timestamp buffer (last 200 frame times in ms since epoch)
        static constexpr int kMaxTimestamps = 200;
        std::vector<double> timestamps;

        // Interval histogram (bucket = ms, value = count)
        std::map<int, int> intervalHistogram;

        // Computed metrics
        double avgIntervalMs = 0.0;
        double estimatedFps = 0.0;
        int64_t framesWhileHidden = 0;
        int64_t framesWhileAnimPaused = 0;
        ActivityState state = ActivityState::Dormant;

        // Transition tracking
        double lastVisibilityChangeTime = 0.0;
        int64_t framesAtLastTransition = 0;

        // Phase 2B.1: Recovery telemetry
        double lastRecoveryMs = -1.0;      // ms from resume to first frame
        int resumeCycleCount = 0;           // total park/resume cycles
        int framesWhileLastParked = 0;      // frames during last parked period
        double bestRecoveryMs = 1e9;        // best recovery across all cycles
        double worstRecoveryMs = 0.0;       // worst recovery across all cycles
        double totalRecoveryMs = 0.0;       // sum for average calculation

        // Native-side recovery tracking (B3 fix)
        double resumeSentTimestampMs = 0.0;  // when we sent resumed to this engine
        bool waitingForRecoveryFrame = false; // true after resume, false after first frame
        int64_t framesAtPark = 0;            // frame count when parked
    };

    // Singleton access
    static FrameCadenceMonitor& instance() {
        static FrameCadenceMonitor inst;
        return inst;
    }

    // Phase 3C: Register a callback to receive frame events.
    // engineId is the Dart-side engine ID.
    // Callback is called under the monitor's lock — keep it fast.
    using FrameCallback = void(*)(int engineId, void* userData);
    void setFrameCallback(FrameCallback cb, void* userData) {
        frameCallback_ = cb;
        frameCallbackData_ = userData;
    }

    // Called by secondary engines on each frame produced
    void recordFrame(int engineId, double timestampMs) {
        std::lock_guard<std::mutex> lock(mutex_);

        auto& data = engines_[engineId];
        data.engineId = engineId;
        data.totalFrames++;

        // Native-side recovery measurement (B3 fix)
        // If we're waiting for the first frame after resume, compute recovery latency
        if (data.waitingForRecoveryFrame) {
            double now = nowMs();
            double recoveryMs = now - data.resumeSentTimestampMs;
            data.lastRecoveryMs = recoveryMs;
            data.resumeCycleCount++;
            data.framesWhileLastParked = static_cast<int>(data.totalFrames - 1 - data.framesAtPark);
            if (recoveryMs < data.bestRecoveryMs) data.bestRecoveryMs = recoveryMs;
            if (recoveryMs > data.worstRecoveryMs) data.worstRecoveryMs = recoveryMs;
            data.totalRecoveryMs += recoveryMs;
            data.waitingForRecoveryFrame = false;

            OutputDebugStringA(("CADENCE_MONITOR: Engine " + std::to_string(engineId) +
                " recovery=" + std::to_string(recoveryMs) + "ms" +
                " cycle=" + std::to_string(data.resumeCycleCount) + "\n").c_str());
        }

        // Track hidden frame production
        if (data.isHidden) data.framesWhileHidden++;
        if (data.animationsPaused) data.framesWhileAnimPaused++;

        // Rolling timestamp buffer
        data.timestamps.push_back(timestampMs);
        if (data.timestamps.size() > EngineFrameData::kMaxTimestamps) {
            data.timestamps.erase(data.timestamps.begin());
        }

        // Update interval histogram (if we have at least 2 timestamps)
        if (data.timestamps.size() >= 2) {
            double interval = data.timestamps.back() -
                data.timestamps[data.timestamps.size() - 2];
            int bucket = static_cast<int>(std::round(interval));
            if (bucket < 0) bucket = 0;
            if (bucket > 1000) bucket = 1000;  // Cap at 1s
            data.intervalHistogram[bucket]++;
        }

        // Recompute metrics periodically (every 50 frames)
        if (data.totalFrames % 50 == 0) {
            recomputeMetrics(data);
        }

        // Phase 3C: Notify frame callback (recovery tracker)
        if (frameCallback_) {
            frameCallback_(engineId, frameCallbackData_);
        }
    }

    // Mark engine as hidden/visible
    void setHidden(int engineId, bool hidden) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& data = engines_[engineId];
        data.isHidden = hidden;
        data.lastVisibilityChangeTime = nowMs();
        data.framesAtLastTransition = data.totalFrames;
    }

    // Mark animations as paused/resumed
    void setAnimationsPaused(int engineId, bool paused) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& data = engines_[engineId];
        data.animationsPaused = paused;
    }

    // B3 fix: Mark engine as about to receive resume command.
    // Called by native orchestration BEFORE sending lifecycle resumed.
    void markResuming(int engineId) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& data = engines_[engineId];
        data.resumeSentTimestampMs = nowMs();
        data.waitingForRecoveryFrame = true;
    }

    // B3 fix: Mark engine as about to be parked.
    // Records frame count at park for delta calculation.
    void markParking(int engineId) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& data = engines_[engineId];
        data.framesAtPark = data.totalFrames;
    }

    // Phase 2B.1: Record recovery event from Dart-reported metrics
    void recordRecovery(int engineId, double recoveryMs, int resumeCycle, int framesWhileParked) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& data = engines_[engineId];
        data.lastRecoveryMs = recoveryMs;
        data.resumeCycleCount = resumeCycle;
        data.framesWhileLastParked = framesWhileParked;
        if (recoveryMs < data.bestRecoveryMs) data.bestRecoveryMs = recoveryMs;
        if (recoveryMs > data.worstRecoveryMs) data.worstRecoveryMs = recoveryMs;
        data.totalRecoveryMs += recoveryMs;
    }

    // Query all engine data (called from main engine)
    std::map<int, EngineFrameData> queryAll() {
        std::lock_guard<std::mutex> lock(mutex_);

        // Recompute all metrics before returning
        for (auto& [id, data] : engines_) {
            recomputeMetrics(data);
        }

        return engines_;
    }

    // Get summary string
    std::string summary() {
        std::lock_guard<std::mutex> lock(mutex_);

        std::string s;
        for (auto& [id, data] : engines_) {
            recomputeMetrics(data);
            s += "Engine" + std::to_string(id) +
                " frames=" + std::to_string(data.totalFrames) +
                " fps=" + std::to_string(data.estimatedFps) +
                " hidden=" + (data.isHidden ? "Y" : "N") +
                " animPaused=" + (data.animationsPaused ? "Y" : "N") +
                " hiddenFrames=" + std::to_string(data.framesWhileHidden) +
                " pausedFrames=" + std::to_string(data.framesWhileAnimPaused) +
                " state=" + stateName(data.state) + "; ";
        }
        return s;
    }

    // Phase X: Remove a specific engine from tracking (hard destroy).
    void removeEngine(int engineId) {
        std::lock_guard<std::mutex> lock(mutex_);
        engines_.erase(engineId);
        OutputDebugStringA(("CADENCE_MONITOR: Removed engine " +
            std::to_string(engineId) + " (hard destroy)\n").c_str());
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        engines_.clear();
    }

private:
    FrameCadenceMonitor() = default;

    void recomputeMetrics(EngineFrameData& data) {
        if (data.timestamps.size() < 2) {
            data.avgIntervalMs = 0;
            data.estimatedFps = 0;
            data.state = ActivityState::Dormant;
            return;
        }

        // Compute average interval from last 50 frames
        int count = (std::min)(50, static_cast<int>(data.timestamps.size()) - 1);
        double sum = 0;
        int start = static_cast<int>(data.timestamps.size()) - count - 1;
        for (int i = start; i < start + count; i++) {
            sum += data.timestamps[i + 1] - data.timestamps[i];
        }
        data.avgIntervalMs = sum / count;
        data.estimatedFps = (data.avgIntervalMs > 0)
            ? 1000.0 / data.avgIntervalMs : 0;

        // Classify activity state
        if (!data.isHidden) {
            data.state = ActivityState::ActiveVisible;
        } else {
            // Hidden — classify based on cadence
            if (data.estimatedFps > 30) {
                // Check if frame rate is decaying
                double timeSinceHide = nowMs() - data.lastVisibilityChangeTime;
                if (timeSinceHide > 30000 && data.estimatedFps > 30) {
                    data.state = ActivityState::Runaway;
                } else if (data.estimatedFps < 50 && timeSinceHide > 5000) {
                    data.state = ActivityState::Decaying;
                } else {
                    data.state = ActivityState::ActiveHidden;
                }
            } else if (data.estimatedFps > 1) {
                data.state = ActivityState::TimerOnly;
            } else {
                data.state = ActivityState::Dormant;
            }
        }
    }

    static double nowMs() {
        auto now = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<double, std::milli>(
            now.time_since_epoch()).count();
    }

    std::mutex mutex_;
    std::map<int, EngineFrameData> engines_;

    // Phase 3C: Frame notification callback
    FrameCallback frameCallback_ = nullptr;
    void* frameCallbackData_ = nullptr;
};

}  // namespace morphic

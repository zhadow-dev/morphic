#pragma once

#include "../core/types.h"
#include "../core/scene_graph.h"
#include "../core/window_host.h"

#include <windows.h>
#include <vector>
#include <unordered_map>
#include <memory>
#include <cmath>
#include <algorithm>
#include <string>
#include <sstream>

namespace morphic {

// Phase 1B Gate 2 — Temporal Correctness Validator.
//
// Compositor systems require "eventually stable within bounded latency"
// NOT "instantly identical."
//
// This validator tracks HOW LONG discrepancies persist, not just whether
// they exist. Humans perceive persistence, not single-frame divergence.
//
// Three temporal metrics:
//   1. Position convergence latency — frames until SYNC returns to 0 after mutation
//   2. Z-order convergence latency — frames until HWND order matches elevation after activation
//   3. Size convergence latency — frames until HWND size matches scene graph after resize
class TemporalValidator {
public:
    struct ConvergenceEvent {
        enum class Type { Position, ZOrder, Size };
        Type type;
        uint64_t startFrame = 0;
        uint64_t endFrame = 0;      // 0 = still diverged
        int durationFrames = 0;
        double maxDivergence = 0.0; // pixels for position/size, count for z-order
        bool resolved = false;
    };

    struct TemporalHealth {
        // Position convergence
        int positionConvergenceP99 = 0;     // 99th percentile convergence latency (frames)
        int positionConvergenceMax = 0;     // worst case
        int positionPersistentFailures = 0; // divergences > threshold
        int positionTransientEvents = 0;    // divergences that resolved in time

        // Z-order convergence
        int zOrderConvergenceP99 = 0;
        int zOrderConvergenceMax = 0;
        int zOrderPersistentFailures = 0;
        int zOrderTransientEvents = 0;

        // Size convergence
        int sizeConvergenceP99 = 0;
        int sizeConvergenceMax = 0;
        int sizePersistentFailures = 0;
        int sizeTransientEvents = 0;

        bool healthy() const {
            return positionPersistentFailures == 0 &&
                   zOrderPersistentFailures == 0 &&
                   sizePersistentFailures == 0;
        }
    };

    // Call every frame after processFrame completes.
    // Measures divergence between scene graph state and actual HWND state.
    void validate(
        const SceneGraph& graph,
        const std::unordered_map<NodeId, std::unique_ptr<WindowHost>>& hosts,
        uint64_t frameNumber)
    {
        checkPositionConvergence(graph, hosts, frameNumber);
        checkSizeConvergence(graph, hosts, frameNumber);
        // Z-order convergence is handled by InvariantChecker's temporal tracking
    }

    // Record that a z-order mismatch event occurred (from InvariantChecker)
    void recordZOrderMismatch(uint64_t frame, int durationFrames, bool resolved) {
        if (resolved && durationFrames <= zOrderThreshold_) {
            health_.zOrderTransientEvents++;
            zOrderDurations_.push_back(durationFrames);
        } else if (!resolved || durationFrames > zOrderThreshold_) {
            health_.zOrderPersistentFailures++;
        }
        if (durationFrames > health_.zOrderConvergenceMax) {
            health_.zOrderConvergenceMax = durationFrames;
        }
        updateZOrderP99();
    }

    const TemporalHealth& health() const { return health_; }

    void setPositionThreshold(int frames) { positionThreshold_ = frames; }
    void setSizeThreshold(int frames) { sizeThreshold_ = frames; }
    void setZOrderThreshold(int frames) { zOrderThreshold_ = frames; }
    void setDivergencePixelThreshold(double px) { divergencePixelThreshold_ = px; }

    void reset() {
        health_ = {};
        positionDurations_.clear();
        sizeDurations_.clear();
        zOrderDurations_.clear();
        positionDiverged_ = false;
        sizeDiverged_ = false;
        positionDivergenceStart_ = 0;
        sizeDivergenceStart_ = 0;
    }

    std::string summary() const {
        std::ostringstream oss;
        oss << "TEMPORAL HEALTH: "
            << (health_.healthy() ? "HEALTHY" : "DEGRADED") << "\n"
            << "  Position: P99=" << health_.positionConvergenceP99
            << " max=" << health_.positionConvergenceMax
            << " transient=" << health_.positionTransientEvents
            << " persistent=" << health_.positionPersistentFailures << "\n"
            << "  Z-Order:  P99=" << health_.zOrderConvergenceP99
            << " max=" << health_.zOrderConvergenceMax
            << " transient=" << health_.zOrderTransientEvents
            << " persistent=" << health_.zOrderPersistentFailures << "\n"
            << "  Size:     P99=" << health_.sizeConvergenceP99
            << " max=" << health_.sizeConvergenceMax
            << " transient=" << health_.sizeTransientEvents
            << " persistent=" << health_.sizePersistentFailures << "\n";
        return oss.str();
    }

private:
    TemporalHealth health_;
    int positionThreshold_ = 1;         // Position must converge within 1 frame
    int sizeThreshold_ = 1;             // Size must converge within 1 frame
    int zOrderThreshold_ = 2;           // Z-order can take up to 2 frames (DWM async)
    double divergencePixelThreshold_ = 1.0;  // < 1px = converged

    // Tracking state
    bool positionDiverged_ = false;
    uint64_t positionDivergenceStart_ = 0;
    double positionMaxDivergence_ = 0.0;

    bool sizeDiverged_ = false;
    uint64_t sizeDivergenceStart_ = 0;
    double sizeMaxDivergence_ = 0.0;

    // Duration histories for P99 calculation
    std::vector<int> positionDurations_;
    std::vector<int> sizeDurations_;
    std::vector<int> zOrderDurations_;

    void checkPositionConvergence(
        const SceneGraph& graph,
        const std::unordered_map<NodeId, std::unique_ptr<WindowHost>>& hosts,
        uint64_t frame)
    {
        double maxDiv = 0.0;

        graph.forEachSurface([&](const CompositionSurface* s) {
            auto it = hosts.find(s->id());
            if (it == hosts.end() || !it->second || !it->second->isAlive()) return;

            RECT wr;
            GetWindowRect(it->second->hwnd(), &wr);
            const auto& wt = s->worldTransform();

            double dx = std::abs(static_cast<double>(wt.x) - wr.left);
            double dy = std::abs(static_cast<double>(wt.y) - wr.top);
            double div = std::sqrt(dx * dx + dy * dy);
            if (div > maxDiv) maxDiv = div;
        });

        if (maxDiv >= divergencePixelThreshold_) {
            // Currently diverged
            if (!positionDiverged_) {
                positionDiverged_ = true;
                positionDivergenceStart_ = frame;
                positionMaxDivergence_ = maxDiv;
            } else {
                if (maxDiv > positionMaxDivergence_) positionMaxDivergence_ = maxDiv;
                int duration = static_cast<int>(frame - positionDivergenceStart_) + 1;
                if (duration > positionThreshold_) {
                    health_.positionPersistentFailures++;
                }
            }
        } else {
            // Converged
            if (positionDiverged_) {
                int duration = static_cast<int>(frame - positionDivergenceStart_);
                if (duration <= positionThreshold_) {
                    health_.positionTransientEvents++;
                }
                positionDurations_.push_back(duration);
                if (duration > health_.positionConvergenceMax) {
                    health_.positionConvergenceMax = duration;
                }
                updatePositionP99();

                positionDiverged_ = false;
                positionMaxDivergence_ = 0.0;
            }
        }
    }

    void checkSizeConvergence(
        const SceneGraph& graph,
        const std::unordered_map<NodeId, std::unique_ptr<WindowHost>>& hosts,
        uint64_t frame)
    {
        double maxDiv = 0.0;

        graph.forEachSurface([&](const CompositionSurface* s) {
            auto it = hosts.find(s->id());
            if (it == hosts.end() || !it->second || !it->second->isAlive()) return;

            RECT wr;
            GetWindowRect(it->second->hwnd(), &wr);
            const auto& wt = s->worldTransform();

            double dw = std::abs(static_cast<double>(wt.width) - (wr.right - wr.left));
            double dh = std::abs(static_cast<double>(wt.height) - (wr.bottom - wr.top));
            double div = (std::max)(dw, dh);
            if (div > maxDiv) maxDiv = div;
        });

        if (maxDiv >= divergencePixelThreshold_) {
            if (!sizeDiverged_) {
                sizeDiverged_ = true;
                sizeDivergenceStart_ = frame;
                sizeMaxDivergence_ = maxDiv;
            } else {
                if (maxDiv > sizeMaxDivergence_) sizeMaxDivergence_ = maxDiv;
                int duration = static_cast<int>(frame - sizeDivergenceStart_) + 1;
                if (duration > sizeThreshold_) {
                    health_.sizePersistentFailures++;
                }
            }
        } else {
            if (sizeDiverged_) {
                int duration = static_cast<int>(frame - sizeDivergenceStart_);
                if (duration <= sizeThreshold_) {
                    health_.sizeTransientEvents++;
                }
                sizeDurations_.push_back(duration);
                if (duration > health_.sizeConvergenceMax) {
                    health_.sizeConvergenceMax = duration;
                }
                updateSizeP99();

                sizeDiverged_ = false;
                sizeMaxDivergence_ = 0.0;
            }
        }
    }

    void updatePositionP99() {
        health_.positionConvergenceP99 = computeP99(positionDurations_);
    }

    void updateSizeP99() {
        health_.sizeConvergenceP99 = computeP99(sizeDurations_);
    }

    void updateZOrderP99() {
        health_.zOrderConvergenceP99 = computeP99(zOrderDurations_);
    }

    static int computeP99(std::vector<int>& durations) {
        if (durations.empty()) return 0;
        std::vector<int> sorted = durations;
        std::sort(sorted.begin(), sorted.end());
        size_t idx = static_cast<size_t>(sorted.size() * 0.99);
        if (idx >= sorted.size()) idx = sorted.size() - 1;
        return sorted[idx];
    }
};

}  // namespace morphic

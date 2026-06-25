#pragma once

#include "../core/types.h"
#include "../core/scene_graph.h"
#include "../core/window_host.h"

#include <windows.h>
#include <vector>
#include <string>
#include <cmath>
#include <unordered_map>
#include <memory>

namespace morphic {

// Phase 1B Track 7 — Frame drift detector.
//
// Purpose: After 10,000+ frames of drag-away/drag-back roundtrips,
// verify transforms haven't accumulated rounding error.
// Spatial systems LOVE accumulating tiny errors.
class DriftDetector {
public:
    struct Baseline {
        NodeId id;
        Transform transform;
    };

    struct DriftResult {
        double maxDrift = 0.0;
        NodeId worstSurface = kInvalidNodeId;
        uint64_t framesSinceBaseline = 0;
        bool stable = true;   // maxDrift < 1.0px
        std::string details;
    };

    void captureBaseline(const SceneGraph& graph) {
        baselines_.clear();
        graph.forEachSurface([&](const CompositionSurface* s) {
            baselines_.push_back({s->id(), s->worldTransform()});
        });
        baselineFrame_ = 0;
        captured_ = true;
    }

    bool hasCaptured() const { return captured_; }

    DriftResult measureDrift(
        const SceneGraph& graph,
        uint64_t currentFrame)
    {
        DriftResult result;
        result.framesSinceBaseline = currentFrame - baselineFrame_;

        for (const auto& baseline : baselines_) {
            auto* surface = const_cast<SceneGraph&>(graph).getSurface(baseline.id);
            if (!surface) continue;

            const auto& current = surface->worldTransform();
            double dx = std::abs(current.x - baseline.transform.x);
            double dy = std::abs(current.y - baseline.transform.y);
            double dw = std::abs(current.width - baseline.transform.width);
            double dh = std::abs(current.height - baseline.transform.height);
            double drift = (std::max)({dx, dy, dw, dh});

            if (drift > result.maxDrift) {
                result.maxDrift = drift;
                result.worstSurface = baseline.id;
            }
        }

        result.stable = result.maxDrift < 1.0;
        if (!result.stable) {
            result.details = "Surface #" + std::to_string(result.worstSurface) +
                " drifted " + std::to_string(result.maxDrift) + "px after " +
                std::to_string(result.framesSinceBaseline) + " frames";
        }

        totalMeasurements_++;
        if (!result.stable) totalDrifts_++;

        return result;
    }

    int totalMeasurements() const { return totalMeasurements_; }
    int totalDrifts() const { return totalDrifts_; }
    void reset() { baselines_.clear(); captured_ = false; totalMeasurements_ = 0; totalDrifts_ = 0; }

private:
    std::vector<Baseline> baselines_;
    uint64_t baselineFrame_ = 0;
    bool captured_ = false;
    int totalMeasurements_ = 0;
    int totalDrifts_ = 0;
};

}  // namespace morphic

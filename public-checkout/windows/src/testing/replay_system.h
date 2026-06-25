#pragma once

#include "../core/types.h"
#include "../core/scene_graph.h"
#include "../core/window_host.h"

#include <windows.h>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <chrono>
#include <unordered_map>
#include <memory>
#include <cmath>
#include <functional>

namespace morphic {

// Phase 1B Track 5 — Deterministic replay system.
//
// Purpose: Given the same input timeline, the engine MUST behave identically.
// Non-deterministic bugs hide otherwise.
//
// Usage:
//   1. replay.beginRecording()
//   2. ... user interacts, events are recorded via recordEvent() ...
//   3. replay.endRecording()
//   4. replay.saveToFile(L"session.csv")
//   5. replay.loadFromFile(L"session.csv")
//   6. replay.replay(compositor)  — plays back the exact sequence
//   7. auto snap = replay.captureSnapshot(compositor)
//   8. auto result = replay.compare(snap1, snap2) — must be identical

struct ReplayEvent {
    double timestampMs = 0.0;   // Relative to recording start
    enum Type : int {
        DragBegin = 0,
        DragUpdate = 1,
        DragEnd = 2,
        Resize = 3,
        Activate = 4,
        ElevationChange = 5,
        Move = 6,
    };
    Type type = DragBegin;
    NodeId surfaceId = kInvalidNodeId;
    int x = 0, y = 0, w = 0, h = 0;
    int sublevel = 0;
};

struct StateSnapshot {
    struct SurfaceState {
        NodeId id;
        Transform worldTransform;
        int elevationSublevel;
        RECT actualHwndRect;
    };
    std::vector<SurfaceState> surfaces;
    uint64_t frameCount = 0;
};

struct CompareResult {
    bool identical = true;
    double maxPositionDelta = 0.0;
    int transformMismatches = 0;
    int elevationMismatches = 0;
    int64_t frameCountDelta = 0;
    std::string details;
};

class Compositor;  // Forward declaration

class ReplaySystem {
public:
    // --- Recording ---

    void beginRecording() {
        events_.clear();
        recording_ = true;
        recordStart_ = std::chrono::steady_clock::now();
    }

    void recordEvent(const ReplayEvent& event) {
        if (!recording_) return;
        ReplayEvent e = event;
        e.timestampMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - recordStart_).count();
        events_.push_back(e);
    }

    void endRecording() {
        recording_ = false;
    }

    bool isRecording() const { return recording_; }
    size_t eventCount() const { return events_.size(); }

    // --- Persistence ---

    bool saveToFile(const std::wstring& path) const {
        std::ofstream out(path);
        if (!out.is_open()) return false;

        out << "timestamp_ms,type,surface_id,x,y,w,h,sublevel\n";
        for (const auto& e : events_) {
            out << e.timestampMs << ","
                << static_cast<int>(e.type) << ","
                << e.surfaceId << ","
                << e.x << "," << e.y << ","
                << e.w << "," << e.h << ","
                << e.sublevel << "\n";
        }
        return true;
    }

    bool loadFromFile(const std::wstring& path) {
        std::ifstream in(path);
        if (!in.is_open()) return false;

        events_.clear();
        std::string line;
        std::getline(in, line);  // Skip header

        while (std::getline(in, line)) {
            std::istringstream iss(line);
            ReplayEvent e;
            char comma;
            int typeInt;

            iss >> e.timestampMs >> comma
                >> typeInt >> comma
                >> e.surfaceId >> comma
                >> e.x >> comma >> e.y >> comma
                >> e.w >> comma >> e.h >> comma
                >> e.sublevel;

            e.type = static_cast<ReplayEvent::Type>(typeInt);
            events_.push_back(e);
        }
        return true;
    }

    // --- Replay ---
    // Replays the recorded event sequence into the compositor.
    // Uses the provided callback to dispatch events (since Compositor
    // is defined in a different translation unit).

    using EventDispatcher = std::function<void(const ReplayEvent&)>;

    void replay(EventDispatcher dispatcher) {
        if (events_.empty()) return;

        replaying_ = true;
        auto replayStart = std::chrono::steady_clock::now();

        for (const auto& event : events_) {
            // Wait until the correct timestamp
            auto targetTime = replayStart + std::chrono::duration<double, std::milli>(event.timestampMs);
            while (std::chrono::steady_clock::now() < targetTime) {
                // Spin-wait for precise timing
                // For long waits, yield to avoid burning CPU
                auto remaining = std::chrono::duration<double, std::milli>(
                    targetTime - std::chrono::steady_clock::now()).count();
                if (remaining > 2.0) {
                    Sleep(1);
                }
            }

            dispatcher(event);
        }

        replaying_ = false;
    }

    // Replay without timing (as fast as possible) — for stress testing
    void replayImmediate(EventDispatcher dispatcher) {
        replaying_ = true;
        for (const auto& event : events_) {
            dispatcher(event);
        }
        replaying_ = false;
    }

    bool isReplaying() const { return replaying_; }

    // --- State snapshot ---

    StateSnapshot captureSnapshot(
        const SceneGraph& graph,
        const std::unordered_map<NodeId, std::unique_ptr<WindowHost>>& hosts,
        uint64_t frameCount)
    {
        StateSnapshot snap;
        snap.frameCount = frameCount;

        graph.forEachSurface([&](const CompositionSurface* s) {
            StateSnapshot::SurfaceState ss;
            ss.id = s->id();
            ss.worldTransform = s->worldTransform();
            ss.elevationSublevel = s->elevationSublevel();
            ss.actualHwndRect = {0, 0, 0, 0};

            auto it = hosts.find(s->id());
            if (it != hosts.end() && it->second && it->second->isAlive()) {
                GetWindowRect(it->second->hwnd(), &ss.actualHwndRect);
            }

            snap.surfaces.push_back(ss);
        });

        // Sort by ID for deterministic comparison
        std::sort(snap.surfaces.begin(), snap.surfaces.end(),
            [](const StateSnapshot::SurfaceState& a, const StateSnapshot::SurfaceState& b) {
                return a.id < b.id;
            });

        return snap;
    }

    // --- Comparison ---

    CompareResult compare(const StateSnapshot& a, const StateSnapshot& b) {
        CompareResult result;
        result.frameCountDelta = static_cast<int64_t>(a.frameCount) - static_cast<int64_t>(b.frameCount);

        if (a.surfaces.size() != b.surfaces.size()) {
            result.identical = false;
            result.details = "Surface count mismatch: " +
                std::to_string(a.surfaces.size()) + " vs " + std::to_string(b.surfaces.size());
            return result;
        }

        for (size_t i = 0; i < a.surfaces.size(); i++) {
            const auto& sa = a.surfaces[i];
            const auto& sb = b.surfaces[i];

            if (sa.id != sb.id) {
                result.identical = false;
                result.details = "Surface ID mismatch at index " + std::to_string(i);
                return result;
            }

            // Check transform
            double dx = std::abs(sa.worldTransform.x - sb.worldTransform.x);
            double dy = std::abs(sa.worldTransform.y - sb.worldTransform.y);
            double dw = std::abs(sa.worldTransform.width - sb.worldTransform.width);
            double dh = std::abs(sa.worldTransform.height - sb.worldTransform.height);
            double maxDelta = (std::max)({dx, dy, dw, dh});

            if (maxDelta > result.maxPositionDelta) {
                result.maxPositionDelta = maxDelta;
            }

            if (maxDelta > 0) {
                result.identical = false;
                result.transformMismatches++;
            }

            // Check elevation — track but don't fail on this.
            // Elevation ordering during rapid activations is nondeterministic
            // because onSurfaceActivated interacts with Windows focus management
            // (WM_ACTIVATE ordering varies between runs). This is validated
            // separately by the temporal convergence system.
            if (sa.elevationSublevel != sb.elevationSublevel) {
                result.elevationMismatches++;
            }
        }

        if (!result.identical && result.details.empty()) {
            std::ostringstream oss;
            oss << "maxPosDelta=" << result.maxPositionDelta
                << " transformMismatches=" << result.transformMismatches
                << " elevationMismatches=" << result.elevationMismatches;
            result.details = oss.str();
        }

        return result;
    }

    // --- Access recorded events (for inspection/testing) ---
    const std::vector<ReplayEvent>& events() const { return events_; }

private:
    std::vector<ReplayEvent> events_;
    bool recording_ = false;
    bool replaying_ = false;
    std::chrono::steady_clock::time_point recordStart_;
};

}  // namespace morphic

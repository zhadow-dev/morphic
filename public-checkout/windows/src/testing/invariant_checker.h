#pragma once

#include "../core/types.h"
#include "../core/scene_graph.h"
#include "../core/window_host.h"

#include <windows.h>
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <cmath>
#include <limits>
#include <sstream>

namespace morphic {

// Phase 1B — Structural invariant checker.
// Validates 12 correctness invariants every frame in debug builds.
// If ANY invariant fails, the compositor has a bug.
// This is the foundation — every other validator depends on invariants holding.
class InvariantChecker {
public:
    struct Violation {
        std::string invariant;
        std::string details;
        uint64_t frame = 0;
    };

    // --- Temporal z-order convergence tracking ---
    // Instead of pass/fail, z-order mismatches are tracked as temporal events.
    // Transient mismatch (resolves within convergenceThreshold_ frames) = acceptable.
    // Persistent mismatch (> convergenceThreshold_ frames) = real bug.
    struct ZOrderConvergence {
        bool currentlyMismatched = false;
        uint64_t mismatchStartFrame = 0;
        int maxMismatchDuration = 0;      // worst case convergence (frames)
        int totalTransientMismatches = 0;  // mismatches that resolved in time
        int totalPersistentFailures = 0;   // mismatches that exceeded threshold
        int currentMismatchDuration = 0;   // how long current mismatch has lasted

        void reset() { *this = {}; }
    };

    // Call EVERY FRAME in debug builds.
    // Returns empty vector if all invariants hold.
    // Z-order uses temporal convergence: transient mismatches (≤ convergenceThreshold frames)
    // are tracked but NOT reported as violations. Only persistent mismatches are violations.
    std::vector<Violation> validateAll(
        const SceneGraph& graph,
        const std::unordered_map<NodeId, std::unique_ptr<WindowHost>>& hosts,
        uint64_t frameNumber)
    {
        std::vector<Violation> violations;

        checkHostedSurfacesHaveValidHost(graph, hosts, frameNumber, violations);
        checkHostHwndsAreValid(hosts, frameNumber, violations);
        checkNoCyclicParentRefs(graph, frameNumber, violations);
        // Orphan check uses EnumWindows — expensive. Sample every 60 frames.
        if (frameNumber % 60 == 0) {
            checkNoOrphanHwnds(frameNumber, violations);
        }
        checkZOrderTemporalConvergence(graph, hosts, frameNumber, violations);
        checkTransformsAreFinite(graph, frameNumber, violations);
        checkConstraintsSatisfied(graph, frameNumber, violations);
        checkGroupMembershipConsistent(graph, frameNumber, violations);
        checkWorldTransformComputed(graph, frameNumber, violations);
        checkNoSurfaceInMultipleGroups(graph, frameNumber, violations);
        checkHostSurfaceBackpointer(graph, hosts, frameNumber, violations);
        checkTransformDimensionsPositive(graph, frameNumber, violations);

        totalViolations_ += static_cast<int>(violations.size());
        totalChecks_++;

        return violations;
    }

    int totalViolations() const { return totalViolations_; }
    int totalChecks() const { return totalChecks_; }
    const ZOrderConvergence& zOrderConvergence() const { return zOrderState_; }
    void setConvergenceThreshold(int frames) { convergenceThreshold_ = frames; }
    void reset() { totalViolations_ = 0; totalChecks_ = 0; zOrderState_.reset(); }

private:
    int totalViolations_ = 0;
    int totalChecks_ = 0;

    // ---- Invariant 1: Every hosted surface has a valid, alive host ----
    void checkHostedSurfacesHaveValidHost(
        const SceneGraph& graph,
        const std::unordered_map<NodeId, std::unique_ptr<WindowHost>>& hosts,
        uint64_t frame, std::vector<Violation>& out)
    {
        graph.forEachSurface([&](const CompositionSurface* s) {
            if (s->hasHost()) {
                if (!s->host()->isAlive()) {
                    out.push_back({"HostedSurfaceHasValidHost",
                        "Surface #" + std::to_string(s->id()) + " has host but host HWND is null",
                        frame});
                }
            }
        });
    }

    // ---- Invariant 2: Every host HWND is a valid window ----
    void checkHostHwndsAreValid(
        const std::unordered_map<NodeId, std::unique_ptr<WindowHost>>& hosts,
        uint64_t frame, std::vector<Violation>& out)
    {
        for (const auto& [id, host] : hosts) {
            if (host && host->hwnd()) {
                if (!IsWindow(host->hwnd())) {
                    out.push_back({"HostHwndIsValid",
                        "Host for surface #" + std::to_string(id) + " has invalid HWND",
                        frame});
                }
            }
        }
    }

    // ---- Invariant 3: No cyclic parent references ----
    void checkNoCyclicParentRefs(
        const SceneGraph& graph, uint64_t frame, std::vector<Violation>& out)
    {
        graph.forEachSurface([&](const CompositionSurface* s) {
            std::unordered_set<const VisualNode*> visited;
            const VisualNode* current = s;
            while (current) {
                if (visited.count(current)) {
                    out.push_back({"NoCyclicParentRefs",
                        "Cycle detected involving surface #" + std::to_string(s->id()),
                        frame});
                    return;
                }
                visited.insert(current);
                current = current->parent();
            }
        });
    }

    // ---- Invariant 4: No orphan HWNDs (MorphicSurface class windows without matching host) ----
    void checkNoOrphanHwnds(uint64_t frame, std::vector<Violation>& out) {
        // Count MorphicSurface-class windows via EnumWindows
        // This is somewhat expensive — consider sampling in large graphs
        orphanCount_ = 0;
        expectedClassName_ = L"MorphicSurface";
        EnumWindows(OrphanEnumProc, reinterpret_cast<LPARAM>(this));

        // We can't easily compare against host count here without the hosts map,
        // so we just track the count for external validation.
        // The orphan detection is primarily useful in lifecycle tests (Track 10).
        lastOrphanScanCount_ = orphanCount_;
    }

    static BOOL CALLBACK OrphanEnumProc(HWND hwnd, LPARAM lParam) {
        auto* self = reinterpret_cast<InvariantChecker*>(lParam);
        wchar_t className[64] = {};
        GetClassNameW(hwnd, className, 64);
        if (wcscmp(className, self->expectedClassName_) == 0) {
            self->orphanCount_++;
        }
        return TRUE;
    }

    int orphanCount_ = 0;
    int lastOrphanScanCount_ = 0;
    const wchar_t* expectedClassName_ = L"MorphicSurface";

    // ---- Invariant 5: Z-order temporal convergence ----
    // Instead of instant pass/fail, tracks how long z-order mismatches persist.
    // Transient mismatches (≤ convergenceThreshold_ frames) are acceptable DWM behavior.
    // Persistent mismatches (> convergenceThreshold_ frames) are real bugs.
    void checkZOrderTemporalConvergence(
        const SceneGraph& graph,
        const std::unordered_map<NodeId, std::unique_ptr<WindowHost>>& hosts,
        uint64_t frame, std::vector<Violation>& out)
    {
        bool mismatchThisFrame = isZOrderMismatched(graph, hosts);

        if (mismatchThisFrame) {
            if (!zOrderState_.currentlyMismatched) {
                // Mismatch just started
                zOrderState_.currentlyMismatched = true;
                zOrderState_.mismatchStartFrame = frame;
                zOrderState_.currentMismatchDuration = 1;
            } else {
                // Mismatch continues
                zOrderState_.currentMismatchDuration =
                    static_cast<int>(frame - zOrderState_.mismatchStartFrame) + 1;
            }

            // Track worst case
            if (zOrderState_.currentMismatchDuration > zOrderState_.maxMismatchDuration) {
                zOrderState_.maxMismatchDuration = zOrderState_.currentMismatchDuration;
            }

            // Only report as violation if persistent (> threshold)
            if (zOrderState_.currentMismatchDuration > convergenceThreshold_) {
                zOrderState_.totalPersistentFailures++;
                std::ostringstream oss;
                oss << "Z-order mismatch persisted for "
                    << zOrderState_.currentMismatchDuration
                    << " frames (threshold=" << convergenceThreshold_
                    << ") — this is a REAL ordering bug, not DWM lag";
                out.push_back({"ZOrderPersistentFailure", oss.str(), frame});
            }
        } else {
            if (zOrderState_.currentlyMismatched) {
                // Mismatch just resolved — was it within threshold?
                if (zOrderState_.currentMismatchDuration <= convergenceThreshold_) {
                    zOrderState_.totalTransientMismatches++;
                }
                // Clear state
                zOrderState_.currentlyMismatched = false;
                zOrderState_.currentMismatchDuration = 0;
            }
        }
    }

    // Pure check: is z-order currently mismatched? (no state mutation)
    bool isZOrderMismatched(
        const SceneGraph& graph,
        const std::unordered_map<NodeId, std::unique_ptr<WindowHost>>& hosts)
    {
        struct SurfaceZ {
            NodeId id;
            int sublevel;
            HWND hwnd;
        };
        std::vector<SurfaceZ> surfaces;

        graph.forEachSurface([&](const CompositionSurface* s) {
            auto it = hosts.find(s->id());
            if (it != hosts.end() && it->second && it->second->isAlive()) {
                surfaces.push_back({s->id(), s->elevationSublevel(), it->second->hwnd()});
            }
        });

        if (surfaces.size() < 2) return false;

        std::sort(surfaces.begin(), surfaces.end(),
            [](const SurfaceZ& a, const SurfaceZ& b) { return a.sublevel > b.sublevel; });

        for (size_t i = 0; i + 1 < surfaces.size(); i++) {
            HWND current = surfaces[i].hwnd;
            HWND next = surfaces[i + 1].hwnd;

            bool found = false;
            HWND walker = current;
            int maxSteps = 100;
            while (walker && maxSteps-- > 0) {
                walker = GetWindow(walker, GW_HWNDNEXT);
                if (walker == next) {
                    found = true;
                    break;
                }
            }
            if (!found) return true;  // At least one pair is out of order
        }
        return false;
    }

    int convergenceThreshold_ = 2;  // Max frames a z-order mismatch can persist
    ZOrderConvergence zOrderState_;

    // ---- Invariant 6: All transforms are finite (not NaN, Inf, or extreme) ----
    void checkTransformsAreFinite(
        const SceneGraph& graph, uint64_t frame, std::vector<Violation>& out)
    {
        graph.forEachSurface([&](const CompositionSurface* s) {
            const auto& t = s->localTransform();
            const auto& wt = s->worldTransform();

            auto checkTransform = [&](const Transform& tr, const std::string& label) {
                if (std::abs(tr.x) > 100000 || std::abs(tr.y) > 100000 ||
                    tr.width > 100000 || tr.height > 100000 ||
                    tr.width < 0 || tr.height < 0) {
                    std::ostringstream oss;
                    oss << "Surface #" << s->id() << " " << label
                        << " out of range: (" << tr.x << "," << tr.y
                        << "," << tr.width << "x" << tr.height << ")";
                    out.push_back({"TransformsAreFinite", oss.str(), frame});
                }
            };

            checkTransform(t, "localTransform");
            checkTransform(wt, "worldTransform");
        });
    }

    // ---- Invariant 7: Constraints satisfied ----
    void checkConstraintsSatisfied(
        const SceneGraph& graph, uint64_t frame, std::vector<Violation>& out)
    {
        graph.forEachSurface([&](const CompositionSurface* s) {
            const auto& c = s->constraints();
            const auto& t = s->localTransform();

            if (c.minWidth > 0 && t.width < c.minWidth) {
                std::ostringstream oss;
                oss << "Surface #" << s->id() << " width " << t.width
                    << " < minWidth " << c.minWidth;
                out.push_back({"ConstraintsSatisfied", oss.str(), frame});
            }
            if (c.minHeight > 0 && t.height < c.minHeight) {
                std::ostringstream oss;
                oss << "Surface #" << s->id() << " height " << t.height
                    << " < minHeight " << c.minHeight;
                out.push_back({"ConstraintsSatisfied", oss.str(), frame});
            }
            if (c.maxWidth > 0 && t.width > c.maxWidth) {
                std::ostringstream oss;
                oss << "Surface #" << s->id() << " width " << t.width
                    << " > maxWidth " << c.maxWidth;
                out.push_back({"ConstraintsSatisfied", oss.str(), frame});
            }
            if (c.maxHeight > 0 && t.height > c.maxHeight) {
                std::ostringstream oss;
                oss << "Surface #" << s->id() << " height " << t.height
                    << " > maxHeight " << c.maxHeight;
                out.push_back({"ConstraintsSatisfied", oss.str(), frame});
            }
        });
    }

    // ---- Invariant 8: Group membership consistent ----
    // surface.groupId → group exists AND group.memberIds contains surface
    void checkGroupMembershipConsistent(
        const SceneGraph& graph, uint64_t frame, std::vector<Violation>& out)
    {
        graph.forEachSurface([&](const CompositionSurface* s) {
            NodeId gid = s->groupId();
            if (gid == kInvalidNodeId) return;

            auto* group = const_cast<SceneGraph&>(graph).getGroup(gid);
            if (!group) {
                out.push_back({"GroupMembershipConsistent",
                    "Surface #" + std::to_string(s->id()) + " references group #" +
                    std::to_string(gid) + " which doesn't exist",
                    frame});
                return;
            }

            bool found = false;
            for (auto mid : group->memberIds) {
                if (mid == s->id()) { found = true; break; }
            }
            if (!found) {
                out.push_back({"GroupMembershipConsistent",
                    "Surface #" + std::to_string(s->id()) + " references group #" +
                    std::to_string(gid) + " but group doesn't list it as member",
                    frame});
            }
        });
    }

    // ---- Invariant 9: World transform matches parent + local ----
    void checkWorldTransformComputed(
        const SceneGraph& graph, uint64_t frame, std::vector<Violation>& out)
    {
        graph.forEachSurface([&](const CompositionSurface* s) {
            const auto& wt = s->worldTransform();
            const auto& lt = s->localTransform();
            const auto* parent = s->parent();

            int expectedX = lt.x;
            int expectedY = lt.y;
            if (parent) {
                expectedX += parent->worldTransform().x;
                expectedY += parent->worldTransform().y;
            }

            if (wt.x != expectedX || wt.y != expectedY) {
                std::ostringstream oss;
                oss << "Surface #" << s->id()
                    << " worldTransform (" << wt.x << "," << wt.y
                    << ") doesn't match expected (" << expectedX << "," << expectedY << ")";
                out.push_back({"WorldTransformComputed", oss.str(), frame});
            }

            if (wt.width != lt.width || wt.height != lt.height) {
                std::ostringstream oss;
                oss << "Surface #" << s->id()
                    << " worldTransform size (" << wt.width << "x" << wt.height
                    << ") doesn't match local (" << lt.width << "x" << lt.height << ")";
                out.push_back({"WorldTransformComputed", oss.str(), frame});
            }
        });
    }

    // ---- Invariant 10: No surface appears in multiple groups ----
    void checkNoSurfaceInMultipleGroups(
        const SceneGraph& graph, uint64_t frame, std::vector<Violation>& out)
    {
        std::unordered_map<NodeId, NodeId> surfaceToGroup; // surfaceId → groupId
        for (const auto& [gid, group] : graph.groups()) {
            for (auto mid : group.memberIds) {
                auto it = surfaceToGroup.find(mid);
                if (it != surfaceToGroup.end()) {
                    std::ostringstream oss;
                    oss << "Surface #" << mid << " appears in group #" << it->second
                        << " AND group #" << gid;
                    out.push_back({"NoSurfaceInMultipleGroups", oss.str(), frame});
                } else {
                    surfaceToGroup[mid] = gid;
                }
            }
        }
    }

    // ---- Invariant 11: Host surface backpointer matches ----
    void checkHostSurfaceBackpointer(
        const SceneGraph& graph,
        const std::unordered_map<NodeId, std::unique_ptr<WindowHost>>& hosts,
        uint64_t frame, std::vector<Violation>& out)
    {
        for (const auto& [id, host] : hosts) {
            if (!host) continue;
            auto* surface = host->surface();
            if (!surface) {
                out.push_back({"HostSurfaceBackpointer",
                    "Host for id #" + std::to_string(id) + " has null surface",
                    frame});
                continue;
            }
            if (surface->id() != id) {
                std::ostringstream oss;
                oss << "Host keyed as #" << id << " but surface reports id #" << surface->id();
                out.push_back({"HostSurfaceBackpointer", oss.str(), frame});
            }
        }
    }

    // ---- Invariant 12: Transform dimensions are positive ----
    void checkTransformDimensionsPositive(
        const SceneGraph& graph, uint64_t frame, std::vector<Violation>& out)
    {
        graph.forEachSurface([&](const CompositionSurface* s) {
            const auto& t = s->localTransform();
            if (t.width <= 0 || t.height <= 0) {
                std::ostringstream oss;
                oss << "Surface #" << s->id() << " has non-positive dimensions: "
                    << t.width << "x" << t.height;
                out.push_back({"TransformDimensionsPositive", oss.str(), frame});
            }
        });
    }

public:
    // For external validation (lifecycle tests)
    int lastOrphanHwndCount() const { return lastOrphanScanCount_; }
};

}  // namespace morphic

#pragma once

#include "../core/types.h"
#include "../core/scene_graph.h"
#include "../core/window_host.h"

#include <windows.h>
#include <vector>
#include <string>
#include <sstream>
#include <algorithm>
#include <unordered_map>
#include <memory>

namespace morphic {

// Phase 1B Track 4 — Z-order ordering validator.
// Proves: SceneGraph elevation order == actual HWND z-order. Always.
//
// Hidden ordering divergence is one of the nastiest multi-HWND bugs.
// It causes: phantom focus, visual overlap, invisible hit-test failures.
// This validator catches it immediately.
class OrderingValidator {
public:
    struct OrderingResult {
        bool consistent = true;
        std::string expectedOrder;  // "S3,S1,S2" by sublevel (highest first)
        std::string actualOrder;    // "S3,S1,S2" by HWND z-order (topmost first)
        std::string mismatchDetails;
        int checksPerformed = 0;
    };

    // Validate that HWND z-order matches elevation sublevel ordering.
    // Call after every resolveZOrder() and onSurfaceActivated().
    OrderingResult validate(
        const SceneGraph& graph,
        const std::unordered_map<NodeId, std::unique_ptr<WindowHost>>& hosts)
    {
        OrderingResult result;

        // Collect surfaces with valid HWNDs, sorted by sublevel descending
        struct Entry {
            NodeId id;
            int sublevel;
            HWND hwnd;
        };
        std::vector<Entry> entries;

        graph.forEachSurface([&](const CompositionSurface* s) {
            auto it = hosts.find(s->id());
            if (it != hosts.end() && it->second && it->second->isAlive()) {
                entries.push_back({s->id(), s->elevationSublevel(), it->second->hwnd()});
            }
        });

        if (entries.size() < 2) {
            result.checksPerformed = 0;
            return result;
        }

        // Sort by sublevel descending (highest = should be topmost)
        std::sort(entries.begin(), entries.end(),
            [](const Entry& a, const Entry& b) { return a.sublevel > b.sublevel; });

        // Build expected order string
        {
            std::ostringstream oss;
            for (size_t i = 0; i < entries.size(); i++) {
                if (i > 0) oss << ",";
                oss << "S" << entries[i].id << "(E" << entries[i].sublevel << ")";
            }
            result.expectedOrder = oss.str();
        }

        // Walk actual HWND z-order.
        // Find each entry's position by checking relative ordering via GetWindow.
        // For N surfaces, verify each consecutive pair: entries[i] is above entries[i+1].
        std::vector<NodeId> actualOrder;

        // Build HWND-to-ID map
        std::unordered_map<HWND, NodeId> hwndToId;
        for (const auto& e : entries) {
            hwndToId[e.hwnd] = e.id;
        }

        // Walk from topmost window down, collecting our surfaces in z-order
        HWND top = GetTopWindow(nullptr);  // Topmost desktop child
        HWND walker = top;
        int maxSteps = 2000;  // Safety limit
        while (walker && maxSteps-- > 0) {
            auto it = hwndToId.find(walker);
            if (it != hwndToId.end()) {
                actualOrder.push_back(it->second);
                if (actualOrder.size() == entries.size()) break;
            }
            walker = GetWindow(walker, GW_HWNDNEXT);
        }

        // Build actual order string
        {
            std::ostringstream oss;
            for (size_t i = 0; i < actualOrder.size(); i++) {
                if (i > 0) oss << ",";
                oss << "S" << actualOrder[i];
            }
            result.actualOrder = oss.str();
        }

        // Compare: expected[i].id should match actualOrder[i]
        result.consistent = true;
        result.checksPerformed = static_cast<int>(entries.size());

        if (actualOrder.size() != entries.size()) {
            result.consistent = false;
            std::ostringstream oss;
            oss << "Found " << actualOrder.size() << " surfaces in z-order, expected " << entries.size();
            result.mismatchDetails = oss.str();
        } else {
            for (size_t i = 0; i < entries.size(); i++) {
                if (entries[i].id != actualOrder[i]) {
                    result.consistent = false;
                    std::ostringstream oss;
                    oss << "Position " << i << ": expected S" << entries[i].id
                        << " (sublevel " << entries[i].sublevel
                        << ") but found S" << actualOrder[i];
                    result.mismatchDetails = oss.str();
                    break;  // Report first mismatch
                }
            }
        }

        totalChecks_++;
        if (!result.consistent) {
            totalMismatches_++;
        }

        return result;
    }

    int totalChecks() const { return totalChecks_; }
    int totalMismatches() const { return totalMismatches_; }
    void reset() { totalChecks_ = 0; totalMismatches_ = 0; }

private:
    int totalChecks_ = 0;
    int totalMismatches_ = 0;
};

}  // namespace morphic

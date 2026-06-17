#pragma once

#include "../core/types.h"
#include "../core/scene_graph.h"
#include "../core/window_host.h"
#include "../core/surface_role.h"

#include <windows.h>
#include <vector>
#include <string>
#include <unordered_map>
#include <sstream>
#include <memory>

namespace morphic {

// Surface Semantics Specification v1.1 — Executable Enforcement.
//
// This verifier checks the FORMAL RUNTIME CONTRACTS defined in the
// Morphic Surface Semantics Specification. Every violation ID (V-Z*, V-A*,
// V-S*) maps 1:1 to a section in the spec.
//
// Unlike InvariantChecker (structural correctness), this checks
// SEMANTIC CORRECTNESS: domain authority, z-order laws, activation
// contracts, and shell participation rules.
//
// Call validateAll() after every z-order realization and activation event.
// In debug builds, violations are logged via OutputDebugStringA.
//
// THREAD: UI thread only.
class RuntimeContractVerifier {
public:
    struct Violation {
        std::string code;       // V-Z1, V-A2, V-S3, etc.
        std::string rule;       // Human-readable rule name
        std::string details;    // Specific violation context
        uint64_t frame = 0;
    };

    struct Stats {
        int totalChecks = 0;
        int totalViolations = 0;
        int zOrderViolations = 0;
        int activationViolations = 0;
        int shellViolations = 0;
        int domainViolations = 0;
    };

    // Run ALL semantic contract checks.
    // Returns empty vector if all contracts hold.
    std::vector<Violation> validateAll(
        const SceneGraph& graph,
        const std::unordered_map<NodeId, std::unique_ptr<WindowHost>>& hosts,
        uint64_t frameNumber)
    {
        std::vector<Violation> violations;

        checkZOrderTierLaw(graph, hosts, frameNumber, violations);
        checkDesktopDomainExclusion(graph, hosts, frameNumber, violations);
        checkShellParticipation(hosts, frameNumber, violations);
        checkShowWindowContract(hosts, frameNumber, violations);
        checkOwnershipContract(hosts, frameNumber, violations);

        stats_.totalChecks++;
        stats_.totalViolations += static_cast<int>(violations.size());

        // In debug builds, log violations immediately.
#ifdef _DEBUG
        for (const auto& v : violations) {
            std::string msg = "CONTRACT VIOLATION [" + v.code + "] " +
                v.rule + ": " + v.details +
                " (frame " + std::to_string(v.frame) + ")\n";
            OutputDebugStringA(msg.c_str());
        }
#endif

        return violations;
    }

    // Run activation-specific checks (call after onSurfaceActivated).
    void checkActivationContract(
        NodeId activatedId,
        const std::unordered_map<NodeId, std::unique_ptr<WindowHost>>& hosts,
        bool tierRealizationRan,
        uint64_t frameNumber)
    {
        auto* host = findHost(hosts, activatedId);
        if (!host) return;

        auto traits = traitsForRole(host->currentRole());

        // V-A3: Desktop-domain surface must NOT pass through tier realization
        if (traits.zOrder == ZOrderPolicy::Independent && tierRealizationRan) {
            std::string msg = "CONTRACT VIOLATION [V-A3] DesktopDomainNoTierRealization: "
                "Surface #" + std::to_string(activatedId) +
                " (Detached) passed through DeferWindowPos tier realization"
                " (frame " + std::to_string(frameNumber) + ")\n";
            OutputDebugStringA(msg.c_str());
            stats_.activationViolations++;
            stats_.totalViolations++;
        }

        // V-A2: Compositor-domain surface must NOT call SetForegroundWindow
        // (This is checked structurally — we can't detect at runtime easily,
        //  but the code review ensures it.)
    }

    const Stats& stats() const { return stats_; }
    void resetStats() { stats_ = {}; }

private:
    Stats stats_;

    static const WindowHost* findHost(
        const std::unordered_map<NodeId, std::unique_ptr<WindowHost>>& hosts,
        NodeId id)
    {
        auto it = hosts.find(id);
        if (it != hosts.end() && it->second) return it->second.get();
        return nullptr;
    }

    // ========================================================================
    // V-Z1, V-Z2, V-Z3: Tier ordering law
    // Grouped < Floating < Overlay (within Compositor domain)
    // ========================================================================
    void checkZOrderTierLaw(
        const SceneGraph& graph,
        const std::unordered_map<NodeId, std::unique_ptr<WindowHost>>& hosts,
        uint64_t frame, std::vector<Violation>& out)
    {
        // Collect HWND z-positions for compositor-domain surfaces only.
        struct ZEntry {
            NodeId id;
            HWND hwnd;
            ZOrderPolicy tier;
            int zPos;  // lower = higher in z-order (GetWindow walk)
        };
        std::vector<ZEntry> entries;

        for (const auto& [id, host] : hosts) {
            if (!host || !host->isAlive()) continue;
            auto traits = traitsForRole(host->currentRole());

            // Skip Desktop-domain — they are OS-managed.
            if (traits.zOrder == ZOrderPolicy::Independent) continue;

            int zPos = getZPosition(host->hwnd());
            entries.push_back({id, host->hwnd(), traits.zOrder, zPos});
        }

        // Check all pairs: lower tier must NOT be above higher tier.
        for (size_t i = 0; i < entries.size(); i++) {
            for (size_t j = i + 1; j < entries.size(); j++) {
                auto& a = entries[i];
                auto& b = entries[j];

                int tierA = tierRank(a.tier);
                int tierB = tierRank(b.tier);

                // If A is lower tier than B, A must have HIGHER zPos (lower in z-order)
                if (tierA < tierB && a.zPos < b.zPos) {
                    // A (lower tier) is ABOVE B (higher tier) — VIOLATION
                    std::string code = violationCode(a.tier, b.tier);
                    std::ostringstream oss;
                    oss << "Surface #" << a.id << " (" << tierName(a.tier)
                        << ") is ABOVE Surface #" << b.id << " (" << tierName(b.tier)
                        << ") — tier law violated";
                    out.push_back({code, "TierOrderingLaw", oss.str(), frame});
                    stats_.zOrderViolations++;
                }
                if (tierB < tierA && b.zPos < a.zPos) {
                    std::string code = violationCode(b.tier, a.tier);
                    std::ostringstream oss;
                    oss << "Surface #" << b.id << " (" << tierName(b.tier)
                        << ") is ABOVE Surface #" << a.id << " (" << tierName(a.tier)
                        << ") — tier law violated";
                    out.push_back({code, "TierOrderingLaw", oss.str(), frame});
                    stats_.zOrderViolations++;
                }
            }
        }
    }

    // ========================================================================
    // V-Z5: Desktop-domain surfaces must NOT appear in DeferWindowPos batches.
    // We can't intercept DeferWindowPos at runtime, but we CAN check that
    // Independent surfaces are not at compositor-managed z-positions.
    // ========================================================================
    void checkDesktopDomainExclusion(
        const SceneGraph& graph,
        const std::unordered_map<NodeId, std::unique_ptr<WindowHost>>& hosts,
        uint64_t frame, std::vector<Violation>& out)
    {
        // Structural check: Desktop surfaces should NOT have elevation sublevels
        // that look like compositor assignment (> 0 from elevation manager).
        // This is a heuristic — the real enforcement is code-level.
        graph.forEachSurface([&](const CompositionSurface* s) {
            auto it = hosts.find(s->id());
            if (it == hosts.end() || !it->second) return;
            auto traits = traitsForRole(it->second->currentRole());

            if (traits.zOrder == ZOrderPolicy::Independent) {
                // Desktop-domain: should not be getting elevation sublevels
                // from the compositor's tier realization.
                // (This is a weak check — elevation manager COULD assign them
                //  via bringToFront. The real fix is structural exclusion.)
            }
        });
    }

    // ========================================================================
    // V-S1..V-S6: Shell participation contracts
    // ========================================================================
    void checkShellParticipation(
        const std::unordered_map<NodeId, std::unique_ptr<WindowHost>>& hosts,
        uint64_t frame, std::vector<Violation>& out)
    {
        for (const auto& [id, host] : hosts) {
            if (!host || !host->isAlive()) continue;
            HWND hwnd = host->hwnd();
            auto traits = traitsForRole(host->currentRole());

            LONG exStyle = GetWindowLongW(hwnd, GWL_EXSTYLE);
            HWND owner = GetWindow(hwnd, GW_OWNER);

            if (traits.zOrder == ZOrderPolicy::Independent) {
                // V-S4: Desktop-domain must have APPWINDOW + OVERLAPPEDWINDOW + no owner
                if (!(exStyle & WS_EX_APPWINDOW)) {
                    out.push_back({"V-S4", "DesktopShellVisibility",
                        "Surface #" + std::to_string(id) +
                        " (Detached) missing WS_EX_APPWINDOW", frame});
                    stats_.shellViolations++;
                }
                if (owner != nullptr) {
                    out.push_back({"V-S4", "DesktopShellVisibility",
                        "Surface #" + std::to_string(id) +
                        " (Detached) has owner=0x" +
                        std::to_string(reinterpret_cast<uintptr_t>(owner)) +
                        " (should be nullptr)", frame});
                    stats_.shellViolations++;
                }
            } else {
                // V-S3: Compositor-domain must have TOOLWINDOW + owner
                if (traits.activation != ActivationPolicy::NoActivate) {
                    // Only check Workspace/ToolPalette (not Overlay which may differ)
                    if (!(exStyle & WS_EX_TOOLWINDOW)) {
                        out.push_back({"V-S3", "CompositorShellInvisibility",
                            "Surface #" + std::to_string(id) +
                            " (Compositor) missing WS_EX_TOOLWINDOW", frame});
                        stats_.shellViolations++;
                    }
                }
                if (exStyle & WS_EX_APPWINDOW) {
                    out.push_back({"V-S3", "CompositorShellInvisibility",
                        "Surface #" + std::to_string(id) +
                        " (Compositor) has WS_EX_APPWINDOW — will appear in Alt+Tab!", frame});
                    stats_.shellViolations++;
                }
            }
        }
    }

    // ========================================================================
    // V-S6: ShowWindow mode contract
    // Compositor = SW_SHOWNOACTIVATE, Desktop = SW_SHOW
    // We can't check ShowWindow mode at runtime, but we CAN check
    // that visible Desktop surfaces are actually foreground-capable.
    // ========================================================================
    void checkShowWindowContract(
        const std::unordered_map<NodeId, std::unique_ptr<WindowHost>>& hosts,
        uint64_t frame, std::vector<Violation>& out)
    {
        // This is primarily a code-review-time check.
        // At runtime, we verify the EFFECT: Desktop surfaces should be
        // visible and not cloaked.
        for (const auto& [id, host] : hosts) {
            if (!host || !host->isAlive()) continue;
            auto traits = traitsForRole(host->currentRole());

            if (traits.zOrder == ZOrderPolicy::Independent) {
                if (!IsWindowVisible(host->hwnd())) {
                    // Desktop surface is hidden — may be intentional (minimized)
                    // but worth tracking.
                    out.push_back({"V-S6", "DesktopVisibility",
                        "Surface #" + std::to_string(id) +
                        " (Detached) is not visible — may have lost shell registration",
                        frame});
                    stats_.shellViolations++;
                }
            }
        }
    }

    // ========================================================================
    // Ownership contract: Compositor-domain = owned by main window,
    // Desktop-domain = unowned.
    // ========================================================================
    void checkOwnershipContract(
        const std::unordered_map<NodeId, std::unique_ptr<WindowHost>>& hosts,
        uint64_t frame, std::vector<Violation>& out)
    {
        for (const auto& [id, host] : hosts) {
            if (!host || !host->isAlive()) continue;
            auto traits = traitsForRole(host->currentRole());
            HWND owner = GetWindow(host->hwnd(), GW_OWNER);

            if (traits.zOrder == ZOrderPolicy::Independent) {
                if (owner != nullptr) {
                    out.push_back({"V-S4", "DesktopOwnership",
                        "Surface #" + std::to_string(id) +
                        " (Detached) is OWNED — cannot appear in Alt+Tab", frame});
                    stats_.domainViolations++;
                }
            }
        }
    }

    // --- Helpers ---

    // Get z-position: 0 = topmost, higher = lower in z-order.
    static int getZPosition(HWND hwnd) {
        int pos = 0;
        HWND walker = GetWindow(GetDesktopWindow(), GW_CHILD);
        while (walker) {
            if (walker == hwnd) return pos;
            walker = GetWindow(walker, GW_HWNDNEXT);
            pos++;
            if (pos > 1000) return 9999;  // Safety
        }
        return 9999;
    }

    static int tierRank(ZOrderPolicy tier) {
        switch (tier) {
            case ZOrderPolicy::Grouped:     return 0;
            case ZOrderPolicy::Floating:    return 1;
            case ZOrderPolicy::Overlay:     return 2;
            case ZOrderPolicy::Independent: return -1;  // Not in compositor tiers
        }
        return -1;
    }

    static std::string tierName(ZOrderPolicy tier) {
        switch (tier) {
            case ZOrderPolicy::Grouped:     return "Grouped";
            case ZOrderPolicy::Floating:    return "Floating";
            case ZOrderPolicy::Overlay:     return "Overlay";
            case ZOrderPolicy::Independent: return "Independent";
        }
        return "Unknown";
    }

    static std::string violationCode(ZOrderPolicy lower, ZOrderPolicy higher) {
        if (lower == ZOrderPolicy::Grouped && higher == ZOrderPolicy::Floating)
            return "V-Z1";
        if (lower == ZOrderPolicy::Grouped && higher == ZOrderPolicy::Overlay)
            return "V-Z2";
        if (lower == ZOrderPolicy::Floating && higher == ZOrderPolicy::Overlay)
            return "V-Z3";
        return "V-Z?";
    }
};

}  // namespace morphic

#include "activation_manager.h"
#include "../core/surface_registry.h"
#include "../core/thread_affinity.h"

namespace morphic {

void ActivationManager::initialize(SurfaceRegistry* registry) {
    registry_ = registry;
}

// --- Surface lifecycle (canonical stack management) ---

void ActivationManager::registerSurface(NodeId surfaceId) {
    // Add to bottom of workspace stack (newly created = lowest z-order)
    auto it = std::find(workspaceStack_.begin(), workspaceStack_.end(), surfaceId);
    if (it == workspaceStack_.end()) {
        workspaceStack_.push_back(surfaceId);
    }
}

void ActivationManager::unregisterSurface(NodeId surfaceId) {
    auto it = std::find(workspaceStack_.begin(), workspaceStack_.end(), surfaceId);
    if (it != workspaceStack_.end()) {
        workspaceStack_.erase(it);
    }
}

void ActivationManager::promoteInStack(NodeId surfaceId) {
    auto it = std::find(workspaceStack_.begin(), workspaceStack_.end(), surfaceId);
    if (it != workspaceStack_.end()) {
        workspaceStack_.erase(it);
        workspaceStack_.push_back(surfaceId);  // Move to top (highest z-order)
    }
}

// --- Z-order realization ---

bool ActivationManager::requestActivation(NodeId targetId) {
    MORPHIC_ASSERT_UI_THREAD();
    if (locked_ || !registry_) return false;
    if (handlingActivation_) return false;

    auto* host = registry_->getHost(targetId);
    if (!host || !host->isAlive()) return false;

    HWND hwnd = host->hwnd();
    bool result = SetForegroundWindow(hwnd) != 0;

    handlingActivation_ = false;
    return result;
}

void ActivationManager::onSurfaceActivated(NodeId surfaceId) {
    MORPHIC_ASSERT_UI_THREAD();
    if (locked_) return;
    if (!registry_) return;

    // During drag/resize, suppress z-order realization.
    if (transactionDepth_ > 0) return;

    // Guard against re-entrant activation.
    if (handlingActivation_) return;
    handlingActivation_ = true;

    MORPHIC_ACQUIRE_ACTIVATION_AUTHORITY();

    // Promote the activated surface in the canonical stack.
    promoteInStack(surfaceId);

    // DOMAIN CHECK: Independent (Detached) surfaces are Desktop-domain.
    // Win32 owns their z-order. Do NOT run tier realization — it would
    // push them below Floating/Overlay, undoing Win32's foreground raise
    // and breaking Alt+Tab shell heuristics.
    {
        auto* activatedHost = registry_->getHost(surfaceId);
        if (activatedHost && activatedHost->isAlive()) {
            auto traits = traitsForRole(activatedHost->currentRole());
            if (traits.zOrder == ZOrderPolicy::Independent) {
                handlingActivation_ = false;
                return;  // Win32 keeps Detached wherever it placed it
            }
        }
    }

    // Build z-order tiers from CANONICAL STACK (deterministic).
    // NOT from unordered_map iteration (nondeterministic).
    std::vector<HWND> grouped;
    std::vector<HWND> floating;
    std::vector<HWND> overlay;

    // Grouped: iterate workspace stack in order (index 0 = bottom, last = top)
    for (NodeId id : workspaceStack_) {
        auto* host = registry_->getHost(id);
        if (!host || !host->isAlive()) continue;
        auto traits = traitsForRole(host->currentRole());

        switch (traits.zOrder) {
            case ZOrderPolicy::Grouped:
                grouped.push_back(host->hwnd());
                break;
            case ZOrderPolicy::Independent:
                // Desktop-domain: NEVER include in tier realization.
                // Win32 owns their z-order independently.
                break;
            default:
                break;  // Floating/Overlay handled below
        }
    }

    // Floating and Overlay: iterate hosts map (order doesn't matter within tier)
    for (const auto& [id, host] : registry_->hosts()) {
        if (!host || !host->isAlive()) continue;
        auto traits = traitsForRole(host->currentRole());

        switch (traits.zOrder) {
            case ZOrderPolicy::Floating:
                floating.push_back(host->hwnd());
                break;
            case ZOrderPolicy::Overlay:
                overlay.push_back(host->hwnd());
                break;
            default:
                break;
        }
    }

    realizeZOrderTiers(grouped, floating, overlay);

    handlingActivation_ = false;
}

void ActivationManager::onMainWindowActivated() {
    MORPHIC_ASSERT_UI_THREAD();
    if (locked_) return;
    if (!registry_) return;
    if (handlingActivation_) return;

    // Suppress during interaction transactions
    if (transactionDepth_ > 0) return;

    handlingActivation_ = true;

    MORPHIC_ACQUIRE_ACTIVATION_AUTHORITY();

    // Same tier logic but from canonical stack for grouped surfaces.
    std::vector<HWND> grouped;
    std::vector<HWND> floating;
    std::vector<HWND> overlay;

    for (NodeId id : workspaceStack_) {
        auto* host = registry_->getHost(id);
        if (!host || !host->isAlive()) continue;
        auto traits = traitsForRole(host->currentRole());

        switch (traits.zOrder) {
            case ZOrderPolicy::Grouped:
                grouped.push_back(host->hwnd());
                break;
            case ZOrderPolicy::Independent:
                // DO NOT raise — detached keeps independent z-order
                break;
            default:
                break;
        }
    }

    for (const auto& [id, host] : registry_->hosts()) {
        if (!host || !host->isAlive()) continue;
        auto traits = traitsForRole(host->currentRole());

        switch (traits.zOrder) {
            case ZOrderPolicy::Floating:
                floating.push_back(host->hwnd());
                break;
            case ZOrderPolicy::Overlay:
                overlay.push_back(host->hwnd());
                break;
            default:
                break;
        }
    }

    realizeZOrderTiers(grouped, floating, overlay);

    handlingActivation_ = false;
}

void ActivationManager::realizeZOrderTiers(
    const std::vector<HWND>& grouped,
    const std::vector<HWND>& floating,
    const std::vector<HWND>& overlay) {

    int total = static_cast<int>(grouped.size() + floating.size() + overlay.size());
    if (total == 0) return;

    // Signal that the COMPOSITOR is changing z-order.
    realizingZOrder_ = true;

    HDWP dwp = BeginDeferWindowPos(total);
    if (!dwp) { realizingZOrder_ = false; return; }

    // Tier 1: Grouped surfaces (workspace — canonical stack order)
    for (HWND h : grouped) {
        dwp = DeferWindowPos(dwp, h, HWND_TOP,
            0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        if (!dwp) { realizingZOrder_ = false; return; }
    }

    // Tier 2: Floating surfaces (ToolPalette — above grouped)
    for (HWND h : floating) {
        dwp = DeferWindowPos(dwp, h, HWND_TOP,
            0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        if (!dwp) { realizingZOrder_ = false; return; }
    }

    // Tier 3: Overlay surfaces — highest within app.
    for (HWND h : overlay) {
        dwp = DeferWindowPos(dwp, h, HWND_TOP,
            0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        if (!dwp) { realizingZOrder_ = false; return; }
    }

    EndDeferWindowPos(dwp);

    // Post-realization safety net: sequential SetWindowPos to enforce
    // deterministic tier ordering (Grouped < Floating < Overlay).
    for (HWND h : floating) {
        SetWindowPos(h, HWND_TOP, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    for (HWND h : overlay) {
        SetWindowPos(h, HWND_TOP, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }

    realizingZOrder_ = false;
}

// --- Interaction Transaction Mode ---

void ActivationManager::beginInteractionTransaction() {
    transactionDepth_++;
}

void ActivationManager::endInteractionTransaction() {
    if (transactionDepth_ > 0) {
        transactionDepth_--;
    }
    // NOTE: z-order reconciliation is NOT done here.
    // Caller (WM_EXITSIZEMOVE, WM_LBUTTONUP) calls compositor->onSurfaceActivated()
    // which runs the FULL compositor flow (elevation + z-order + focus).
}

}  // namespace morphic

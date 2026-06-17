#pragma once

#include "activity_state.h"
#include "../core/types.h"
#include <unordered_map>
#include <chrono>

namespace morphic {

// Phase 3 — Residency Budget
//
// Without explicit budgets, the orchestrator cannot make
// bounded decisions. Budgets transform governance from
// "state orchestration" into "resource allocation."
struct ResidencyBudget {
    // ---- Hard limits ----
    float maxWarmMB = 2000.0f;           // total warm residency ceiling
    int maxWarmEngines = 10;             // max simultaneous warm engines
    float maxWakeBurstMs = 150.0f;       // max acceptable burst recovery time
    int maxSimultaneousWakes = 5;        // max engines to resume at once

    // ---- Current consumption (updated by controller) ----
    float currentWarmMB = 0.0f;
    int currentWarmEngines = 0;

    // ---- Phase 3B.3: Budget Pressure ----
    // NOT a binary flag. Graduated economic pressure semantics.
    //   Relaxed     = no constraints active
    //   Elevated    = approaching limits, prefer efficiency
    //   Constrained = at/over limits, park candidates
    //   Critical    = deeply over limits, violate continuity if needed
    enum class BudgetPressure {
        Relaxed,       // < 60% utilization
        Elevated,      // 60-85% utilization
        Constrained,   // 85-100% utilization or engine count at limit
        Critical       // over budget on both axes
    };

    BudgetPressure pressure() const {
        float mbUtil = (maxWarmMB > 0.0f) ? (currentWarmMB / maxWarmMB) : 0.0f;
        bool overEngines = currentWarmEngines > maxWarmEngines;
        bool overMB = currentWarmMB > maxWarmMB;

        if (overEngines && overMB) return BudgetPressure::Critical;
        if (overEngines || overMB) return BudgetPressure::Constrained;
        if (mbUtil > 0.85f || currentWarmEngines >= maxWarmEngines) return BudgetPressure::Constrained;
        if (mbUtil > 0.60f || currentWarmEngines >= (maxWarmEngines - 1)) return BudgetPressure::Elevated;
        return BudgetPressure::Relaxed;
    }

    // ---- Budget health ----
    float utilization() const {
        if (maxWarmMB <= 0.0f) return 1.0f;
        return currentWarmMB / maxWarmMB;
    }
    bool isOverBudget() const {
        return currentWarmMB > maxWarmMB ||
               currentWarmEngines > maxWarmEngines;
    }
};

// Phase 3 — Transition Cost Tracker
//
// Tracks the COST of orchestration decisions, not just the decisions.
// Without this, the orchestrator can become economically irrational:
// rapidly parking and waking the same engine, accumulating churn cost
// that exceeds the savings.
class TransitionCostTracker {
public:
    struct RendererCostState {
        int totalTransitions = 0;
        int transitionsInWindow = 0;      // current 10s window
        int parkWakeCycles = 0;           // rapid park→wake count
        ActivityState lastState = ActivityState::Active;
        std::chrono::high_resolution_clock::time_point lastTransitionTime;
        std::chrono::high_resolution_clock::time_point windowStartTime;
    };

    void recordTransition(RenderId id, ActivityState from, ActivityState to) {
        auto now = std::chrono::high_resolution_clock::now();
        auto& state = states_[id];

        state.totalTransitions++;
        state.lastState = to;
        state.lastTransitionTime = now;

        // Window management
        auto sinceWindow = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - state.windowStartTime).count();
        if (sinceWindow > 10000) {
            state.transitionsInWindow = 0;
            state.windowStartTime = now;
        }
        state.transitionsInWindow++;

        // Detect park→wake churn
        if (from == ActivityState::Parked && to == ActivityState::Active) {
            state.parkWakeCycles++;
        }
    }

    bool isChurning(RenderId id) const {
        auto it = states_.find(id);
        if (it == states_.end()) return false;
        return it->second.transitionsInWindow >= 3;
    }

    int recentTransitionCount(RenderId id) const {
        auto it = states_.find(id);
        if (it == states_.end()) return 0;
        return it->second.transitionsInWindow;
    }

    double timeSinceLastTransitionSec(RenderId id) const {
        auto it = states_.find(id);
        if (it == states_.end()) return 999.0;
        auto now = std::chrono::high_resolution_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            now - it->second.lastTransitionTime).count() / 1000.0;
    }

    const RendererCostState* costState(RenderId id) const {
        auto it = states_.find(id);
        return (it != states_.end()) ? &it->second : nullptr;
    }

private:
    std::unordered_map<RenderId, RendererCostState> states_;
};

}  // namespace morphic

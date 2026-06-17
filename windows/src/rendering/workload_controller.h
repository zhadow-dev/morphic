#pragma once

#include "runtime_policy.h"
#include "renderer_adapter.h"
#include "renderer_capabilities.h"
#include "activity_state.h"
#include "visibility_state.h"
#include "workload_profile.h"
#include "observable_context.h"
#include "policy_explanation.h"
#include "orchestration_invariants.h"
#include "residency_budget.h"
#include "system_pressure_monitor.h"
#include "recovery_state.h"
#include "adaptive_policy.h"
#include "../core/types.h"
#include <unordered_map>
#include <memory>
#include <chrono>
#include <string>
#include <windows.h>

namespace morphic {

// Phase 3E: Governance overhead profiling.
// Aggregate timings always collected. Per-renderer evaluate() sampled.
struct GovernanceTimings {
    // Aggregate — always collected (cheap: 4 QPC pairs per drain)
    double totalDrainUs = 0;        // full drainGovernance() cost
    double adaptiveComputeUs = 0;   // computeAdaptiveParams() cost
    double recoveryReportUs = 0;    // recoveryReport() aggregation cost
    double dispatchWakesUs = 0;     // dispatchPendingWakes() cost
    int evaluateCount = 0;          // renderers evaluated this drain

    // Sampled per-renderer evaluate() — collected every Nth drain cycle
    double sampledEvaluateUsP50 = 0;
    double sampledEvaluateUsP95 = 0;
    double sampledEvaluateUsMax = 0;
    int evaluateSampleCount = 0;    // total samples collected

    // Block 2: Dormancy audit — verify zero cost at default
    int adaptCadenceSkips = 0;      // times compute() returned cached (cadence)
    int adaptDefaultCount = 0;      // times compute() resulted in default params
    int staggerEmptyChecks = 0;     // times dispatchPendingWakes() returned immediately (empty)
    int timerKeptAlive = 0;         // times compositor timer stayed alive for wake queue
    bool anyQueueAllocation = false; // true if pendingWakes_ vector ever grew
};

// Rolling percentile tracker for governance timings.
// Fixed-size ring buffer, no dynamic allocation after init.
class TimingSampler {
public:
    static constexpr int kMaxSamples = 128;

    void record(double valueUs) {
        samples_[writeIdx_ % kMaxSamples] = valueUs;
        writeIdx_++;
        if (count_ < kMaxSamples) count_++;
    }

    double p50() const { return percentile(0.50); }
    double p95() const { return percentile(0.95); }
    double maxVal() const {
        double m = 0;
        for (int i = 0; i < count_; i++) {
            if (samples_[i] > m) m = samples_[i];
        }
        return m;
    }
    int count() const { return count_; }

private:
    double percentile(double p) const {
        if (count_ == 0) return 0;
        // Insertion sort on small fixed buffer — no <algorithm> dependency
        double tmp[kMaxSamples];
        for (int i = 0; i < count_; i++) tmp[i] = samples_[i];
        for (int i = 1; i < count_; i++) {
            double key = tmp[i];
            int j = i - 1;
            while (j >= 0 && tmp[j] > key) {
                tmp[j + 1] = tmp[j];
                j--;
            }
            tmp[j + 1] = key;
        }
        int idx = static_cast<int>(p * (count_ - 1));
        return tmp[idx];
    }

    double samples_[kMaxSamples] = {};
    int writeIdx_ = 0;
    int count_ = 0;
};

// QPC helper — returns elapsed microseconds between two LARGE_INTEGER values.
inline double qpcElapsedUs(const LARGE_INTEGER& qpcStart, const LARGE_INTEGER& qpcStop) {
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    return static_cast<double>(qpcStop.QuadPart - qpcStart.QuadPart) * 1e6 / freq.QuadPart;
}

// Phase 3 — Workload Controller (Governance-Aware)
// THREAD: UI thread only.
class WorkloadController {
public:
    // Per-renderer orchestration state.
    struct RendererOrchState {
        RenderId rendererId = 0;
        ActivityState currentActivity = ActivityState::Active;
        ActivityState lastRecommendation = ActivityState::Active;
        VisibilityState lastVisibility = VisibilityState::Unknown;

        // Phase 3: Governance
        EffectiveWorkloadProfile profile;    // host-authoritative
        ObservableContext context;           // latest observations
        PolicyExplanation lastExplanation;   // last policy decision (inspectable)

        // Anti-oscillation
        std::chrono::high_resolution_clock::time_point lastTransitionTime;
        std::chrono::high_resolution_clock::time_point visibilityChangeTime;
        std::chrono::high_resolution_clock::time_point lastInteractionTime;

        // Last command result
        CommandResult lastResult = CommandResult::Accepted;

        // Phase 3B.3: Simulated visibility (for semantic testing)
        bool hasSimulatedVisibility = false;
        VisibilityState simulatedVisibility = VisibilityState::Unknown;
        float simulatedConfidence = 0.0f;

        // Phase 3B.3: Governance backpressure tracking
        int budgetDenials = 0;           // times budget escalated this renderer
        int budgetProtections = 0;       // times MustRemainWarm survived budget pressure
        int suppressionBlocks = 0;       // times cooldown/hysteresis blocked
        double lastBlockedMs = 0.0;      // time of last blocked transition

        // Phase 3B.3 v3: Split mismatch aging — separate governance failure domains
        std::chrono::high_resolution_clock::time_point mismatchStartTime;
        bool inMismatch = false;         // currently desired != actual
        bool mismatchIsBudget = false;   // true if mismatch caused by budget
        bool mismatchIsSuppression = false; // true if mismatch caused by invariant

        // Phase 3C: Experiential recovery tracking
        RecoveryTracker recovery;

        // Adapter (owned)
        std::unique_ptr<RendererAdapter> adapter;
    };

    // Register a renderer for orchestration.
    void registerRenderer(RenderId id, std::unique_ptr<RendererAdapter> adapter) {
        RendererOrchState state;
        state.rendererId = id;
        state.adapter = std::move(adapter);
        auto now = std::chrono::high_resolution_clock::now();
        state.lastTransitionTime = now;
        state.visibilityChangeTime = now;
        state.lastInteractionTime = now;
        states_[id] = std::move(state);

        // Update budget
        budget_.currentWarmEngines++;
    }

    // Unregister a renderer.
    void unregisterRenderer(RenderId id) {
        auto it = states_.find(id);
        if (it != states_.end()) {
            budget_.currentWarmEngines--;
            budget_.currentWarmMB -= it->second.profile.estimatedWarmMB;
            if (budget_.currentWarmMB < 0) budget_.currentWarmMB = 0;
        }
        states_.erase(id);
        costTracker_.recordTransition(id, ActivityState::Active, ActivityState::Dormant);
    }

    // Read-only access to orchestration state (for rendererManager sync).
    const RendererOrchState* getOrchState(RenderId id) const {
        auto it = states_.find(id);
        if (it == states_.end()) return nullptr;
        return &it->second;
    }

    // ---- GOVERNANCE AUTHORITY BOUNDARY ----
    // ALL lifecycle transitions MUST route through this method.
    // No direct adapter manipulation. No exceptions.
    //
    // Parameters:
    //   force  — bypass invariant enforcement (for adversarial testing).
    //            Still logs, records costs, and tracks overrides.
    //   source — who requested (for telemetry/debugging).
    //
    // Returns: true if transition occurred.
    struct TransitionResult {
        bool transitioned = false;
        CommandResult commandResult = CommandResult::Failed;
        const char* reason = "unknown";
        bool invariantBypassed = false;
        bool deferred = false;  // Phase 3D: wake queued for stagger
    };

    // Phase 3D: Pending wake entry for stagger queue.
    // INVARIANT: Before dispatch, the renderer's desired state is
    // revalidated. If the renderer is no longer wanted Active,
    // the queued wake is DISCARDED. This prevents delayed lies.
    struct PendingWake {
        RenderId id = 0;
        const char* source = "";
        std::chrono::high_resolution_clock::time_point queuedAt;
        int priority = 2;  // 0=immediate, 1=minimal, 2=normal, 3=extended

        // Priority multiplier on stagger interval.
        // 0 = bypass stagger entirely (InteractionCritical)
        // Lower = faster wake.
        static int priorityForProfile(const EffectiveWorkloadProfile& p) {
            using ST = EffectiveWorkloadProfile::SurvivalTier;
            if (p.survivalTier == ST::MustRemainWarm) return 0;  // immediate
            if (p.survivalTier == ST::PreferWarm)     return 1;  // minimal stagger
            if (p.survivalTier == ST::ParkingCandidate) return 2; // normal stagger
            return 3;  // EvictionCandidate / ephemeral: extended stagger
        }
    };

    TransitionResult requestTransition(RenderId id, ActivityState target,
                                       const char* source, bool force = false) {
        auto it = states_.find(id);
        if (it == states_.end()) {
            return { false, CommandResult::Failed, "renderer not found", false };
        }
        auto& orch = it->second;
        if (!orch.adapter) {
            return { false, CommandResult::Failed, "no adapter", false };
        }

        // Phase X: Block all transitions for renderers being destroyed.
        // Governance must not resurrect a dying renderer.
        if (orch.currentActivity == ActivityState::Destroying) {
            return { false, CommandResult::Failed, "renderer destroying", false };
        }

        // Same state? No-op.
        if (orch.currentActivity == target) {
            return { false, CommandResult::Accepted, "already in target state", false };
        }

        // ---- Invariant validation ----
        int recentTransitions = costTracker_.recentTransitionCount(id);
        double timeSinceLastTransition = costTracker_.timeSinceLastTransitionSec(id);

        auto validation = invariants_.validate(
            id, orch.currentActivity, target, orch.profile,
            recentTransitions, timeSinceLastTransition);

        if (!validation.allowed && !force) {
            // Invariant blocked this transition — structural telemetry
            orch.suppressionBlocks++;
            orch.lastBlockedMs = static_cast<double>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::high_resolution_clock::now().time_since_epoch()).count());

            OutputDebugStringA(("GOV_AUTH: #" + std::to_string(id) +
                " BLOCKED " + toString(orch.currentActivity) + " -> " + toString(target) +
                " [" + validation.violatedInvariant + "] source=" + source + "\n").c_str());

            return { false, CommandResult::Failed, validation.violatedInvariant, false };
        }

        bool invariantBypassed = (!validation.allowed && force);

        // ---- Phase 3D: Adaptive wake management ----
        // Only applies to wake transitions (→ Active from Parked/Dormant).
        // Only enforced when adaptive params are NOT default.
        //
        // ABSOLUTE INVARIANTS (AdaptivePolicy may NEVER):
        //   - reorder survival tiers
        //   - reinterpret workload meaning
        //   - block invariant-protected workloads
        //   - permanently defer progress
        //
        // AdaptivePolicy ONLY: modulates parameters, influences timing,
        // influences concurrency, influences thresholds.
        // RuntimePolicy remains sole semantic authority.
        if (target == ActivityState::Active && !force &&
            (orch.currentActivity == ActivityState::Parked ||
             orch.currentActivity == ActivityState::Dormant)) {

            const auto& ap = adaptivePolicy_.current();
            if (!ap.isDefault()) {
                int wakePriority = PendingWake::priorityForProfile(orch.profile);

                // Priority 0 (InteractionCritical/MustRemainWarm): ALWAYS immediate
                // These bypass both concurrency and stagger gates.
                if (wakePriority > 0) {
                    // ---- Concurrency gate ----
                    int concurrentRecoveries = countConcurrentRecoveries();
                    if (concurrentRecoveries >= ap.maxConcurrentWakes) {
                        deferredWakeCount_++;
                        OutputDebugStringA(("GOV_ADAPT: #" + std::to_string(id) +
                            " WAKE DEFERRED concurrent=" + std::to_string(concurrentRecoveries) +
                            " limit=" + std::to_string(ap.maxConcurrentWakes) +
                            " source=" + std::string(ap.adaptationSource) + "\n").c_str());
                        return { false, CommandResult::Failed,
                                 "adaptive concurrency limit", false, false };
                    }

                    // ---- Stagger gate ----
                    if (ap.wakeStaggerMs > 0) {
                        auto now = std::chrono::high_resolution_clock::now();
                        auto sinceLastWake = std::chrono::duration_cast<std::chrono::milliseconds>(
                            now - lastWakeDispatchedAt_).count();
                        int requiredDelay = (ap.wakeStaggerMs * wakePriority) / 2;

                        // Check hard ceiling: if total stagger window would exceed max,
                        // release immediately to prevent sluggishness.
                        auto queueAge = pendingWakes_.empty() ? 0 :
                            std::chrono::duration_cast<std::chrono::milliseconds>(
                                now - pendingWakes_.front().queuedAt).count();
                        bool ceilingHit = queueAge >= kMaxTotalWakeWindowMs;

                        if (sinceLastWake < requiredDelay && !ceilingHit) {
                            // Queue for staggered dispatch (with coalescing)
                            queueWake(id, source, wakePriority, now);
                            staggeredWakeCount_++;
                            return { false, CommandResult::Accepted,
                                     "stagger queued", false, true };
                        }
                    }
                } else {
                    // Priority 0: immediate wake, count as protected
                    protectedWakeCount_++;
                    immediateWakeCount_++;
                }

                // Track dispatch time for stagger spacing
                lastWakeDispatchedAt_ = std::chrono::high_resolution_clock::now();
            }
        }

        // ---- Execute transition ----
        CommandResult cmdResult = orch.adapter->requestActivityTransition(target);

        if (cmdResult == CommandResult::Accepted ||
            cmdResult == CommandResult::PartiallyApplied) {

            ActivityState oldActivity = orch.currentActivity;
            orch.currentActivity = target;
            orch.lastTransitionTime = std::chrono::high_resolution_clock::now();

            // Record transition cost
            costTracker_.recordTransition(id, oldActivity, target);

            // Phase 3C: Recovery tracking — begin recovery on wake transitions
            if (target == ActivityState::Active &&
                (oldActivity == ActivityState::Parked || oldActivity == ActivityState::Dormant)) {
                orch.recovery.beginRecovery();
            }

            // Track override debt
            if (invariantBypassed) {
                forceOverrideCount_++;
            }

            // Log with full governance context
            OutputDebugStringA(("GOV_AUTH: #" + std::to_string(id) +
                " " + toString(oldActivity) + " -> " + toString(target) +
                " source=" + source +
                (invariantBypassed ? " FORCE_OVERRIDE" : "") +
                " result=" + toString(cmdResult) + "\n").c_str());

            return { true, cmdResult, "accepted", invariantBypassed };
        }

        // Renderer refused
        OutputDebugStringA(("GOV_AUTH: #" + std::to_string(id) +
            " refused " + toString(target) +
            " result=" + toString(cmdResult) +
            " source=" + source + "\n").c_str());

        return { false, cmdResult, "renderer refused", false };
    }

    // ---- Override observability ----
    int forceOverrideCount() const { return forceOverrideCount_; }

    // Set workload profile for a renderer (host-authoritative).
    void setWorkloadProfile(RenderId id, const EffectiveWorkloadProfile& profile) {
        auto it = states_.find(id);
        if (it != states_.end()) {
            // Update budget with new estimate
            budget_.currentWarmMB -= it->second.profile.estimatedWarmMB;
            it->second.profile = profile;
            budget_.currentWarmMB += profile.estimatedWarmMB;
        }
    }

    // Declare workload from Dart side (untrusted → effective).
    void declareWorkloadTraits(RenderId id, const DeclaredWorkloadProfile& declared) {
        auto eff = EffectiveWorkloadProfile::fromDeclared(declared);
        setWorkloadProfile(id, eff);
    }

    // ---- Phase 3B.3: Visibility Injection ----
    // Inject a simulated visibility state for governance reasoning.
    // Bypasses VisibilityObserver — governance reasons about claimed reality.
    // This creates semantic isolation: governance semantics tested independently
    // from Win32 visibility APIs.
    void injectVisibility(RenderId id, VisibilityState visibility, float confidence) {
        auto it = states_.find(id);
        if (it == states_.end()) return;
        it->second.hasSimulatedVisibility = true;
        it->second.simulatedVisibility = visibility;
        it->second.simulatedConfidence = confidence;

        OutputDebugStringA(("GOV_INJECT_VIS: #" + std::to_string(id) +
            " state=" + toString(visibility) +
            " confidence=" + std::to_string(confidence) + "\n").c_str());
    }

    // Clear simulated visibility — revert to real VisibilityObserver.
    void clearSimulatedVisibility(RenderId id) {
        auto it = states_.find(id);
        if (it == states_.end()) return;
        it->second.hasSimulatedVisibility = false;
    }

    void clearAllSimulatedVisibility() {
        for (auto& [id, orch] : states_) {
            orch.hasSimulatedVisibility = false;
        }
    }

    // ---- Phase 3B.3: Budget Configuration ----
    // Artificially constrain budget for economic governance testing.
    void configureBudget(int maxEngines, float maxMB) {
        budget_.maxWarmEngines = maxEngines;
        budget_.maxWarmMB = maxMB;
        OutputDebugStringA(("GOV_BUDGET_CONFIG: maxEngines=" + std::to_string(maxEngines) +
            " maxMB=" + std::to_string(maxMB) +
            " current=" + std::to_string(budget_.currentWarmEngines) +
            "/" + std::to_string(static_cast<int>(budget_.currentWarmMB)) + "MB\n").c_str());
    }

    // Reset budget to generous defaults.
    void resetBudget() {
        budget_.maxWarmEngines = 10;
        budget_.maxWarmMB = 2000.0f;
    }

    // ---- Transition Semantics ----
    // Transitions are NOT symmetric. Wake is user-critical.
    // Parking is optimization. These have fundamentally different priorities.
    enum class TransitionClass {
        WakeCritical,   // Parked/Throttled → Active (user needs window NOW)
        Optimization,   // Active → Throttled/Parked (save resources)
        Reclamation,    // Any → Dormant/Destroyed (free resources)
        Recovery,       // Internal recovery after wake
        None,           // No transition needed
    };

    // ---- Governance Recommendation ----
    // Pure output of evaluate(). ZERO side effects.
    // NEVER mutates orchestration state.
    // ONLY requestTransition() may execute transitions.
    struct GovernanceRecommendation {
        RenderId rendererId = 0;
        ActivityState desiredState = ActivityState::Active;
        ActivityState actualState = ActivityState::Active;
        TransitionClass transitionClass = TransitionClass::None;
        PolicyExplanation explanation;
        bool transitionNeeded = false;
        bool hysteresisBlocked = false;  // waiting for sustained recommendation
    };

    // Evaluate a renderer's governance state. PURE — no side effects.
    // Updates ONLY observable context and lastExplanation (read state).
    // Returns a recommendation. Caller (drainGovernance) decides whether
    // to call requestTransition().
    GovernanceRecommendation evaluate(RenderId id, VisibilityState visibility,
                                      float visibilityConfidence) {
        GovernanceRecommendation rec;
        rec.rendererId = id;

        auto it = states_.find(id);
        if (it == states_.end()) return rec;
        auto& orch = it->second;
        if (!orch.adapter) return rec;

        rec.actualState = orch.currentActivity;
        auto now = std::chrono::high_resolution_clock::now();

        // Phase 3B.3: Use simulated visibility when injected.
        // Governance reasons about claimed reality, not absolute reality.
        if (orch.hasSimulatedVisibility) {
            visibility = orch.simulatedVisibility;
            visibilityConfidence = orch.simulatedConfidence;
        }

        // ---- Build ObservableContext from current observations ----
        if (visibility != orch.lastVisibility) {
            orch.lastVisibility = visibility;
            orch.visibilityChangeTime = now;
        }
        double timeInStateSec = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - orch.visibilityChangeTime).count() / 1000.0;

        orch.context.visibility = visibility;
        orch.context.visibilityConfidence = visibilityConfidence;
        orch.context.timeInCurrentStateSec = timeInStateSec;
        orch.context.lastInteractionMs = static_cast<double>(std::chrono::duration_cast<
            std::chrono::milliseconds>(now - orch.lastInteractionTime).count());

        // ---- Get policy recommendation (with full explanation) ----
        auto caps = orch.adapter->capabilities();
        int recentTransitions = costTracker_.recentTransitionCount(id);
        double timeSinceLastTransition = costTracker_.timeSinceLastTransitionSec(id);

        auto expl = policy_.evaluate(
            orch.context, orch.profile, budget_, invariants_,
            caps, orch.currentActivity, id,
            recentTransitions, timeSinceLastTransition);

        orch.lastExplanation = expl;
        rec.explanation = expl;
        rec.desiredState = expl.recommended;

        // Same as current? No transition needed.
        if (expl.recommended == orch.currentActivity) {
            orch.lastRecommendation = expl.recommended;
            rec.transitionNeeded = false;
            rec.transitionClass = TransitionClass::None;
            return rec;
        }

        // Classify the transition
        rec.transitionClass = classifyTransition(orch.currentActivity, expl.recommended);

        // Hysteresis: sustained recommendation check (anti-oscillation).
        // Skip hysteresis for WakeCritical — user needs window NOW.
        if (rec.transitionClass != TransitionClass::WakeCritical) {
            if (expl.recommended != orch.lastRecommendation) {
                orch.lastRecommendation = expl.recommended;
                rec.transitionNeeded = false;
                rec.hysteresisBlocked = true;
                return rec;  // First time seeing this, wait.
            }
        }

        orch.lastRecommendation = expl.recommended;
        rec.transitionNeeded = true;
        return rec;
        // NOTE: No mutation happened. No adapter called. No state changed.
        // Caller must call requestTransition() if rec.transitionNeeded.
    }

    // Record an interaction event (input) on a renderer.
    void recordInteraction(RenderId id) {
        auto it = states_.find(id);
        if (it != states_.end()) {
            it->second.lastInteractionTime = std::chrono::high_resolution_clock::now();
            it->second.context.recentInteractionCount++;
        }
    }

    // Update system pressure (call periodically).
    void updatePressure() {
        pressureMonitor_.update();
    }

    // ---- Phase 3D: Adaptive parameter computation ----
    // Feeds current system state into AdaptivePolicy.
    // Returns bounded, deterministic parameters.
    // Call periodically (every governance tick is fine).
    AdaptiveParameters computeAdaptiveParams() {
        LARGE_INTEGER tStart, tEnd;
        QueryPerformanceCounter(&tStart);

        auto pressure = adaptivePolicy_.applySimulation(pressureMonitor_.pressure());

        LARGE_INTEGER tReportStart, tReportEnd;
        QueryPerformanceCounter(&tReportStart);
        auto report = recoveryReport();
        QueryPerformanceCounter(&tReportEnd);
        lastTimings_.recoveryReportUs = qpcElapsedUs(tReportStart, tReportEnd);

        auto result = adaptivePolicy_.compute(pressure, report.saturation, report);

        // Block 2: Dormancy audit
        if (adaptivePolicy_.wasCached()) lastTimings_.adaptCadenceSkips++;
        if (result.isDefault()) lastTimings_.adaptDefaultCount++;

        QueryPerformanceCounter(&tEnd);
        lastTimings_.adaptiveComputeUs = qpcElapsedUs(tStart, tEnd);
        return result;
    }

    // Get current adaptive parameters (no recompute).
    const AdaptiveParameters& adaptiveParams() const {
        return adaptivePolicy_.current();
    }

    // ---- Phase 3D Block 3: Wake stagger dispatch ----
    // Call on each governance tick. Dispatches pending wakes respecting
    // stagger intervals and priority ordering.
    //
    // REVALIDATION: Before executing any queued wake, verify the renderer
    // still wants to be Active. If visibility/budget/state changed since
    // the wake was queued, the wake is DISCARDED. This prevents stagger
    // queues from becoming "delayed lies."
    //
    // HARD CEILING: If the oldest queued wake has been waiting longer than
    // kMaxTotalWakeWindowMs (250ms), ALL remaining wakes are released
    // immediately. Adaptive governance must NEVER become sluggish.
    void dispatchPendingWakes() {
        if (pendingWakes_.empty()) {
            lastTimings_.staggerEmptyChecks++;
            return;
        }

        LARGE_INTEGER tDispStart;
        QueryPerformanceCounter(&tDispStart);

        auto now = std::chrono::high_resolution_clock::now();
        const auto& ap = adaptivePolicy_.current();

        // Sort by priority (lower = more urgent)
        std::sort(pendingWakes_.begin(), pendingWakes_.end(),
                  [](const PendingWake& a, const PendingWake& b) {
                      return a.priority < b.priority;
                  });

        // Check hard ceiling
        auto oldestAge = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - pendingWakes_.front().queuedAt).count();
        bool ceilingHit = oldestAge >= kMaxTotalWakeWindowMs;

        // Process queue
        std::vector<PendingWake> remaining;
        for (auto& pw : pendingWakes_) {
            // ---- Revalidation: is this wake still valid? ----
            auto it = states_.find(pw.id);
            if (it == states_.end()) continue;  // renderer gone — discard

            auto& orch = it->second;
            // If renderer is already Active, or no longer Parked/Dormant → discard
            if (orch.currentActivity == ActivityState::Active) continue;
            if (orch.currentActivity != ActivityState::Parked &&
                orch.currentActivity != ActivityState::Dormant) continue;

            // ---- Timing check ----
            if (!ceilingHit) {
                auto sinceLastDispatch = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - lastWakeDispatchedAt_).count();
                int requiredDelay = (ap.wakeStaggerMs * pw.priority) / 2;
                if (requiredDelay < 1) requiredDelay = 1;

                if (sinceLastDispatch < requiredDelay) {
                    remaining.push_back(pw);  // not time yet
                    continue;
                }
            }

            // ---- Execute deferred wake ----
            auto result = requestTransition(pw.id, ActivityState::Active,
                                            pw.source, true);  // force=true bypasses re-entry

            if (result.transitioned) {
                double delayMs = static_cast<double>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        now - pw.queuedAt).count());
                if (delayMs > maxWakeDelayMs_) maxWakeDelayMs_ = delayMs;
                totalWakeDelayMs_ += delayMs;
                completedStaggeredWakes_++;
                lastWakeDispatchedAt_ = now;

                OutputDebugStringA(("GOV_ADAPT: #" + std::to_string(pw.id) +
                    " STAGGER DISPATCHED delay=" + std::to_string(static_cast<int>(delayMs)) +
                    "ms priority=" + std::to_string(pw.priority) +
                    (ceilingHit ? " CEILING_HIT" : "") + "\n").c_str());
            }
        }
        pendingWakes_ = std::move(remaining);

        LARGE_INTEGER tDispEnd;
        QueryPerformanceCounter(&tDispEnd);
        lastTimings_.dispatchWakesUs = qpcElapsedUs(tDispStart, tDispEnd);
    }

    // Wake stagger telemetry accessors (for CSV export).
    int wakeQueueDepth() const { return static_cast<int>(pendingWakes_.size()); }
    double avgWakeDelayMs() const {
        return completedStaggeredWakes_ > 0
            ? totalWakeDelayMs_ / completedStaggeredWakes_ : 0.0;
    }
    double maxWakeDelayMs() const { return maxWakeDelayMs_; }
    int staggeredWakeCount() const { return staggeredWakeCount_; }
    int immediateWakeCount() const { return immediateWakeCount_; }

    // Phase 3E: Governance timings accessor.
    const GovernanceTimings& lastTimings() const { return lastTimings_; }
    GovernanceTimings& lastTimingsMut() { return lastTimings_; }

    // Phase 3E: Call from drainGovernance() to record total drain time
    // and update sampled evaluate stats.
    void recordDrainTiming(double totalDrainUs, int evalCount) {
        lastTimings_.totalDrainUs = totalDrainUs;
        lastTimings_.evaluateCount = evalCount;

        // Update sampled evaluate percentiles
        lastTimings_.sampledEvaluateUsP50 = evaluateSampler_.p50();
        lastTimings_.sampledEvaluateUsP95 = evaluateSampler_.p95();
        lastTimings_.sampledEvaluateUsMax = evaluateSampler_.maxVal();
        lastTimings_.evaluateSampleCount = evaluateSampler_.count();

        drainCycleCount_++;
    }

    // Phase 3E: Should this drain cycle sample per-renderer evaluate()?
    bool shouldSampleEvaluate() const {
        return (drainCycleCount_ % kEvaluateSampleInterval) == 0;
    }

    // Phase 3E: Record a sampled evaluate() timing.
    void recordEvaluateSample(double us) {
        evaluateSampler_.record(us);
    }

    // ============================================================
    //  Phase 3D: Transactional Wake Arbitration
    // ============================================================
    //
    // Problem: sequential requestTransition() calls mutate state
    // between evaluations. Wake #2 sees saturation=1 caused by
    // wake #1. This makes orchestration ORDER-DEPENDENT.
    //
    // Solution: Collect → Arbitrate → Commit.
    //
    //   Phase 1: collectWakeRequests() — gather candidates, NO mutation
    //   Phase 2: arbitrate (sort, stagger, concurrency) against STABLE snapshot
    //   Phase 3: commit transitions
    //
    // This ensures governance decisions are made against a consistent
    // view of system state, not incrementally contaminated.

    struct WakeCandidate {
        RenderId id = 0;
        int priority = 2;      // from PendingWake::priorityForProfile
        const char* source = "";
        bool immediate = false; // priority 0 → bypass all gates
    };

    // Phase 1: Collect all renderers that should wake.
    // PURE — no side effects, no state mutation.
    std::vector<WakeCandidate> collectWakeRequests(const char* source) const {
        std::vector<WakeCandidate> candidates;
        for (const auto& [id, orch] : states_) {
            if (orch.currentActivity == ActivityState::Parked ||
                orch.currentActivity == ActivityState::Dormant) {
                WakeCandidate wc;
                wc.id = id;
                wc.priority = PendingWake::priorityForProfile(orch.profile);
                wc.source = source;
                wc.immediate = (wc.priority == 0);
                candidates.push_back(wc);
            }
        }
        // Sort: lower priority number = more urgent = first
        std::sort(candidates.begin(), candidates.end(),
                  [](const WakeCandidate& a, const WakeCandidate& b) {
                      return a.priority < b.priority;
                  });
        return candidates;
    }

    // Phase 2+3: Transactional batch wake.
    // Arbitrates all candidates against a STABLE snapshot, then commits.
    //
    // Returns: number of wakes actually dispatched.
    //
    // Stagger semantics:
    //   - Priority 0 (MustRemainWarm): immediate, bypass all gates
    //   - Priority 1-3: subject to concurrency limit + stagger queue
    //   - Hard ceiling: if total stagger window > kMaxTotalWakeWindowMs,
    //     release all remaining immediately
    //
    // Concurrency semantics:
    //   - Snapshot concurrent recoveries ONCE before arbitration
    //   - Each dispatched wake increments the count for subsequent checks
    //   - Deferred wakes go into the stagger queue for dispatchPendingWakes()
    int batchWakeAll(const char* source) {
        // Reset per-batch counters — telemetry reflects THIS batch only
        staggeredWakeCount_ = 0;
        immediateWakeCount_ = 0;
        deferredWakeCount_ = 0;
        maxWakeDelayMs_ = 0.0;
        totalWakeDelayMs_ = 0.0;
        completedStaggeredWakes_ = 0;
        pendingWakes_.clear();

        // Phase 1: Collect
        auto candidates = collectWakeRequests(source);
        if (candidates.empty()) return 0;

        // Recompute adaptive params before arbitration
        computeAdaptiveParams();
        const auto& ap = adaptivePolicy_.current();

        // Phase 2: Arbitrate against stable snapshot
        int concurrentRecoveries = countConcurrentRecoveries();
        int dispatched = 0;
        auto now = std::chrono::high_resolution_clock::now();
        auto batchStartTime = now;

        for (auto& wc : candidates) {
            // ---- Priority 0: always immediate ----
            if (wc.immediate) {
                auto result = requestTransition(wc.id, ActivityState::Active,
                                                wc.source, true);
                if (result.transitioned) {
                    dispatched++;
                    concurrentRecoveries++;
                    protectedWakeCount_++;
                    immediateWakeCount_++;
                    lastWakeDispatchedAt_ = std::chrono::high_resolution_clock::now();
                }
                continue;
            }

            // ---- Non-default adaptive params: apply gates ----
            if (!ap.isDefault()) {
                // Concurrency gate
                if (concurrentRecoveries >= ap.maxConcurrentWakes) {
                    // Queue for deferred dispatch
                    queueWake(wc.id, wc.source, wc.priority, now);
                    deferredWakeCount_++;
                    staggeredWakeCount_++;

                    OutputDebugStringA(("GOV_BATCH: #" + std::to_string(wc.id) +
                        " DEFERRED conc=" + std::to_string(concurrentRecoveries) +
                        " limit=" + std::to_string(ap.maxConcurrentWakes) +
                        " prio=" + std::to_string(wc.priority) + "\n").c_str());
                    continue;
                }

                // Stagger gate: if stagger enabled and we've already dispatched
                // a non-immediate wake, queue the rest
                if (ap.wakeStaggerMs > 0 && dispatched > 0) {
                    // Check hard ceiling
                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::high_resolution_clock::now() - batchStartTime).count();
                    if (elapsed < kMaxTotalWakeWindowMs) {
                        queueWake(wc.id, wc.source, wc.priority, now);
                        staggeredWakeCount_++;

                        OutputDebugStringA(("GOV_BATCH: #" + std::to_string(wc.id) +
                            " STAGGERED prio=" + std::to_string(wc.priority) +
                            " staggerMs=" + std::to_string(ap.wakeStaggerMs) + "\n").c_str());
                        continue;
                    }
                    // Ceiling hit — dispatch immediately
                }
            }

            // ---- Execute wake ----
            auto result = requestTransition(wc.id, ActivityState::Active,
                                            wc.source, true);
            if (result.transitioned) {
                dispatched++;
                concurrentRecoveries++;
                lastWakeDispatchedAt_ = std::chrono::high_resolution_clock::now();
            }
        }

        OutputDebugStringA(("GOV_BATCH: completed dispatched=" + std::to_string(dispatched) +
            " queued=" + std::to_string(static_cast<int>(pendingWakes_.size())) +
            " total_candidates=" + std::to_string(static_cast<int>(candidates.size())) +
            " adaptLevel=" + std::string(ap.levelName()) + "\n").c_str());

        return dispatched;
    }

    // ---- Phase 3D: Simulation hooks for deterministic testing ----
    void simulateBattery(bool onBattery, float batteryPercent) {
        adaptivePolicy_.simulateBattery(onBattery, batteryPercent);
    }
    void clearSimulatedBattery() {
        adaptivePolicy_.clearSimulatedBattery();
    }
    // Force adaptive recompute on next call (for testing).
    void invalidateAdaptiveCache() {
        adaptivePolicy_.invalidateCache();
    }

    // ---- Phase 3C: Frame notification for recovery tracking ----
    // Called when a renderer produces a frame (from FrameCadenceMonitor or similar).
    // Feeds the per-renderer RecoveryTracker state machine.
    void onRendererFrame(RenderId id) {
        auto it = states_.find(id);
        if (it == states_.end()) return;
        it->second.recovery.onFrame();
    }

    // ---- Phase 3C: Aggregate recovery experience report ----
    RecoveryExperienceReport recoveryReport() const {
        RecoveryExperienceReport r;
        r.totalRenderers = static_cast<int>(states_.size());

        // Aggregate distribution across all renderers
        std::vector<double> allColdSamples;
        std::vector<double> allTotalSamples;

        for (const auto& [id, orch] : states_) {
            const auto& t = orch.recovery;

            // Phase tracking
            if (t.phase == RecoveryPhase::RecoveringCold ||
                t.phase == RecoveryPhase::RecoveringWarm) {
                r.recoveringCount++;
            }
            if (t.phase == RecoveryPhase::StaleVisible) {
                r.staleCount++;
            }
            if (t.continuityBreaks > 0) {
                r.continuityBrokenCount++;
            }

            // Worst-case metrics
            if (t.coldRecoveryMs > r.worstColdRecoveryMs) r.worstColdRecoveryMs = t.coldRecoveryMs;
            if (t.warmRecoveryMs > r.worstWarmRecoveryMs) r.worstWarmRecoveryMs = t.warmRecoveryMs;
            if (t.totalRecoveryMs > r.worstTotalRecoveryMs) r.worstTotalRecoveryMs = t.totalRecoveryMs;
            if (t.staleExposureMs > r.worstStaleExposureMs) r.worstStaleExposureMs = t.staleExposureMs;
            if (t.pacingVarianceMs > r.worstPacingVarianceMs) r.worstPacingVarianceMs = t.pacingVarianceMs;
            if (t.maxFrameGapMs > r.worstMaxFrameGapMs) r.worstMaxFrameGapMs = t.maxFrameGapMs;
            if (t.clampedContinuityScore() < r.lowestContinuityScore) r.lowestContinuityScore = t.clampedContinuityScore();
            r.totalContinuityBreaks += t.continuityBreaks;
            r.totalBurstFrames += t.burstFrames;

            // Priority 1: Split panic semantics
            if (t.recoveryBurstObserved) r.anyBurstObserved = true;
            r.totalBurstEpisodes += t.burstEpisodes;
            if (t.experientialInstability) {
                r.anyExperientialInstability = true;
                r.worstInstabilityReason = t.instabilityReason;
            }

            // Priority 4: Visual silence
            r.totalVisualSilenceMs += t.visualSilenceMs;
            r.totalVisualSilenceEvents += t.visualSilenceEvents;

            // Classification distribution
            switch (t.lastClassification) {
                case RecoveryClass::Instant:    r.instantCount++; break;
                case RecoveryClass::Smooth:     r.smoothCount++; break;
                case RecoveryClass::Hitchy:     r.hitchyCount++; break;
                case RecoveryClass::Disruptive: r.disruptiveCount++; break;
                case RecoveryClass::Unknown:    r.unknownCount++; break;
            }

            // Best/worst ever
            if (t.bestRecoveryMs >= 0.0) {
                if (r.bestEverRecoveryMs < 0.0 || t.bestRecoveryMs < r.bestEverRecoveryMs) {
                    r.bestEverRecoveryMs = t.bestRecoveryMs;
                }
            }
            if (t.worstRecoveryMs >= 0.0) {
                if (r.worstEverRecoveryMs < 0.0 || t.worstRecoveryMs > r.worstEverRecoveryMs) {
                    r.worstEverRecoveryMs = t.worstRecoveryMs;
                }
            }

            // Priority 2: Collect distribution samples
            for (double s : t.distribution.coldSamples) allColdSamples.push_back(s);
            for (double s : t.distribution.totalSamples) allTotalSamples.push_back(s);

            // Pre-Fix 3: Aggregate wake ordering from per-renderer backpressure
            r.protectedWakeCount += orch.budgetProtections;
        }

        // Priority 2: Compute aggregate distribution stats
        if (!allColdSamples.empty()) {
            r.coldP50 = RecoveryDistribution::percentile(allColdSamples, 0.50);
            r.coldP95 = RecoveryDistribution::percentile(allColdSamples, 0.95);
            r.coldVariance = RecoveryDistribution::variance(allColdSamples);
            r.coldJitter = RecoveryDistribution::jitter(allColdSamples);
        }
        if (!allTotalSamples.empty()) {
            r.totalP50 = RecoveryDistribution::percentile(allTotalSamples, 0.50);
            r.totalP95 = RecoveryDistribution::percentile(allTotalSamples, 0.95);
            r.totalVariance = RecoveryDistribution::variance(allTotalSamples);
            r.totalJitter = RecoveryDistribution::jitter(allTotalSamples);
        }
        r.distributionSamples = static_cast<int>(allTotalSamples.size());

        // Pre-Fix 2: Compute recovery saturation
        r.saturation.totalEngines = r.totalRenderers;
        r.saturation.concurrentRecoveries = r.recoveringCount;
        if (r.recoveringCount > peakRecoveryConcurrency_) {
            peakRecoveryConcurrency_ = r.recoveringCount;
        }
        r.saturation.peakConcurrency = peakRecoveryConcurrency_;
        r.saturation.pendingWakes = deferredWakeCount_;

        // Pre-Fix 3: Wake ordering telemetry
        r.deferredWakeCount = deferredWakeCount_;
        r.protectedWakeCount = protectedWakeCount_;

        r.classifyDominantIssue();
        return r;
    }

    // ---- Accessors for metrics/debugging ----

    const RendererOrchState* orchState(RenderId id) const {
        auto it = states_.find(id);
        return (it != states_.end()) ? &it->second : nullptr;
    }

    const RuntimePolicy& policy() const { return policy_; }
    const ResidencyBudget& budget() const { return budget_; }
    ResidencyBudget& budget() { return budget_; }
    const TransitionCostTracker& costTracker() const { return costTracker_; }
    const SystemPressureMonitor& pressureMonitor() const { return pressureMonitor_; }
    const OrchestrationInvariants& invariants() const { return invariants_; }

    // Get governance summary for telemetry/CSV.
    struct GovernanceSummary {
        int totalRenderers = 0;
        int activeCount = 0;
        int parkedCount = 0;
        float budgetUtilization = 0.0f;
        const char* pressureLevel = "?";
        int totalTransitions = 0;
        int invariantOverrides = 0;
        int budgetDrivenCount = 0;
        bool churnDetected = false;
        std::string dominantReasons;

        // Phase 3B.3: Enriched governance semantics telemetry
        int visibilityDrivenCount = 0;
        int workloadDrivenCount = 0;
        int budgetDenialCount = 0;
        int suppressionBlockCount = 0;
        int desiredActualMismatch = 0;
        bool budgetOverBudget = false;

        // v3: Economic governance telemetry
        int budgetEscalatedCount = 0;    // renderers where budget escalated warm→parked
        int budgetProtectedCount = 0;    // MustRemainWarm renderers that survived budget
        double maxMismatchDurationMs = 0.0;
        double maxBudgetMismatchMs = 0.0;      // max mismatch caused by budget
        double maxSuppressionMismatchMs = 0.0;  // max mismatch caused by invariant
        const char* budgetPressureLevel = "Relaxed";
    };

    GovernanceSummary governanceSummary() {
        GovernanceSummary s;
        s.totalRenderers = static_cast<int>(states_.size());
        auto now = std::chrono::high_resolution_clock::now();
        std::string reasons;
        for (auto& [id, orch] : states_) {
            if (orch.currentActivity == ActivityState::Active) s.activeCount++;
            if (orch.currentActivity == ActivityState::Parked) s.parkedCount++;
            if (orch.lastExplanation.dominatedByInvariant) s.invariantOverrides++;
            if (orch.lastExplanation.budgetDriven) s.budgetDrivenCount++;
            if (orch.lastExplanation.visibilityDriven) s.visibilityDrivenCount++;
            if (orch.lastExplanation.workloadDriven) s.workloadDrivenCount++;

            // v3: Economic authority tracking
            if (orch.lastExplanation.budgetEscalated) {
                s.budgetEscalatedCount++;
                orch.budgetDenials++;  // structural: increment denial on escalation
            }
            if (orch.lastExplanation.budgetProtected) {
                s.budgetProtectedCount++;
                orch.budgetProtections++;
            }

            // Aggregate backpressure
            s.budgetDenialCount += orch.budgetDenials;
            s.suppressionBlockCount += orch.suppressionBlocks;

            // Desired vs actual mismatch + split aging
            bool currentMismatch = (orch.lastExplanation.recommended != orch.currentActivity);
            if (currentMismatch) {
                s.desiredActualMismatch++;
                if (!orch.inMismatch) {
                    orch.inMismatch = true;
                    orch.mismatchStartTime = now;
                    orch.mismatchIsBudget = orch.lastExplanation.budgetDriven;
                    orch.mismatchIsSuppression = orch.lastExplanation.dominatedByInvariant;
                }
                double mismatchMs = static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - orch.mismatchStartTime).count());
                if (mismatchMs > s.maxMismatchDurationMs) {
                    s.maxMismatchDurationMs = mismatchMs;
                }
                // Split into budget vs suppression domains
                if (orch.mismatchIsBudget && mismatchMs > s.maxBudgetMismatchMs) {
                    s.maxBudgetMismatchMs = mismatchMs;
                }
                if (orch.mismatchIsSuppression && mismatchMs > s.maxSuppressionMismatchMs) {
                    s.maxSuppressionMismatchMs = mismatchMs;
                }
            } else {
                orch.inMismatch = false;
                orch.mismatchIsBudget = false;
                orch.mismatchIsSuppression = false;
            }

            // Collect dominant reason
            if (orch.lastExplanation.dominantReason[0] != '\0') {
                if (!reasons.empty()) reasons += '|';
                reasons += orch.lastExplanation.dominantReason;
            }
            // Check for churn
            int recent = costTracker_.recentTransitionCount(id);
            if (recent >= 3) s.churnDetected = true;
            s.totalTransitions += recent;
        }
        s.budgetUtilization = budget_.utilization();
        s.budgetOverBudget = budget_.isOverBudget();
        s.pressureLevel = toString(pressureMonitor_.pressure().overallPressure);
        s.dominantReasons = reasons;

        // Budget pressure level
        auto bp = budget_.pressure();
        switch (bp) {
            case ResidencyBudget::BudgetPressure::Relaxed:     s.budgetPressureLevel = "Relaxed"; break;
            case ResidencyBudget::BudgetPressure::Elevated:    s.budgetPressureLevel = "Elevated"; break;
            case ResidencyBudget::BudgetPressure::Constrained: s.budgetPressureLevel = "Constrained"; break;
            case ResidencyBudget::BudgetPressure::Critical:    s.budgetPressureLevel = "Critical"; break;
        }

        return s;
    }

private:
    RuntimePolicy policy_;
    OrchestrationInvariants invariants_;
    ResidencyBudget budget_;
    TransitionCostTracker costTracker_;
    SystemPressureMonitor pressureMonitor_;
    std::unordered_map<RenderId, RendererOrchState> states_;
    int forceOverrideCount_ = 0;  // governance debt tracking

    // Phase 3D: Adaptive parameter modulation
    AdaptivePolicy adaptivePolicy_;

    // Pre-Fix 2: Recovery saturation tracking
    mutable int peakRecoveryConcurrency_ = 0;  // mutable: updated in const recoveryReport()

    // Pre-Fix 3: Wake ordering counters
    int deferredWakeCount_ = 0;   // wakes delayed by concurrency/budget
    int protectedWakeCount_ = 0;  // MustRemainWarm survived budget pressure

    // Phase 3D Block 3: Wake stagger queue
    std::vector<PendingWake> pendingWakes_;
    std::chrono::high_resolution_clock::time_point lastWakeDispatchedAt_;
    int staggeredWakeCount_ = 0;   // wakes that went through stagger queue
    int immediateWakeCount_ = 0;   // wakes that bypassed stagger (priority 0)
    double maxWakeDelayMs_ = 0.0;  // worst-case wake delay observed
    double totalWakeDelayMs_ = 0.0; // sum of all wake delays (for avg)
    int completedStaggeredWakes_ = 0; // completed dispatches (for avg calc)

    // Hard ceiling: maximum total wake window before releasing all.
    // Prevents adaptive governance from becoming "stable but sluggish."
    static constexpr int64_t kMaxTotalWakeWindowMs = 250;

    // Phase 3E: Governance overhead profiling
    GovernanceTimings lastTimings_;
    TimingSampler evaluateSampler_;     // per-renderer evaluate() samples
    int drainCycleCount_ = 0;          // drain cycle counter for sampling cadence
    static constexpr int kEvaluateSampleInterval = 8;  // sample every 8th drain

    // Count concurrent recoveries (shared by concurrency gate + telemetry).
    int countConcurrentRecoveries() const {
        int count = 0;
        for (const auto& [rid, rorch] : states_) {
            auto rphase = rorch.recovery.phase;
            if (rphase == RecoveryPhase::RecoveringCold ||
                rphase == RecoveryPhase::RecoveringWarm) {
                count++;
            }
        }
        return count;
    }

    // Queue a wake with coalescing: if renderer already queued, update priority.
    void queueWake(RenderId id, const char* source, int priority,
                   std::chrono::high_resolution_clock::time_point now) {
        // Coalescing: check if already queued
        for (auto& pw : pendingWakes_) {
            if (pw.id == id) {
                // Update priority (take lower = more urgent) and timestamp
                if (priority < pw.priority) pw.priority = priority;
                pw.queuedAt = now;
                pw.source = source;
                return;  // no duplicate
            }
        }
        pendingWakes_.push_back({ id, source, now, priority });
        lastTimings_.anyQueueAllocation = true;  // Block 2: allocation observed
    }

    // Classify a transition for asymmetric governance semantics.
    static TransitionClass classifyTransition(ActivityState from, ActivityState to) {
        if (to == ActivityState::Active) {
            // Any → Active is wake-critical (user needs window)
            return TransitionClass::WakeCritical;
        }
        if (to == ActivityState::Dormant) {
            // Any → Dormant is reclamation
            return TransitionClass::Reclamation;
        }
        if (from == ActivityState::Active &&
            (to == ActivityState::Throttled || to == ActivityState::Parked)) {
            // Active → Throttled/Parked is optimization
            return TransitionClass::Optimization;
        }
        return TransitionClass::Optimization;  // default: conservative
    }
};

}  // namespace morphic

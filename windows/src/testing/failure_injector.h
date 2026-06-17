#pragma once

#include <windows.h>
#include <string>
#include <atomic>

namespace morphic {

// Phase 1B Track 9 — Failure injection.
// Simulates failure conditions to validate error handling paths.
//
// Phase 5D: Extended with semantic fault injection for continuity validation.
class FailureInjector {
public:
    // --- Original: Structural faults ---

    void failNextHwndCreation() { failHwndCreation_ = true; }
    void failNextDeferWindowPos() { failDeferWindowPos_ = true; }
    void stallScheduler(int ms) { schedulerStallMs_ = ms; }
    void corruptNextTransform() { corruptTransform_ = true; }

    bool shouldFailHwndCreation() {
        if (failHwndCreation_) { failHwndCreation_ = false; return true; }
        return false;
    }

    bool shouldFailDeferWindowPos() {
        if (failDeferWindowPos_) { failDeferWindowPos_ = false; return true; }
        return false;
    }

    int getSchedulerStall() {
        int stall = schedulerStallMs_;
        schedulerStallMs_ = 0;
        return stall;
    }

    bool shouldCorruptTransform() {
        if (corruptTransform_) { corruptTransform_ = false; return true; }
        return false;
    }

    // --- Phase 5D: Semantic fault injection ---

    // Simulates ActivationResult::DeniedByOS on next activation attempt
    void failNextActivation() { failActivation_ = true; }
    bool shouldFailActivation() {
        if (failActivation_) { failActivation_ = false; return true; }
        return false;
    }

    // Simulates ActivationResult::Deferred (delayed, not denied)
    // The activation will eventually succeed, but is delayed by delayMs
    void delayNextActivation(int delayMs) { activationDelayMs_ = delayMs; }
    int getActivationDelay() {
        int delay = activationDelayMs_;
        activationDelayMs_ = 0;
        return delay;
    }

    // Marks that the next surface should be destroyed AFTER semantic commit
    // but BEFORE Win32 realization — the most dangerous timing window
    void destroyAfterCommitBeforeRealization() { destroyPostCommit_ = true; }
    bool shouldDestroyPostCommit() {
        if (destroyPostCommit_) { destroyPostCommit_ = false; return true; }
        return false;
    }

    // Fires N consecutive OSDeniedActivation divergences
    void injectDivergenceStorm(int count) { divergenceStormCount_ = count; }
    int getDivergenceStormCount() {
        int c = divergenceStormCount_;
        divergenceStormCount_ = 0;
        return c;
    }

    // Forces a specific workspace into ContinuityState::Fractured
    void fractureWorkspaceContinuity(uint64_t workspaceKey) {
        fracturedWorkspace_ = workspaceKey;
    }
    uint64_t getFracturedWorkspace() {
        uint64_t ws = fracturedWorkspace_;
        fracturedWorkspace_ = 0;
        return ws;
    }

    // Simulates checkpoint restore against a surface whose generation has advanced
    void restoreAgainstStaleLineage() { staleLineageRestore_ = true; }
    bool shouldRestoreStaleLineage() {
        if (staleLineageRestore_) { staleLineageRestore_ = false; return true; }
        return false;
    }

    // --- Reset all injections ---
    void reset() {
        failHwndCreation_ = false;
        failDeferWindowPos_ = false;
        schedulerStallMs_ = 0;
        corruptTransform_ = false;
        failActivation_ = false;
        activationDelayMs_ = 0;
        destroyPostCommit_ = false;
        divergenceStormCount_ = 0;
        fracturedWorkspace_ = 0;
        staleLineageRestore_ = false;
    }

    bool isInjecting() const {
        return failHwndCreation_ || failDeferWindowPos_ ||
               schedulerStallMs_ > 0 || corruptTransform_ ||
               failActivation_ || activationDelayMs_ > 0 ||
               destroyPostCommit_ || divergenceStormCount_ > 0 ||
               fracturedWorkspace_ != 0 || staleLineageRestore_;
    }

private:
    // Structural
    bool failHwndCreation_ = false;
    bool failDeferWindowPos_ = false;
    int schedulerStallMs_ = 0;
    bool corruptTransform_ = false;

    // Phase 5D: Semantic
    bool failActivation_ = false;
    int activationDelayMs_ = 0;
    bool destroyPostCommit_ = false;
    int divergenceStormCount_ = 0;
    uint64_t fracturedWorkspace_ = 0;
    bool staleLineageRestore_ = false;
};

}  // namespace morphic

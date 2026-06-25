#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <windows.h>
#include <fstream>

#include "../core/runtime_commit_scheduler.h"

namespace morphic {

// Runtime Bootstrap Sequence.
//
// Explicit 9-phase startup order with failure classification.
enum class BootstrapPhase {
    Uninitialized = 0,
    ConstitutionLoad,         // 1. Structural invariants, version checks, panic structures
    RuntimeInit,              // 2. Facade construction & memory allocation
    SchedulerInit,            // 3. Transaction queue & scheduler ready
    TraceInit,                // 4. Trace ring buffers & persistent logs
    SurfaceRegistryInit,      // 5. Host window binding & surface registry setup
    RealizationBridgeInit,    // 6. Win32SurfaceRealizer creation & bridging
    WorkspaceRestore,         // 7. Workspace & layout session recovery
    ActivationUnlock,         // 8. Focus & activation managers unlocked
    OperationalStart          // 9. Frame timers & steady-state execution started
};

inline const char* toString(BootstrapPhase phase) {
    switch (phase) {
        case BootstrapPhase::Uninitialized:         return "Uninitialized";
        case BootstrapPhase::ConstitutionLoad:      return "ConstitutionLoad";
        case BootstrapPhase::RuntimeInit:           return "RuntimeInit";
        case BootstrapPhase::SchedulerInit:         return "SchedulerInit";
        case BootstrapPhase::TraceInit:             return "TraceInit";
        case BootstrapPhase::SurfaceRegistryInit:   return "SurfaceRegistryInit";
        case BootstrapPhase::RealizationBridgeInit: return "RealizationBridgeInit";
        case BootstrapPhase::WorkspaceRestore:      return "WorkspaceRestore";
        case BootstrapPhase::ActivationUnlock:      return "ActivationUnlock";
        case BootstrapPhase::OperationalStart:      return "OperationalStart";
    }
    return "Unknown";
}

enum class FailurePolicy {
    Fatal,
    RecoverableDegraded
};

inline FailurePolicy getFailurePolicy(BootstrapPhase phase) {
    switch (phase) {
        case BootstrapPhase::ConstitutionLoad:
        case BootstrapPhase::RuntimeInit:
        case BootstrapPhase::SchedulerInit:
        case BootstrapPhase::SurfaceRegistryInit:
        case BootstrapPhase::RealizationBridgeInit:
        case BootstrapPhase::OperationalStart:
            return FailurePolicy::Fatal;
        case BootstrapPhase::TraceInit:
        case BootstrapPhase::WorkspaceRestore:
        case BootstrapPhase::ActivationUnlock:
            return FailurePolicy::RecoverableDegraded;
        default:
            return FailurePolicy::Fatal;
    }
}

struct BootstrapPhaseSnapshot {
    BootstrapPhase phase;
    uint64_t enterTimeUs;
    uint64_t exitTimeUs;
    uint64_t durationUs;
    bool failed;
    std::string errorMessage;
    bool degradedFallback;
    bool skippedRecovery;
};

class RuntimeBootstrap {
public:
    RuntimeBootstrap() = default;

    BootstrapPhase currentPhase() const { return phase_; }

    const std::vector<BootstrapPhaseSnapshot>& snapshots() const { return snapshots_; }

    void appendLog(const std::string& line) {
        std::ofstream file("morphic_bootstrap.log", std::ios::app);
        if (file.is_open()) {
            file << line << "\n";
        }
    }

    bool advanceTo(BootstrapPhase target, bool success = true, const std::string& errorMsg = "", bool skippedRec = false) {
        int current = static_cast<int>(phase_);
        int targetInt = static_cast<int>(target);

        // Enforce strictly sequential transitions
        if (targetInt != current + 1) {
            std::string msg = "BOOTSTRAP: Cannot advance from " +
                std::string(toString(phase_)) + " to " +
                std::string(toString(target)) + " — must be sequential";
            OutputDebugStringA((msg + "\n").c_str());
            appendLog("BOOTSTRAP ERROR: " + msg);
            
            // If the requested phase is Fatal, we abort immediately on sequential validation failure
            if (getFailurePolicy(target) == FailurePolicy::Fatal) {
                ::morphic::KernelPanicCoordinator::panic(msg, __FILE__, __LINE__);
            }
            return false;
        }

        uint64_t now = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()
        ).count();

        // If we are exiting a phase, finalize the snapshot of that phase
        if (phase_ != BootstrapPhase::Uninitialized && !snapshots_.empty()) {
            auto& last = snapshots_.back();
            last.exitTimeUs = now;
            last.durationUs = last.exitTimeUs - last.enterTimeUs;
            
            std::string status = last.failed ? (last.degradedFallback ? "DEGRADED" : "FAILED") : "SUCCESS";
            std::string exitMsg = "BOOTSTRAP PHASE EXIT: " + std::string(toString(last.phase)) +
                                  " | Enter: " + std::to_string(last.enterTimeUs) + " us" +
                                  " | Exit: " + std::to_string(last.exitTimeUs) + " us" +
                                  " | Duration: " + std::to_string(last.durationUs) + " us" +
                                  " | Status: " + status +
                                  " | Error: " + (last.errorMessage.empty() ? "None" : last.errorMessage) +
                                  " | SkippedRecovery: " + (last.skippedRecovery ? "True" : "False");
            appendLog(exitMsg);
        }

        // Record the new phase snapshot
        BootstrapPhaseSnapshot snapshot{};
        snapshot.phase = target;
        snapshot.enterTimeUs = now;
        snapshot.failed = !success;
        snapshot.errorMessage = errorMsg;
        snapshot.degradedFallback = !success && (getFailurePolicy(target) == FailurePolicy::RecoverableDegraded);
        snapshot.skippedRecovery = skippedRec;

        appendLog("BOOTSTRAP PHASE ENTER: " + std::string(toString(target)) + " at " + std::to_string(now) + " us");

        FailurePolicy policy = getFailurePolicy(target);
        if (!success) {
            if (policy == FailurePolicy::Fatal) {
                std::string msg = "BOOTSTRAP FATAL FAILURE in phase " + std::string(toString(target)) + ": " + errorMsg;
                OutputDebugStringA((msg + "\n").c_str());
                appendLog("BOOTSTRAP FATAL FAILURE: " + msg);
                ::morphic::KernelPanicCoordinator::panic(msg, __FILE__, __LINE__);
            } else {
                std::string msg = "BOOTSTRAP RECOVERABLE DEGRADED FAILURE in phase " + std::string(toString(target)) + ": " + errorMsg;
                OutputDebugStringA((msg + "\n").c_str());
                appendLog("BOOTSTRAP RECOVERABLE DEGRADED FAILURE: " + msg);
            }
        }

        phase_ = target;
        snapshots_.push_back(snapshot);

        OutputDebugStringA(("BOOTSTRAP: Advanced to " +
            std::string(toString(phase_)) + "\n").c_str());
        
        if (target == BootstrapPhase::OperationalStart) {
            appendLog("BOOTSTRAP SEQUENCE COMPLETED SUCCESSFULLY at " + std::to_string(now) + " us");
        }
        return true;
    }

    bool isReady() const { return phase_ == BootstrapPhase::OperationalStart; }
    
    bool isAtLeast(BootstrapPhase min) const {
        return static_cast<int>(phase_) >= static_cast<int>(min);
    }

private:
    BootstrapPhase phase_ = BootstrapPhase::Uninitialized;
    std::vector<BootstrapPhaseSnapshot> snapshots_;
};

} // namespace morphic

#include "runtime_commit_scheduler.h"
#include "runtime_surface_realizer.h"
#include "kernel_trace.h"
#include "../composition/compositor.h"
#include <cassert>
#include <windows.h>
#include <dwmapi.h>
#include <fstream>
#include <iostream>
#include <cmath>

#pragma comment(lib, "dwmapi.lib")

namespace morphic {

std::mutex KernelPanicCoordinator::mutex_;
void* KernelPanicCoordinator::scheduler_ = nullptr;

void KernelPanicCoordinator::registerScheduler(void* scheduler) {
    std::lock_guard<std::mutex> lock(mutex_);
    scheduler_ = scheduler;
}

void KernelPanicCoordinator::panic(const std::string& reason, const std::string& file, int line) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (scheduler_) {
        auto* s = static_cast<RuntimeCommitScheduler*>(scheduler_);
        s->freezeSchedulerEpoch();
        if (s->traceRecorder()) {
            s->traceRecorder()->freezeTraceCapture();
            s->traceRecorder()->flushSnapshot("morphic_panic.crashdump");
        }
        std::ofstream log("morphic_panic.log");
        if (log.is_open()) {
            log << "Reason: " << reason << "\n"
                << "File: " << file << ":" << line << "\n";
        }
    }
    std::cerr << "MORPHIC FATAL KERNEL VIOLATION: " << reason << " at " << file << ":" << line << std::endl;
    std::abort();
}

void KernelPanicCoordinator::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    scheduler_ = nullptr;
}

RuntimeCommitScheduler::RuntimeCommitScheduler(RuntimeSceneState& sceneState, std::shared_ptr<RuntimeSurfaceRealizer> realizer, Compositor* compositor)
    : sceneState_(sceneState), realizer_(realizer), compositor_(compositor) {
    traceRecorder_ = std::make_shared<KernelTrace>();
    KernelPanicCoordinator::registerScheduler(this);
}

void RuntimeCommitScheduler::enqueueIntent(const RuntimeMutationIntent& intent) {
    if (traceRecorder_ && traceRecorder_->isReplayMode()) {
        MORPHIC_FATAL_KERNEL_VIOLATION("REPLAY PASSIVE BOUNDARY VIOLATION: Cannot enqueue mutation intent during replay");
        return;
    }
    // Phase assertions: Mutating semantic state outside ApplyingSemanticState is forbidden.
    // Subsystems can enqueue intents at any time, but mutations themselves are scheduler-bound.
    std::lock_guard<std::mutex> lock(queueMutex_);
    queue_.enqueue(intent);
    metrics_.totalIntentsEnqueued++;
}

void RuntimeCommitScheduler::invalidateSurface(NodeId surfaceId) {
    if (traceRecorder_ && traceRecorder_->isReplayMode()) {
        MORPHIC_FATAL_KERNEL_VIOLATION("REPLAY PASSIVE BOUNDARY VIOLATION: Cannot invalidate surface during replay");
        return;
    }
    std::lock_guard<std::mutex> lock(queueMutex_);
    queue_.invalidateSurfaceIntents(surfaceId);
    eventLog_.log(FailureEvent::Type::Quarantine, surfaceId, "Invalidated surface intents in queue");
}

void RuntimeCommitScheduler::invalidateWorkspace(WorkspaceId workspaceId) {
    if (traceRecorder_ && traceRecorder_->isReplayMode()) {
        MORPHIC_FATAL_KERNEL_VIOLATION("REPLAY PASSIVE BOUNDARY VIOLATION: Cannot invalidate workspace during replay");
        return;
    }
    std::lock_guard<std::mutex> lock(queueMutex_);
    queue_.invalidateWorkspaceIntents(workspaceId);
    eventLog_.log(FailureEvent::Type::Rollback, kInvalidNodeId, "Invalidated workspace intents in queue: ws=" + std::to_string(workspaceId.value));
}

void RuntimeCommitScheduler::tick() {
    executeCommitCycle();
}

void RuntimeCommitScheduler::executeCommitCycle() {
    auto startTime = std::chrono::high_resolution_clock::now();
    double startTimeMs = std::chrono::duration<double, std::milli>(startTime.time_since_epoch()).count();
    
    // Track Frame/Commit Jitter
    if (lastCommitTimestampMs_ > 0.0) {
        double elapsed = startTimeMs - lastCommitTimestampMs_;
        if (elapsed < 2.0) {
            metrics_.frameJitterMs = 0.0;
        } else {
            metrics_.frameJitterMs = std::abs(elapsed - (1000.0 / targetFPS_));
        }
    }
    lastCommitTimestampMs_ = startTimeMs;

    // Track GUI resources on Windows
    metrics_.userHandlesCount = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
    metrics_.gdiHandlesCount = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);

    // DWM Throttling Observability
    HWND mainHwnd = nullptr;
    if (compositor_) {
        mainHwnd = compositor_->window();
    }
    
    metrics_.isOcclusionThrottled = false;
    metrics_.isInvisibleWindowThrottled = false;
    metrics_.observedMonitorRefreshRate = 60;
    metrics_.framePacingDriftMs = 0.0;

    if (compositor_ && compositor_->displayManager().isSimulated()) {
        auto* primaryDisp = compositor_->displayManager().getPrimaryDisplay();
        if (primaryDisp) {
            metrics_.observedMonitorRefreshRate = primaryDisp->refreshRate;
        }
    }

    if (mainHwnd) {
        if (IsIconic(mainHwnd)) {
            metrics_.isInvisibleWindowThrottled = true;
        }
        
        BOOL cloaked = FALSE;
        HRESULT hr = DwmGetWindowAttribute(mainHwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
        if (SUCCEEDED(hr) && cloaked) {
            metrics_.isOcclusionThrottled = true;
        }

        DWM_TIMING_INFO timingInfo = {0};
        timingInfo.cbSize = sizeof(DWM_TIMING_INFO);
        hr = DwmGetCompositionTimingInfo(mainHwnd, &timingInfo);
        if (SUCCEEDED(hr)) {
            if (timingInfo.rateRefresh.uiDenominator > 0) {
                metrics_.observedMonitorRefreshRate = static_cast<int>(
                    std::round(static_cast<double>(timingInfo.rateRefresh.uiNumerator) / 
                               static_cast<double>(timingInfo.rateRefresh.uiDenominator))
                );
            }
        }
    }

    if (metrics_.frameJitterMs > 0.0) {
        metrics_.framePacingDriftMs = metrics_.frameJitterMs;
    }

    // Replay Mode injection
    if (traceRecorder_ && traceRecorder_->isReplayMode()) {
        const auto& buffer = traceRecorder_->getRingBuffer();
        if (replayFrameIndex_ < buffer.size()) {
            const auto& frame = buffer[replayFrameIndex_];
            
            // Clear queue and enqueue replay frame intents
            std::lock_guard<std::mutex> lock(queueMutex_);
            queue_.clear();
            for (const auto& intent : frame.intentsProcessed) {
                queue_.enqueue(intent);
            }
        }
    }

    collectStage();
    arbitrateStage();
    
    // Pull batch of intents bounding to budget
    activeBatch_.clear();
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        auto& intents = queue_.intents();
        metrics_.queueDepthBeforeCommit = intents.size();
        
        size_t opsProcessed = 0;
        for (auto it = intents.begin(); it != intents.end(); ) {
            auto now = std::chrono::high_resolution_clock::now();
            double elapsedMs = std::chrono::duration<double, std::milli>(now - startTime).count();
            if (elapsedMs >= budget_.maxFrameTimeMs || opsProcessed >= budget_.maxOpsPerCycle) {
                metrics_.rolloverCount++;
                break;
            }

            activeBatch_.push_back(*it);
            opsProcessed++;
            it = intents.erase(it); // Dequeue
        }
    }

    if (!activeBatch_.empty()) {
        if (activeBatch_.size() > metrics_.maxEpochMutationCount) {
            metrics_.maxEpochMutationCount = activeBatch_.size();
        }

        phase_ = CommitPhase::ApplyingSemanticState;
        applySemanticStage();
        
        phase_ = CommitPhase::ApplyingTopology;
        applyTopologyStage();
        
        phase_ = CommitPhase::ApplyingActivation;
        applyActivationStage();
        
        phase_ = CommitPhase::Reconciling;
        reconcileStage();

        // Calculate user perceived latency metrics for processed intents
        if (!(traceRecorder_ && traceRecorder_->isReplayMode())) {
            auto now = std::chrono::steady_clock::now();
            uint64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

            for (const auto& intent : activeBatch_) {
                if (intent.surfaceEpoch != getSurfaceEpoch(intent.surfaceId)) continue;
                
                double latency = static_cast<double>(nowMs - intent.enqueueTimeMs);
                if (latency < 0.0) latency = 0.0;
                
                if (intent.type == RuntimeMutationIntent::Type::ActivationChange || 
                    (intent.type == RuntimeMutationIntent::Type::OrchChange && intent.active)) {
                    metrics_.focusAcquisitionLatencyMs = latency;
                } else if (intent.type == RuntimeMutationIntent::Type::GeometryChange && 
                           intent.priority == MutationPriority::Interactive) {
                    metrics_.dragResponsivenessMs = latency;
                } else if (intent.type == RuntimeMutationIntent::Type::OrchChange && 
                           intent.presence == RuntimePresence::ResidencyBudgeted) {
                    metrics_.workspaceSwitchPerceptionMs = latency;
                } else if (intent.priority == MutationPriority::Critical) {
                    metrics_.keyboardNavigationDelayMs = latency;
                }
            }
        }

        // Detect Feedback Loop Cascades (Cascade Collapse Firewall)
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            size_t postCommitQueueSize = queue_.intents().size();
            if (postCommitQueueSize > metrics_.maxMutationFanout) {
                metrics_.maxMutationFanout = postCommitQueueSize;
            }

            if (postCommitQueueSize > 100) { // Storm detected!
                metrics_.cascadeCollapseCount++;
                eventLog_.log(FailureEvent::Type::Rollback, kInvalidNodeId, 
                    "Feedback cascade collapse: queue spammed with " + std::to_string(postCommitQueueSize) + " intents. Purging queue.");
                queue_.clear(); // Assertive firewall: collapse the feedback loop!
            }
        }
    }
    
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        metrics_.queueDepthAfterCommit = queue_.intents().size();
        
        // Age and advance remaining intents
        if (!epochFrozen_) {
            queue_.ageAndEscalateIntents();
            queue_.advanceEpoch();
        }
    }
    
    auto endTime = std::chrono::high_resolution_clock::now();
    metrics_.cycleDurationMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();
    commitLatencyTracker_.record(metrics_.cycleDurationMs);

    // Calculate DWM latency spike based on final cycle duration
    double targetPeriodMs = 1000.0 / (metrics_.observedMonitorRefreshRate > 0 ? metrics_.observedMonitorRefreshRate : 60);
    if (metrics_.cycleDurationMs > targetPeriodMs * 1.5) {
        metrics_.dwmLatencySpikeMs = metrics_.cycleDurationMs - targetPeriodMs;
    } else {
        metrics_.dwmLatencySpikeMs = 0.0;
    }

    // Record trace frame if traceRecorder exists, not replaying, and epoch not frozen
    if (traceRecorder_ && !traceRecorder_->isReplayMode() && !epochFrozen_) {
        TraceFrame frame;
        frame.epoch = queue_.currentEpoch();
        frame.timestampMs = static_cast<uint64_t>(startTimeMs);
        frame.intentsProcessed = activeBatch_;
        frame.mockHwndCreateSuccess = true;
        frame.mockActivationSuccess = true;
        frame.injectedDelayMs = 0;
        frame.temporalHealth = health_.temporal;
        frame.semanticHealth = health_.semantic;
        frame.operationalHealth = health_.operational;
        frame.realizationHealth = health_.realization;
        frame.confidence = static_cast<int>(health_.confidence);
        traceRecorder_->recordFrame(frame);
    }

    if (traceRecorder_ && traceRecorder_->isReplayMode()) {
        replayFrameIndex_++;
    }
    
    phase_ = CommitPhase::Idle;
}

void RuntimeCommitScheduler::collectStage() {
    phase_ = CommitPhase::Collecting;
    
    // Injects timing jitter skew from injection config
    if (traceRecorder_ && traceRecorder_->isReplayMode()) {
        uint64_t skew = traceRecorder_->injectionConfig().timingJitterSkewMs;
        if (skew > 0) {
            Sleep(static_cast<DWORD>(skew));
        }
    }

    // Simulated OS / Commit phase timing dilation
    if (commitPhaseArtificialDelayMs > 0) {
        Sleep(static_cast<DWORD>(commitPhaseArtificialDelayMs));
    }
}

void RuntimeCommitScheduler::arbitrateStage() {
    phase_ = CommitPhase::Arbitrating;
    std::lock_guard<std::mutex> lock(queueMutex_);
    queue_.sort();
}

void RuntimeCommitScheduler::applySemanticStage() {
    // Hard internal phase assertion
    if (phase_ != CommitPhase::ApplyingSemanticState) {
        MORPHIC_FATAL_KERNEL_VIOLATION("Mutating desired semantic state outside ApplyingSemanticState phase!");
    }
    
    RuntimeSceneMutationAuthority auth(true);
    
    for (const auto& intent : activeBatch_) {
        // Epoch Desync Safety: If incoming intent surface epoch is stale, hard discard it!
        if (intent.surfaceEpoch < getSurfaceEpoch(intent.surfaceId)) {
            metrics_.droppedIntents++;
            eventLog_.log(FailureEvent::Type::EpochDesync, intent.surfaceId,
                "Discarded stale epoch intent: surfaceId=" + std::to_string(intent.surfaceId) + 
                " intentEpoch=" + std::to_string(intent.surfaceEpoch) +
                " currentEpoch=" + std::to_string(getSurfaceEpoch(intent.surfaceId)));
            continue;
        }

        // Standard epoch generation check (exact match or newer)
        if (intent.surfaceEpoch != getSurfaceEpoch(intent.surfaceId)) {
            metrics_.coalescedCount++;
            continue;
        }

        switch (intent.type) {
            case RuntimeMutationIntent::Type::Destroy:
                sceneState_.clearState(intent.surfaceId);
                break;
            case RuntimeMutationIntent::Type::GeometryChange:
                sceneState_.updateDesiredGeometry(auth, intent.surfaceId, intent.geometry);
                break;
            case RuntimeMutationIntent::Type::VisibilityChange:
                sceneState_.updateDesiredVisibility(auth, intent.surfaceId, intent.visible);
                break;
            case RuntimeMutationIntent::Type::RoleChange:
                sceneState_.updateDesiredRole(auth, intent.surfaceId, intent.role);
                break;
            case RuntimeMutationIntent::Type::ElevationChange:
                sceneState_.updateDesiredElevation(auth, intent.surfaceId, intent.elevation, intent.sublevel);
                break;
            case RuntimeMutationIntent::Type::ActivationChange:
                sceneState_.updateDesiredActivation(auth, intent.surfaceId, intent.active);
                break;
            case RuntimeMutationIntent::Type::OrchChange:
                sceneState_.updateDesiredOrch(auth, intent.surfaceId, intent.continuity, intent.attention, intent.workspaceId, intent.semanticVisibility, intent.presence);
                sceneState_.updateDesiredActivation(auth, intent.surfaceId, intent.active);
                sceneState_.updateDesiredVisibility(auth, intent.surfaceId, intent.visible);
                break;
        }
    }
}

void RuntimeCommitScheduler::applyTopologyStage() {
    // Hard internal phase assertion
    if (phase_ != CommitPhase::ApplyingTopology) {
        MORPHIC_FATAL_KERNEL_VIOLATION("Applying topology outside ApplyingTopology phase!");
    }

    if (realizationLatencyInjectionMs > 0) {
        Sleep(static_cast<DWORD>(realizationLatencyInjectionMs));
    }

    bool forceHwndFail = false;
    if (traceRecorder_ && traceRecorder_->isReplayMode()) {
        forceHwndFail = traceRecorder_->injectionConfig().forceHwndCreationFailure;
    }

    for (const auto& intent : activeBatch_) {
        if (intent.surfaceEpoch != getSurfaceEpoch(intent.surfaceId)) continue;

        // Partial Commit Failure Warfare: Skip topology realization to simulate partial success corruption
        if (simulatePartialCommitFailure && intent.surfaceId == 99) {
            eventLog_.log(FailureEvent::Type::Divergence, intent.surfaceId, "Simulated partial commit failure: skipped topology realization");
            continue;
        }

        if (intent.type == RuntimeMutationIntent::Type::GeometryChange ||
            intent.type == RuntimeMutationIntent::Type::VisibilityChange ||
            intent.type == RuntimeMutationIntent::Type::RoleChange ||
            intent.type == RuntimeMutationIntent::Type::OrchChange) {
            
            const auto* state = sceneState_.getWorkingState(intent.surfaceId);
            if (state) {
                if (state->presence == RuntimePresence::Hibernating || state->presence == RuntimePresence::Quarantined) {
                    continue;
                }
                if (!forceHwndFail) {
                    realizer_->realizeTopology(intent.surfaceId, state->desiredGeometry, state->desiredVisible, state->desiredRole);
                } else {
                    eventLog_.log(FailureEvent::Type::Divergence, intent.surfaceId, "Mock HWND realization failure injected via Replay config");
                }
            }
        }
    }
}

void RuntimeCommitScheduler::applyActivationStage() {
    // Hard internal phase assertion
    if (phase_ != CommitPhase::ApplyingActivation) {
        MORPHIC_FATAL_KERNEL_VIOLATION("Applying activation outside ApplyingActivation phase!");
    }

    bool forceActivationFail = false;
    if (traceRecorder_ && traceRecorder_->isReplayMode()) {
        forceActivationFail = traceRecorder_->injectionConfig().forceActivationDenial;
    }

    for (const auto& intent : activeBatch_) {
        if (intent.surfaceEpoch != getSurfaceEpoch(intent.surfaceId)) continue;
        if (intent.type == RuntimeMutationIntent::Type::ActivationChange ||
            intent.type == RuntimeMutationIntent::Type::OrchChange) {
            const auto* state = sceneState_.getWorkingState(intent.surfaceId);
            if (state) {
                if (state->presence == RuntimePresence::Hibernating || state->presence == RuntimePresence::Quarantined) {
                    continue;
                }
                if (!forceActivationFail) {
                    realizer_->realizeActivation(intent.surfaceId, state->desiredActive);
                } else {
                    eventLog_.log(FailureEvent::Type::ActivationDenial, intent.surfaceId, "Mock Activation realization denial injected via Replay config");
                }
            }
        } else if (intent.type == RuntimeMutationIntent::Type::ElevationChange) {
            const auto* state = sceneState_.getWorkingState(intent.surfaceId);
            if (state) {
                realizer_->realizeElevation(intent.surfaceId, state->desiredElevation, state->desiredSublevel);
            }
        }
    }
}

void RuntimeCommitScheduler::reconcileStage() {
    // Hard internal phase assertion
    if (phase_ != CommitPhase::Reconciling) {
        MORPHIC_FATAL_KERNEL_VIOLATION("Reconciling outside Reconciling phase!");
    }

    if (reconciliationStallMs > 0) {
        Sleep(static_cast<DWORD>(reconciliationStallMs));
    }

    // Capture previous divergence ticks to compute repair velocity
    std::unordered_map<NodeId, size_t> prevDivergenceTicks;
    for (const auto& pair : sceneState_.workingStates()) {
        if (pair.second.divergenceTicks > 0) {
            prevDivergenceTicks[pair.second.surfaceId] = pair.second.divergenceTicks;
        }
    }

    // Check divergences and update severity
    sceneState_.checkDivergences();
    
    // Stage 8: Divergence Severity quarantine execution
    metrics_.quarantinedSurfaces = 0;
    if (compositor_) {
        for (auto& pair : sceneState_.workingStates()) {
            SurfaceSceneState& state = pair.second;
            if (state.presence == RuntimePresence::Quarantined) {
                metrics_.quarantinedSurfaces++;
                auto* host = compositor_->surfaceRegistry().getHost(state.surfaceId);
                if (host && host->isAlive() && host->hasRenderer()) {
                    RenderId renderId = host->activeRendererId();
                    if (renderId != kInvalidRenderId) {
                        compositor_->workloadController().requestTransition(
                            renderId, ActivityState::Dormant, "quarantine_quell", true);
                        eventLog_.log(FailureEvent::Type::Quarantine, state.surfaceId, "Quarantined surface to dormant");
                    }
                }
            } else if (state.divergenceTicks > 0) {
                eventLog_.log(FailureEvent::Type::Divergence, state.surfaceId, 
                    "Observed divergence: ticks=" + std::to_string(state.divergenceTicks) + 
                    " severity=" + std::to_string(static_cast<int>(state.severity)));
            }
        }
    }
    
    // Post-commit Renderer Notification Barrier for isolated Flutter resizes
    for (const auto& intent : activeBatch_) {
        if (intent.surfaceEpoch != getSurfaceEpoch(intent.surfaceId)) continue;
        if (intent.type == RuntimeMutationIntent::Type::GeometryChange) {
            const auto* state = sceneState_.getWorkingState(intent.surfaceId);
            if (state) {
                realizer_->notifyRendererResize(intent.surfaceId, state->desiredGeometry.width, state->desiredGeometry.height);
            }
        }
    }
    
    // Telemetry and maximum divergence calculation
    size_t maxTicks = 0;
    size_t totalDivergentSurfaces = 0;
    size_t totalDivergenceTicks = 0;
    for (const auto& pair : sceneState_.workingStates()) {
        if (pair.second.divergenceTicks > maxTicks) {
            maxTicks = pair.second.divergenceTicks;
        }
        if (pair.second.divergenceTicks > 0) {
            totalDivergentSurfaces++;
            totalDivergenceTicks += pair.second.divergenceTicks;
        }
    }
    metrics_.maxDivergenceTicks = maxTicks;

    // --- Calculate Multi-Dimensional Health Matrix & Recovery velocity ---
    // 1. temporal health
    double tempHealth = 1.0;
    if (metrics_.cycleDurationMs > budget_.maxFrameTimeMs) {
        tempHealth -= 0.1 * (metrics_.cycleDurationMs / budget_.maxFrameTimeMs);
    }
    if (metrics_.frameJitterMs > 4.0) {
        tempHealth -= 0.1;
    }
    if (tempHealth < 0.0) tempHealth = 0.0;
    health_.temporal = tempHealth;

    // 2. semantic health
    double semHealth = 1.0;
    if (totalDivergentSurfaces > 0) {
        semHealth -= 0.05 * totalDivergentSurfaces;
        semHealth -= 0.02 * maxTicks;
    }
    if (semHealth < 0.0) semHealth = 0.0;
    health_.semantic = semHealth;

    // 3. operational health
    double operHealth = 1.0;
    if (metrics_.queueDepthBeforeCommit > 50) {
        operHealth -= 0.2;
    }
    if (metrics_.rolloverCount > 0) {
        operHealth -= 0.1;
    }
    if (metrics_.cascadeCollapseCount > 0) {
        operHealth -= 0.4;
    }
    if (operHealth < 0.0) operHealth = 0.0;
    health_.operational = operHealth;

    // 4. realization health
    double realHealth = 1.0;
    if (metrics_.quarantinedSurfaces > 0) {
        realHealth -= 0.2 * metrics_.quarantinedSurfaces;
    }
    if (metrics_.userHandlesCount > 9000 || metrics_.gdiHandlesCount > 9000) {
        realHealth -= 0.5;
    }
    if (realHealth < 0.0) realHealth = 0.0;
    health_.realization = realHealth;

    // 5. confidence
    if (health_.temporal >= 0.95 && health_.semantic >= 0.95 &&
        health_.operational >= 0.95 && health_.realization >= 0.95 &&
        totalDivergentSurfaces == 0) {
        health_.confidence = KernelConfidence::High;
    } else if (totalDivergentSurfaces > 0 && maxTicks <= 10 && health_.realization >= 0.8) {
        health_.confidence = KernelConfidence::Degraded;
    } else {
        health_.confidence = KernelConfidence::Uncertain;
    }

    // 6. recovery velocity
    for (const auto& pair : sceneState_.workingStates()) {
        NodeId sid = pair.second.surfaceId;
        if (pair.second.divergenceTicks == 0 && prevDivergenceTicks.count(sid) > 0) {
            double ticksToRepair = static_cast<double>(prevDivergenceTicks[sid]);
            if (health_.meanDivergenceRepairTicks == 0.0) {
                health_.meanDivergenceRepairTicks = ticksToRepair;
            } else {
                health_.meanDivergenceRepairTicks = 0.9 * health_.meanDivergenceRepairTicks + 0.1 * ticksToRepair;
            }
        }
    }

    if (health_.meanDivergenceRepairTicks > 0.0) {
        health_.stabilizationHalfLife = health_.meanDivergenceRepairTicks * 0.693;
    } else {
        health_.stabilizationHalfLife = 0.0;
    }

    // Calculate convergence velocity (rate of total divergence reduction)
    double currentVelocity = static_cast<double>(prevTotalDivergenceTicks_) - static_cast<double>(totalDivergenceTicks);
    health_.convergenceVelocity = 0.8 * health_.convergenceVelocity + 0.2 * currentVelocity;
    prevTotalDivergenceTicks_ = totalDivergenceTicks;

    for (auto& pair : sceneState_.workingStates()) {
        SurfaceSceneState& state = pair.second;
        if (state.presence == RuntimePresence::Quarantined) {
            quarantineCount_[state.surfaceId]++;
        } else if (quarantineCount_.count(state.surfaceId) > 0) {
            double recoveryTime = static_cast<double>(quarantineCount_[state.surfaceId]);
            if (health_.meanQuarantineRecoveryTime == 0.0) {
                health_.meanQuarantineRecoveryTime = recoveryTime;
            } else {
                health_.meanQuarantineRecoveryTime = 0.9 * health_.meanQuarantineRecoveryTime + 0.1 * recoveryTime;
            }
            quarantineCount_.erase(state.surfaceId);
        }
    }

    // 7. long-horizon decay
    health_.quarantineRecurrence = 0;
    for (const auto& pair : sceneState_.workingStates()) {
        if (pair.second.presence == RuntimePresence::Quarantined && quarantineCount_[pair.first] == 1) {
            health_.quarantineRecurrence++;
        }
    }

    health_.backlogTrendSlope = 0.95 * health_.backlogTrendSlope + 0.05 * static_cast<double>(metrics_.rolloverCount);

    bool budgetExhausted = (metrics_.queueDepthBeforeCommit > activeBatch_.size());
    health_.budgetPressure = 0.95 * health_.budgetPressure + 0.05 * (budgetExhausted ? 1.0 : 0.0);

    // Repair frequency trend
    static size_t repairTriggerCount = 0;
    static uint64_t tickCounter = 0;
    tickCounter++;
    if (totalDivergentSurfaces > 0) {
        repairTriggerCount++;
    }
    if (tickCounter % 500 == 0) {
        health_.repairFrequencyTrend = static_cast<double>(repairTriggerCount) / 500.0;
        repairTriggerCount = 0;
    } else {
        health_.repairFrequencyTrend = 0.99 * health_.repairFrequencyTrend + 0.01 * (totalDivergentSurfaces > 0 ? 1.0 : 0.0);
    }

    // Double-buffered snap commit swap
    sceneState_.swapCommitBoundary();
}

} // namespace morphic

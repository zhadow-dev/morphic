#include "morphic_runtime_impl.h"
#include "../composition/compositor.h"
#include "../core/kernel_trace.h"
#include <windows.h>

namespace morphic {

// --- Bootstrap Sequence ---

void MorphicRuntimeImpl::bootstrap(Compositor& compositor) {
    compositor_ = &compositor;

    // Stage 1: ConstitutionLoad (Fatal)
    bool constOk = true; 
    std::string constErr = "";
    if (!compositor.window()) {
        constOk = false;
        constErr = "Compositor window handle is null during ConstitutionLoad";
    }
    bootstrap_.advanceTo(BootstrapPhase::ConstitutionLoad, constOk, constErr);

    // Stage 2: RuntimeInit (Fatal)
    bool initOk = true;
    std::string initErr = "";
    try {
        framesSinceMemoryCheck_ = 0;
        framesSinceActivationCheck_ = 0;
        framesSinceRecoveryCheck_ = 0;
        framesSinceFramePacingCheck_ = 0;
        currentDegradedMode_ = RuntimeDegradedMode::None;
    } catch (const std::exception& e) {
        initOk = false;
        initErr = e.what();
    }
    bootstrap_.advanceTo(BootstrapPhase::RuntimeInit, initOk, initErr);

    // Stage 3: SchedulerInit (Fatal)
    bool schedOk = true;
    std::string schedErr = "";
    try {
        scheduler_ = std::make_unique<RuntimeCommitScheduler>(sceneState_, realizer_, &compositor);
    } catch (const std::exception& e) {
        schedOk = false;
        schedErr = e.what();
    }
    bootstrap_.advanceTo(BootstrapPhase::SchedulerInit, schedOk, schedErr);

    // Stage 4: TraceInit (Recoverable degraded)
    bool traceOk = true;
    std::string traceErr = "";
    try {
        scheduler_->traceRecorder()->setOptInPersistence(false);
    } catch (const std::exception& e) {
        traceOk = false;
        traceErr = e.what();
    }
    bootstrap_.advanceTo(BootstrapPhase::TraceInit, traceOk, traceErr);

    // Stage 5: SurfaceRegistryInit (Fatal)
    bool regOk = true;
    std::string regErr = "";
    if (&compositor.surfaceRegistry() == nullptr) {
        regOk = false;
        regErr = "SurfaceRegistry reference is null";
    }
    bootstrap_.advanceTo(BootstrapPhase::SurfaceRegistryInit, regOk, regErr);

    // Stage 6: RealizationBridgeInit (Fatal)
    bool bridgeOk = true;
    std::string bridgeErr = "";
    try {
        realizer_ = std::make_shared<Win32SurfaceRealizer>(
            compositor.surfaceRegistry(),
            compositor.activationManager()
        );
        scheduler_->setRealizer(realizer_);
        compositor.setCommitScheduler(scheduler_.get());
    } catch (const std::exception& e) {
        bridgeOk = false;
        bridgeErr = e.what();
    }
    bootstrap_.advanceTo(BootstrapPhase::RealizationBridgeInit, bridgeOk, bridgeErr);

    // Stage 7: WorkspaceRestore (Recoverable degraded)
    // Restore MUST run BEFORE ActivationUnlock to prevent races during restore
    bool restoreOk = true;
    std::string restoreErr = "";
    bool skippedRec = false;
    try {
        workspaceRuntime_.initialize(compositor.focusGraph());
        if (!workspaceRuntime_.restoreSession()) {
            restoreOk = false;
            restoreErr = "Session restore returned false or no session file found";
            skippedRec = true;
        }
    } catch (const std::exception& e) {
        restoreOk = false;
        restoreErr = e.what();
    }
    bootstrap_.advanceTo(BootstrapPhase::WorkspaceRestore, restoreOk, restoreErr, skippedRec);

    // Stage 8: ActivationUnlock (Recoverable degraded)
    bool unlockOk = true;
    std::string unlockErr = "";
    try {
        compositor.activationManager().unlock();
    } catch (const std::exception& e) {
        unlockOk = false;
        unlockErr = e.what();
    }
    bootstrap_.advanceTo(BootstrapPhase::ActivationUnlock, unlockOk, unlockErr);

    // Stage 9: OperationalStart (Fatal)
    bool startOk = true;
    std::string startErr = "";
    try {
        validationRuntime_.initialize(compositor.focusGraph(), workspaceRuntime_);
    } catch (const std::exception& e) {
        startOk = false;
        startErr = e.what();
    }
    bootstrap_.advanceTo(BootstrapPhase::OperationalStart, startOk, startErr);
}


// --- Public API Implementation ---

api::WorkspaceHandle MorphicRuntimeImpl::createWorkspace(const api::WorkspaceConfig& config) {
    if (!bootstrap_.isAtLeast(BootstrapPhase::WorkspaceRestore)) {
        return {0};
    }

    WorkspaceId wsId = workspaceRuntime_.createWorkspace();
    workspaceRuntime_.setWorkspaceIntent(wsId,
        toKernelActivity(config.activity),
        toKernelDisposition(config.disposition),
        config.label);

    return {wsId.value};
}

void MorphicRuntimeImpl::destroyWorkspace(api::WorkspaceHandle workspace) {
    if (!bootstrap_.isAtLeast(BootstrapPhase::WorkspaceRestore)) return;
    if (scheduler_) {
        scheduler_->invalidateWorkspace(WorkspaceId{workspace.id});
    }
    workspaceRuntime_.destroyWorkspace(WorkspaceId{workspace.id});
}

void MorphicRuntimeImpl::switchWorkspace(api::WorkspaceHandle workspace) {
    if (!bootstrap_.isAtLeast(BootstrapPhase::WorkspaceRestore)) return;
    workspaceRuntime_.switchWorkspace(WorkspaceId{workspace.id});
}

api::WorkspaceHandle MorphicRuntimeImpl::activeWorkspace() const {
    if (!bootstrap_.isAtLeast(BootstrapPhase::WorkspaceRestore)) return {0};
    return {workspaceRuntime_.activeWorkspace().value};
}

void MorphicRuntimeImpl::updateWorkspaceConfig(api::WorkspaceHandle workspace,
                                                 const api::WorkspaceConfig& config) {
    if (!bootstrap_.isAtLeast(BootstrapPhase::WorkspaceRestore)) return;
    workspaceRuntime_.setWorkspaceIntent(WorkspaceId{workspace.id},
        toKernelActivity(config.activity),
        toKernelDisposition(config.disposition),
        config.label);
}

void MorphicRuntimeImpl::declareSurfaceAttention(api::SurfaceHandle surface,
                                                   api::Attention level) {
    if (!bootstrap_.isAtLeast(BootstrapPhase::WorkspaceRestore)) return;
    workspaceRuntime_.setSurfaceAttention(static_cast<NodeId>(surface.id), toKernelAttention(level));
}

void MorphicRuntimeImpl::associateSurfaces(api::SurfaceHandle a,
                                             api::SurfaceHandle b,
                                             api::Association association) {
    if (!bootstrap_.isAtLeast(BootstrapPhase::WorkspaceRestore)) return;
    workspaceRuntime_.associateSurfaces({static_cast<NodeId>(a.id)},
                                {static_cast<NodeId>(b.id)}, toKernelAssociation(association));
}

void MorphicRuntimeImpl::dissociateSurface(api::SurfaceHandle surface) {
    if (!bootstrap_.isAtLeast(BootstrapPhase::WorkspaceRestore)) return;
    workspaceRuntime_.dissociateSurface({static_cast<NodeId>(surface.id)});
}

void MorphicRuntimeImpl::saveSession(api::InterruptionReason reason) {
    if (!bootstrap_.isAtLeast(BootstrapPhase::WorkspaceRestore)) return;
    workspaceRuntime_.saveSession(toKernelInterruption(reason));
}

void MorphicRuntimeImpl::restoreSession() {
    if (!bootstrap_.isAtLeast(BootstrapPhase::WorkspaceRestore)) return;
    workspaceRuntime_.restoreSession();
}

void MorphicRuntimeImpl::setOptInProductivity(bool enabled) {
    workspaceRuntime_.setOptInProductivity(enabled);
}

bool MorphicRuntimeImpl::isOptInProductivity() const {
    return workspaceRuntime_.isOptInProductivity();
}

api::RuntimeHealth MorphicRuntimeImpl::getHealth() const {
    api::RuntimeHealth health;
    if (!compositor_) return health;

    health.workspaceCount = workspaceRuntime_.workspaceCount();
    health.surfaceCount = static_cast<int>(compositor_->surfaceRegistry().hosts().size());
    health.healthyRenderers = 0;
    health.totalRenderers = 0;

    for (const auto& [rid, rec] : compositor_->rendererManager().records()) {
        health.totalRenderers++;
        if (rec.isAlive()) {
            health.healthyRenderers++;
        }
    }

    auto pressureState = pressure_.lastSnapshot();
    health.underPressure = (pressureState.globalState != RuntimePressureState::Stable);
    health.degradedModeActive = (currentDegradedMode_ != RuntimeDegradedMode::None);

    health.summary = "Workspaces:" + std::to_string(health.workspaceCount) +
        " Surfaces:" + std::to_string(health.surfaceCount) +
        " Renderers:" + std::to_string(health.healthyRenderers) +
        "/" + std::to_string(health.totalRenderers) +
        (health.underPressure ? " [PRESSURE]" : "") +
        (health.degradedModeActive ? " [DEGRADED]" : "");

    return health;
}

// --- Non-public operations ---

NodeId MorphicRuntimeImpl::createSurface(const SurfaceConfig& config) {
    if (!compositor_) return kInvalidNodeId;
    return compositor_->createSurface(config);
}

void MorphicRuntimeImpl::destroySurface(NodeId id) {
    if (!compositor_) return;
    // Clean up semantic state
    workspaceRuntime_.dissociateSurface(id);
    compositor_->destroySurface(id);
}

ValidationSuiteResult MorphicRuntimeImpl::runValidation() {
    if (!bootstrap_.isAtLeast(BootstrapPhase::WorkspaceRestore)) {
        return {};
    }
    return validationRuntime_.runAll();
}

void MorphicRuntimeImpl::evaluatePressure() {
    if (!compositor_ || !bootstrap_.isAtLeast(BootstrapPhase::RealizationBridgeInit)) {
        return;
    }

    // Feed renderer count to pressure evaluator
    int rendererCount = 0;
    int recoveringCount = 0;
    for (const auto& [rid, rec] : compositor_->rendererManager().records()) {
        rendererCount++;
        if (rec.lifecycle == RendererLifecycle::Zombie) recoveringCount++;
    }
    pressure_.setRendererRecoveryCount(recoveringCount, rendererCount);

    // Domain-specific cadences
    framesSinceMemoryCheck_++;
    framesSinceActivationCheck_++;
    framesSinceRecoveryCheck_++;
    framesSinceFramePacingCheck_++;

    // Activation check — every frame (immediate response to storms)
    if (framesSinceActivationCheck_ >= kActivationCheckCadence) {
        framesSinceActivationCheck_ = 0;
        // Activation pressure is tracked by ActivationManager directly
    }

    // Frame pacing check — ~166ms rolling window
    if (framesSinceFramePacingCheck_ >= kFramePacingCheckCadence) {
        framesSinceFramePacingCheck_ = 0;
        auto state = pressure_.evaluate();
        auto mode = degradation_.evaluateMode(state.globalState);
        if (mode != RuntimeDegradedMode::None && currentDegradedMode_ == RuntimeDegradedMode::None) {
            currentDegradedMode_ = mode;
            OutputDebugStringA("RUNTIME: Entering degraded mode — frame pacing\n");
        }
    }

    // Recovery check — ~500ms burst-sensitive
    if (framesSinceRecoveryCheck_ >= kRecoveryCheckCadence) {
        framesSinceRecoveryCheck_ = 0;
        // Recovery saturation check
    }

    // Memory check — ~2s slow cadence
    if (framesSinceMemoryCheck_ >= kMemoryCheckCadence) {
        framesSinceMemoryCheck_ = 0;
        auto state = pressure_.evaluate();
        auto mode = degradation_.evaluateMode(state.globalState);
        if (mode == RuntimeDegradedMode::None && currentDegradedMode_ != RuntimeDegradedMode::None) {
            currentDegradedMode_ = RuntimeDegradedMode::None;
            OutputDebugStringA("RUNTIME: Exiting degraded mode — pressure relieved\n");
        }
    }
}

// --- Type conversion ---

OperationalActivity MorphicRuntimeImpl::toKernelActivity(api::Activity a) {
    switch (a) {
        case api::Activity::Editing:     return OperationalActivity::Editing;
        case api::Activity::Inspecting:  return OperationalActivity::Inspecting;
        case api::Activity::Monitoring:  return OperationalActivity::Monitoring;
        case api::Activity::Comparing:   return OperationalActivity::Comparing;
        case api::Activity::Reviewing:   return OperationalActivity::Reviewing;
        case api::Activity::Searching:   return OperationalActivity::Searching;
        case api::Activity::Debugging:   return OperationalActivity::Debugging;
        case api::Activity::Reference:   return OperationalActivity::Reference;
    }
    return OperationalActivity::Editing;
}

IntentDisposition MorphicRuntimeImpl::toKernelDisposition(api::Disposition d) {
    switch (d) {
        case api::Disposition::Persistent:          return IntentDisposition::Persistent;
        case api::Disposition::Transient:           return IntentDisposition::Transient;
        case api::Disposition::InterruptSensitive:  return IntentDisposition::InterruptSensitive;
        case api::Disposition::ContinuityCritical:  return IntentDisposition::ContinuityCritical;
        case api::Disposition::BackgroundDominant:  return IntentDisposition::BackgroundDominant;
        case api::Disposition::Collaborative:       return IntentDisposition::Collaborative;
    }
    return IntentDisposition::Persistent;
}

AttentionLevel MorphicRuntimeImpl::toKernelAttention(api::Attention a) {
    switch (a) {
        case api::Attention::Active:             return AttentionLevel::Active;
        case api::Attention::PassiveMonitoring:  return AttentionLevel::PassiveMonitoring;
        case api::Attention::LatentContinuity:   return AttentionLevel::LatentContinuity;
        case api::Attention::Interruptible:      return AttentionLevel::Interruptible;
        case api::Attention::Urgent:             return AttentionLevel::Urgent;
        case api::Attention::Background:         return AttentionLevel::Background;
    }
    return AttentionLevel::Active;
}

WorkflowRelationship MorphicRuntimeImpl::toKernelAssociation(api::Association a) {
    switch (a) {
        case api::Association::CoEditing:           return WorkflowRelationship::CoEditing;
        case api::Association::Inspecting:          return WorkflowRelationship::InspectedBy;
        case api::Association::Monitoring:          return WorkflowRelationship::MonitoredBy;
        case api::Association::TemporaryCompanion:  return WorkflowRelationship::TemporaryCompanion;
        case api::Association::SharedContext:       return WorkflowRelationship::SharedContext;
    }
    return WorkflowRelationship::SharedContext;
}

InterruptionReason MorphicRuntimeImpl::toKernelInterruption(api::InterruptionReason r) {
    switch (r) {
        case api::InterruptionReason::IntentionalPause:     return InterruptionReason::IntentionalPause;
        case api::InterruptionReason::ForcedSuspend:        return InterruptionReason::ForcedSuspend;
        case api::InterruptionReason::CrashRecovery:        return InterruptionReason::CrashRecovery;
        case api::InterruptionReason::TemporaryDiversion:   return InterruptionReason::TemporaryDiversion;
        case api::InterruptionReason::UrgentInterruption:   return InterruptionReason::UrgentInterruption;
        case api::InterruptionReason::SessionEnd:           return InterruptionReason::SessionEnd;
    }
    return InterruptionReason::IntentionalPause;
}

} // namespace morphic

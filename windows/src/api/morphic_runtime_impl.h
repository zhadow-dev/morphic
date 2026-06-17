#pragma once

#include "../../include/morphic/morphic_api.h"
#include "runtime_bootstrap.h"
#include "workspace_runtime.h"
#include "validation_runtime.h"
#include "../core/runtime_scene_state.h"
#include "../core/runtime_commit_scheduler.h"
#include "../core/win32_surface_realizer.h"
#include "../core/runtime_pressure.h"
#include "../core/degraded_runtime_policy.h"
#include <memory>

namespace morphic {

class Compositor;

// MorphicRuntimeImpl — THE Runtime Facade.
//
// ALL public operations route through here.
// NOT an adapter. THE operational runtime boundary.
//
// Routing:
//   Dart → Method Channel → MorphicRuntimeImpl → WorkspaceRuntime / Kernel
//
// MorphicRuntimeImpl owns:
//   - WorkspaceRuntime (semantic layer)
//   - ValidationRuntime (test orchestration)
//   - RuntimePressureEvaluator (pressure sensing)
//   - DegradedRuntimePolicy (degradation)
//   - RuntimeDiagnostics (observability)
//   - RuntimeBootstrap (startup sequencing)
//   - RuntimeSceneState & RuntimeCommitScheduler (Phase 9 execution kernel)
//
// MorphicRuntimeImpl does NOT own:
//   - Compositor (kernel — separate authority)
//   - SurfaceRegistry
//   - ActivationManager
//   - RendererManager
//   - FocusGraph
//   -
//
// It holds a reference to Compositor for kernel coordination,
// but NEVER exposes it.

class MorphicRuntimeImpl : public api::MorphicRuntime {
public:
    MorphicRuntimeImpl() = default;
    ~MorphicRuntimeImpl() override = default;

    // --- Bootstrap ---
    // Combined 9-stage deterministic bootstrap sequence
    void bootstrap(Compositor& compositor);

    // Deprecated individual bootstrap entry points (noop since combined bootstrap is used)
    void bootstrapKernel(Compositor& compositor) {}
    void bootstrapWorkspaceRuntime() {}
    void bootstrapSessionRuntime() {}
    void bootstrapDiagnostics() {}
    void bootstrapDartBridge() {}
    void bootstrapSessionRestore() {}
    void bootstrapSteadyState() {}

    BootstrapPhase currentPhase() const { return bootstrap_.currentPhase(); }
    bool isReady() const { return bootstrap_.isReady(); }
    const std::vector<BootstrapPhaseSnapshot>& getBootstrapSnapshots() const { return bootstrap_.snapshots(); }


    // --- Public API (morphic::api::MorphicRuntime) ---
    api::WorkspaceHandle createWorkspace(const api::WorkspaceConfig& config) override;
    void destroyWorkspace(api::WorkspaceHandle workspace) override;
    void switchWorkspace(api::WorkspaceHandle workspace) override;
    api::WorkspaceHandle activeWorkspace() const override;
    void updateWorkspaceConfig(api::WorkspaceHandle workspace,
                                const api::WorkspaceConfig& config) override;

    void declareSurfaceAttention(api::SurfaceHandle surface,
                                  api::Attention level) override;

    void associateSurfaces(api::SurfaceHandle a, api::SurfaceHandle b,
                            api::Association association) override;
    void dissociateSurface(api::SurfaceHandle surface) override;

    void saveSession(api::InterruptionReason reason) override;
    void restoreSession() override;

    // --- Telemetry & Privacy Boundaries ---
    void setOptInProductivity(bool enabled) override;
    bool isOptInProductivity() const override;

    api::RuntimeHealth getHealth() const override;

    // --- Non-public operations (internal routing) ---
    // Surface lifecycle (delegates to Compositor kernel)
    NodeId createSurface(const SurfaceConfig& config);
    void destroySurface(NodeId id);

    // Validation
    ValidationSuiteResult runValidation();

    // Pressure evaluation (called by frame loop timer)
    void evaluatePressure();

    // --- Sub-runtimes (internal access) ---
    WorkspaceRuntime& workspaceRuntime() { return workspaceRuntime_; }
    const WorkspaceRuntime& workspaceRuntime() const { return workspaceRuntime_; }
    ValidationRuntime& validationRuntime() { return validationRuntime_; }

    RuntimeCommitScheduler& scheduler() { return *scheduler_; }
    const RuntimeCommitScheduler& scheduler() const { return *scheduler_; }

    RuntimeSceneState& sceneState() { return sceneState_; }
    const RuntimeSceneState& sceneState() const { return sceneState_; }

private:
    // Startup
    RuntimeBootstrap bootstrap_;

    // Phase 9 execution kernel
    RuntimeSceneState sceneState_;
    std::shared_ptr<Win32SurfaceRealizer> realizer_;
    std::unique_ptr<RuntimeCommitScheduler> scheduler_;


    // Kernel reference (NOT owned)
    Compositor* compositor_ = nullptr;

    // Semantic orchestration (owned)
    WorkspaceRuntime workspaceRuntime_;

    // Validation (owned)
    ValidationRuntime validationRuntime_;

    // Pressure sensing (owned)
    RuntimePressureEvaluator pressure_;
    DegradedRuntimePolicy degradation_;

    // Current degraded mode
    RuntimeDegradedMode currentDegradedMode_ = RuntimeDegradedMode::None;

    // Pressure domain cadence counters
    int framesSinceMemoryCheck_ = 0;
    int framesSinceActivationCheck_ = 0;
    int framesSinceRecoveryCheck_ = 0;
    int framesSinceFramePacingCheck_ = 0;

    // Cadence intervals (domain-specific, not global)
    static constexpr int kMemoryCheckCadence = 120;        // ~2s at 60fps (slow)
    static constexpr int kActivationCheckCadence = 1;      // every frame (immediate)
    static constexpr int kRecoveryCheckCadence = 30;       // ~500ms (burst-sensitive)
    static constexpr int kFramePacingCheckCadence = 10;    // ~166ms (rolling window)

    // Internal API ↔ kernel type conversion helpers
    static OperationalActivity toKernelActivity(api::Activity a);
    static IntentDisposition toKernelDisposition(api::Disposition d);
    static AttentionLevel toKernelAttention(api::Attention a);
    static WorkflowRelationship toKernelAssociation(api::Association a);
    static InterruptionReason toKernelInterruption(api::InterruptionReason r);
};

} // namespace morphic

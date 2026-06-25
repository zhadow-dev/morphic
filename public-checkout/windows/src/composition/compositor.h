#pragma once

#include "../core/types.h"
#include "../core/scene_graph.h"
#include "../core/window_host.h"
#include "../core/surface_registry.h"
#include "../interaction/activation_manager.h"
#include "composition_clock.h"
#include "workspace_mode.h"
#include "../elevation/elevation_manager.h"
#include "../display/display_manager.h"
#include "../interaction/drag_controller.h"
#include "../interaction/resize_controller.h"
#include "../debug/debug_overlay.h"
#include "../debug/metrics_collector.h"
#include "../testing/invariant_checker.h"
#include "../testing/runtime_contract_verifier.h"
#include "../testing/ordering_validator.h"
#include "../testing/temporal_validator.h"
#include "../testing/temporal_auditor.h"
#include "../testing/performance_budget.h"
#include "../testing/failure_injector.h"
#include "../testing/replay_system.h"
#include "../rendering/renderer_surface.h"
#include "../rendering/renderer_manager.h"
#include "../debug/render_skew_tracker.h"
#include "../debug/input_photon_tracker.h"
#include "../interaction/capture_manager.h"
#include "../interaction/focus_graph.h"
#include "../interaction/input_router.h"
#include "../interaction/visibility_observer.h"
#include "../rendering/workload_controller.h"
#include "../rendering/governance_scheduler.h"
#include "../debug/frame_pacer.h"
#include "../debug/tick_coherence_probe.h"

#include <windows.h>
#include <memory>
#include <vector>
#include <functional>
#include <unordered_map>
#include <variant>

namespace morphic {

class RuntimeCommitScheduler;

// --- Composition Transaction ---
// Atomic batched mutations. All operations queued, then applied in one frame.
class CompositionTransaction {
public:
    struct Op {
        enum Type { Move, Resize, SetElevation, Detach, Attach, Show, Hide };
        Type type;
        NodeId targetId;
        int x = 0, y = 0, w = 0, h = 0;
        ElevationLayer layer = ElevationLayer::Base;
        int sublevel = 0;
        NodeId groupId = kInvalidNodeId;
    };

    void move(NodeId id, int x, int y) {
        ops_.push_back({ Op::Move, id, x, y, 0, 0 });
    }

    void resize(NodeId id, int w, int h) {
        ops_.push_back({ Op::Resize, id, 0, 0, w, h });
    }

    void setElevation(NodeId id, ElevationLayer layer, int sublevel = 0) {
        Op op;
        op.type = Op::SetElevation;
        op.targetId = id;
        op.layer = layer;
        op.sublevel = sublevel;
        ops_.push_back(op);
    }

    void show(NodeId id) {
        ops_.push_back({ Op::Show, id });
    }

    void hide(NodeId id) {
        ops_.push_back({ Op::Hide, id });
    }

    const std::vector<Op>& operations() const { return ops_; }
    bool isEmpty() const { return ops_.empty(); }
    size_t operationCount() const { return ops_.size(); }
    void clear() { ops_.clear(); }

private:
    std::vector<Op> ops_;
};

// --- Compositor ---
// The central orchestrator. Owns all subsystems.
// Processes: scene graph -> scheduler -> batch flush.
class Compositor {
public:
    Compositor();
    ~Compositor();

    // --- Initialization ---
    void initialize(HWND ownerWindow = nullptr);
    void shutdown();

    // --- Surface lifecycle ---
    NodeId createSurface(const SurfaceConfig& config);
    void destroySurface(NodeId id);

    // --- Surface role (Phase 3E.3) ---
    void setSurfaceRole(NodeId surfaceId, SurfaceRole role);
    SurfaceRole surfaceRole(NodeId surfaceId) const;

    // --- Internal navigation (Phase 3E.3) ---
    // Compositor-native surface cycling (replaces Alt+Tab for workspace surfaces)
    void focusNextSurface();
    void focusPreviousSurface();

    // --- Group lifecycle ---
    NodeId createGroup(const std::vector<NodeId>& memberIds);
    void destroyGroup(NodeId groupId);

    // --- Transactions ---
    CompositionTransaction& beginTransaction();
    void commitTransaction();

    // --- Direct mutations (convenience, wraps transaction internally) ---
    void moveSurface(NodeId id, int x, int y);
    void resizeSurface(NodeId id, int w, int h);

    // --- Frame scheduling ---
    void requestFrame();
    void tick();  // Process one frame

    // --- Event handlers (called from WindowHost) ---
    void onDragBegin(NodeId surfaceId, POINT screenPos);
    void onDragUpdate(NodeId surfaceId, POINT screenPos);
    void onDragEnd(NodeId surfaceId, POINT screenPos);
    void onSurfaceResized(NodeId surfaceId, int width, int height);
    void onSurfaceActivated(NodeId surfaceId);
    void onMainWindowActivated();  // Phase 3E.3: raised via subclass proc
    
    // --- Phase 4 Step 3D: Input realization ---
    void realizeFocusDecision(const FocusDecision& decision);

    // --- Renderer integration (Phase 2A) ---
    // Renderer lifecycle is managed through RendererManager.
    // Compositor coordinates: register -> bind to host -> lifecycle transitions.
    bool attachRenderer(NodeId surfaceId, std::unique_ptr<RendererSurface> renderer);
    void detachRenderer(NodeId surfaceId);

    // Phase X: Hard destruction — 7-stage authoritative renderer destruction.
    // Unlike detachRenderer (which creates zombies), this truly destroys the
    // Flutter engine, releases all HWNDs, and removes from all tracking.
    // Returns: {destroyed: bool, hwndsBefore: int, hwndsAfter: int, error: string}
    struct HardDestroyResult {
        bool destroyed = false;
        int hwndsBefore = 0;
        int hwndsAfter = 0;
        std::string error;
    };
    HardDestroyResult hardDetachRenderer(NodeId surfaceId);

    // --- Debug ---
    void setDebugOverlay(bool enabled);
    bool isDebugEnabled() const;

    // --- Display ---
    void refreshDisplays();
    const std::vector<DisplayInfo>& displays() const;

    // --- Accessors ---
    SceneGraph& sceneGraph() { return surfaceRegistry_.sceneGraph(); }
    const MetricsCollector& metrics() const { return metrics_; }
    const std::unordered_map<NodeId, std::unique_ptr<WindowHost>>& hosts() const { return surfaceRegistry_.hosts(); }
    SurfaceRegistry& surfaceRegistry() { return surfaceRegistry_; }
    const SurfaceRegistry& surfaceRegistry() const { return surfaceRegistry_; }
    ActivationManager& activationManager() { return activationManager_; }
    const TemporalValidator& temporalValidator() const { return temporalValidator_; }
    FailureInjector& failureInjector() { return failureInjector_; }
    ReplaySystem& replaySystem() { return replaySystem_; }
    RendererManager& rendererManager() { return rendererManager_; }
    const RendererManager& rendererManager() const { return rendererManager_; }
    RenderSkewTracker& skewTracker() { return skewTracker_; }
    const RenderSkewTracker& skewTracker() const { return skewTracker_; }
    InputPhotonTracker& inputPhotonTracker() { return inputPhotonTracker_; }
    const InputPhotonTracker& inputPhotonTracker() const { return inputPhotonTracker_; }
    CaptureManager& captureManager() { return captureManager_; }
    const CaptureManager& captureManager() const { return captureManager_; }
    FocusGraph& focusGraph() { return focusGraph_; }
    const FocusGraph& focusGraph() const { return focusGraph_; }
    InputRouter& inputRouter() { return *inputRouter_; }
    DisplayManager& displayManager() { return displayManager_; }
    const DisplayManager& displayManager() const { return displayManager_; }
    FramePacer& framePacer() { return framePacer_; }
    const FramePacer& framePacer() const { return framePacer_; }
    VisibilityObserver& visibilityObserver() { return visibilityObserver_; }
    const VisibilityObserver& visibilityObserver() const { return visibilityObserver_; }
    WorkloadController& workloadController() { return workloadController_; }
    const WorkloadController& workloadController() const { return workloadController_; }
    // Phase 8B.6: Temporal integrity observational probes
    TemporalAuditor& temporalAuditor() { return temporalAuditor_; }
    const TemporalAuditor& temporalAuditor() const { return temporalAuditor_; }
    TickCoherenceProbe& tickCoherenceProbe() { return tickCoherenceProbe_; }
    const TickCoherenceProbe& tickCoherenceProbe() const { return tickCoherenceProbe_; }
    PerformanceBudgetMonitor& performanceBudget() { return performanceBudget_; }
    const PerformanceBudgetMonitor& performanceBudget() const { return performanceBudget_; }
    GovernanceScheduler& governanceScheduler() { return governanceScheduler_; }
    const GovernanceScheduler& governanceScheduler() const { return governanceScheduler_; }

    // Drain governance: evaluate all dirty renderers through the controller.
    // Call after state-change events or on idle timer.
    void drainGovernance();

    // --- Scheduler config ---
    void setTargetFPS(int fps);
    void setCommitScheduler(RuntimeCommitScheduler* scheduler) { commitScheduler_ = scheduler; }
    RuntimeCommitScheduler* commitScheduler() const { return commitScheduler_; }
    HWND window() const { return ownerWindow_; }

private:
    // --- Phase 4: Extracted subsystems ---
    SurfaceRegistry surfaceRegistry_;
    ActivationManager activationManager_;
    FocusGraph focusGraph_;
    std::unique_ptr<InputRouter> inputRouter_;

    // --- Core systems ---
    CompositionClock clock_;
    MetricsCollector metrics_;
    ElevationManager elevationManager_;
    DisplayManager displayManager_;
    DragController dragController_;
    ResizeController resizeController_;
    DebugOverlay debugOverlay_;
    InvariantChecker invariantChecker_;
    RuntimeContractVerifier contractVerifier_;
    OrderingValidator orderingValidator_;
    TemporalValidator temporalValidator_;
    FailureInjector failureInjector_;
    ReplaySystem replaySystem_;
    RendererManager rendererManager_;
    RenderSkewTracker skewTracker_;
    InputPhotonTracker inputPhotonTracker_;
    CaptureManager captureManager_;
    FramePacer framePacer_;
    VisibilityObserver visibilityObserver_;
    WorkloadController workloadController_;
    GovernanceScheduler governanceScheduler_;
    // Phase 8B.6: Observational probes (do NOT affect orchestration)
    TemporalAuditor temporalAuditor_;
    TickCoherenceProbe tickCoherenceProbe_;
    PerformanceBudgetMonitor performanceBudget_;
    // --- Transaction ---
    CompositionTransaction currentTransaction_;

    // --- Scheduler state ---
    RuntimeCommitScheduler* commitScheduler_ = nullptr;
    UINT_PTR timerId_ = 0;
    bool framePending_ = false;
    bool initialized_ = false;
    int targetFPS_ = 60;
    bool elevationDirty_ = false;
    bool handlingActivation_ = false;  // Re-entrancy guard for WM_ACTIVATE
    HWND ownerWindow_ = nullptr;       // Main window — owner for all surface windows
    WorkspaceMode workspaceMode_ = WorkspaceMode::Normal;  // Phase 3E.3: future overview

    void processFrame();
    void applyTransaction(const CompositionTransaction& tx);
    void flushWindowPositions();

    // Timer callback
    static void CALLBACK TimerProc(HWND hwnd, UINT msg, UINT_PTR id, DWORD time);
    static Compositor* activeInstance_;
};

}  // namespace morphic

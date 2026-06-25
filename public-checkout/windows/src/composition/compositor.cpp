#include "compositor.h"
#include "../rendering/flutter_renderer.h"
#include "../rendering/flutter_renderer_adapter.h"
#include "../debug/frame_cadence_monitor.h"
#include "../core/thread_affinity.h"
#include "../core/runtime_commit_scheduler.h"
#include "../core/runtime_mutation_intent.h"
#include "../core/runtime_scene_state.h"
#include <algorithm>

namespace morphic {

Compositor* Compositor::activeInstance_ = nullptr;

Compositor::Compositor() {
    activeInstance_ = this;
    inputRouter_ = std::make_unique<InputRouter>(this);
}

Compositor::~Compositor() {
    shutdown();
    if (activeInstance_ == this) {
        activeInstance_ = nullptr;
    }
}

void Compositor::initialize(HWND ownerWindow) {
    if (initialized_) return;

    ownerWindow_ = ownerWindow;

    // Capture UI thread identity — all future assertions check against this.
    ThreadAffinity::initialize();

    // Phase 4: Initialize extracted subsystems
    surfaceRegistry_.initialize(ownerWindow, this);
    activationManager_.initialize(&surfaceRegistry_);

    clock_.start();
    metrics_.setTargetFPS(targetFPS_);
    displayManager_.refresh();
    debugOverlay_.initialize();

    initialized_ = true;

    if (ownerWindow_) {
        OutputDebugStringA("COMPOSITOR: Initialized with owner window (surfaces will be owned popups)\n");
    } else {
        OutputDebugStringA("COMPOSITOR: Initialized WITHOUT owner (surfaces will be independent — fallback)\n");
    }
}

void Compositor::shutdown() {
    if (!initialized_) return;

    // Phase 8B.6: Write instrumentation reports before teardown.
    // These go next to the exe so they're easy to find after a run.
    {
        char exePath[MAX_PATH] = {};
        GetModuleFileNameA(nullptr, exePath, MAX_PATH);
        std::string exeDir(exePath);
        auto lastSlash = exeDir.find_last_of("\\/");
        if (lastSlash != std::string::npos) exeDir = exeDir.substr(0, lastSlash + 1);

        std::string reportDir = exeDir + "temporal_reports\\";
        CreateDirectoryA(reportDir.c_str(), nullptr);

        temporalAuditor_.writeReport(reportDir + "temporal_audit.json");
        tickCoherenceProbe_.writeReport(reportDir + "tick_coherence.json");
        performanceBudget_.writeReport(reportDir + "performance_budget.json");

        OutputDebugStringA(("[PHASE 8B.6] Reports written to " + reportDir + "\n").c_str());
    }

    // Kill frame timer first
    if (timerId_) {
        KillTimer(nullptr, timerId_);
        timerId_ = 0;
    }

    // Phase 4: Delegate surface cleanup to SurfaceRegistry
    surfaceRegistry_.destroyAll();

    // Destroy debug overlay
    debugOverlay_.destroy();

    clock_.stop();
    initialized_ = false;
}

// --- Surface lifecycle ---

NodeId Compositor::createSurface(const SurfaceConfig& config) {
    MORPHIC_ASSERT_UI_THREAD();

    // Phase 4: Delegate to SurfaceRegistry (surface lifetime authority)
    NodeId id = surfaceRegistry_.createSurface(config);
    if (id == kInvalidNodeId) return kInvalidNodeId;

    elevationDirty_ = true;
    requestFrame();

    // Register node in FocusGraph
    FocusNode fn;
    fn.id = id;
    fn.domain = (config.role == SurfaceRole::Workspace) ? FocusDomain::Workspace : FocusDomain::Detached;
    fn.behavior = AttentionBehavior::Interactive; // Simplification for now
    fn.currentEligibility = config.visible ? FocusEligibility::Eligible : FocusEligibility::Hidden;
    fn.restorePolicy = FocusRestorePolicy::PreviousSemanticFocus;
    focusGraph_.registerNode(fn);

    // Register in activation manager's canonical workspace stack
    activationManager_.registerSurface(id);

    return id;
}

void Compositor::destroySurface(NodeId id) {
    MORPHIC_ASSERT_UI_THREAD();

    if (commitScheduler_) {
        commitScheduler_->invalidateSurface(id);
        commitScheduler_->sceneState().clearState(id);
    }

    // Renderer cleanup MUST happen before surface destruction.
    // SurfaceRegistry does NOT own renderer lifecycle.
    auto* host = surfaceRegistry_.getHost(id);
    if (host) {
        RenderId rendId = host->activeRendererId();
        if (rendId != kInvalidRenderId) {
            host->unbindRenderer();
        }

        // Clean renderer from ALL tracking systems
        for (const auto& [rid, rec] : rendererManager_.records()) {
            if (rec.surfaceId == id) {
                OutputDebugStringA(("DESTROY_SURFACE: Cleaning renderer #" +
                    std::to_string(rid) + " from surface #" +
                    std::to_string(id) + "\n").c_str());

                rendererManager_.transitionActivity(rid, ActivityState::Destroying);
                governanceScheduler_.untrackRenderer(rid);
                workloadController_.unregisterRenderer(rid);
                FrameCadenceMonitor::instance().removeEngine(static_cast<int>(rid));

                auto* renderer = rendererManager_.getRenderer(rid);
                if (renderer && renderer->type() == RendererSurface::Type::Flutter) {
                    auto* fr = static_cast<FlutterRenderer*>(renderer);
                    if (fr->isAlive()) {
                        fr->hideWithoutDestroy();
                    }
                }
                rendererManager_.removeRenderer(rid);
                break;
            }
        }
    }

    // Remove from activation manager's canonical workspace stack
    activationManager_.unregisterSurface(id);

    // Phase 4: Delegate surface destruction to SurfaceRegistry
    surfaceRegistry_.destroySurface(id);
    requestFrame();
}

// --- Surface role (Phase 3E.3) ---

void Compositor::setSurfaceRole(NodeId surfaceId, SurfaceRole role) {
    MORPHIC_ASSERT_UI_THREAD();

    SurfaceRole oldRole = surfaceRegistry_.surfaceRole(surfaceId);
    if (oldRole == role) return;

    // Phase 4: Delegate role change to SurfaceRegistry (surface + topology)
    surfaceRegistry_.setSurfaceRole(surfaceId, role);

    // Notify governance of role change (orchestration concern, stays here)
    auto* host = surfaceRegistry_.getHost(surfaceId);
    if (host) {
        RenderId rendId = host->activeRendererId();
        if (rendId != kInvalidRenderId) {
            auto* rec = rendererManager_.getMutableRecord(rendId);
            if (rec) {
                rec->surfaceRole = role;
            }
            governanceScheduler_.markDirty(rendId, GovernanceDirtyReason::RoleChanged);
        }
    }
}

SurfaceRole Compositor::surfaceRole(NodeId surfaceId) const {
    return surfaceRegistry_.surfaceRole(surfaceId);
}

// --- Renderer integration (Phase 2A) ---
// Lifecycle: Manager registers (owns) -> Host binds (borrows) -> Manager transitions state

bool Compositor::attachRenderer(NodeId surfaceId,
                                 std::unique_ptr<RendererSurface> renderer) {
    MORPHIC_ASSERT_UI_THREAD();
    auto* host = surfaceRegistry_.getHost(surfaceId);
    if (!host) {
        OutputDebugStringA("COMPOSITOR: attachRenderer — surface not found\n");
        return false;
    }

    // 1. Register with manager (transfers ownership)
    RenderId rendId = rendererManager_.registerRenderer(std::move(renderer), surfaceId);

    // 2. Get raw pointer for host to borrow
    RendererSurface* rawPtr = rendererManager_.getRenderer(rendId);
    if (!rawPtr) {
        OutputDebugStringA("COMPOSITOR: attachRenderer — registration failed\n");
        return false;
    }

    // 3. Bind to WindowHost (triggers create())
    host->bindRenderer(rawPtr, rendId);

    // 4. Transition state based on create success
    if (rawPtr->isCreated()) {
        rendererManager_.markRunning(rendId);

        // 5. Phase 2B: Register adapter with WorkloadController for orchestration.
        if (rawPtr->type() == RendererSurface::Type::Flutter) {
            auto* flutterRenderer = static_cast<FlutterRenderer*>(rawPtr);
            auto adapter = std::make_unique<FlutterRendererAdapter>(flutterRenderer->engine());
            workloadController_.registerRenderer(rendId, std::move(adapter));
            governanceScheduler_.trackRenderer(rendId);
            rendererManager_.setVisibility(rendId, VisibilityState::Visible);
            OutputDebugStringA(("COMPOSITOR: Registered FlutterRendererAdapter for #" +
                std::to_string(rendId) + "\n").c_str());
        } else {
            auto adapter = std::make_unique<NullRendererAdapter>();
            workloadController_.registerRenderer(rendId, std::move(adapter));
            governanceScheduler_.trackRenderer(rendId);
        }

        OutputDebugStringA(("COMPOSITOR: Renderer #" + std::to_string(rendId) +
            " -> RUNNING on Surface #" + std::to_string(surfaceId) + "\n").c_str());
    } else {
        rendererManager_.markFailed(rendId);
        host->unbindRenderer();
        OutputDebugStringA(("COMPOSITOR: Renderer #" + std::to_string(rendId) +
            " -> FAILED on Surface #" + std::to_string(surfaceId) + "\n").c_str());
        return false;
    }

    return true;
}

void Compositor::detachRenderer(NodeId surfaceId) {
    MORPHIC_ASSERT_UI_THREAD();
    auto* host = surfaceRegistry_.getHost(surfaceId);
    if (!host) return;

    RenderId rendId = host->activeRendererId();
    if (rendId == kInvalidRenderId) return;

    RendererSurface* renderer = rendererManager_.getRenderer(rendId);

    // 1. Unbind from WindowHost (clears borrowed reference)
    host->unbindRenderer();

    // 2. Transition lifecycle through Manager
    if (renderer) {
        if (renderer->type() == RendererSurface::Type::Flutter) {
            // Flutter: hide without destroy (VM safety)
            auto* fr = static_cast<FlutterRenderer*>(renderer);
            fr->hideWithoutDestroy();
            rendererManager_.markHidden(rendId);
            rendererManager_.markZombie(rendId);
            OutputDebugStringA(("COMPOSITOR: Renderer #" + std::to_string(rendId) +
                " -> ZOMBIE (Flutter, VM safety)\n").c_str());
        } else {
            // Non-Flutter: safe to destroy
            renderer->destroy();
            rendererManager_.markZombie(rendId);  // Still tracked for metrics
            OutputDebugStringA(("COMPOSITOR: Renderer #" + std::to_string(rendId) +
                " -> DESTROYED\n").c_str());
        }
    }
}

// --- Phase X: HWND census helper ---
namespace {
    struct HwndCensusData {
        DWORD processId;
        int count;
    };
    BOOL CALLBACK CountProcessHwnds(HWND hwnd, LPARAM lParam) {
        auto* data = reinterpret_cast<HwndCensusData*>(lParam);
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid == data->processId) {
            data->count++;
        }
        return TRUE;
    }
    int countProcessTopLevelHwnds() {
        HwndCensusData data;
        data.processId = GetCurrentProcessId();
        data.count = 0;
        EnumWindows(CountProcessHwnds, reinterpret_cast<LPARAM>(&data));
        return data.count;
    }
}

// --- Phase X: 7-stage Hard Destruction ---
Compositor::HardDestroyResult Compositor::hardDetachRenderer(NodeId surfaceId) {
    MORPHIC_ASSERT_UI_THREAD();
    HardDestroyResult result;

    OutputDebugStringA(("HARD_DESTROY: === BEGIN surface #" +
        std::to_string(surfaceId) + " ===\n").c_str());

    // HWND census BEFORE
    result.hwndsBefore = countProcessTopLevelHwnds();
    OutputDebugStringA(("HARD_DESTROY: HWNDs before = " +
        std::to_string(result.hwndsBefore) + "\n").c_str());

    // Resolve renderer
    auto* host = surfaceRegistry_.getHost(surfaceId);
    if (!host) {
        result.error = "surface not found";
        OutputDebugStringA("HARD_DESTROY: FAILED — surface not found\n");
        return result;
    }

    RenderId rendId = host->activeRendererId();
    if (rendId == kInvalidRenderId) {
        result.error = "no active renderer";
        OutputDebugStringA("HARD_DESTROY: FAILED — no active renderer\n");
        return result;
    }

    RendererSurface* renderer = rendererManager_.getRenderer(rendId);
    if (!renderer || renderer->type() != RendererSurface::Type::Flutter) {
        result.error = "not a Flutter renderer";
        OutputDebugStringA("HARD_DESTROY: FAILED — not Flutter\n");
        return result;
    }

    auto* fr = static_cast<FlutterRenderer*>(renderer);
    HWND flutterHwnd = fr->childHwnd();
    HWND parentHwnd = host->hwnd();

    // ========================================
    // Stage 0: Enter Destroying state
    // ========================================
    OutputDebugStringA("HARD_DESTROY: Stage 0 — Destroying state\n");
    rendererManager_.transitionActivity(rendId, ActivityState::Destroying);

    // ========================================
    // Stage 1: Detach from governance
    // ========================================
    OutputDebugStringA("HARD_DESTROY: Stage 1 — Governance detach\n");
    governanceScheduler_.untrackRenderer(rendId);
    workloadController_.unregisterRenderer(rendId);

    // Remove from FrameCadenceMonitor (uses Dart engine ID, not RenderId)
    // Engine IDs are stored in the cadence monitor by Dart-side registration.
    // We must enumerate and remove any entry whose RenderId matches.
    // For now, we remove by RenderId since that's what we have.
    FrameCadenceMonitor::instance().removeEngine(static_cast<int>(rendId));

    // ========================================
    // Stage 2: Block new messages
    // ========================================
    OutputDebugStringA("HARD_DESTROY: Stage 2 — Block messages\n");
    // isAlive_ is already set false by destroy(), but set it early
    // to block any callbacks during remaining stages
    fr->setRendererId(kInvalidRenderId);  // poison the ID

    // ========================================
    // Stage 3: Release focus/capture
    // ========================================
    OutputDebugStringA("HARD_DESTROY: Stage 3 — Release focus/capture\n");
    if (flutterHwnd) {
        if (GetCapture() == flutterHwnd) ReleaseCapture();
        if (GetFocus() == flutterHwnd) SetFocus(nullptr);
    }
    if (parentHwnd) {
        if (GetCapture() == parentHwnd) ReleaseCapture();
        if (GetFocus() == parentHwnd) SetFocus(nullptr);
    }

    // ========================================
    // Stage 4: Unbind from host + destroy child HWNDs
    // ========================================
    OutputDebugStringA("HARD_DESTROY: Stage 4 — Unbind + child HWND kill\n");
    host->unbindRenderer();

    // Enumerate and destroy all child HWNDs of the surface window
    if (parentHwnd && IsWindow(parentHwnd)) {
        std::vector<HWND> children;
        EnumChildWindows(parentHwnd, [](HWND child, LPARAM lParam) -> BOOL {
            auto* vec = reinterpret_cast<std::vector<HWND>*>(lParam);
            vec->push_back(child);
            return TRUE;
        }, reinterpret_cast<LPARAM>(&children));

        for (HWND child : children) {
            if (IsWindow(child)) {
                OutputDebugStringA(("HARD_DESTROY: Destroying child HWND " +
                    std::to_string(reinterpret_cast<uintptr_t>(child)) + "\n").c_str());
                DestroyWindow(child);
            }
        }
    }

    // ========================================
    // Stage 5: Destroy Flutter ViewController
    // ========================================
    OutputDebugStringA("HARD_DESTROY: Stage 5 — ViewController destroy\n");
    fr->destroy();  // Calls FlutterDesktopViewControllerDestroy

    // ========================================
    // Stage 6: Remove from RendererManager (erases ownership)
    // ========================================
    OutputDebugStringA("HARD_DESTROY: Stage 6 — Manager removal\n");
    auto removedPtr = rendererManager_.removeRenderer(rendId);
    // removedPtr goes out of scope here — unique_ptr destructor runs
    // but destroy() was already called, so ~FlutterRenderer is safe

    // ========================================
    // Stage 7: Post-destruction validation
    // ========================================
    OutputDebugStringA("HARD_DESTROY: Stage 7 — Validation\n");

    // Validate Flutter HWND destroyed
    if (flutterHwnd && IsWindow(flutterHwnd)) {
        OutputDebugStringA("HARD_DESTROY: WARNING — Flutter HWND still alive!\n");
        // Force destroy
        DestroyWindow(flutterHwnd);
    }

    // HWND census AFTER
    result.hwndsAfter = countProcessTopLevelHwnds();
    result.destroyed = true;

    OutputDebugStringA(("HARD_DESTROY: === DONE surface #" +
        std::to_string(surfaceId) +
        " HWNDs: " + std::to_string(result.hwndsBefore) +
        " -> " + std::to_string(result.hwndsAfter) +
        " ===\n").c_str());

    return result;
}

// --- Group lifecycle ---

NodeId Compositor::createGroup(const std::vector<NodeId>& memberIds) {
    return surfaceRegistry_.sceneGraph().createGroup(memberIds);
}

void Compositor::destroyGroup(NodeId groupId) {
    surfaceRegistry_.sceneGraph().destroyGroup(groupId);
}

// --- Transactions ---

CompositionTransaction& Compositor::beginTransaction() {
    currentTransaction_.clear();
    return currentTransaction_;
}

void Compositor::commitTransaction() {
    if (currentTransaction_.isEmpty()) return;
    applyTransaction(currentTransaction_);
    currentTransaction_.clear();
    requestFrame();
}

void Compositor::applyTransaction(const CompositionTransaction& tx) {
    for (const auto& op : tx.operations()) {
        auto* s = surfaceRegistry_.sceneGraph().getSurface(op.targetId);
        if (!s) continue;

        switch (op.type) {
            case CompositionTransaction::Op::Move:
                s->setPosition(op.x, op.y);
                break;
            case CompositionTransaction::Op::Resize: {
                // Enforce constraints — this prevents invariant violations
                const auto& c = s->constraints();
                int w = op.w;
                int h = op.h;
                if (c.minWidth > 0 && w < c.minWidth) w = c.minWidth;
                if (c.minHeight > 0 && h < c.minHeight) h = c.minHeight;
                if (c.maxWidth > 0 && w > c.maxWidth) w = c.maxWidth;
                if (c.maxHeight > 0 && h > c.maxHeight) h = c.maxHeight;
                s->setSize(w, h);
                break;
            }
            case CompositionTransaction::Op::SetElevation:
                s->setElevation(op.layer, op.sublevel);
                elevationDirty_ = true;
                break;
            case CompositionTransaction::Op::Show:
                s->setVisible(true);
                break;
            case CompositionTransaction::Op::Hide:
                s->setVisible(false);
                break;
            default:
                break;
        }
    }
}

// --- Convenience mutations ---

void Compositor::moveSurface(NodeId id, int x, int y) {
    inputPhotonTracker_.recordCompositorProcess(clock_.frameCount());
    
    if (commitScheduler_) {
        auto* s = surfaceRegistry_.sceneGraph().getSurface(id);
        if (!s) return;
        RuntimeMutationIntent intent;
        intent.type = RuntimeMutationIntent::Type::GeometryChange;
        intent.surfaceId = id;
        intent.priority = MutationPriority::Interactive;
        Transform geom = s->localTransform();
        geom.x = x;
        geom.y = y;
        intent.geometry = geom;
        commitScheduler_->enqueueIntent(intent);
        requestFrame();
        return;
    }

    auto& tx = beginTransaction();
    tx.move(id, x, y);
    commitTransaction();
}

void Compositor::resizeSurface(NodeId id, int w, int h) {
    if (commitScheduler_) {
        auto* s = surfaceRegistry_.sceneGraph().getSurface(id);
        if (!s) return;

        const auto& c = s->constraints();
        if (c.minWidth > 0 && w < c.minWidth) w = c.minWidth;
        if (c.minHeight > 0 && h < c.minHeight) h = c.minHeight;
        if (c.maxWidth > 0 && w > c.maxWidth) w = c.maxWidth;
        if (c.maxHeight > 0 && h > c.maxHeight) h = c.maxHeight;

        RuntimeMutationIntent intent;
        intent.type = RuntimeMutationIntent::Type::GeometryChange;
        intent.surfaceId = id;
        intent.priority = MutationPriority::Interactive;
        Transform geom = s->localTransform();
        geom.width = w;
        geom.height = h;
        intent.geometry = geom;
        commitScheduler_->enqueueIntent(intent);
        requestFrame();
        return;
    }

    auto& tx = beginTransaction();
    tx.resize(id, w, h);
    commitTransaction();
}

// --- Frame scheduling ---

void Compositor::requestFrame() {
    metrics_.recordFrameRequest();
    if (framePending_) return;
    framePending_ = true;

    // Start timer if not running
    if (!timerId_) {
        int intervalMs = 1000 / targetFPS_;
        timerId_ = SetTimer(nullptr, 0, intervalMs, TimerProc);
    }
}

void Compositor::tick() {
    processFrame();
}

void Compositor::processFrame() {
    MORPHIC_ASSERT_UI_THREAD();
    if (!initialized_) return;

    // Phase 8B.6: QPC-stamp frame start for cost measurement
    LARGE_INTEGER frameStartQpc_;
    QueryPerformanceCounter(&frameStartQpc_);

    clock_.tick();
    metrics_.beginFrame();
    skewTracker_.beginFrame(clock_.frameCount());
    framePacer_.onFrameBegin();

    // Failure injection: artificial scheduler stall
    int stallMs = failureInjector_.getSchedulerStall();
    if (stallMs > 0) {
        OutputDebugStringA(("INJECTED STALL: " + std::to_string(stallMs) + "ms\n").c_str());
        Sleep(stallMs);
    }

    if (commitScheduler_) {
        // Phase 9 execution kernel: run scheduler tick and update skew
        commitScheduler_->tick();

        // Record render skew for all active renderers (Stage 4 diagnostic)
        for (const auto& [id, host] : surfaceRegistry_.hosts()) {
            if (!host || !host->hasRenderer()) continue;
            auto rendId = host->activeRendererId();
            auto* renderer = host->renderer();
            if (!renderer || rendId == kInvalidRenderId) continue;
            const auto& rm = renderer->metrics();
            skewTracker_.recordPresent(rendId, id, rm.lastProducedFrame, rm.presentLatencyMs);
            skewTracker_.recordCommit(rendId, clock_.frameCount());
        }
        skewTracker_.endFrame();

        // Drain governance on every frame tick
        {
            LARGE_INTEGER govStart, govEnd;
            QueryPerformanceCounter(&govStart);
            drainGovernance();
            QueryPerformanceCounter(&govEnd);
            LARGE_INTEGER govFreq;
            QueryPerformanceFrequency(&govFreq);
            double govMs = (govEnd.QuadPart - govStart.QuadPart) * 1000.0 /
                           static_cast<double>(govFreq.QuadPart);
            tickCoherenceProbe_.recordGovernanceCost(govMs);
        }

        metrics_.endFrame();
        framePacer_.onFrameEnd();

        // Phase 8B.6: Record frame cost and feed temporal auditor
        {
            LARGE_INTEGER frameEndQpc_;
            QueryPerformanceCounter(&frameEndQpc_);
            LARGE_INTEGER freq_;
            QueryPerformanceFrequency(&freq_);
            double frameCostMs = (frameEndQpc_.QuadPart - frameStartQpc_.QuadPart) * 1000.0 /
                                 static_cast<double>(freq_.QuadPart);
            tickCoherenceProbe_.recordFrameCost(frameCostMs);

            // Determine current interaction phase for attribution
            InteractionPhase currentPhase = InteractionPhase::Idle;
            if (dragController_.isDragging()) currentPhase = InteractionPhase::Drag;
            temporalAuditor_.recordTick(frameCostMs, currentPhase);
        }

        framePending_ = false;
        bool needsContinuousUpdates = dragController_.isDragging() ||
                                       debugOverlay_.isEnabled() ||
                                       workloadController_.wakeQueueDepth() > 0 ||
                                       commitScheduler_->metrics().queueDepthAfterCommit > 0;
        
        if (needsContinuousUpdates && !timerId_) {
            int intervalMs = 1000 / targetFPS_;
            timerId_ = SetTimer(nullptr, 0, intervalMs, TimerProc);
        }
        return;
    }

    // Stage 1: Resolve dirty nodes (compute world transforms)
    auto t1 = metrics_.stageTimer();
    surfaceRegistry_.sceneGraph().resolveDirty();
    metrics_.recordStageSceneResolve(t1);

    // Stage 2: Collect dirty surfaces
    auto dirtySurfaces = surfaceRegistry_.sceneGraph().getDirtySurfaces();
    metrics_.recordDirtyNodes(static_cast<int>(dirtySurfaces.size()));
    metrics_.recordHwndCount(static_cast<int>(surfaceRegistry_.hosts().size()));

    // Stage 3: Batch flush window positions
    if (!dirtySurfaces.empty()) {
        auto t3 = metrics_.stageTimer();
        metrics_.recordDwmCommitStart();
        flushWindowPositions();
        metrics_.recordDwmCommitEnd();
        metrics_.recordStageDwmFlush(t3);
    }

    // Stage 4: Measure sync error — scene graph vs actual HWND positions
    {
        double maxDiv = 0.0;
        for (const auto& [id, host] : surfaceRegistry_.hosts()) {
            if (!host || !host->isAlive()) continue;
            auto* surface = surfaceRegistry_.sceneGraph().getSurface(id);
            if (!surface) continue;

            RECT wr;
            GetWindowRect(host->hwnd(), &wr);
            const auto& wt = surface->worldTransform();

            double dx = std::abs(wt.x - static_cast<int>(wr.left));
            double dy = std::abs(wt.y - static_cast<int>(wr.top));
            double dw = std::abs(wt.width - static_cast<int>(wr.right - wr.left));
            double dh = std::abs(wt.height - static_cast<int>(wr.bottom - wr.top));

            double divergence = (std::max)({dx, dy, dw, dh});
            if (divergence > maxDiv) maxDiv = divergence;
        }
        metrics_.recordPositionalDivergence(maxDiv);
    }

    // Record render skew for all active renderers
    for (const auto& [id, host] : surfaceRegistry_.hosts()) {
        if (!host || !host->hasRenderer()) continue;
        auto rendId = host->activeRendererId();
        auto* renderer = host->renderer();
        if (!renderer || rendId == kInvalidRenderId) continue;
        const auto& rm = renderer->metrics();
        skewTracker_.recordPresent(rendId, id, rm.lastProducedFrame, rm.presentLatencyMs);
        skewTracker_.recordCommit(rendId, clock_.frameCount());
    }
    skewTracker_.endFrame();

    // Stage 5: Resolve z-order if needed
    if (elevationDirty_) {
        auto t5 = metrics_.stageTimer();
        elevationManager_.resolveZOrder(surfaceRegistry_.sceneGraph());
        elevationDirty_ = false;
        metrics_.recordStageElevation(t5);

        // Validate z-order matches elevation (debug only)
#ifdef _DEBUG
        auto orderResult = orderingValidator_.validate(surfaceRegistry_.sceneGraph(), surfaceRegistry_.hosts());
        if (!orderResult.consistent) {
            std::string msg = "Z-ORDER MISMATCH: expected=[" + orderResult.expectedOrder +
                "] actual=[" + orderResult.actualOrder + "] " + orderResult.mismatchDetails + "\n";
            OutputDebugStringA(msg.c_str());
        }
#endif
    }

    // Stage 6: Clear dirty flags
    surfaceRegistry_.sceneGraph().clearAllDirty();

    // Stage 7: Validate invariants (debug builds only)
#ifdef _DEBUG
    {
        auto t7 = metrics_.stageTimer();
        auto violations = invariantChecker_.validateAll(
            surfaceRegistry_.sceneGraph(), surfaceRegistry_.hosts(), clock_.frameCount());
        metrics_.recordStageInvariantCheck(t7);

        for (const auto& v : violations) {
            std::string msg = "INVARIANT VIOLATION [frame " +
                std::to_string(v.frame) + "] " + v.invariant + ": " + v.details + "\n";
            OutputDebugStringA(msg.c_str());
        }

        // Temporal convergence: track how long discrepancies persist
        temporalValidator_.validate(surfaceRegistry_.sceneGraph(), surfaceRegistry_.hosts(), clock_.frameCount());

        // Surface Semantics Spec v1.1: Contract verification.
        // Check z-order tier law, shell participation, and domain invariants.
        // Sampled every 30 frames to reduce overhead.
        if (clock_.frameCount() % 30 == 0) {
            contractVerifier_.validateAll(
                surfaceRegistry_.sceneGraph(), surfaceRegistry_.hosts(), clock_.frameCount());
        }
    }
#endif

    // Stage 8: Update debug overlay
    if (debugOverlay_.isEnabled()) {
        auto t8 = metrics_.stageTimer();
        debugOverlay_.render(surfaceRegistry_.sceneGraph(), metrics_, commitScheduler_);
        metrics_.recordStageDebugOverlay(t8);
    }

    // Phase 3D: Governance drain on every frame tick.
    // This ensures stagger queue drains within frame intervals (~16ms).
    {
        LARGE_INTEGER govStart, govEnd;
        QueryPerformanceCounter(&govStart);
        drainGovernance();
        QueryPerformanceCounter(&govEnd);
        LARGE_INTEGER govFreq;
        QueryPerformanceFrequency(&govFreq);
        double govMs = (govEnd.QuadPart - govStart.QuadPart) * 1000.0 /
                       static_cast<double>(govFreq.QuadPart);
        tickCoherenceProbe_.recordGovernanceCost(govMs);
    }

    metrics_.endFrame();
    framePacer_.onFrameEnd();

    // Phase 8B.6: Record frame cost and feed temporal auditor (legacy path)
    {
        LARGE_INTEGER frameEndQpc_;
        QueryPerformanceCounter(&frameEndQpc_);
        LARGE_INTEGER freq_;
        QueryPerformanceFrequency(&freq_);
        double frameCostMs = (frameEndQpc_.QuadPart - frameStartQpc_.QuadPart) * 1000.0 /
                             static_cast<double>(freq_.QuadPart);
        tickCoherenceProbe_.recordFrameCost(frameCostMs);

        InteractionPhase currentPhase = InteractionPhase::Idle;
        if (dragController_.isDragging()) currentPhase = InteractionPhase::Drag;
        if (elevationDirty_) currentPhase = InteractionPhase::Activation;
        temporalAuditor_.recordTick(frameCostMs, currentPhase);
    }

    // Determine if we should keep the timer alive
    framePending_ = false;
    bool needsContinuousUpdates = dragController_.isDragging() ||
                                   debugOverlay_.isEnabled() ||
                                   workloadController_.wakeQueueDepth() > 0;
    // Block 2: Track timer churn from wake queue
    if (workloadController_.wakeQueueDepth() > 0 && !dragController_.isDragging() && !debugOverlay_.isEnabled()) {
        workloadController_.lastTimingsMut().timerKeptAlive++;
    }
    if (!needsContinuousUpdates && timerId_) {
        KillTimer(nullptr, timerId_);
        timerId_ = 0;
    }
}

void Compositor::flushWindowPositions() {
    LARGE_INTEGER flushStart;
    QueryPerformanceCounter(&flushStart);

    auto dirtySurfaces = surfaceRegistry_.sceneGraph().getDirtySurfaces();
    if (dirtySurfaces.empty()) return;

    int count = 0;
    for (auto* s : dirtySurfaces) {
        if (s->hasHost() && s->host()->isAlive()) count++;
    }

    if (count == 0) return;

    // Failure injection: simulate DeferWindowPos failure
    bool useFallback = failureInjector_.shouldFailDeferWindowPos();

    if (!useFallback) {
        // Normal path: Use DeferWindowPos for atomic batch update
        HDWP hdwp = BeginDeferWindowPos(count);
        if (!hdwp) {
            useFallback = true;  // Real failure — use fallback
        } else {
            int deferCount = 0;
            for (auto* s : dirtySurfaces) {
                if (!s->hasHost() || !s->host()->isAlive()) continue;

                auto& wt = s->worldTransform();
                UINT flags = SWP_NOZORDER | SWP_NOACTIVATE;

                if (s->isVisible()) {
                    flags |= SWP_SHOWWINDOW;
                } else {
                    flags |= SWP_HIDEWINDOW;
                }

                hdwp = DeferWindowPos(hdwp, s->host()->hwnd(), nullptr,
                                      wt.x, wt.y, wt.width, wt.height, flags);
                if (!hdwp) { useFallback = true; break; }
                deferCount++;
            }

            if (!useFallback) {
                metrics_.recordDeferCount(deferCount);
                EndDeferWindowPos(hdwp);
            }
        }
    }

    if (useFallback) {
        // Fallback: individual SetWindowPos calls (slower but always works)
        OutputDebugStringA("FALLBACK: DeferWindowPos failed, using individual SetWindowPos\n");
        int updateCount = 0;
        for (auto* s : dirtySurfaces) {
            if (!s->hasHost() || !s->host()->isAlive()) continue;
            auto& wt = s->worldTransform();
            UINT flags = SWP_NOZORDER | SWP_NOACTIVATE;
            if (s->isVisible()) flags |= SWP_SHOWWINDOW;
            else flags |= SWP_HIDEWINDOW;
            SetWindowPos(s->host()->hwnd(), nullptr,
                         wt.x, wt.y, wt.width, wt.height, flags);
            updateCount++;
        }
        metrics_.recordDeferCount(updateCount);
    }

    // Phase 8B.6: Record SetWindowPos / DeferWindowPos cost
    LARGE_INTEGER flushEnd;
    QueryPerformanceCounter(&flushEnd);
    LARGE_INTEGER flushFreq;
    QueryPerformanceFrequency(&flushFreq);
    double flushMs = (flushEnd.QuadPart - flushStart.QuadPart) * 1000.0 /
                     static_cast<double>(flushFreq.QuadPart);
    tickCoherenceProbe_.recordFlushPositionsCost(flushMs);
}

// --- Event handlers ---

void Compositor::onDragBegin(NodeId surfaceId, POINT screenPos) {
    if (!initialized_) return;
    if (replaySystem_.isRecording()) {
        replaySystem_.recordEvent({0, ReplayEvent::DragBegin, surfaceId, screenPos.x, screenPos.y});
    }

    // Acquire drag capture through CaptureManager
    HWND hwnd = nullptr;
    auto* dragHost = surfaceRegistry_.getHost(surfaceId);
    if (dragHost) hwnd = dragHost->hwnd();
    captureManager_.acquireCapture(surfaceId, CaptureManager::CaptureType::Drag, hwnd);

    // Obsolete: focusManager_.suppressDuringDrag();

    dragController_.beginDrag(surfaceRegistry_.sceneGraph(), surfaceId, screenPos);
    if (!handlingActivation_) {
        handlingActivation_ = true;
        elevationManager_.bringToFront(surfaceRegistry_.sceneGraph(), surfaceId);
        handlingActivation_ = false;
    }
}

void Compositor::onDragUpdate(NodeId surfaceId, POINT screenPos) {
    // Phase 8B.6: Record pointer event for pointer→projection latency
    tickCoherenceProbe_.recordPointerEvent();

    auto updates = dragController_.updateDrag(screenPos);

    if (updates.empty()) return;

    // Apply all moves via transaction
    auto& tx = beginTransaction();
    for (auto& u : updates) {
        tx.move(u.surfaceId, u.newX, u.newY);
    }
    applyTransaction(tx);
    currentTransaction_.clear();

    if (replaySystem_.isRecording()) {
        replaySystem_.recordEvent({0, ReplayEvent::DragUpdate, surfaceId, screenPos.x, screenPos.y});
    }

    processFrame();

    // Phase 8B.6: Record projection complete (SetWindowPos has finished in processFrame)
    tickCoherenceProbe_.recordProjectionComplete(InteractionPhase::Drag);
}

void Compositor::onDragEnd(NodeId surfaceId, POINT screenPos) {
    if (!initialized_) return;
    if (replaySystem_.isRecording()) {
        replaySystem_.recordEvent({0, ReplayEvent::DragEnd, surfaceId, screenPos.x, screenPos.y});
    }
    dragController_.endDrag();

    // Release drag capture and resume focus transitions
    captureManager_.releaseCapture(surfaceId);
    // Obsolete: focusManager_.resumeAfterDrag();
}

void Compositor::onSurfaceResized(NodeId surfaceId, int width, int height) {
    if (!initialized_) return;
    if (replaySystem_.isRecording()) {
        replaySystem_.recordEvent({0, ReplayEvent::Resize, surfaceId, 0, 0, width, height});
    }
    resizeController_.onSurfaceResized(surfaceRegistry_.sceneGraph(), surfaceId, width, height);
    requestFrame();
}

void Compositor::onSurfaceActivated(NodeId surfaceId) {
    if (!initialized_) return;
    // Guard against re-entrant activation.
    if (handlingActivation_) return;

    HWND hwnd = nullptr;
    RenderId rendId = 0;
    auto* actHost = surfaceRegistry_.getHost(surfaceId);
    if (actHost) {
        hwnd = actHost->hwnd();
        rendId = actHost->activeRendererId();
    }
    // Phase 4 Step 3: Win32 activation observation
    focusGraph_.commitRealizedActivation(surfaceId, 0);

    handlingActivation_ = true;

    // DOMAIN CHECK: ElevationManager is tier-unaware — it sorts ALL surfaces
    // by sublevel, ignoring Grouped < Floating < Overlay. For Independent
    // (Desktop-domain) surfaces, Win32 owns z-order. Running bringToFront
    // here corrupts compositor-domain tier ordering (V-Z1/V-Z2 violation).
    {
        bool isDesktopDomain = false;
        if (actHost) {
            auto traits = traitsForRole(actHost->currentRole());
            isDesktopDomain = (traits.zOrder == ZOrderPolicy::Independent);
        }
        if (!isDesktopDomain) {
            elevationManager_.bringToFront(surfaceRegistry_.sceneGraph(), surfaceId);
            elevationDirty_ = true;
        }
    }
    requestFrame();

    // Phase 4: Delegate z-order realization to ActivationManager
    activationManager_.onSurfaceActivated(surfaceId);

#ifdef _DEBUG
    auto orderResult = orderingValidator_.validate(surfaceRegistry_.sceneGraph(), surfaceRegistry_.hosts());
    if (!orderResult.consistent) {
        std::string msg = "Z-ORDER MISMATCH (activation S" + std::to_string(surfaceId) +
            "): expected=[" + orderResult.expectedOrder +
            "] actual=[" + orderResult.actualOrder + "] " + orderResult.mismatchDetails + "\n";
        OutputDebugStringA(msg.c_str());
    }

    // Surface Semantics Spec v1.1: Activation contract check.
    // Detect V-A3: Desktop-domain surface should NOT have run tier realization.
    {
        bool tierRealizationRan = true;
        auto* host = surfaceRegistry_.getHost(surfaceId);
        if (host) {
            auto traits = traitsForRole(host->currentRole());
            tierRealizationRan = (traits.zOrder != ZOrderPolicy::Independent);
        }
        contractVerifier_.checkActivationContract(
            surfaceId, surfaceRegistry_.hosts(), tierRealizationRan, clock_.frameCount());
    }
#endif

    handlingActivation_ = false;
}

void Compositor::onMainWindowActivated() {
    if (!initialized_) return;

    // Phase 4: Delegate z-order realization to ActivationManager
    activationManager_.onMainWindowActivated();
}

// --- Debug ---

void Compositor::setDebugOverlay(bool enabled) {
    debugOverlay_.setEnabled(enabled);
    if (enabled) requestFrame();
}

bool Compositor::isDebugEnabled() const {
    return debugOverlay_.isEnabled();
}

// --- Display ---

void Compositor::refreshDisplays() {
    displayManager_.refresh();
}

const std::vector<DisplayInfo>& Compositor::displays() const {
    return displayManager_.displays();
}

// --- Config ---

void Compositor::setTargetFPS(int fps) {
    targetFPS_ = fps;
    metrics_.setTargetFPS(fps);
}

// (createHostForSurface / destroyHostForSurface removed — now in SurfaceRegistry)

// --- Timer callback ---

void CALLBACK Compositor::TimerProc(HWND, UINT, UINT_PTR, DWORD) {
    if (activeInstance_) {
        // Phase 8B.6: Record timer arrival for cadence diagnostics
        activeInstance_->tickCoherenceProbe_.recordTimerArrival();
        activeInstance_->processFrame();
    }
}

// --- Governance drain ---

void Compositor::drainGovernance() {
    LARGE_INTEGER tDrainStart;
    QueryPerformanceCounter(&tDrainStart);

    // Phase 3D: Stagger dispatch runs UNCONDITIONALLY.
    // It has its own internal timing — it does NOT depend on dirty flags.
    // If we gated this behind hasDirty(), queued wakes would sit forever
    // after the initial dirty flags are cleared.
    if (workloadController_.wakeQueueDepth() > 0) {
        workloadController_.dispatchPendingWakes();

        // Sync rendererManager for any wakes that were dispatched
        for (const auto& [rendId, rec] : rendererManager_.records()) {
            auto* orch = workloadController_.getOrchState(rendId);
            if (orch && orch->currentActivity == ActivityState::Active &&
                rec.activity != ActivityState::Active) {
                rendererManager_.transitionActivity(rendId, ActivityState::Active);
            }
        }

        // If queue still has items, re-mark dirty so we get called again
        if (workloadController_.wakeQueueDepth() > 0) {
            for (const auto& [rendId, rec] : rendererManager_.records()) {
                governanceScheduler_.markDirty(
                    rendId, GovernanceDirtyReason::WorkloadChanged);
            }
        }
    }

    // Recompute adaptive params periodically
    workloadController_.computeAdaptiveParams();

    int evalCount = 0;
    bool sampleEvals = workloadController_.shouldSampleEvaluate();

    if (governanceScheduler_.hasDirty() && governanceScheduler_.canDrain()) {
        auto dirtyIds = governanceScheduler_.dirtyRenderers();  // copy before clear

        for (RenderId rendId : dirtyIds) {
            // Get the surface HWND for visibility observation
            HWND surfaceHwnd = nullptr;
            auto* rec = rendererManager_.getRecord(rendId);
            if (!rec) continue;

            auto* govHost = surfaceRegistry_.getHost(static_cast<NodeId>(rec->surfaceId));
            if (govHost) {
                surfaceHwnd = govHost->hwnd();
            }
            if (!surfaceHwnd) continue;

            // Observe current visibility
            auto vis = visibilityObserver_.observe(surfaceHwnd, nullptr);

            // Phase 3E: Sampled per-renderer evaluate timing
            LARGE_INTEGER tEvalStart, tEvalEnd;
            if (sampleEvals) QueryPerformanceCounter(&tEvalStart);

            // PURE evaluation — returns recommendation, no side effects
            auto recommendation = workloadController_.evaluate(rendId, vis.state, vis.confidence);
            evalCount++;

            if (sampleEvals) {
                QueryPerformanceCounter(&tEvalEnd);
                workloadController_.recordEvaluateSample(
                    qpcElapsedUs(tEvalStart, tEvalEnd));
            }

            // Execute transition ONLY through centralized authority
            if (recommendation.transitionNeeded) {
                auto txResult = workloadController_.requestTransition(
                    rendId, recommendation.desiredState, "governance_drain");

                // Sync RendererManager state tracking
                if (txResult.transitioned) {
                    rendererManager_.transitionActivity(rendId, recommendation.desiredState);
                }
            }
        }

        governanceScheduler_.clearDirty();
    }

    LARGE_INTEGER tDrainEnd;
    QueryPerformanceCounter(&tDrainEnd);
    workloadController_.recordDrainTiming(
        qpcElapsedUs(tDrainStart, tDrainEnd), evalCount);
}

void Compositor::realizeFocusDecision(const FocusDecision& decision) {
    if (decision.target == kInvalidNodeId) {
        return; // No target to activate
    }

    auto* host = surfaceRegistry_.getHost(decision.target);
    if (!host) {
        // Node might have been destroyed during transition
        focusGraph_.commitDivergedActivation(FocusDivergence::DestroyedDuringTransition, 0);
        return;
    }

    // Pass the request to ActivationManager which has authority over Win32 Z-order and focus
    bool success = activationManager_.requestActivation(decision.target);
    if (success) {
        // Tell the graph it succeeded
        focusGraph_.commitRealizedActivation(decision.target, 0);
    } else {
        focusGraph_.commitDivergedActivation(FocusDivergence::OSDeniedActivation, 0);
    }
}

}  // namespace morphic

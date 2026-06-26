#include "morphic_plugin.h"

#include <windows.h>
#include <flutter/method_channel.h>
#include <flutter/plugin_registrar_windows.h>
#include <flutter/standard_method_codec.h>

#include <memory>
#include <sstream>
#include <vector>
#include <random>

#include "include/morphic/morphic_extension.h"

#include "src/testing/stress_harness.h"
#include "src/testing/replay_system.h"
#include "src/testing/soak_test.h"
#include "src/testing/crash_recovery_test.h"
#include "src/testing/render_resize_storm.h"
#include "src/debug/zombie_auditor.h"
#include "src/debug/gpu_profiler.h"
#include "src/debug/thread_activity_auditor.h"
#include "src/debug/zombie_decay_tracker.h"
#include "src/debug/frame_cadence_monitor.h"
#include "src/rendering/flutter_renderer.h"
#include "src/testing/sustainability_benchmark.h"
#include "src/core/thread_affinity.h"
#include "src/rendering/workload_profile.h"
#include "src/rendering/governance_scheduler.h"

namespace morphic {

// Only one compositor exists per process, so this is safe for C-API dispatch.
static MorphicPlugin* g_activePlugin = nullptr;

// static
void MorphicPlugin::RegisterWithRegistrar(
    flutter::PluginRegistrarWindows *registrar) {
  auto channel =
      std::make_unique<flutter::MethodChannel<flutter::EncodableValue>>(
          registrar->messenger(), "morphic",
          &flutter::StandardMethodCodec::GetInstance());

  auto plugin = std::make_unique<MorphicPlugin>();

  // Capture the primary Flutter runner window HWND.
  // This becomes the owner for all compositor surface windows,
  // establishing a single activation group (one Alt+Tab entry, one taskbar button).
  // NOTE: This means Morphic's compositor authority depends on Flutter Runner
  // topology. This is a transitional architecture — long-term, Morphic should
  // own the root HWND hierarchy.
  //
  // RUNTIME CORE (H1) — a HEADLESS engine (the application bootstrap that runs
  // main() with no window) has no view, so registrar->GetView() is null. Capture
  // the host window ONLY when a view exists; otherwise mainWindowHwnd_ stays null
  // and downstream uses already guard on it (e.g. hideHostWindow). This lets the
  // plugin register on a viewless bootstrap engine without crashing.
  if (auto *view = registrar->GetView()) {
    HWND childHwnd = view->GetNativeWindow();
    HWND parentHwnd = GetAncestor(childHwnd, GA_ROOT);
    plugin->mainWindowHwnd_ = parentHwnd ? parentHwnd : childHwnd;
  }

  channel->SetMethodCallHandler(
      [plugin_pointer = plugin.get()](const auto &call, auto result) {
        plugin_pointer->HandleMethodCall(call, std::move(result));
      });

  registrar->AddPlugin(std::move(plugin));
}

MorphicPlugin::MorphicPlugin() {
    g_activePlugin = this;
}

MorphicPlugin::~MorphicPlugin() {
    if (compositor_) {
        compositor_->shutdown();
    }
}

void MorphicPlugin::HandleMethodCall(
    const flutter::MethodCall<flutter::EncodableValue> &method_call,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {

    const auto& method = method_call.method_name();
    const auto* args = std::get_if<flutter::EncodableMap>(method_call.arguments());

    if (method == "initialize") {
        handleInitialize(std::move(result));
    } else if (method == "shutdown") {
        handleShutdown(std::move(result));
    } else if (method == "hideHostWindow") {
        // Phase 1 invisible-host architecture: hide the Flutter runner window.
        // The Dart VM and plugin continue running — only the HWND is hidden.
        // Morphic surfaces become the sole visible UI.
        if (mainWindowHwnd_) {
            ShowWindow(mainWindowHwnd_, SW_HIDE);
            result->Success(flutter::EncodableValue(true));
        } else {
            result->Error("NO_HOST", "Main window HWND not captured");
        }
    } else if (method == "showHostWindow") {
        if (mainWindowHwnd_) {
            ShowWindow(mainWindowHwnd_, SW_SHOW);
            result->Success(flutter::EncodableValue(true));
        } else {
            result->Error("NO_HOST", "Main window HWND not captured");
        }
    } else if (method == "createSurface" && args) {
        handleCreateSurface(*args, std::move(result));
    } else if (method == "destroySurface" && args) {
        handleDestroySurface(*args, std::move(result));
    } else if (method == "createGroup" && args) {
        handleCreateGroup(*args, std::move(result));
    } else if (method == "destroyGroup" && args) {
        handleDestroyGroup(*args, std::move(result));
    } else if (method == "moveSurface" && args) {
        handleMoveSurface(*args, std::move(result));
    } else if (method == "resizeSurface" && args) {
        handleResizeSurface(*args, std::move(result));
    } else if (method == "setDebugOverlay" && args) {
        handleSetDebugOverlay(*args, std::move(result));
    } else if (method == "getDisplays") {
        handleGetDisplays(std::move(result));
    } else if (method == "getMetrics") {
        handleGetMetrics(std::move(result));
    } else if (method == "runStressTest" && args) {
        handleRunStressTest(*args, std::move(result));
    } else if (method == "runReplayTest") {
        handleRunReplayTest(std::move(result));
    } else if (method == "runSoakTest" && args) {
        handleRunSoakTest(*args, std::move(result));
    } else if (method == "runCrashRecoveryTest") {
        handleRunCrashRecoveryTest(std::move(result));
    } else if (method == "attachRenderer" && args) {
        handleAttachRenderer(*args, std::move(result));
    } else if (method == "detachRenderer" && args) {
        handleDetachRenderer(*args, std::move(result));
    } else if (method == "hardDetachRenderer" && args) {
        // Phase X: 7-stage hard destruction
        if (!compositor_) { result->Error("NO_COMPOSITOR", "Not initialized"); return; }
        NodeId surfaceId = static_cast<NodeId>(getInt(*args, "surfaceId"));
        auto r = compositor_->hardDetachRenderer(surfaceId);
        flutter::EncodableMap rm;
        rm[flutter::EncodableValue("destroyed")] = flutter::EncodableValue(r.destroyed);
        rm[flutter::EncodableValue("hwndsBefore")] = flutter::EncodableValue(r.hwndsBefore);
        rm[flutter::EncodableValue("hwndsAfter")] = flutter::EncodableValue(r.hwndsAfter);
        rm[flutter::EncodableValue("error")] = flutter::EncodableValue(r.error);
        result->Success(flutter::EncodableValue(rm));
    } else if (method == "setSurfaceRole" && args) {
        // Phase 3E.3: Runtime surface role mutation
        if (!compositor_) { result->Error("NO_COMPOSITOR", "Not initialized"); return; }
        NodeId surfaceId = static_cast<NodeId>(getInt(*args, "id"));
        auto roleIt = args->find(flutter::EncodableValue("role"));
        if (roleIt != args->end()) {
            auto roleStr = std::get<std::string>(roleIt->second);
            SurfaceRole role = roleFromString(roleStr);
            compositor_->setSurfaceRole(surfaceId, role);
            result->Success(flutter::EncodableValue(true));
        } else {
            result->Error("MISSING_ROLE", "role argument required");
        }
    } else if (method == "hwndCensus") {
        // Phase X: Full HWND topology audit — enumerate ALL HWNDs in process
        if (!compositor_) { result->Error("NO_COMPOSITOR", "Not initialized"); return; }
        DWORD pid = GetCurrentProcessId();

        struct WindowInfo {
            HWND hwnd;
            HWND parent;
            HWND owner;
            LONG style;
            LONG exStyle;
            bool visible;
            char className[256];
        };
        struct CensusData {
            DWORD pid;
            std::vector<WindowInfo> windows;
        };
        CensusData census{pid, {}};

        EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
            auto* cd = reinterpret_cast<CensusData*>(lParam);
            DWORD wpid = 0;
            GetWindowThreadProcessId(hwnd, &wpid);
            if (wpid == cd->pid) {
                WindowInfo wi{};
                wi.hwnd = hwnd;
                wi.parent = GetParent(hwnd);
                wi.owner = GetWindow(hwnd, GW_OWNER);
                wi.style = GetWindowLong(hwnd, GWL_STYLE);
                wi.exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
                wi.visible = IsWindowVisible(hwnd) != 0;
                GetClassNameA(hwnd, wi.className, 256);
                cd->windows.push_back(wi);
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&census));

        // Log topology to debug output
        int total = 0, visible = 0, hidden = 0;
        int wsChild = 0, wsPopup = 0, exAppWindow = 0, exToolWindow = 0;
        OutputDebugStringA("=== HWND TOPOLOGY CENSUS ===\n");
        flutter::EncodableList windowList;
        for (const auto& wi : census.windows) {
            total++;
            if (wi.visible) visible++; else hidden++;
            if (wi.style & WS_CHILD) wsChild++;
            if (wi.style & WS_POPUP) wsPopup++;
            if (wi.exStyle & WS_EX_APPWINDOW) exAppWindow++;
            if (wi.exStyle & WS_EX_TOOLWINDOW) exToolWindow++;

            char buf[512];
            snprintf(buf, sizeof(buf),
                "  HWND=%p class='%s' %s parent=%p owner=%p %s%s%s%s\n",
                wi.hwnd, wi.className,
                wi.visible ? "VISIBLE" : "HIDDEN",
                wi.parent, wi.owner,
                (wi.style & WS_CHILD) ? "WS_CHILD " : "",
                (wi.style & WS_POPUP) ? "WS_POPUP " : "",
                (wi.exStyle & WS_EX_APPWINDOW) ? "WS_EX_APPWINDOW " : "",
                (wi.exStyle & WS_EX_TOOLWINDOW) ? "WS_EX_TOOLWINDOW " : "");
            OutputDebugStringA(buf);

            flutter::EncodableMap wm;
            wm[flutter::EncodableValue("hwnd")] = flutter::EncodableValue(
                static_cast<int64_t>(reinterpret_cast<uintptr_t>(wi.hwnd)));
            wm[flutter::EncodableValue("class")] = flutter::EncodableValue(
                std::string(wi.className));
            wm[flutter::EncodableValue("visible")] = flutter::EncodableValue(wi.visible);
            wm[flutter::EncodableValue("isChild")] = flutter::EncodableValue(
                (wi.style & WS_CHILD) != 0);
            wm[flutter::EncodableValue("isPopup")] = flutter::EncodableValue(
                (wi.style & WS_POPUP) != 0);
            wm[flutter::EncodableValue("isAppWindow")] = flutter::EncodableValue(
                (wi.exStyle & WS_EX_APPWINDOW) != 0);
            wm[flutter::EncodableValue("isToolWindow")] = flutter::EncodableValue(
                (wi.exStyle & WS_EX_TOOLWINDOW) != 0);
            wm[flutter::EncodableValue("hasParent")] = flutter::EncodableValue(
                wi.parent != nullptr);
            wm[flutter::EncodableValue("hasOwner")] = flutter::EncodableValue(
                wi.owner != nullptr);
            windowList.push_back(flutter::EncodableValue(wm));
        }
        OutputDebugStringA(("=== CENSUS: total=" + std::to_string(total) +
            " visible=" + std::to_string(visible) +
            " hidden=" + std::to_string(hidden) +
            " popup=" + std::to_string(wsPopup) +
            " appWindow=" + std::to_string(exAppWindow) +
            " toolWindow=" + std::to_string(exToolWindow) + " ===\n").c_str());

        flutter::EncodableMap rm;
        rm[flutter::EncodableValue("total")] = flutter::EncodableValue(total);
        rm[flutter::EncodableValue("visible")] = flutter::EncodableValue(visible);
        rm[flutter::EncodableValue("hidden")] = flutter::EncodableValue(hidden);
        rm[flutter::EncodableValue("wsPopup")] = flutter::EncodableValue(wsPopup);
        rm[flutter::EncodableValue("exAppWindow")] = flutter::EncodableValue(exAppWindow);
        rm[flutter::EncodableValue("exToolWindow")] = flutter::EncodableValue(exToolWindow);
        rm[flutter::EncodableValue("rendererTotal")] = flutter::EncodableValue(
            static_cast<int>(compositor_->rendererManager().totalCount()));
        rm[flutter::EncodableValue("rendererActive")] = flutter::EncodableValue(
            static_cast<int>(compositor_->rendererManager().activeCount()));
        rm[flutter::EncodableValue("rendererZombies")] = flutter::EncodableValue(
            static_cast<int>(compositor_->rendererManager().zombieCount()));
        rm[flutter::EncodableValue("windows")] = flutter::EncodableValue(windowList);
        result->Success(flutter::EncodableValue(rm));
    } else if (method == "runResizeStorm") {
        handleRunResizeStorm(std::move(result));
    } else if (method == "runZombieAudit") {
        handleRunZombieAudit(std::move(result));
    } else if (method == "runGpuProfile") {
        handleRunGpuProfile(std::move(result));
    } else if (method == "runDecayTracking") {
        handleRunDecayTracking(std::move(result));
    } else if (method == "runThreadAudit") {
        handleRunThreadAudit(std::move(result));
    } else if (method == "frameProduced" && args) {
        handleFrameProduced(*args, std::move(result));
    } else if (method == "queryFrameCadence") {
        handleQueryFrameCadence(std::move(result));
    } else if (method == "pauseRendererAnimations") {
        handlePauseRendererAnimations(std::move(result));
    } else if (method == "resumeRendererAnimations") {
        handleResumeRendererAnimations(std::move(result));
    } else if (method == "testLifecycleOrchestration" && args) {
        handleTestLifecycleOrchestration(*args, std::move(result));
    } else if (method == "takeBenchmarkSnapshot" && args) {
        handleTakeBenchmarkSnapshot(*args, std::move(result));
    } else if (method == "declareWorkloadTraits" && args) {
        handleDeclareWorkloadTraits(*args, std::move(result));
    } else if (method == "simulateVisibility" && args) {
        handleSimulateVisibility(*args, std::move(result));
    } else if (method == "configureBudget" && args) {
        handleConfigureBudget(*args, std::move(result));
    } else if (method == "clearSimulatedVisibility") {
        compositor_->workloadController().clearAllSimulatedVisibility();
        // P3: Invalidation completeness — mark all dirty so governance re-evaluates
        for (const auto& [rendId, rec] : compositor_->rendererManager().records()) {
            compositor_->governanceScheduler().markDirty(
                rendId, morphic::GovernanceDirtyReason::VisibilityChanged);
        }
        result->Success(flutter::EncodableValue(true));
    } else if (method == "resetBudget") {
        compositor_->workloadController().resetBudget();
        for (const auto& [rendId, rec] : compositor_->rendererManager().records()) {
            compositor_->governanceScheduler().markDirty(
                rendId, morphic::GovernanceDirtyReason::PressureChanged);
        }
        result->Success(flutter::EncodableValue(true));
    } else if (method == "simulateBattery" && args) {
        // Phase 3D: Battery simulation for deterministic adaptive testing
        bool onBattery = getBool(*args, "onBattery", false);
        float pct = static_cast<float>(getDouble(*args, "percent", 100.0));
        compositor_->workloadController().simulateBattery(onBattery, pct);
        compositor_->workloadController().invalidateAdaptiveCache();
        result->Success(flutter::EncodableValue(true));
    } else if (method == "clearSimulatedBattery") {
        compositor_->workloadController().clearSimulatedBattery();
        compositor_->workloadController().invalidateAdaptiveCache();
        result->Success(flutter::EncodableValue(true));
    } else if (method == "batchWakeAll") {
        // Phase 3D: Transactional batch wake — bypasses method channel serialization.
        // Parks all renderers first, then wakes all via collectWakeRequests → arbitrate → commit.
        auto& wc = compositor_->workloadController();
        auto& mgr = compositor_->rendererManager();

        // Mark cadence monitor timestamps + park all
        {
            auto allData = FrameCadenceMonitor::instance().queryAll();
            for (const auto& [engineId, data] : allData) {
                FrameCadenceMonitor::instance().markParking(engineId);
            }
        }
        for (const auto& [rendId, rec] : mgr.records()) {
            auto parkResult = wc.requestTransition(rendId, morphic::ActivityState::Parked,
                                 "batchWakeAll_park", true);
            if (parkResult.transitioned) {
                mgr.transitionActivity(rendId, morphic::ActivityState::Parked);
            }
        }

        // Mark resuming timestamps
        {
            auto allData = FrameCadenceMonitor::instance().queryAll();
            for (const auto& [engineId, data] : allData) {
                FrameCadenceMonitor::instance().markResuming(engineId);
            }
        }

        // Batch wake — collect → arbitrate → commit
        int dispatched = wc.batchWakeAll("batchWakeAll");

        // Sync rendererManager for immediately dispatched wakes
        for (const auto& [rendId, rec] : mgr.records()) {
            // Check if controller transitioned this renderer to Active
            auto* orch = wc.getOrchState(rendId);
            if (orch && orch->currentActivity == morphic::ActivityState::Active) {
                mgr.transitionActivity(rendId, morphic::ActivityState::Active);
            }
        }

        // Mark governance dirty so drain loop dispatches staggered wakes
        for (const auto& [rendId, rec] : mgr.records()) {
            compositor_->governanceScheduler().markDirty(
                rendId, morphic::GovernanceDirtyReason::WorkloadChanged);
        }
        // Trigger immediate drain to start dispatching queued wakes
        compositor_->drainGovernance();
        // Keep frame timer alive for stagger dispatch
        if (wc.wakeQueueDepth() > 0) {
            compositor_->requestFrame();
        }

        flutter::EncodableMap rm;
        rm[flutter::EncodableValue("dispatched")] = flutter::EncodableValue(dispatched);
        rm[flutter::EncodableValue("queued")] = flutter::EncodableValue(wc.wakeQueueDepth());
        rm[flutter::EncodableValue("staggered")] = flutter::EncodableValue(wc.staggeredWakeCount());
        rm[flutter::EncodableValue("immediate")] = flutter::EncodableValue(wc.immediateWakeCount());
        result->Success(flutter::EncodableValue(rm));
    } else if (method == "lifecycleOrchestrateSurfaces" && args) {
        // Phase 3E: Scoped lifecycle — only affects specified surface IDs (avoids zombies)
        if (!compositor_) { result->Error("NO_COMPOSITOR", "Not initialized"); return; }

        std::string targetStr = "parked";
        auto tIt = args->find(flutter::EncodableValue("target"));
        if (tIt != args->end() && std::holds_alternative<std::string>(tIt->second)) {
            targetStr = std::get<std::string>(tIt->second);
        }
        ActivityState target = ActivityState::Parked;
        if (targetStr == "active") target = ActivityState::Active;
        else if (targetStr == "throttled") target = ActivityState::Throttled;
        else if (targetStr == "dormant") target = ActivityState::Dormant;

        auto sIt = args->find(flutter::EncodableValue("surfaceIds"));
        std::vector<NodeId> surfaceIds;
        if (sIt != args->end()) {
            const auto* list = std::get_if<flutter::EncodableList>(&sIt->second);
            if (list) {
                for (const auto& v : *list) {
                    if (auto* i32 = std::get_if<int32_t>(&v)) surfaceIds.push_back(static_cast<NodeId>(*i32));
                    else if (auto* i64 = std::get_if<int64_t>(&v)) surfaceIds.push_back(static_cast<NodeId>(*i64));
                }
            }
        }

        auto& mgr = compositor_->rendererManager();
        auto& ctrl = compositor_->workloadController();
        const auto& hosts = compositor_->hosts();
        int transitioned = 0;

        for (NodeId surfId : surfaceIds) {
            auto hIt = hosts.find(surfId);
            if (hIt == hosts.end()) continue;
            RenderId rendId = hIt->second->activeRendererId();
            if (rendId == kInvalidRenderId) continue;

            if (target == ActivityState::Active) {
                FrameCadenceMonitor::instance().markResuming(rendId);
            } else if (target == ActivityState::Parked || target == ActivityState::Dormant) {
                FrameCadenceMonitor::instance().markParking(rendId);
            }

            auto txResult = ctrl.requestTransition(rendId, target, "scopedLifecycle", true);
            if (txResult.transitioned) {
                mgr.transitionActivity(rendId, target);
                compositor_->governanceScheduler().markDirty(rendId, GovernanceDirtyReason::ExternalCommand);
                transitioned++;
            }
        }
        compositor_->drainGovernance();

        flutter::EncodableMap rm;
        rm[flutter::EncodableValue("target")] = flutter::EncodableValue(targetStr);
        rm[flutter::EncodableValue("transitioned")] = flutter::EncodableValue(transitioned);
        rm[flutter::EncodableValue("total")] = flutter::EncodableValue(static_cast<int>(surfaceIds.size()));
        result->Success(flutter::EncodableValue(rm));

    } else if (method == "batchWakeSurfaces" && args) {
        // Phase 3E: Scoped batch wake — only parks+wakes specified surface IDs
        if (!compositor_) { result->Error("NO_COMPOSITOR", "Not initialized"); return; }

        auto sIt = args->find(flutter::EncodableValue("surfaceIds"));
        std::vector<NodeId> surfaceIds;
        if (sIt != args->end()) {
            const auto* list = std::get_if<flutter::EncodableList>(&sIt->second);
            if (list) {
                for (const auto& v : *list) {
                    if (auto* i32 = std::get_if<int32_t>(&v)) surfaceIds.push_back(static_cast<NodeId>(*i32));
                    else if (auto* i64 = std::get_if<int64_t>(&v)) surfaceIds.push_back(static_cast<NodeId>(*i64));
                }
            }
        }

        auto& wc = compositor_->workloadController();
        auto& mgr = compositor_->rendererManager();
        const auto& hosts = compositor_->hosts();

        // Park specified surfaces
        for (NodeId surfId : surfaceIds) {
            auto hIt = hosts.find(surfId);
            if (hIt == hosts.end()) continue;
            RenderId rendId = hIt->second->activeRendererId();
            if (rendId == kInvalidRenderId) continue;
            FrameCadenceMonitor::instance().markParking(rendId);
            auto parkResult = wc.requestTransition(rendId, ActivityState::Parked, "scopedWake_park", true);
            if (parkResult.transitioned) mgr.transitionActivity(rendId, ActivityState::Parked);
        }

        // Wake specified surfaces
        for (NodeId surfId : surfaceIds) {
            auto hIt = hosts.find(surfId);
            if (hIt == hosts.end()) continue;
            RenderId rendId = hIt->second->activeRendererId();
            if (rendId == kInvalidRenderId) continue;
            FrameCadenceMonitor::instance().markResuming(rendId);
        }

        // Batch wake through governance
        int dispatched = wc.batchWakeAll("scopedWake");

        // Sync only our surfaces
        for (NodeId surfId : surfaceIds) {
            auto hIt = hosts.find(surfId);
            if (hIt == hosts.end()) continue;
            RenderId rendId = hIt->second->activeRendererId();
            if (rendId == kInvalidRenderId) continue;
            auto* orch = wc.getOrchState(rendId);
            if (orch && orch->currentActivity == ActivityState::Active) {
                mgr.transitionActivity(rendId, ActivityState::Active);
            }
            compositor_->governanceScheduler().markDirty(rendId, GovernanceDirtyReason::WorkloadChanged);
        }

        compositor_->drainGovernance();
        if (wc.wakeQueueDepth() > 0) compositor_->requestFrame();

        flutter::EncodableMap rm;
        rm[flutter::EncodableValue("dispatched")] = flutter::EncodableValue(dispatched);
        rm[flutter::EncodableValue("queued")] = flutter::EncodableValue(wc.wakeQueueDepth());
        result->Success(flutter::EncodableValue(rm));

    } else if (method == "createWorkspace" && args) {
        if (!workspaceChannel_) { result->Error("NO_RUNTIME", "Not initialized"); return; }
        workspaceChannel_->handleCreateWorkspace(*args, std::move(result));
    } else if (method == "destroyWorkspace" && args) {
        if (!workspaceChannel_) { result->Error("NO_RUNTIME", "Not initialized"); return; }
        workspaceChannel_->handleDestroyWorkspace(*args, std::move(result));
    } else if (method == "switchWorkspace" && args) {
        if (!workspaceChannel_) { result->Error("NO_RUNTIME", "Not initialized"); return; }
        workspaceChannel_->handleSwitchWorkspace(*args, std::move(result));
    } else if (method == "activeWorkspace") {
        if (!workspaceChannel_) { result->Error("NO_RUNTIME", "Not initialized"); return; }
        workspaceChannel_->handleActiveWorkspace(std::move(result));
    } else if (method == "setWorkspaceIntent" && args) {
        if (!workspaceChannel_) { result->Error("NO_RUNTIME", "Not initialized"); return; }
        workspaceChannel_->handleSetWorkspaceIntent(*args, std::move(result));
    } else if (method == "setSurfaceAttention" && args) {
        if (!workspaceChannel_) { result->Error("NO_RUNTIME", "Not initialized"); return; }
        workspaceChannel_->handleSetSurfaceAttention(*args, std::move(result));
    } else if (method == "associateSurfaces" && args) {
        if (!workspaceChannel_) { result->Error("NO_RUNTIME", "Not initialized"); return; }
        workspaceChannel_->handleAssociateSurfaces(*args, std::move(result));
    } else if (method == "dissociateSurface" && args) {
        if (!workspaceChannel_) { result->Error("NO_RUNTIME", "Not initialized"); return; }
        workspaceChannel_->handleDissociateSurface(*args, std::move(result));
    } else if (method == "saveSession" && args) {
        if (!sessionChannel_) { result->Error("NO_RUNTIME", "Not initialized"); return; }
        sessionChannel_->handleSaveSession(*args, std::move(result));
    } else if (method == "restoreSession") {
        if (!sessionChannel_) { result->Error("NO_RUNTIME", "Not initialized"); return; }
        sessionChannel_->handleRestoreSession(std::move(result));
    } else if (method == "getDiagnostics") {
        if (!diagnosticsChannel_) { result->Error("NO_RUNTIME", "Not initialized"); return; }
        diagnosticsChannel_->handleGetDiagnostics(std::move(result));
    } else if (method == "runValidation") {
        if (!diagnosticsChannel_) { result->Error("NO_RUNTIME", "Not initialized"); return; }
        diagnosticsChannel_->handleRunValidation(std::move(result));
    } else if (method == "getBootstrapPhase") {
        if (!diagnosticsChannel_) { result->Error("NO_RUNTIME", "Not initialized"); return; }
        diagnosticsChannel_->handleGetBootstrapPhase(std::move(result));
    } else {
        // PHASE 10.2 — delegate to the extension handler (product layer) before
        // returning NotImplemented. This is how the example's ecology layer
        // handles methods without the library knowing about SurfaceEcology.
        // The handler takes ownership of `result` and MUST respond.
        const auto& ext = GetMorphicExtensionHandler();
        if (ext) {
            ext(method, method_call.arguments(), std::move(result));
            return;
        }
        result->NotImplemented();
    }
}

// --- Method handlers ---

void MorphicPlugin::handleInitialize(
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
    if (!compositor_) {
        compositor_ = std::make_unique<Compositor>();
    }
    compositor_->initialize(mainWindowHwnd_);

    // Bootstrap MorphicRuntimeImpl — THE runtime facade
    runtime_ = std::make_unique<MorphicRuntimeImpl>();
    runtime_->bootstrap(*compositor_);

    // Create channel handlers (transport only)
    workspaceChannel_ = std::make_unique<WorkspaceChannel>(*runtime_);
    sessionChannel_ = std::make_unique<SessionChannel>(*runtime_);
    diagnosticsChannel_ = std::make_unique<DiagnosticsChannel>(*runtime_);

    // Phase 4 Step 2: Main window is now owned by MorphicRuntime.

    // Phase 3C: Register frame callback for recovery tracking.
    FrameCadenceMonitor::instance().setFrameCallback(
        &MorphicPlugin::onFrameProduced, this);

    result->Success(flutter::EncodableValue(true));
}

void MorphicPlugin::handleShutdown(
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
    if (compositor_) {
        compositor_->shutdown();
        compositor_.reset();
    }
    result->Success(flutter::EncodableValue(true));
}

void MorphicPlugin::handleCreateSurface(
    const flutter::EncodableMap& args,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
    if (!compositor_) {
        result->Error("NOT_INITIALIZED", "Compositor not initialized");
        return;
    }

    SurfaceConfig config;
    config.transform.x = static_cast<int>(getInt(args, "x", 100));
    config.transform.y = static_cast<int>(getInt(args, "y", 100));
    config.transform.width = static_cast<int>(getInt(args, "width", 400));
    config.transform.height = static_cast<int>(getInt(args, "height", 300));
    config.color = static_cast<uint32_t>(getInt(args, "color", 0x1a1a2e));
    config.elevation = static_cast<int>(getInt(args, "elevation", 0));
    config.visible = getBool(args, "visible", true);

    config.constraints.minWidth = static_cast<int>(getInt(args, "minWidth", 100));
    config.constraints.minHeight = static_cast<int>(getInt(args, "minHeight", 100));

    // Phase 3E.3: Role
    auto roleIt = args.find(flutter::EncodableValue("role"));
    if (roleIt != args.end()) {
        config.role = roleFromString(std::get<std::string>(roleIt->second));
    }

    NodeId id = compositor_->createSurface(config);
    if (id == kInvalidNodeId) {
        result->Error("CREATE_FAILED", "Failed to create surface");
        return;
    }

    result->Success(flutter::EncodableValue(static_cast<int64_t>(id)));
}

void MorphicPlugin::handleDestroySurface(
    const flutter::EncodableMap& args,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
    if (!compositor_) {
        result->Error("NOT_INITIALIZED", "Compositor not initialized");
        return;
    }

    NodeId id = static_cast<NodeId>(getInt(args, "id"));
    compositor_->destroySurface(id);
    result->Success(flutter::EncodableValue(true));
}

void MorphicPlugin::handleCreateGroup(
    const flutter::EncodableMap& args,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
    if (!compositor_) {
        result->Error("NOT_INITIALIZED", "Compositor not initialized");
        return;
    }

    auto it = args.find(flutter::EncodableValue("memberIds"));
    if (it == args.end()) {
        result->Error("INVALID_ARGS", "memberIds required");
        return;
    }

    const auto* memberList = std::get_if<flutter::EncodableList>(&it->second);
    if (!memberList) {
        result->Error("INVALID_ARGS", "memberIds must be a list");
        return;
    }

    std::vector<NodeId> memberIds;
    for (const auto& v : *memberList) {
        if (auto* intVal = std::get_if<int32_t>(&v)) {
            memberIds.push_back(static_cast<NodeId>(*intVal));
        } else if (auto* int64Val = std::get_if<int64_t>(&v)) {
            memberIds.push_back(static_cast<NodeId>(*int64Val));
        }
    }

    NodeId groupId = compositor_->createGroup(memberIds);
    result->Success(flutter::EncodableValue(static_cast<int64_t>(groupId)));
}

void MorphicPlugin::handleDestroyGroup(
    const flutter::EncodableMap& args,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
    if (!compositor_) {
        result->Error("NOT_INITIALIZED", "Compositor not initialized");
        return;
    }

    NodeId id = static_cast<NodeId>(getInt(args, "id"));
    compositor_->destroyGroup(id);
    result->Success(flutter::EncodableValue(true));
}

void MorphicPlugin::handleMoveSurface(
    const flutter::EncodableMap& args,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
    if (!compositor_) {
        result->Error("NOT_INITIALIZED", "Compositor not initialized");
        return;
    }

    NodeId id = static_cast<NodeId>(getInt(args, "id"));
    int x = static_cast<int>(getInt(args, "x"));
    int y = static_cast<int>(getInt(args, "y"));

    compositor_->moveSurface(id, x, y);
    result->Success(flutter::EncodableValue(true));
}

void MorphicPlugin::handleResizeSurface(
    const flutter::EncodableMap& args,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
    if (!compositor_) {
        result->Error("NOT_INITIALIZED", "Compositor not initialized");
        return;
    }

    NodeId id = static_cast<NodeId>(getInt(args, "id"));
    int w = static_cast<int>(getInt(args, "width"));
    int h = static_cast<int>(getInt(args, "height"));

    compositor_->resizeSurface(id, w, h);
    result->Success(flutter::EncodableValue(true));
}

void MorphicPlugin::handleSetDebugOverlay(
    const flutter::EncodableMap& args,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
    if (!compositor_) {
        result->Error("NOT_INITIALIZED", "Compositor not initialized");
        return;
    }

    bool enabled = getBool(args, "enabled", false);
    compositor_->setDebugOverlay(enabled);
    result->Success(flutter::EncodableValue(true));
}

void MorphicPlugin::handleGetDisplays(
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
    if (!compositor_) {
        result->Error("NOT_INITIALIZED", "Compositor not initialized");
        return;
    }

    compositor_->refreshDisplays();
    const auto& displays = compositor_->displays();

    flutter::EncodableList list;
    for (const auto& d : displays) {
        flutter::EncodableMap m;
        m[flutter::EncodableValue("isPrimary")] = flutter::EncodableValue(d.isPrimary);
        m[flutter::EncodableValue("dpiX")] = flutter::EncodableValue(d.dpiX);
        m[flutter::EncodableValue("dpiY")] = flutter::EncodableValue(d.dpiY);
        m[flutter::EncodableValue("scaleFactor")] = flutter::EncodableValue(d.scaleFactor);
        m[flutter::EncodableValue("refreshRate")] = flutter::EncodableValue(d.refreshRate);
        m[flutter::EncodableValue("boundsLeft")] = flutter::EncodableValue(static_cast<int32_t>(d.bounds.left));
        m[flutter::EncodableValue("boundsTop")] = flutter::EncodableValue(static_cast<int32_t>(d.bounds.top));
        m[flutter::EncodableValue("boundsRight")] = flutter::EncodableValue(static_cast<int32_t>(d.bounds.right));
        m[flutter::EncodableValue("boundsBottom")] = flutter::EncodableValue(static_cast<int32_t>(d.bounds.bottom));
        m[flutter::EncodableValue("workLeft")] = flutter::EncodableValue(static_cast<int32_t>(d.workArea.left));
        m[flutter::EncodableValue("workTop")] = flutter::EncodableValue(static_cast<int32_t>(d.workArea.top));
        m[flutter::EncodableValue("workRight")] = flutter::EncodableValue(static_cast<int32_t>(d.workArea.right));
        m[flutter::EncodableValue("workBottom")] = flutter::EncodableValue(static_cast<int32_t>(d.workArea.bottom));
        list.push_back(flutter::EncodableValue(m));
    }

    result->Success(flutter::EncodableValue(list));
}

void MorphicPlugin::handleGetMetrics(
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
    if (!compositor_) {
        result->Error("NOT_INITIALIZED", "Compositor not initialized");
        return;
    }

    const auto& m = compositor_->metrics();
    const auto& last = m.lastFrame();
    const auto rmMetrics = compositor_->rendererManager().computeMetrics();

    flutter::EncodableMap metrics;
    metrics[flutter::EncodableValue("fps")] = flutter::EncodableValue(m.currentFPS());
    metrics[flutter::EncodableValue("frameTimeMs")] = flutter::EncodableValue(last.frameTimeMs);
    metrics[flutter::EncodableValue("dwmCommitMs")] = flutter::EncodableValue(last.dwmCommitCostMs);
    metrics[flutter::EncodableValue("hwndCount")] = flutter::EncodableValue(last.hwndCount);
    metrics[flutter::EncodableValue("droppedFrames")] = flutter::EncodableValue(m.totalDroppedFrames());

    // Renderer manager metrics
    metrics[flutter::EncodableValue("rendererTotal")] = flutter::EncodableValue(static_cast<int32_t>(rmMetrics.totalCreated));
    metrics[flutter::EncodableValue("rendererActive")] = flutter::EncodableValue(static_cast<int32_t>(rmMetrics.activeCount));
    metrics[flutter::EncodableValue("rendererZombies")] = flutter::EncodableValue(static_cast<int32_t>(rmMetrics.zombieCount));
    metrics[flutter::EncodableValue("rendererHidden")] = flutter::EncodableValue(static_cast<int32_t>(rmMetrics.hiddenCount));
    metrics[flutter::EncodableValue("flutterEngines")] = flutter::EncodableValue(static_cast<int32_t>(rmMetrics.flutterEngines));

    // Thread affinity metrics
    metrics[flutter::EncodableValue("uiThreadId")] = flutter::EncodableValue(static_cast<int32_t>(ThreadAffinity::uiThreadId()));
    metrics[flutter::EncodableValue("callerThreadId")] = flutter::EncodableValue(static_cast<int32_t>(GetCurrentThreadId()));
    metrics[flutter::EncodableValue("onUIThread")] = flutter::EncodableValue(ThreadAffinity::isUIThread());

    // Render skew metrics
    const auto skewDist = compositor_->skewTracker().computeSkewDistribution();
    metrics[flutter::EncodableValue("skewP50")] = flutter::EncodableValue(skewDist.p50);
    metrics[flutter::EncodableValue("skewP95")] = flutter::EncodableValue(skewDist.p95);
    metrics[flutter::EncodableValue("skewP99")] = flutter::EncodableValue(skewDist.p99);
    metrics[flutter::EncodableValue("skewMax")] = flutter::EncodableValue(skewDist.max);
    metrics[flutter::EncodableValue("skewSamples")] = flutter::EncodableValue(static_cast<int32_t>(skewDist.sampleCount));

    // Input-to-photon latency
    const auto latencyDist = compositor_->inputPhotonTracker().computeDistribution();
    metrics[flutter::EncodableValue("latencyP50")] = flutter::EncodableValue(latencyDist.p50);
    metrics[flutter::EncodableValue("latencyP95")] = flutter::EncodableValue(latencyDist.p95);
    metrics[flutter::EncodableValue("latencyP99")] = flutter::EncodableValue(latencyDist.p99);
    metrics[flutter::EncodableValue("latencyMax")] = flutter::EncodableValue(latencyDist.max);
    const auto& jank = compositor_->inputPhotonTracker().jankPersistence();
    metrics[flutter::EncodableValue("jankStreak")] = flutter::EncodableValue(jank.currentStreak);
    metrics[flutter::EncodableValue("jankMaxStreak")] = flutter::EncodableValue(jank.maxStreak);

    // Capture manager metrics
    const auto& capMetrics = compositor_->captureManager().metrics();
    metrics[flutter::EncodableValue("captureActive")] = flutter::EncodableValue(compositor_->captureManager().isActive());
    metrics[flutter::EncodableValue("captureDrags")] = flutter::EncodableValue(capMetrics.dragCount);
    metrics[flutter::EncodableValue("captureStolen")] = flutter::EncodableValue(capMetrics.captureStolen);

    // Focus manager metrics (removed for FocusGraph replacement)
    metrics[flutter::EncodableValue("focusActiveSurface")] = flutter::EncodableValue(0);
    metrics[flutter::EncodableValue("focusTransitions")] = flutter::EncodableValue(0);
    metrics[flutter::EncodableValue("focusSuppressed")] = flutter::EncodableValue(0);
    metrics[flutter::EncodableValue("focusStolen")] = flutter::EncodableValue(0);

    // Frame pacing metrics (replaces FPS obsession)
    const auto pacing = compositor_->framePacer().computeDistribution();
    metrics[flutter::EncodableValue("pacingIntervalMs")] = flutter::EncodableValue(pacing.intervalMeanMs);
    metrics[flutter::EncodableValue("pacingVarianceMs")] = flutter::EncodableValue(pacing.intervalVarianceMs);
    metrics[flutter::EncodableValue("pacingStdDevMs")] = flutter::EncodableValue(pacing.intervalStdDevMs);
    metrics[flutter::EncodableValue("pacingStability")] = flutter::EncodableValue(pacing.pacingStability);
    metrics[flutter::EncodableValue("frameP99Ms")] = flutter::EncodableValue(pacing.frameP99Ms);
    metrics[flutter::EncodableValue("frameMaxMs")] = flutter::EncodableValue(pacing.frameMaxMs);
    metrics[flutter::EncodableValue("pacingJitterMs")] = flutter::EncodableValue(pacing.jitterMs);
    metrics[flutter::EncodableValue("worstConvergenceMs")] = flutter::EncodableValue(compositor_->framePacer().worstConvergenceMs());

    // Process resource metrics (for CSV snapshot — zombie audit correlation)
    {
        ZombieAuditor auditor;
        auto snap = auditor.captureProcessSnapshot();
        metrics[flutter::EncodableValue("processThreads")] = flutter::EncodableValue(snap.threadCount);
        metrics[flutter::EncodableValue("processHandles")] = flutter::EncodableValue(static_cast<int64_t>(snap.handleCount));
        metrics[flutter::EncodableValue("processGDI")] = flutter::EncodableValue(static_cast<int64_t>(snap.gdiObjects));
        metrics[flutter::EncodableValue("processUserObjects")] = flutter::EncodableValue(static_cast<int64_t>(snap.userObjects));
        metrics[flutter::EncodableValue("workingSetKB")] = flutter::EncodableValue(static_cast<int64_t>(snap.workingSetBytes / 1024));
        metrics[flutter::EncodableValue("privateKB")] = flutter::EncodableValue(static_cast<int64_t>(snap.privateBytes / 1024));
    }

    result->Success(flutter::EncodableValue(metrics));
}

void MorphicPlugin::handleRunStressTest(
    const flutter::EncodableMap& args,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
    if (!compositor_) {
        result->Error("NOT_INITIALIZED", "Compositor not initialized");
        return;
    }

    // Build callbacks that bridge stress harness → compositor
    StressHarness::CompositorCallbacks cb;
    auto* comp = compositor_.get();

    cb.moveSurface = [comp](NodeId id, int x, int y) { comp->moveSurface(id, x, y); };
    cb.resizeSurface = [comp](NodeId id, int w, int h) { comp->resizeSurface(id, w, h); };
    cb.dragBegin = [comp](NodeId id, POINT pt) { comp->onDragBegin(id, pt); };
    cb.dragUpdate = [comp](NodeId id, POINT pt) { comp->onDragUpdate(id, pt); };
    cb.dragEnd = [comp](NodeId id, POINT pt) { comp->onDragEnd(id, pt); };
    cb.activate = [comp](NodeId id) { comp->onSurfaceActivated(id); };
    cb.processFrame = [comp]() { comp->tick(); };
    cb.getSurfaceIds = [comp]() -> std::vector<NodeId> {
        std::vector<NodeId> ids;
        comp->sceneGraph().forEachSurface([&](const CompositionSurface* s) {
            ids.push_back(s->id());
        });
        return ids;
    };
    cb.getSceneGraph = [comp]() -> const SceneGraph& { return comp->sceneGraph(); };
    cb.getHosts = [comp]() -> const std::unordered_map<NodeId, std::unique_ptr<WindowHost>>& {
        return comp->hosts();
    };
    cb.getMetrics = [comp]() -> const MetricsCollector& { return comp->metrics(); };

    // Save original positions so we can restore after stress tests
    struct SavedState { NodeId id; int x, y, w, h; };
    std::vector<SavedState> savedPositions;
    comp->sceneGraph().forEachSurface([&](const CompositionSurface* s) {
        const auto& t = s->worldTransform();
        savedPositions.push_back({s->id(), t.x, t.y, t.width, t.height});
    });

    // Run tests
    StressHarness harness;
    auto results_vec = harness.runAll(cb);

    // Restore original positions via a SINGLE atomic transaction.
    // Important: individual moveSurface calls cause group propagation —
    // restoring surface #1 would drag #2 and #3 through the group,
    // corrupting their positions before we can restore them.
    {
        auto& tx = comp->beginTransaction();
        for (const auto& saved : savedPositions) {
            tx.move(saved.id, saved.x, saved.y);
            tx.resize(saved.id, saved.w, saved.h);
        }
        comp->commitTransaction();
    }

    // Pump Windows messages to prevent "not responding" after long stress test
    MSG msg;
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // Format and return
    flutter::EncodableList resultList;
    for (const auto& r : results_vec) {
        flutter::EncodableMap entry;
        entry[flutter::EncodableValue("name")] = flutter::EncodableValue(r.testName);
        entry[flutter::EncodableValue("passed")] = flutter::EncodableValue(r.passed);
        entry[flutter::EncodableValue("iterations")] = flutter::EncodableValue(r.iterations);
        entry[flutter::EncodableValue("durationMs")] = flutter::EncodableValue(r.durationMs);
        entry[flutter::EncodableValue("avgFrameMs")] = flutter::EncodableValue(r.avgFrameMs);
        entry[flutter::EncodableValue("maxSyncError")] = flutter::EncodableValue(r.maxSyncError);
        entry[flutter::EncodableValue("droppedFrames")] = flutter::EncodableValue(r.droppedFrames);
        entry[flutter::EncodableValue("invariantViolations")] = flutter::EncodableValue(r.invariantViolations);
        entry[flutter::EncodableValue("orderingMismatches")] = flutter::EncodableValue(r.orderingMismatches);
        if (!r.passed) {
            entry[flutter::EncodableValue("failureReason")] = flutter::EncodableValue(r.failureReason);
        }
        resultList.push_back(flutter::EncodableValue(entry));
    }

    result->Success(flutter::EncodableValue(resultList));
}

// --- Helpers ---

int64_t MorphicPlugin::getInt(const flutter::EncodableMap& map, const std::string& key, int64_t def) {
    auto it = map.find(flutter::EncodableValue(key));
    if (it == map.end()) return def;
    if (auto* v = std::get_if<int32_t>(&it->second)) return *v;
    if (auto* v = std::get_if<int64_t>(&it->second)) return *v;
    return def;
}

double MorphicPlugin::getDouble(const flutter::EncodableMap& map, const std::string& key, double def) {
    auto it = map.find(flutter::EncodableValue(key));
    if (it == map.end()) return def;
    if (auto* v = std::get_if<double>(&it->second)) return *v;
    if (auto* v = std::get_if<int32_t>(&it->second)) return static_cast<double>(*v);
    if (auto* v = std::get_if<int64_t>(&it->second)) return static_cast<double>(*v);
    return def;
}

bool MorphicPlugin::getBool(const flutter::EncodableMap& map, const std::string& key, bool def) {
    auto it = map.find(flutter::EncodableValue(key));
    if (it == map.end()) return def;
    if (auto* v = std::get_if<bool>(&it->second)) return *v;
    return def;
}

// --- Gate 3: Replay Determinism Proof ---
void MorphicPlugin::handleRunReplayTest(
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result)
{
    if (!compositor_) {
        result->Error("NOT_INITIALIZED", "Compositor not initialized");
        return;
    }

    auto* comp = compositor_.get();
    auto& replay = comp->replaySystem();

    // Step 1: Generate a deterministic synthetic event sequence
    // (Don't rely on user recording — that introduces timing nondeterminism)
    std::vector<ReplayEvent> syntheticEvents;
    std::mt19937 rng(42);  // Deterministic seed
    std::uniform_int_distribution<int> posDist(200, 800);
    std::uniform_int_distribution<int> actionDist(0, 2);

    std::vector<NodeId> ids;
    comp->sceneGraph().forEachSurface([&](const CompositionSurface* s) {
        ids.push_back(s->id());
    });

    if (ids.empty()) {
        result->Error("NO_SURFACES", "Create surfaces before running replay test");
        return;
    }

    // Generate 200 events: mix of moves, resizes, and activations
    for (int i = 0; i < 200; i++) {
        ReplayEvent e;
        e.timestampMs = i * 16.0;  // 60fps-ish timing
        e.surfaceId = ids[i % ids.size()];
        int action = actionDist(rng);
        switch (action) {
            case 0:
                e.type = ReplayEvent::Move;
                e.x = posDist(rng);
                e.y = posDist(rng);
                break;
            case 1:
                e.type = ReplayEvent::Resize;
                e.w = 200 + (rng() % 400);
                e.h = 200 + (rng() % 300);
                break;
            case 2:
                e.type = ReplayEvent::Activate;
                break;
        }
        syntheticEvents.push_back(e);
    }

    // Step 2: Build event dispatcher
    auto dispatcher = [comp](const ReplayEvent& e) {
        switch (e.type) {
            case ReplayEvent::Move:
                comp->moveSurface(e.surfaceId, e.x, e.y);
                break;
            case ReplayEvent::Resize:
                comp->resizeSurface(e.surfaceId, e.w, e.h);
                break;
            case ReplayEvent::Activate:
                comp->onSurfaceActivated(e.surfaceId);
                break;
            case ReplayEvent::DragBegin: {
                POINT pt = {e.x, e.y};
                comp->onDragBegin(e.surfaceId, pt);
                break;
            }
            case ReplayEvent::DragUpdate: {
                POINT pt = {e.x, e.y};
                comp->onDragUpdate(e.surfaceId, pt);
                break;
            }
            case ReplayEvent::DragEnd: {
                POINT pt = {e.x, e.y};
                comp->onDragEnd(e.surfaceId, pt);
                break;
            }
            default: break;
        }
        comp->tick();
    };

    // Save original positions
    struct Saved { NodeId id; int x, y, w, h; };
    std::vector<Saved> originals;
    comp->sceneGraph().forEachSurface([&](const CompositionSurface* s) {
        const auto& t = s->worldTransform();
        originals.push_back({s->id(), t.x, t.y, t.width, t.height});
    });

    auto restorePositions = [&]() {
        auto& tx = comp->beginTransaction();
        for (const auto& o : originals) {
            tx.move(o.id, o.x, o.y);
            tx.resize(o.id, o.w, o.h);
        }
        comp->commitTransaction();
    };

    // Step 3: Run 3 replays, capture snapshots
    std::vector<StateSnapshot> snapshots;
    const int numRuns = 3;

    for (int run = 0; run < numRuns; run++) {
        restorePositions();  // Reset to identical starting state

        // Play events
        for (const auto& e : syntheticEvents) {
            dispatcher(e);
        }

        // Capture snapshot
        snapshots.push_back(replay.captureSnapshot(
            comp->sceneGraph(), comp->hosts(), run));
    }

    // Step 4: Compare all snapshots
    bool allIdentical = true;
    std::string details;
    double maxDelta = 0.0;

    for (int i = 1; i < numRuns; i++) {
        auto cmp = replay.compare(snapshots[0], snapshots[i]);
        if (!cmp.identical) {
            allIdentical = false;
            details += "Run " + std::to_string(i) + " diverged: " + cmp.details + "; ";
        }
        if (cmp.maxPositionDelta > maxDelta) maxDelta = cmp.maxPositionDelta;
    }

    // Restore original positions
    restorePositions();

    // Return results
    flutter::EncodableMap resultMap;
    resultMap[flutter::EncodableValue("passed")] = flutter::EncodableValue(allIdentical);
    resultMap[flutter::EncodableValue("runs")] = flutter::EncodableValue(numRuns);
    resultMap[flutter::EncodableValue("events")] = flutter::EncodableValue(static_cast<int>(syntheticEvents.size()));
    resultMap[flutter::EncodableValue("maxDelta")] = flutter::EncodableValue(maxDelta);
    if (!allIdentical) {
        resultMap[flutter::EncodableValue("details")] = flutter::EncodableValue(details);
    }

    OutputDebugStringA(("REPLAY DETERMINISM: " + std::string(allIdentical ? "PASS" : "FAIL") +
        " maxDelta=" + std::to_string(maxDelta) +
        " events=" + std::to_string(syntheticEvents.size()) + "\n").c_str());

    result->Success(flutter::EncodableValue(resultMap));
}

// --- Gate 6: Soak Test ---
void MorphicPlugin::handleRunSoakTest(
    const flutter::EncodableMap& args,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result)
{
    if (!compositor_) {
        result->Error("NOT_INITIALIZED", "Compositor not initialized");
        return;
    }

    int durationMinutes = static_cast<int>(getInt(args, "durationMinutes", 1));

    auto* comp = compositor_.get();

    // Build callbacks
    StressHarness::CompositorCallbacks cb;
    cb.moveSurface = [comp](NodeId id, int x, int y) { comp->moveSurface(id, x, y); };
    cb.resizeSurface = [comp](NodeId id, int w, int h) { comp->resizeSurface(id, w, h); };
    cb.dragBegin = [comp](NodeId id, POINT pt) { comp->onDragBegin(id, pt); };
    cb.dragUpdate = [comp](NodeId id, POINT pt) { comp->onDragUpdate(id, pt); };
    cb.dragEnd = [comp](NodeId id, POINT pt) { comp->onDragEnd(id, pt); };
    cb.activate = [comp](NodeId id) { comp->onSurfaceActivated(id); };
    cb.processFrame = [comp]() { comp->tick(); };
    cb.getSurfaceIds = [comp]() -> std::vector<NodeId> {
        std::vector<NodeId> ids;
        comp->sceneGraph().forEachSurface([&](const CompositionSurface* s) {
            ids.push_back(s->id());
        });
        return ids;
    };
    cb.getSceneGraph = [comp]() -> const SceneGraph& { return comp->sceneGraph(); };
    cb.getHosts = [comp]() -> const std::unordered_map<NodeId, std::unique_ptr<WindowHost>>& {
        return comp->hosts();
    };
    cb.getMetrics = [comp]() -> const MetricsCollector& { return comp->metrics(); };

    // Save positions
    struct Saved { NodeId id; int x, y, w, h; };
    std::vector<Saved> originals;
    comp->sceneGraph().forEachSurface([&](const CompositionSurface* s) {
        const auto& t = s->worldTransform();
        originals.push_back({s->id(), t.x, t.y, t.width, t.height});
    });

    SoakTest soak;
    auto soakResult = soak.run(cb, durationMinutes);

    // Restore positions via atomic transaction (prevents group cascade)
    {
        auto& tx = comp->beginTransaction();
        for (const auto& o : originals) {
            tx.move(o.id, o.x, o.y);
            tx.resize(o.id, o.w, o.h);
        }
        comp->commitTransaction();
    }

    // Pump messages to prevent 'not responding'
    MSG msg;
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    OutputDebugStringA(soakResult.summary.c_str());

    flutter::EncodableMap resultMap;
    resultMap[flutter::EncodableValue("passed")] = flutter::EncodableValue(soakResult.stable);
    resultMap[flutter::EncodableValue("totalFrames")] = flutter::EncodableValue(soakResult.totalFrames);
    resultMap[flutter::EncodableValue("droppedFrames")] = flutter::EncodableValue(soakResult.droppedFrames);
    resultMap[flutter::EncodableValue("dropRate")] = flutter::EncodableValue(soakResult.dropRate);
    resultMap[flutter::EncodableValue("maxFrameMs")] = flutter::EncodableValue(soakResult.maxFrameMs);
    resultMap[flutter::EncodableValue("invariantViolations")] = flutter::EncodableValue(soakResult.invariantViolations);
    resultMap[flutter::EncodableValue("orderingMismatches")] = flutter::EncodableValue(soakResult.orderingMismatches);
    resultMap[flutter::EncodableValue("maxSyncError")] = flutter::EncodableValue(soakResult.maxSyncError);
    resultMap[flutter::EncodableValue("peakMemoryKB")] = flutter::EncodableValue(static_cast<int>(soakResult.peakMemoryKB));
    resultMap[flutter::EncodableValue("endMemoryKB")] = flutter::EncodableValue(static_cast<int>(soakResult.endMemoryKB));
    resultMap[flutter::EncodableValue("memoryStable")] = flutter::EncodableValue(soakResult.memoryStable);
    resultMap[flutter::EncodableValue("summary")] = flutter::EncodableValue(soakResult.summary);

    result->Success(flutter::EncodableValue(resultMap));
}

// --- Gate 5: Crash Recovery Test ---
void MorphicPlugin::handleRunCrashRecoveryTest(
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result)
{
    if (!compositor_) {
        result->Error("NOT_INITIALIZED", "Compositor not initialized");
        return;
    }

    auto* comp = compositor_.get();

    // Count HWNDs before shutdown
    int hwndsBefore = CrashRecoveryTest::countMorphicWindows();
    int handlesBefore = CrashRecoveryTest::getProcessHandleCount();

    // Test: destroy all surfaces, then recreate
    std::vector<NodeId> surfaceIds;
    comp->sceneGraph().forEachSurface([&](const CompositionSurface* s) {
        surfaceIds.push_back(s->id());
    });

    for (NodeId id : surfaceIds) {
        comp->destroySurface(id);
    }
    comp->tick();

    // Verify: 0 MorphicSurface HWNDs remain
    int hwndsAfterDestroy = CrashRecoveryTest::countMorphicWindows();
    int handlesAfter = CrashRecoveryTest::getProcessHandleCount();

    bool passed = (hwndsAfterDestroy == 0);
    int handleDelta = handlesAfter - handlesBefore;

    std::string details;
    if (passed) {
        details = "Clean: " + std::to_string(hwndsBefore) + " HWNDs destroyed, 0 orphans";
    } else {
        details = "LEAK: " + std::to_string(hwndsAfterDestroy) + " orphan HWNDs remain";
    }
    details += " | HandleDelta=" + std::to_string(handleDelta);

    OutputDebugStringA(("CRASH RECOVERY: " + details + "\n").c_str());

    flutter::EncodableMap resultMap;
    resultMap[flutter::EncodableValue("passed")] = flutter::EncodableValue(passed);
    resultMap[flutter::EncodableValue("hwndsBefore")] = flutter::EncodableValue(hwndsBefore);
    resultMap[flutter::EncodableValue("hwndsAfter")] = flutter::EncodableValue(hwndsAfterDestroy);
    resultMap[flutter::EncodableValue("handleDelta")] = flutter::EncodableValue(handleDelta);
    resultMap[flutter::EncodableValue("details")] = flutter::EncodableValue(details);

    result->Success(flutter::EncodableValue(resultMap));
}

// --- Resize Storm Test ---
void MorphicPlugin::handleRunResizeStorm(
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result)
{
    if (!compositor_) {
        result->Error("NOT_INITIALIZED", "Compositor not initialized");
        return;
    }

    auto* comp = compositor_.get();

    // Gather surface IDs
    std::vector<NodeId> ids;
    comp->sceneGraph().forEachSurface([&](const CompositionSurface* s) {
        ids.push_back(s->id());
    });

    if (ids.size() < 2) {
        result->Error("NEED_SURFACES", "Need at least 2 surfaces for resize storm");
        return;
    }

    // Save original positions for restore
    struct Original { NodeId id; int x, y, w, h; };
    std::vector<Original> originals;
    comp->sceneGraph().forEachSurface([&](const CompositionSurface* s) {
        auto& t = s->worldTransform();
        originals.push_back({s->id(), t.x, t.y, t.width, t.height});
    });

    RenderResizeStorm storm;
    RenderResizeStorm::StormConfig config;
    config.iterations = 200;
    config.seed = 42;

    auto resizeFn = [comp](NodeId id, int w, int h) {
        comp->resizeSurface(id, w, h);
    };
    auto frameFn = [comp]() {
        comp->tick();
    };
    auto skewFn = [comp]() -> double {
        return comp->skewTracker().computeSkewDistribution().p99;
    };
    auto pacingFn = [comp]() -> double {
        return comp->framePacer().computeDistribution().pacingStability;
    };

    auto results_vec = storm.runAll(
        ids[0], ids[1], ids, resizeFn, frameFn, skewFn, pacingFn, config);

    // Restore original positions
    {
        auto& tx = comp->beginTransaction();
        for (const auto& o : originals) {
            tx.move(o.id, o.x, o.y);
            tx.resize(o.id, o.w, o.h);
        }
        comp->commitTransaction();
    }

    // Pump messages
    MSG msg;
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // Build response
    flutter::EncodableList resultList;
    bool allPassed = true;
    for (const auto& r : results_vec) {
        flutter::EncodableMap m;
        m[flutter::EncodableValue("scenario")] = flutter::EncodableValue(r.scenario);
        m[flutter::EncodableValue("passed")] = flutter::EncodableValue(r.passed);
        m[flutter::EncodableValue("totalResizes")] = flutter::EncodableValue(r.totalResizes);
        m[flutter::EncodableValue("avgResizeMs")] = flutter::EncodableValue(r.avgResizeMs);
        m[flutter::EncodableValue("maxResizeMs")] = flutter::EncodableValue(r.maxResizeMs);
        m[flutter::EncodableValue("skewP99")] = flutter::EncodableValue(r.skewP99);
        m[flutter::EncodableValue("convergenceMs")] = flutter::EncodableValue(r.convergenceMs);
        m[flutter::EncodableValue("pacingStability")] = flutter::EncodableValue(r.pacingStabilityDuring);
        m[flutter::EncodableValue("details")] = flutter::EncodableValue(r.details);
        resultList.push_back(flutter::EncodableValue(m));
        if (!r.passed) allPassed = false;

        OutputDebugStringA(("RESIZE_STORM [" + r.scenario + "]: " +
            (r.passed ? "PASS" : "FAIL") + " " + r.details + "\n").c_str());
    }

    flutter::EncodableMap resultMap;
    resultMap[flutter::EncodableValue("passed")] = flutter::EncodableValue(allPassed);
    resultMap[flutter::EncodableValue("scenarios")] = flutter::EncodableValue(resultList);

    result->Success(flutter::EncodableValue(resultMap));
}

// --- Zombie Behavioral Audit ---
void MorphicPlugin::handleRunZombieAudit(
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result)
{
    if (!compositor_) {
        result->Error("NOT_INITIALIZED", "Compositor not initialized");
        return;
    }

    ZombieAuditor auditor;
    auto auditResult = auditor.runFullAudit(
        compositor_->rendererManager(),
        5,     // 5 decay points
        1000   // 1 second intervals
    );

    // Build per-zombie detail list
    flutter::EncodableList zombieList;
    for (const auto& zb : auditResult.zombieBehaviors) {
        flutter::EncodableMap zm;
        zm[flutter::EncodableValue("rendererId")] = flutter::EncodableValue(static_cast<int64_t>(zb.rendererId));
        zm[flutter::EncodableValue("surfaceId")] = flutter::EncodableValue(static_cast<int64_t>(zb.lastSurfaceId));
        zm[flutter::EncodableValue("lifecycle")] = flutter::EncodableValue(static_cast<int>(zb.lifecycle));
        zm[flutter::EncodableValue("attachment")] = flutter::EncodableValue(static_cast<int>(zb.attachment));
        zm[flutter::EncodableValue("ageMs")] = flutter::EncodableValue(zb.ageMs);
        zm[flutter::EncodableValue("timeSinceCreationMs")] = flutter::EncodableValue(zb.timeSinceCreationMs);
        zm[flutter::EncodableValue("totalFramesNow")] = flutter::EncodableValue(zb.totalFramesNow);
        zm[flutter::EncodableValue("framesSinceZombify")] = flutter::EncodableValue(zb.framesSinceZombify);
        zm[flutter::EncodableValue("hwndVisible")] = flutter::EncodableValue(zb.hwndState.isVisible);
        zm[flutter::EncodableValue("hwndEnabled")] = flutter::EncodableValue(zb.hwndState.isEnabled);
        zm[flutter::EncodableValue("hwndHasParent")] = flutter::EncodableValue(zb.hwndState.hasParent);
        zm[flutter::EncodableValue("pendingMessages")] = flutter::EncodableValue(zb.hwndState.pendingMessages);
        zombieList.push_back(flutter::EncodableValue(zm));
    }

    // Build decay curve
    flutter::EncodableList decayList;
    for (const auto& dp : auditResult.decayCurve) {
        flutter::EncodableMap dm;
        dm[flutter::EncodableValue("elapsedMs")] = flutter::EncodableValue(dp.elapsedSinceZombifyMs);
        dm[flutter::EncodableValue("threadCount")] = flutter::EncodableValue(dp.processState.threadCount);
        dm[flutter::EncodableValue("handleCount")] = flutter::EncodableValue(static_cast<int64_t>(dp.processState.handleCount));
        dm[flutter::EncodableValue("gdiObjects")] = flutter::EncodableValue(static_cast<int64_t>(dp.processState.gdiObjects));
        dm[flutter::EncodableValue("workingSetKB")] = flutter::EncodableValue(static_cast<int64_t>(dp.processState.workingSetBytes / 1024));
        dm[flutter::EncodableValue("privateKB")] = flutter::EncodableValue(static_cast<int64_t>(dp.processState.privateBytes / 1024));
        dm[flutter::EncodableValue("zombieFrames")] = flutter::EncodableValue(dp.totalZombieFramesSinceZombify);
        dm[flutter::EncodableValue("zombieTimers")] = flutter::EncodableValue(dp.totalZombieTimers);
        dm[flutter::EncodableValue("zombieMessages")] = flutter::EncodableValue(dp.totalZombiePendingMessages);
        decayList.push_back(flutter::EncodableValue(dm));
    }

    // Build result
    flutter::EncodableMap resultMap;
    resultMap[flutter::EncodableValue("passed")] = flutter::EncodableValue(auditResult.passed);
    resultMap[flutter::EncodableValue("zombieCount")] = flutter::EncodableValue(auditResult.zombieCount);
    resultMap[flutter::EncodableValue("threadDelta")] = flutter::EncodableValue(auditResult.resourceDelta.threadDelta);
    resultMap[flutter::EncodableValue("handleDelta")] = flutter::EncodableValue(auditResult.resourceDelta.handleDelta);
    resultMap[flutter::EncodableValue("gdiDelta")] = flutter::EncodableValue(auditResult.resourceDelta.gdiDelta);
    resultMap[flutter::EncodableValue("workingSetDeltaKB")] = flutter::EncodableValue(auditResult.resourceDelta.workingSetDeltaKB);
    resultMap[flutter::EncodableValue("privateDeltaKB")] = flutter::EncodableValue(auditResult.resourceDelta.privateDeltaKB);
    resultMap[flutter::EncodableValue("cpuDeltaMs")] = flutter::EncodableValue(auditResult.resourceDelta.cpuTotalDeltaMs);
    resultMap[flutter::EncodableValue("anyZombieRendering")] = flutter::EncodableValue(auditResult.anyZombieRendering);
    resultMap[flutter::EncodableValue("anyZombieTimerActive")] = flutter::EncodableValue(auditResult.anyZombieTimerActive);
    resultMap[flutter::EncodableValue("maxZombieAgeMs")] = flutter::EncodableValue(auditResult.maxZombieAgeMs);
    resultMap[flutter::EncodableValue("details")] = flutter::EncodableValue(auditResult.details);
    resultMap[flutter::EncodableValue("zombies")] = flutter::EncodableValue(zombieList);
    resultMap[flutter::EncodableValue("decayCurve")] = flutter::EncodableValue(decayList);

    result->Success(flutter::EncodableValue(resultMap));
}

// --- GPU Pressure Profiling ---
void MorphicPlugin::handleRunGpuProfile(
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result)
{
    GpuProfiler profiler;

    if (!profiler.isAvailable()) {
        result->Error("GPU_NOT_AVAILABLE", "DXGI adapter not found");
        return;
    }

    auto profileResult = profiler.runProfile(5, 1000);  // 5 points × 1s

    // Build snapshot list
    flutter::EncodableList snapList;
    for (const auto& snap : profileResult.snapshots) {
        flutter::EncodableMap sm;
        sm[flutter::EncodableValue("label")] = flutter::EncodableValue(snap.label);
        sm[flutter::EncodableValue("dedicatedUsageMB")] = flutter::EncodableValue(snap.dedicatedUsageMB);
        sm[flutter::EncodableValue("sharedUsageMB")] = flutter::EncodableValue(snap.sharedUsageMB);
        sm[flutter::EncodableValue("dedicatedUsagePercent")] = flutter::EncodableValue(snap.dedicatedUsagePercent);
        snapList.push_back(flutter::EncodableValue(sm));
    }

    // Build delta list
    flutter::EncodableList deltaList;
    for (const auto& d : profileResult.deltas) {
        flutter::EncodableMap dm;
        dm[flutter::EncodableValue("fromLabel")] = flutter::EncodableValue(d.fromLabel);
        dm[flutter::EncodableValue("toLabel")] = flutter::EncodableValue(d.toLabel);
        dm[flutter::EncodableValue("dedicatedDeltaMB")] = flutter::EncodableValue(d.dedicatedDeltaMB);
        dm[flutter::EncodableValue("sharedDeltaMB")] = flutter::EncodableValue(d.sharedDeltaMB);
        dm[flutter::EncodableValue("elapsedMs")] = flutter::EncodableValue(d.elapsedMs);
        deltaList.push_back(flutter::EncodableValue(dm));
    }

    flutter::EncodableMap resultMap;
    resultMap[flutter::EncodableValue("available")] = flutter::EncodableValue(profileResult.available);
    resultMap[flutter::EncodableValue("adapterName")] = flutter::EncodableValue(profileResult.adapterName);
    resultMap[flutter::EncodableValue("baselineDedicatedMB")] = flutter::EncodableValue(profileResult.baseline.dedicatedUsageMB);
    resultMap[flutter::EncodableValue("baselineSharedMB")] = flutter::EncodableValue(profileResult.baseline.sharedUsageMB);
    resultMap[flutter::EncodableValue("baselineBudgetPercent")] = flutter::EncodableValue(profileResult.baseline.dedicatedUsagePercent);
    resultMap[flutter::EncodableValue("totalDedicatedDeltaMB")] = flutter::EncodableValue(profileResult.totalDedicatedDeltaMB);
    resultMap[flutter::EncodableValue("totalSharedDeltaMB")] = flutter::EncodableValue(profileResult.totalSharedDeltaMB);
    resultMap[flutter::EncodableValue("vramGrowthMonotonic")] = flutter::EncodableValue(profileResult.vramGrowthMonotonic);
    resultMap[flutter::EncodableValue("vramStabilized")] = flutter::EncodableValue(profileResult.vramStabilizedAfterHide);
    resultMap[flutter::EncodableValue("details")] = flutter::EncodableValue(profileResult.details);
    resultMap[flutter::EncodableValue("snapshots")] = flutter::EncodableValue(snapList);
    resultMap[flutter::EncodableValue("deltas")] = flutter::EncodableValue(deltaList);

    // Also add current VRAM to getMetrics-style output
    auto currentSnap = profiler.captureSnapshot("current");
    resultMap[flutter::EncodableValue("currentDedicatedMB")] = flutter::EncodableValue(currentSnap.dedicatedUsageMB);
    resultMap[flutter::EncodableValue("currentSharedMB")] = flutter::EncodableValue(currentSnap.sharedUsageMB);
    resultMap[flutter::EncodableValue("totalAdapterVRAM_MB")] = flutter::EncodableValue(
        static_cast<double>(currentSnap.adapterDedicatedVideoMemory) / (1024.0 * 1024.0));

    result->Success(flutter::EncodableValue(resultMap));
}

// --- Zombie Decay Tracking (Phase 2A.4) ---
void MorphicPlugin::handleRunDecayTracking(
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result)
{
    if (!compositor_) {
        result->Error("NOT_INITIALIZED", "Compositor not initialized");
        return;
    }

    ZombieDecayTracker tracker;
    // Use shorter intervals for UI responsiveness: 1s, 2s, 5s, 10s, 30s = ~48s
    auto decayResult = tracker.runDecayTracking(
        compositor_->rendererManager(),
        {1000, 2000, 5000, 10000, 30000}
    );

    // Build curve
    flutter::EncodableList curveList;
    for (const auto& dp : decayResult.curve) {
        flutter::EncodableMap dm;
        dm[flutter::EncodableValue("index")] = flutter::EncodableValue(dp.index);
        dm[flutter::EncodableValue("elapsedMs")] = flutter::EncodableValue(dp.elapsedSinceStartMs);
        dm[flutter::EncodableValue("intervalMs")] = flutter::EncodableValue(dp.intervalMs);
        dm[flutter::EncodableValue("threadCount")] = flutter::EncodableValue(dp.threadCount);
        dm[flutter::EncodableValue("handleCount")] = flutter::EncodableValue(dp.handleCount);
        dm[flutter::EncodableValue("workingSetKB")] = flutter::EncodableValue(dp.workingSetKB);
        dm[flutter::EncodableValue("privateKB")] = flutter::EncodableValue(dp.privateKB);
        dm[flutter::EncodableValue("dedicatedVramMB")] = flutter::EncodableValue(dp.dedicatedVramMB);
        dm[flutter::EncodableValue("dormantThreads")] = flutter::EncodableValue(dp.dormantThreads);
        dm[flutter::EncodableValue("sporadicThreads")] = flutter::EncodableValue(dp.sporadicThreads);
        dm[flutter::EncodableValue("periodicThreads")] = flutter::EncodableValue(dp.periodicThreads);
        dm[flutter::EncodableValue("activeThreads")] = flutter::EncodableValue(dp.activeThreads);
        dm[flutter::EncodableValue("cpuDeltaMs")] = flutter::EncodableValue(dp.cpuDeltaMs);
        dm[flutter::EncodableValue("phase")] = flutter::EncodableValue(std::string(ZombieDecayTracker::phaseName(dp.phase)));
        dm[flutter::EncodableValue("marker")] = flutter::EncodableValue(dp.marker);
        curveList.push_back(flutter::EncodableValue(dm));
    }

    flutter::EncodableMap resultMap;
    resultMap[flutter::EncodableValue("zombieCount")] = flutter::EncodableValue(decayResult.zombieCount);
    resultMap[flutter::EncodableValue("totalObservationMs")] = flutter::EncodableValue(decayResult.totalObservationMs);
    resultMap[flutter::EncodableValue("totalThreadDelta")] = flutter::EncodableValue(decayResult.totalThreadDelta);
    resultMap[flutter::EncodableValue("totalPrivateDeltaKB")] = flutter::EncodableValue(decayResult.totalPrivateDeltaKB);
    resultMap[flutter::EncodableValue("totalVramDeltaMB")] = flutter::EncodableValue(decayResult.totalVramDeltaMB);
    resultMap[flutter::EncodableValue("resourcesDecayed")] = flutter::EncodableValue(decayResult.resourcesDecayed);
    resultMap[flutter::EncodableValue("vramStabilized")] = flutter::EncodableValue(decayResult.vramStabilized);
    resultMap[flutter::EncodableValue("threadsDecayed")] = flutter::EncodableValue(decayResult.threadsDecayed);
    resultMap[flutter::EncodableValue("details")] = flutter::EncodableValue(decayResult.details);
    resultMap[flutter::EncodableValue("transitionLog")] = flutter::EncodableValue(decayResult.transitionLog);
    resultMap[flutter::EncodableValue("curve")] = flutter::EncodableValue(curveList);

    result->Success(flutter::EncodableValue(resultMap));
}

// --- Thread Activity Attribution (Phase 2A.4) ---
void MorphicPlugin::handleRunThreadAudit(
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result)
{
    ThreadActivityAuditor auditor;
    auto auditResult = auditor.runAudit(5000);  // 5 second observation

    // Build per-thread list (top 20 only to avoid huge payloads)
    flutter::EncodableList threadList;
    int count = (std::min)(20, static_cast<int>(auditResult.activities.size()));
    for (int i = 0; i < count; i++) {
        const auto& ta = auditResult.activities[i];
        flutter::EncodableMap tm;
        tm[flutter::EncodableValue("threadId")] = flutter::EncodableValue(static_cast<int64_t>(ta.threadId));
        tm[flutter::EncodableValue("priority")] = flutter::EncodableValue(ta.priority);
        tm[flutter::EncodableValue("ageMs")] = flutter::EncodableValue(ta.ageMs);
        tm[flutter::EncodableValue("kernelDeltaMs")] = flutter::EncodableValue(ta.kernelDeltaMs);
        tm[flutter::EncodableValue("userDeltaMs")] = flutter::EncodableValue(ta.userDeltaMs);
        tm[flutter::EncodableValue("totalDeltaMs")] = flutter::EncodableValue(ta.totalDeltaMs);
        tm[flutter::EncodableValue("cycleDelta")] = flutter::EncodableValue(static_cast<int64_t>(ta.cycleDelta));
        tm[flutter::EncodableValue("cadence")] = flutter::EncodableValue(std::string(ThreadActivityAuditor::cadenceName(ta.cadence)));
        tm[flutter::EncodableValue("estimatedWakesPerSec")] = flutter::EncodableValue(ta.estimatedWakesPerSec);
        threadList.push_back(flutter::EncodableValue(tm));
    }

    flutter::EncodableMap resultMap;
    resultMap[flutter::EncodableValue("totalThreads")] = flutter::EncodableValue(auditResult.totalThreads);
    resultMap[flutter::EncodableValue("observationMs")] = flutter::EncodableValue(auditResult.observationWindowMs);
    resultMap[flutter::EncodableValue("dormantCount")] = flutter::EncodableValue(auditResult.dormantCount);
    resultMap[flutter::EncodableValue("sporadicCount")] = flutter::EncodableValue(auditResult.sporadicCount);
    resultMap[flutter::EncodableValue("periodicCount")] = flutter::EncodableValue(auditResult.periodicCount);
    resultMap[flutter::EncodableValue("activeCount")] = flutter::EncodableValue(auditResult.activeCount);
    resultMap[flutter::EncodableValue("totalCpuDeltaMs")] = flutter::EncodableValue(auditResult.totalCpuDeltaMs);
    resultMap[flutter::EncodableValue("activeCpuDeltaMs")] = flutter::EncodableValue(auditResult.activeCpuDeltaMs);
    resultMap[flutter::EncodableValue("details")] = flutter::EncodableValue(auditResult.details);
    resultMap[flutter::EncodableValue("threads")] = flutter::EncodableValue(threadList);

    result->Success(flutter::EncodableValue(resultMap));
}

// --- Frame Cadence Monitor (Phase 2A.5) ---

void MorphicPlugin::handleFrameProduced(
    const flutter::EncodableMap& args,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result)
{
    int engineId = static_cast<int>(getInt(args, "engineId", 0));
    double timestampMs = getDouble(args, "timestampMs", 0.0);

    FrameCadenceMonitor::instance().recordFrame(engineId, timestampMs);

    // Phase 2B.1: Capture recovery metrics if reported
    double recoveryMs = getDouble(args, "recoveryMs", -1.0);
    if (recoveryMs >= 0.0) {
        int resumeCycle = static_cast<int>(getInt(args, "resumeCycle", 0));
        int framesWhileParked = static_cast<int>(getInt(args, "framesWhileParked", 0));
        // Store in FrameCadenceMonitor for snapshot access
        FrameCadenceMonitor::instance().recordRecovery(engineId, recoveryMs, resumeCycle, framesWhileParked);
        OutputDebugStringA(("BENCHMARK: Engine " + std::to_string(engineId) +
            " recovery=" + std::to_string(recoveryMs) + "ms" +
            " cycle=" + std::to_string(resumeCycle) +
            " framesWhileParked=" + std::to_string(framesWhileParked) + "\n").c_str());
    }

    // Lightweight — just acknowledge
    result->Success(flutter::EncodableValue(true));
}

void MorphicPlugin::handleQueryFrameCadence(
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result)
{
    auto allData = FrameCadenceMonitor::instance().queryAll();

    flutter::EncodableList engineList;
    for (const auto& [id, data] : allData) {
        flutter::EncodableMap em;
        em[flutter::EncodableValue("engineId")] = flutter::EncodableValue(data.engineId);
        em[flutter::EncodableValue("totalFrames")] = flutter::EncodableValue(data.totalFrames);
        em[flutter::EncodableValue("estimatedFps")] = flutter::EncodableValue(data.estimatedFps);
        em[flutter::EncodableValue("avgIntervalMs")] = flutter::EncodableValue(data.avgIntervalMs);
        em[flutter::EncodableValue("isHidden")] = flutter::EncodableValue(data.isHidden);
        em[flutter::EncodableValue("animationsPaused")] = flutter::EncodableValue(data.animationsPaused);
        em[flutter::EncodableValue("framesWhileHidden")] = flutter::EncodableValue(data.framesWhileHidden);
        em[flutter::EncodableValue("framesWhileAnimPaused")] = flutter::EncodableValue(data.framesWhileAnimPaused);
        em[flutter::EncodableValue("state")] = flutter::EncodableValue(
            std::string(FrameCadenceMonitor::stateName(data.state)));

        // Build interval histogram (top 10 buckets by count)
        flutter::EncodableMap histMap;
        std::vector<std::pair<int, int>> sorted(data.intervalHistogram.begin(),
            data.intervalHistogram.end());
        std::sort(sorted.begin(), sorted.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });
        int histCount = (std::min)(10, static_cast<int>(sorted.size()));
        for (int i = 0; i < histCount; i++) {
            histMap[flutter::EncodableValue(std::to_string(sorted[i].first) + "ms")] =
                flutter::EncodableValue(sorted[i].second);
        }
        em[flutter::EncodableValue("intervalHistogram")] = flutter::EncodableValue(histMap);

        engineList.push_back(flutter::EncodableValue(em));
    }

    flutter::EncodableMap resultMap;
    resultMap[flutter::EncodableValue("engineCount")] = flutter::EncodableValue(
        static_cast<int>(allData.size()));
    resultMap[flutter::EncodableValue("summary")] = flutter::EncodableValue(
        FrameCadenceMonitor::instance().summary());
    resultMap[flutter::EncodableValue("engines")] = flutter::EncodableValue(engineList);

    result->Success(flutter::EncodableValue(resultMap));
}

void MorphicPlugin::handlePauseRendererAnimations(
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result)
{
    if (!compositor_) {
        result->Error("NOT_INITIALIZED", "No compositor");
        return;
    }

    // Send pause command to all secondary engines
    int sent = sendCommandToSecondaryEngines("pauseAnimations");

    // Mark in monitor
    auto allData = FrameCadenceMonitor::instance().queryAll();
    for (const auto& [id, data] : allData) {
        FrameCadenceMonitor::instance().setAnimationsPaused(id, true);
    }

    flutter::EncodableMap rm;
    rm[flutter::EncodableValue("enginesSent")] = flutter::EncodableValue(sent);
    result->Success(flutter::EncodableValue(rm));
}

void MorphicPlugin::handleResumeRendererAnimations(
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result)
{
    if (!compositor_) {
        result->Error("NOT_INITIALIZED", "No compositor");
        return;
    }

    int sent = sendCommandToSecondaryEngines("resumeAnimations");

    auto allData = FrameCadenceMonitor::instance().queryAll();
    for (const auto& [id, data] : allData) {
        FrameCadenceMonitor::instance().setAnimationsPaused(id, false);
    }

    flutter::EncodableMap rm;
    rm[flutter::EncodableValue("enginesSent")] = flutter::EncodableValue(sent);
    result->Success(flutter::EncodableValue(rm));
}

int MorphicPlugin::sendCommandToSecondaryEngines(const std::string& command)
{
    int sent = 0;
    if (!compositor_) return sent;

    auto& mgr = compositor_->rendererManager();

    // Collect renderer IDs first from const records
    std::vector<RenderId> ids;
    for (const auto& [renderId, rec] : mgr.records()) {
        ids.push_back(renderId);
    }

    // Now iterate with non-const access
    for (auto renderId : ids) {
        auto* renderer = mgr.getRenderer(renderId);
        if (!renderer) continue;

        auto* flutterRenderer = dynamic_cast<FlutterRenderer*>(renderer);
        if (!flutterRenderer) continue;

        auto* engine = flutterRenderer->engine();
        if (!engine) continue;

        auto* messenger = FlutterDesktopEngineGetMessenger(engine);
        if (!messenger) continue;

        auto& codec = flutter::StandardMethodCodec::GetInstance();
        flutter::MethodCall<flutter::EncodableValue> call(command,
            std::make_unique<flutter::EncodableValue>(flutter::EncodableMap()));
        auto encoded = codec.EncodeMethodCall(call);

        FlutterDesktopMessengerSend(messenger, "morphic",
            encoded->data(), encoded->size());
        sent++;
    }
    return sent;
}

// --- Renderer integration (Phase 2A) ---

void MorphicPlugin::handleAttachRenderer(
    const flutter::EncodableMap& args,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
    if (!compositor_) {
        result->Error("NO_COMPOSITOR", "Not initialized");
        return;
    }

    NodeId surfaceId = static_cast<NodeId>(getInt(args, "surfaceId"));
    std::string rendererType = "gdi";

    auto typeIt = args.find(flutter::EncodableValue("type"));
    if (typeIt != args.end()) {
        if (auto* str = std::get_if<std::string>(&typeIt->second)) {
            rendererType = *str;
        }
    }

    std::unique_ptr<RendererSurface> renderer;

    if (rendererType == "null") {
        renderer = std::make_unique<NullRenderer>();
    } else if (rendererType == "flutter") {
        // Get the executable directory for asset paths
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        std::wstring exeDir(exePath);
        exeDir = exeDir.substr(0, exeDir.find_last_of(L"\\/"));

        FlutterRenderer::EngineConfig config;
        config.assetsPath = exeDir + L"\\data\\flutter_assets";
        config.icuDataPath = exeDir + L"\\data\\icudtl.dat";
        // Use dedicated secondary entrypoint — avoids rendering the full app UI
        config.dartEntrypoint = "secondaryMain";

        renderer = std::make_unique<FlutterRenderer>(config);

        OutputDebugStringA(("PLUGIN: Creating FlutterRenderer for surface " +
            std::to_string(surfaceId) + "\n").c_str());
    } else {
        // Default: GDI renderer with surface color
        auto* surface = compositor_->sceneGraph().getSurface(surfaceId);
        COLORREF color = RGB(0x16, 0x21, 0x3e);
        if (surface) {
            uint32_t c = surface->color();
            color = RGB((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
        }
        renderer = std::make_unique<GdiRenderer>(color);
    }

    bool ok = compositor_->attachRenderer(surfaceId, std::move(renderer));

    flutter::EncodableMap resultMap;
    resultMap[flutter::EncodableValue("success")] = flutter::EncodableValue(ok);
    resultMap[flutter::EncodableValue("type")] = flutter::EncodableValue(rendererType);
    result->Success(flutter::EncodableValue(resultMap));
}

void MorphicPlugin::handleDetachRenderer(
    const flutter::EncodableMap& args,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
    if (!compositor_) {
        result->Error("NO_COMPOSITOR", "Not initialized");
        return;
    }

    NodeId surfaceId = static_cast<NodeId>(getInt(args, "surfaceId"));
    compositor_->detachRenderer(surfaceId);
    result->Success(flutter::EncodableValue(true));
}

void MorphicPlugin::handleTestLifecycleOrchestration(
    const flutter::EncodableMap& args,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
    if (!compositor_) {
        result->Error("NO_COMPOSITOR", "Not initialized");
        return;
    }

    // Get target state from args: "active", "throttled", "parked", "dormant"
    std::string targetStr = "parked";
    auto tIt = args.find(flutter::EncodableValue("target"));
    if (tIt != args.end() && std::holds_alternative<std::string>(tIt->second)) {
        targetStr = std::get<std::string>(tIt->second);
    }

    // Map string to ActivityState
    ActivityState target = ActivityState::Parked;
    if (targetStr == "active") target = ActivityState::Active;
    else if (targetStr == "throttled") target = ActivityState::Throttled;
    else if (targetStr == "parked") target = ActivityState::Parked;
    else if (targetStr == "dormant") target = ActivityState::Dormant;

    auto& mgr = compositor_->rendererManager();
    auto& ctrl = compositor_->workloadController();

    flutter::EncodableList results;
    int transitioned = 0;

    // B3 fix: Mark recovery/parking timestamps BEFORE sending lifecycle commands.
    // Uses FrameCadenceMonitor engine IDs (Dart namespace).
    {
        auto allData = FrameCadenceMonitor::instance().queryAll();
        for (const auto& [engineId, data] : allData) {
            if (target == ActivityState::Active) {
                FrameCadenceMonitor::instance().markResuming(engineId);
            } else if (target == ActivityState::Parked || target == ActivityState::Dormant) {
                FrameCadenceMonitor::instance().markParking(engineId);
            }
        }
    }

    // Route ALL transitions through governance authority.
    // force=true: bypass invariants for adversarial testing,
    // but still log, record costs, and track overrides.
    for (const auto& [renderId, rec] : mgr.records()) {
        if (rec.type != RendererSurface::Type::Flutter) continue;

        auto txResult = ctrl.requestTransition(
            renderId, target, "testLifecycleOrchestration", /*force=*/true);

        // Sync RendererManager state tracking
        if (txResult.transitioned) {
            mgr.transitionActivity(renderId, target);
        }

        flutter::EncodableMap entry;
        entry[flutter::EncodableValue("rendererId")] = flutter::EncodableValue(static_cast<int64_t>(renderId));
        entry[flutter::EncodableValue("target")] = flutter::EncodableValue(targetStr);
        entry[flutter::EncodableValue("result")] = flutter::EncodableValue(std::string(toString(txResult.commandResult)));
        if (txResult.invariantBypassed) {
            entry[flutter::EncodableValue("invariantBypassed")] = flutter::EncodableValue(true);
        }
        results.push_back(flutter::EncodableValue(entry));

        if (txResult.transitioned) {
            transitioned++;
            // Mark as governance-dirty so evaluate() runs with updated state
            compositor_->governanceScheduler().markDirty(
                renderId, morphic::GovernanceDirtyReason::ExternalCommand);
        }
    }

    // Drain governance — evaluate dirty renderers with current visibility
    compositor_->drainGovernance();

    flutter::EncodableMap rm;
    rm[flutter::EncodableValue("target")] = flutter::EncodableValue(targetStr);
    rm[flutter::EncodableValue("transitioned")] = flutter::EncodableValue(transitioned);
    rm[flutter::EncodableValue("results")] = flutter::EncodableValue(results);
    result->Success(flutter::EncodableValue(rm));
}

void MorphicPlugin::handleTakeBenchmarkSnapshot(
    const flutter::EncodableMap& args,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
    if (!compositor_) {
        result->Error("NO_COMPOSITOR", "Not initialized");
        return;
    }

    double elapsedSec = getDouble(args, "elapsedSec", 0.0);
    std::string phase = "active";
    auto pIt = args.find(flutter::EncodableValue("phase"));
    if (pIt != args.end() && std::holds_alternative<std::string>(pIt->second)) {
        phase = std::get<std::string>(pIt->second);
    }
    int64_t prevFrames = static_cast<int64_t>(getInt(args, "prevTotalFrames", 0));

    // Sync frame metrics from live renderers into records
    compositor_->rendererManager().syncAllMetrics();

    auto snapshot = SustainabilityBenchmark::takeSnapshot(
        compositor_->rendererManager(), elapsedSec, prevFrames, phase);

    // Return as map
    flutter::EncodableMap rm;
    rm[flutter::EncodableValue("timestampSec")] = flutter::EncodableValue(snapshot.timestampSec);
    rm[flutter::EncodableValue("privateKB")] = flutter::EncodableValue(snapshot.privateKB);
    rm[flutter::EncodableValue("workingSetKB")] = flutter::EncodableValue(snapshot.workingSetKB);
    rm[flutter::EncodableValue("threadCount")] = flutter::EncodableValue(static_cast<int64_t>(snapshot.threadCount));
    rm[flutter::EncodableValue("handleCount")] = flutter::EncodableValue(static_cast<int64_t>(snapshot.handleCount));
    rm[flutter::EncodableValue("gdiObjects")] = flutter::EncodableValue(static_cast<int64_t>(snapshot.gdiObjects));
    rm[flutter::EncodableValue("userObjects")] = flutter::EncodableValue(static_cast<int64_t>(snapshot.userObjects));
    rm[flutter::EncodableValue("rendererTotal")] = flutter::EncodableValue(static_cast<int64_t>(snapshot.rendererTotal));
    rm[flutter::EncodableValue("rendererActive")] = flutter::EncodableValue(static_cast<int64_t>(snapshot.rendererActive));
    rm[flutter::EncodableValue("rendererParked")] = flutter::EncodableValue(static_cast<int64_t>(snapshot.rendererParked));
    rm[flutter::EncodableValue("rendererThrottled")] = flutter::EncodableValue(static_cast<int64_t>(snapshot.rendererThrottled));
    rm[flutter::EncodableValue("rendererDormant")] = flutter::EncodableValue(static_cast<int64_t>(snapshot.rendererDormant));
    rm[flutter::EncodableValue("totalFramesRendered")] = flutter::EncodableValue(snapshot.totalFramesRendered);
    rm[flutter::EncodableValue("frameDelta")] = flutter::EncodableValue(snapshot.frameDeltaSinceLastSnapshot);
    rm[flutter::EncodableValue("cadenceTotalFrames")] = flutter::EncodableValue(snapshot.cadenceTotalFrames);
    rm[flutter::EncodableValue("cadenceHiddenFrames")] = flutter::EncodableValue(snapshot.cadenceHiddenFrames);
    rm[flutter::EncodableValue("cadenceParkedFrames")] = flutter::EncodableValue(snapshot.cadenceParkedFrames);
    rm[flutter::EncodableValue("cadenceActiveFps")] = flutter::EncodableValue(snapshot.cadenceActiveFps);
    rm[flutter::EncodableValue("recoveryLastMs")] = flutter::EncodableValue(snapshot.recoveryLastMs);
    rm[flutter::EncodableValue("recoveryBestMs")] = flutter::EncodableValue(snapshot.recoveryBestMs);
    rm[flutter::EncodableValue("recoveryWorstMs")] = flutter::EncodableValue(snapshot.recoveryWorstMs);
    rm[flutter::EncodableValue("recoveryResumeCycles")] = flutter::EncodableValue(static_cast<int64_t>(snapshot.recoveryResumeCycles));
    rm[flutter::EncodableValue("phase")] = flutter::EncodableValue(snapshot.phase);

    // Governance telemetry
    auto govSummary = compositor_->workloadController().governanceSummary();
    rm[flutter::EncodableValue("budgetUtil")] = flutter::EncodableValue(static_cast<double>(govSummary.budgetUtilization));
    rm[flutter::EncodableValue("pressureLevel")] = flutter::EncodableValue(std::string(govSummary.pressureLevel));
    rm[flutter::EncodableValue("govTransitions")] = flutter::EncodableValue(static_cast<int64_t>(govSummary.totalTransitions));
    rm[flutter::EncodableValue("invariantOverrides")] = flutter::EncodableValue(static_cast<int64_t>(govSummary.invariantOverrides));
    rm[flutter::EncodableValue("churnDetected")] = flutter::EncodableValue(govSummary.churnDetected);
    rm[flutter::EncodableValue("dominantReasons")] = flutter::EncodableValue(govSummary.dominantReasons);

    // Phase 3B.3: Enriched governance semantics telemetry
    rm[flutter::EncodableValue("visibilityDriven")] = flutter::EncodableValue(static_cast<int64_t>(govSummary.visibilityDrivenCount));
    rm[flutter::EncodableValue("workloadDriven")] = flutter::EncodableValue(static_cast<int64_t>(govSummary.workloadDrivenCount));
    rm[flutter::EncodableValue("budgetDenials")] = flutter::EncodableValue(static_cast<int64_t>(govSummary.budgetDenialCount));
    rm[flutter::EncodableValue("suppressionBlocks")] = flutter::EncodableValue(static_cast<int64_t>(govSummary.suppressionBlockCount));
    rm[flutter::EncodableValue("desiredActualMismatch")] = flutter::EncodableValue(static_cast<int64_t>(govSummary.desiredActualMismatch));
    rm[flutter::EncodableValue("budgetOverBudget")] = flutter::EncodableValue(govSummary.budgetOverBudget);
    rm[flutter::EncodableValue("maxMismatchMs")] = flutter::EncodableValue(govSummary.maxMismatchDurationMs);
    rm[flutter::EncodableValue("budgetPressure")] = flutter::EncodableValue(std::string(govSummary.budgetPressureLevel));
    // v3: Economic governance telemetry
    rm[flutter::EncodableValue("budgetEscalated")] = flutter::EncodableValue(static_cast<int64_t>(govSummary.budgetEscalatedCount));
    rm[flutter::EncodableValue("budgetProtected")] = flutter::EncodableValue(static_cast<int64_t>(govSummary.budgetProtectedCount));
    rm[flutter::EncodableValue("budgetMismatchMs")] = flutter::EncodableValue(govSummary.maxBudgetMismatchMs);
    rm[flutter::EncodableValue("suppressionMismatchMs")] = flutter::EncodableValue(govSummary.maxSuppressionMismatchMs);

    // Build CSV row with governance columns appended
    std::string csvRow = SustainabilityBenchmark::snapshotToCsvRow(snapshot);
    csvRow += "," + std::to_string(govSummary.budgetUtilization);
    csvRow += "," + std::string(govSummary.pressureLevel);
    csvRow += "," + std::to_string(govSummary.totalTransitions);
    csvRow += "," + std::to_string(govSummary.invariantOverrides);
    csvRow += "," + std::to_string(govSummary.budgetDrivenCount);
    csvRow += "," + std::string(govSummary.churnDetected ? "Y" : "N");
    // Phase 3B.3 columns
    csvRow += "," + std::to_string(govSummary.visibilityDrivenCount);
    csvRow += "," + std::to_string(govSummary.workloadDrivenCount);
    csvRow += "," + std::to_string(govSummary.budgetDenialCount);
    csvRow += "," + std::to_string(govSummary.suppressionBlockCount);
    csvRow += "," + std::to_string(govSummary.desiredActualMismatch);
    csvRow += "," + std::string(govSummary.budgetOverBudget ? "Y" : "N");
    // v3 economic columns
    csvRow += "," + std::to_string(govSummary.budgetEscalatedCount);
    csvRow += "," + std::to_string(govSummary.budgetProtectedCount);
    csvRow += "," + std::to_string(static_cast<int>(govSummary.maxMismatchDurationMs));
    csvRow += "," + std::to_string(static_cast<int>(govSummary.maxBudgetMismatchMs));
    csvRow += "," + std::to_string(static_cast<int>(govSummary.maxSuppressionMismatchMs));
    csvRow += "," + std::string(govSummary.budgetPressureLevel);

    // Phase 3C: Recovery experience telemetry (Priority 1-4)
    auto recoveryRpt = compositor_->workloadController().recoveryReport();
    rm[flutter::EncodableValue("recoveringCount")] = flutter::EncodableValue(static_cast<int64_t>(recoveryRpt.recoveringCount));
    rm[flutter::EncodableValue("staleCount")] = flutter::EncodableValue(static_cast<int64_t>(recoveryRpt.staleCount));
    rm[flutter::EncodableValue("worstColdMs")] = flutter::EncodableValue(recoveryRpt.worstColdRecoveryMs);
    rm[flutter::EncodableValue("worstWarmMs")] = flutter::EncodableValue(recoveryRpt.worstWarmRecoveryMs);
    rm[flutter::EncodableValue("worstTotalMs")] = flutter::EncodableValue(recoveryRpt.worstTotalRecoveryMs);
    rm[flutter::EncodableValue("worstStaleMs")] = flutter::EncodableValue(recoveryRpt.worstStaleExposureMs);
    rm[flutter::EncodableValue("continuityBreaks")] = flutter::EncodableValue(static_cast<int64_t>(recoveryRpt.totalContinuityBreaks));
    rm[flutter::EncodableValue("continuityScore")] = flutter::EncodableValue(recoveryRpt.lowestContinuityScore);
    rm[flutter::EncodableValue("recInstant")] = flutter::EncodableValue(static_cast<int64_t>(recoveryRpt.instantCount));
    rm[flutter::EncodableValue("recSmooth")] = flutter::EncodableValue(static_cast<int64_t>(recoveryRpt.smoothCount));
    rm[flutter::EncodableValue("recHitchy")] = flutter::EncodableValue(static_cast<int64_t>(recoveryRpt.hitchyCount));
    rm[flutter::EncodableValue("recDisruptive")] = flutter::EncodableValue(static_cast<int64_t>(recoveryRpt.disruptiveCount));
    rm[flutter::EncodableValue("dominantIssue")] = flutter::EncodableValue(std::string(recoveryRpt.dominantIssue));
    // Priority 1
    rm[flutter::EncodableValue("burstObserved")] = flutter::EncodableValue(recoveryRpt.anyBurstObserved);
    rm[flutter::EncodableValue("burstEpisodes")] = flutter::EncodableValue(static_cast<int64_t>(recoveryRpt.totalBurstEpisodes));
    rm[flutter::EncodableValue("expInstability")] = flutter::EncodableValue(recoveryRpt.anyExperientialInstability);
    rm[flutter::EncodableValue("instabilityReason")] = flutter::EncodableValue(std::string(recoveryRpt.worstInstabilityReason));
    // Priority 2
    rm[flutter::EncodableValue("coldP50")] = flutter::EncodableValue(recoveryRpt.coldP50);
    rm[flutter::EncodableValue("coldP95")] = flutter::EncodableValue(recoveryRpt.coldP95);
    rm[flutter::EncodableValue("totalP50")] = flutter::EncodableValue(recoveryRpt.totalP50);
    rm[flutter::EncodableValue("totalP95")] = flutter::EncodableValue(recoveryRpt.totalP95);
    // Priority 4
    rm[flutter::EncodableValue("visualSilenceMs")] = flutter::EncodableValue(recoveryRpt.totalVisualSilenceMs);
    rm[flutter::EncodableValue("silenceEvents")] = flutter::EncodableValue(static_cast<int64_t>(recoveryRpt.totalVisualSilenceEvents));

    // 3C CSV columns (expanded with Priority 1-4)
    csvRow += "," + std::to_string(recoveryRpt.recoveringCount);
    csvRow += "," + std::to_string(recoveryRpt.staleCount);
    csvRow += "," + std::to_string(static_cast<int>(recoveryRpt.worstColdRecoveryMs));
    csvRow += "," + std::to_string(static_cast<int>(recoveryRpt.worstTotalRecoveryMs));
    csvRow += "," + std::to_string(static_cast<int>(recoveryRpt.worstStaleExposureMs));
    csvRow += "," + std::to_string(recoveryRpt.totalContinuityBreaks);
    csvRow += "," + std::to_string(recoveryRpt.lowestContinuityScore).substr(0, 4);
    csvRow += "," + std::to_string(recoveryRpt.instantCount);
    csvRow += "," + std::to_string(recoveryRpt.smoothCount);
    csvRow += "," + std::to_string(recoveryRpt.hitchyCount);
    csvRow += "," + std::to_string(recoveryRpt.disruptiveCount);
    // Priority 1: Split semantics
    csvRow += "," + std::string(recoveryRpt.anyBurstObserved ? "Y" : "N");
    csvRow += "," + std::to_string(recoveryRpt.totalBurstEpisodes);
    csvRow += "," + std::string(recoveryRpt.anyExperientialInstability ? "Y" : "N");
    csvRow += "," + std::string(recoveryRpt.worstInstabilityReason);
    // Priority 2: Distribution
    csvRow += "," + std::to_string(static_cast<int>(recoveryRpt.coldP50));
    csvRow += "," + std::to_string(static_cast<int>(recoveryRpt.coldP95));
    csvRow += "," + std::to_string(static_cast<int>(recoveryRpt.totalP50));
    csvRow += "," + std::to_string(static_cast<int>(recoveryRpt.totalP95));
    csvRow += "," + std::to_string(static_cast<int>(recoveryRpt.coldJitter));
    csvRow += "," + std::to_string(recoveryRpt.distributionSamples);
    // Priority 4: Visual silence
    csvRow += "," + std::to_string(static_cast<int>(recoveryRpt.totalVisualSilenceMs));
    csvRow += "," + std::to_string(recoveryRpt.totalVisualSilenceEvents);
    // Pre-Fix 2: Recovery saturation
    csvRow += "," + std::to_string(recoveryRpt.saturation.concurrentRecoveries);
    csvRow += "," + std::to_string(recoveryRpt.saturation.peakConcurrency);
    csvRow += "," + std::to_string(recoveryRpt.saturation.pendingWakes);
    // Pre-Fix 3: Wake ordering
    csvRow += "," + std::to_string(recoveryRpt.deferredWakeCount);
    csvRow += "," + std::to_string(recoveryRpt.protectedWakeCount);
    // Dominant issue
    csvRow += "," + std::string(recoveryRpt.dominantIssue);

    // Saturation + ordering in return map
    rm[flutter::EncodableValue("concRecoveries")] = flutter::EncodableValue(static_cast<int64_t>(recoveryRpt.saturation.concurrentRecoveries));
    rm[flutter::EncodableValue("peakConcurrency")] = flutter::EncodableValue(static_cast<int64_t>(recoveryRpt.saturation.peakConcurrency));
    rm[flutter::EncodableValue("deferredWakes")] = flutter::EncodableValue(static_cast<int64_t>(recoveryRpt.deferredWakeCount));
    rm[flutter::EncodableValue("protectedWakes")] = flutter::EncodableValue(static_cast<int64_t>(recoveryRpt.protectedWakeCount));

    // Phase 3D: Adaptive policy telemetry
    // Dispatch pending stagger wakes before snapshot (frame-tick model)
    compositor_->workloadController().dispatchPendingWakes();
    auto adaptParams = compositor_->workloadController().computeAdaptiveParams();
    csvRow += "," + std::to_string(static_cast<int>(adaptParams.parkAggressiveness * 100)) + "%";
    csvRow += "," + std::to_string(adaptParams.maxConcurrentWakes);
    csvRow += "," + std::to_string(adaptParams.wakeStaggerMs);
    csvRow += "," + std::string(adaptParams.levelName());
    csvRow += "," + std::string(adaptParams.adaptationSource);

    // Phase 3D Block 3: Wake stagger telemetry
    auto& wc = compositor_->workloadController();
    csvRow += "," + std::to_string(wc.wakeQueueDepth());
    csvRow += "," + std::to_string(static_cast<int>(wc.avgWakeDelayMs()));
    csvRow += "," + std::to_string(static_cast<int>(wc.maxWakeDelayMs()));
    csvRow += "," + std::to_string(wc.staggeredWakeCount());
    csvRow += "," + std::to_string(wc.immediateWakeCount());

    rm[flutter::EncodableValue("parkAggr")] = flutter::EncodableValue(static_cast<double>(adaptParams.parkAggressiveness));
    rm[flutter::EncodableValue("maxConcWakes")] = flutter::EncodableValue(static_cast<int64_t>(adaptParams.maxConcurrentWakes));
    rm[flutter::EncodableValue("wakeStagger")] = flutter::EncodableValue(static_cast<int64_t>(adaptParams.wakeStaggerMs));
    rm[flutter::EncodableValue("adaptLevel")] = flutter::EncodableValue(std::string(adaptParams.levelName()));
    rm[flutter::EncodableValue("adaptSource")] = flutter::EncodableValue(std::string(adaptParams.adaptationSource));
    rm[flutter::EncodableValue("wakeQueueDepth")] = flutter::EncodableValue(static_cast<int64_t>(wc.wakeQueueDepth()));
    rm[flutter::EncodableValue("avgWakeDelay")] = flutter::EncodableValue(wc.avgWakeDelayMs());
    rm[flutter::EncodableValue("maxWakeDelay")] = flutter::EncodableValue(wc.maxWakeDelayMs());
    rm[flutter::EncodableValue("staggeredWakes")] = flutter::EncodableValue(static_cast<int64_t>(wc.staggeredWakeCount()));
    rm[flutter::EncodableValue("immediateWakes")] = flutter::EncodableValue(static_cast<int64_t>(wc.immediateWakeCount()));

    // Phase 3E: Governance overhead profiling
    const auto& gt = wc.lastTimings();
    csvRow += "," + std::to_string(static_cast<int>(gt.totalDrainUs));
    csvRow += "," + std::to_string(static_cast<int>(gt.adaptiveComputeUs));
    csvRow += "," + std::to_string(static_cast<int>(gt.recoveryReportUs));
    csvRow += "," + std::to_string(static_cast<int>(gt.dispatchWakesUs));
    csvRow += "," + std::to_string(static_cast<int>(gt.sampledEvaluateUsP50));
    csvRow += "," + std::to_string(static_cast<int>(gt.sampledEvaluateUsP95));
    csvRow += "," + std::to_string(static_cast<int>(gt.sampledEvaluateUsMax));

    rm[flutter::EncodableValue("govDrainUs")] = flutter::EncodableValue(gt.totalDrainUs);
    rm[flutter::EncodableValue("govAdaptUs")] = flutter::EncodableValue(gt.adaptiveComputeUs);
    rm[flutter::EncodableValue("govReportUs")] = flutter::EncodableValue(gt.recoveryReportUs);
    rm[flutter::EncodableValue("govDispatchUs")] = flutter::EncodableValue(gt.dispatchWakesUs);
    rm[flutter::EncodableValue("govEvalP50Us")] = flutter::EncodableValue(gt.sampledEvaluateUsP50);
    rm[flutter::EncodableValue("govEvalP95Us")] = flutter::EncodableValue(gt.sampledEvaluateUsP95);
    rm[flutter::EncodableValue("govEvalMaxUs")] = flutter::EncodableValue(gt.sampledEvaluateUsMax);

    // Phase 3E Block 2: Dormancy audit
    csvRow += "," + std::to_string(gt.adaptCadenceSkips);
    csvRow += "," + std::to_string(gt.adaptDefaultCount);
    csvRow += "," + std::to_string(gt.staggerEmptyChecks);
    csvRow += "," + std::to_string(gt.timerKeptAlive);
    csvRow += "," + std::string(gt.anyQueueAllocation ? "Y" : "N");

    rm[flutter::EncodableValue("adaptCadenceSkips")] = flutter::EncodableValue(static_cast<int64_t>(gt.adaptCadenceSkips));
    rm[flutter::EncodableValue("adaptDefaultCount")] = flutter::EncodableValue(static_cast<int64_t>(gt.adaptDefaultCount));
    rm[flutter::EncodableValue("staggerEmptyChecks")] = flutter::EncodableValue(static_cast<int64_t>(gt.staggerEmptyChecks));
    rm[flutter::EncodableValue("timerKeptAlive")] = flutter::EncodableValue(static_cast<int64_t>(gt.timerKeptAlive));
    rm[flutter::EncodableValue("anyQueueAlloc")] = flutter::EncodableValue(gt.anyQueueAllocation);

    csvRow += "," + govSummary.dominantReasons;
    rm[flutter::EncodableValue("csvRow")] = flutter::EncodableValue(csvRow);

    result->Success(flutter::EncodableValue(rm));
}

void MorphicPlugin::handleDeclareWorkloadTraits(
    const flutter::EncodableMap& args,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
    if (!compositor_) {
        result->Error("NO_COMPOSITOR", "Not initialized");
        return;
    }

    int64_t surfaceId = getInt(args, "surfaceId", -1);
    if (surfaceId < 0) {
        result->Error("INVALID_ARGS", "surfaceId required");
        return;
    }

    // Build DeclaredWorkloadProfile from Dart arguments
    morphic::DeclaredWorkloadProfile declared;
    declared.traits.animationSensitive = getBool(args, "animationSensitive", false);
    declared.traits.cadenceIntensive = getBool(args, "cadenceIntensive", false);
    declared.traits.latencyCritical = getBool(args, "latencyCritical", false);
    declared.traits.interactionCritical = getBool(args, "interactionCritical", false);
    declared.traits.memoryHeavy = getBool(args, "memoryHeavy", false);
    declared.traits.backgroundCompute = getBool(args, "backgroundCompute", false);
    declared.traits.continuitySensitive = getBool(args, "continuitySensitive", false);
    declared.traits.ephemeral = getBool(args, "ephemeral", false);
    declared.estimatedWarmMB = static_cast<float>(getDouble(args, "estimatedWarmMB", 117.0));

    // Compute effective profile (host-authoritative)
    auto effective = morphic::EffectiveWorkloadProfile::fromDeclared(declared);

    // Find renderId for this surface and apply profile to controller
    auto& mgr = compositor_->rendererManager();
    morphic::RenderId targetRenderId = 0;
    for (const auto& [rendId, rec] : mgr.records()) {
        if (rec.surfaceId == static_cast<morphic::NodeId>(surfaceId)) {
            targetRenderId = rendId;
            break;
        }
    }

    if (targetRenderId > 0) {
        // Apply profile to controller (was missing — profiles were computed but never stored!)
        compositor_->workloadController().setWorkloadProfile(targetRenderId, effective);

        // Mark governance dirty — workload changed
        compositor_->governanceScheduler().markDirty(
            targetRenderId, morphic::GovernanceDirtyReason::WorkloadChanged);
    }

    OutputDebugStringA(("GOV_DECLARE: surface=" + std::to_string(surfaceId) +
        " renderer=" + std::to_string(targetRenderId) +
        " wake=" + morphic::toString(effective.wakePriority) +
        " park=" + morphic::toString(effective.parkingAffinity) +
        " destroy=" + morphic::toString(effective.destructionCost) +
        " persist=" + morphic::toString(effective.persistenceValue) + "\n").c_str());

    flutter::EncodableMap rm;
    rm[flutter::EncodableValue("success")] = flutter::EncodableValue(true);
    rm[flutter::EncodableValue("wakePriority")] = flutter::EncodableValue(std::string(morphic::toString(effective.wakePriority)));
    rm[flutter::EncodableValue("parkingAffinity")] = flutter::EncodableValue(std::string(morphic::toString(effective.parkingAffinity)));
    rm[flutter::EncodableValue("destructionCost")] = flutter::EncodableValue(std::string(morphic::toString(effective.destructionCost)));
    rm[flutter::EncodableValue("persistenceValue")] = flutter::EncodableValue(std::string(morphic::toString(effective.persistenceValue)));
    result->Success(flutter::EncodableValue(rm));
}

void MorphicPlugin::handleSimulateVisibility(
    const flutter::EncodableMap& args,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {

    if (!compositor_) {
        result->Error("NOT_INITIALIZED", "Compositor not initialized");
        return;
    }

    auto surfaceIdIt = args.find(flutter::EncodableValue("surfaceId"));
    auto stateIt = args.find(flutter::EncodableValue("state"));
    auto confIt = args.find(flutter::EncodableValue("confidence"));

    if (surfaceIdIt == args.end() || stateIt == args.end()) {
        result->Error("INVALID_ARGS", "surfaceId and state required");
        return;
    }

    int surfaceId = std::get<int>(surfaceIdIt->second);
    std::string stateStr = std::get<std::string>(stateIt->second);
    float confidence = confIt != args.end()
        ? static_cast<float>(std::get<double>(confIt->second))
        : 1.0f;

    // Parse visibility state
    morphic::VisibilityState vis = morphic::VisibilityState::Unknown;
    if (stateStr == "visible") vis = morphic::VisibilityState::Visible;
    else if (stateStr == "partiallyVisible") vis = morphic::VisibilityState::PartiallyVisible;
    else if (stateStr == "fullyOccluded") vis = morphic::VisibilityState::FullyOccluded;
    else if (stateStr == "minimized") vis = morphic::VisibilityState::Minimized;
    else if (stateStr == "hidden") vis = morphic::VisibilityState::Hidden;

    // Find renderId for this surface
    auto& mgr = compositor_->rendererManager();
    morphic::RenderId targetRenderId = 0;
    for (const auto& [rendId, rec] : mgr.records()) {
        if (rec.surfaceId == static_cast<morphic::NodeId>(surfaceId)) {
            targetRenderId = rendId;
            break;
        }
    }

    if (targetRenderId > 0) {
        compositor_->workloadController().injectVisibility(
            targetRenderId, vis, confidence);

        // Mark governance dirty — visibility changed
        compositor_->governanceScheduler().markDirty(
            targetRenderId, morphic::GovernanceDirtyReason::VisibilityChanged);
    }

    flutter::EncodableMap rm;
    rm[flutter::EncodableValue("success")] = flutter::EncodableValue(targetRenderId > 0);
    rm[flutter::EncodableValue("renderId")] = flutter::EncodableValue(static_cast<int>(targetRenderId));
    rm[flutter::EncodableValue("state")] = flutter::EncodableValue(stateStr);
    result->Success(flutter::EncodableValue(rm));
}

void MorphicPlugin::handleConfigureBudget(
    const flutter::EncodableMap& args,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {

    if (!compositor_) {
        result->Error("NOT_INITIALIZED", "Compositor not initialized");
        return;
    }

    auto maxEnginesIt = args.find(flutter::EncodableValue("maxEngines"));
    auto maxMBIt = args.find(flutter::EncodableValue("maxMB"));

    int maxEngines = maxEnginesIt != args.end()
        ? std::get<int>(maxEnginesIt->second) : 10;
    float maxMB = maxMBIt != args.end()
        ? static_cast<float>(std::get<double>(maxMBIt->second)) : 2000.0f;

    compositor_->workloadController().configureBudget(maxEngines, maxMB);

    // Mark all renderers dirty — budget changed
    for (const auto& [rendId, rec] : compositor_->rendererManager().records()) {
        compositor_->governanceScheduler().markDirty(
            rendId, morphic::GovernanceDirtyReason::PressureChanged);
    }

    flutter::EncodableMap rm;
    rm[flutter::EncodableValue("success")] = flutter::EncodableValue(true);
    rm[flutter::EncodableValue("maxEngines")] = flutter::EncodableValue(maxEngines);
    rm[flutter::EncodableValue("maxMB")] = flutter::EncodableValue(static_cast<double>(maxMB));
    result->Success(flutter::EncodableValue(rm));
}

// Phase 3C: Static callback invoked by FrameCadenceMonitor on every frame.
// NOTE: Called under FrameCadenceMonitor's mutex — must be fast.
void MorphicPlugin::onFrameProduced(int /*engineId*/, void* userData) {
    auto* plugin = static_cast<MorphicPlugin*>(userData);
    if (!plugin || !plugin->compositor_) return;

    // Broadcast to all renderers — only those in recovery phase process
    for (const auto& [id, rec] : plugin->compositor_->rendererManager().records()) {
        plugin->compositor_->workloadController().onRendererFrame(id);
    }
}

// --- Phase 4 Step 2: Native Runtime callback ---

void MorphicPlugin::onMainWindowActivated() {
    if (compositor_) {
        compositor_->onMainWindowActivated();
    }
}
extern "C" __declspec(dllexport) void MorphicPlugin_OnMainWindowActivated(bool active) {
    if (active && g_activePlugin) {
        // Main window activated (Alt+Tab, taskbar click, etc.)
        // Raise all compositor surfaces to the foreground.
        g_activePlugin->onMainWindowActivated();
    }
}

// --- Phase 4 Step 3: Global Keyboard Interception ---
extern "C" __declspec(dllexport) bool MorphicPlugin_OnGlobalKey(unsigned int msg, unsigned long long wParam, long long lParam) {
    if (g_activePlugin) {
        if (auto* compositor = g_activePlugin->compositor()) {
            return compositor->inputRouter().routeKeyboardEvent(msg, wParam, lParam);
        }
    }
    return false;
}

}  // namespace morphic

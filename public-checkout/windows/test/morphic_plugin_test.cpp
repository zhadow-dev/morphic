#include <gtest/gtest.h>
#include <windows.h>
#include <memory>
#include <unordered_map>
#include <vector>
#include <fstream>

#include "src/core/runtime_scene_state.h"
#include "src/core/runtime_commit_scheduler.h"
#include "src/core/kernel_trace.h"
#include "src/core/runtime_mutation_intent.h"
#include "src/core/runtime_surface_realizer.h"
#include "src/testing/runtime_warfare_validator.h"
#include "src/api/runtime_bootstrap.h"

namespace morphic {
namespace test {

// 1. Mock Surface Realizer for testing scheduler actions without Win32 hooks
class MockSurfaceRealizer : public RuntimeSurfaceRealizer {
public:
    struct Topo { Transform geom; bool visible; SurfaceRole role; };
    struct Elev { ElevationLayer layer; int sublevel; };
    struct Size { int w; int h; };

    RuntimeSceneState* sceneState = nullptr;
    MockSurfaceRealizer(RuntimeSceneState* state = nullptr) : sceneState(state) {}

    std::unordered_map<NodeId, Topo> realizedTopologies;
    std::unordered_map<NodeId, Elev> realizedElevations;
    std::unordered_map<NodeId, bool> realizedActivations;
    std::unordered_map<NodeId, Size> resizedRenderers;

    void realizeTopology(NodeId surfaceId, const Transform& desiredGeom, bool desiredVisible, SurfaceRole role) override {
        realizedTopologies[surfaceId] = { desiredGeom, desiredVisible, role };
        if (sceneState) {
            sceneState->updateRealizedGeometry(surfaceId, desiredGeom);
            sceneState->updateRealizedVisibility(surfaceId, desiredVisible);
            sceneState->updateRealizedRole(surfaceId, role);
        }
    }
    
    void realizeElevation(NodeId surfaceId, ElevationLayer layer, int sublevel) override {
        realizedElevations[surfaceId] = { layer, sublevel };
        if (sceneState) {
            sceneState->updateRealizedElevation(surfaceId, layer, sublevel);
        }
    }

    void realizeActivation(NodeId surfaceId, bool desiredActive) override {
        realizedActivations[surfaceId] = desiredActive;
        if (sceneState) {
            sceneState->updateRealizedActivation(surfaceId, desiredActive);
        }
    }

    void notifyRendererResize(NodeId surfaceId, int width, int height) override {
        resizedRenderers[surfaceId] = { width, height };
    }
};

// 2. Test Double-Buffered Scene State Isolation
TEST(MorphicKernelTest, DoubleBufferedSceneStateIsolation) {
    RuntimeSceneState sceneState;
    RuntimeSceneMutationAuthority auth(true);

    NodeId surfaceId = 42;
    Transform initialGeom{10, 10, 100, 100};
    
    // Mutate desired state (only working state should change initially)
    sceneState.updateDesiredGeometry(auth, surfaceId, initialGeom);

    const auto* working = sceneState.getWorkingState(surfaceId);
    const auto* committed = sceneState.getCommittedState(surfaceId);

    ASSERT_NE(working, nullptr);
    EXPECT_EQ(working->desiredGeometry, initialGeom);
    
    // Committed state should still be null (or default) before the swap
    EXPECT_EQ(committed, nullptr);

    // Perform swap commit boundary
    sceneState.swapCommitBoundary();

    committed = sceneState.getCommittedState(surfaceId);
    ASSERT_NE(committed, nullptr);
    EXPECT_EQ(committed->desiredGeometry, initialGeom);
}

// 3. Test Stable Sorted Arbitration
TEST(MorphicKernelTest, StableSortedArbitration) {
    RuntimeSceneState sceneState;
    auto realizer = std::make_shared<MockSurfaceRealizer>();
    RuntimeCommitScheduler scheduler(sceneState, realizer, nullptr);

    WorkspaceId ws{1};

    // Enqueue different priority intents
    RuntimeMutationIntent backgroundIntent;
    backgroundIntent.type = RuntimeMutationIntent::Type::GeometryChange;
    backgroundIntent.surfaceId = 100;
    backgroundIntent.priority = MutationPriority::Background;
    backgroundIntent.workspaceId = ws;
    backgroundIntent.geometry = {0, 0, 100, 100};

    RuntimeMutationIntent criticalIntent;
    criticalIntent.type = RuntimeMutationIntent::Type::GeometryChange;
    criticalIntent.surfaceId = 101;
    criticalIntent.priority = MutationPriority::Critical;
    criticalIntent.workspaceId = ws;
    criticalIntent.geometry = {0, 0, 200, 200};

    RuntimeMutationIntent interactiveIntent;
    interactiveIntent.type = RuntimeMutationIntent::Type::GeometryChange;
    interactiveIntent.surfaceId = 102;
    interactiveIntent.priority = MutationPriority::Interactive;
    interactiveIntent.workspaceId = ws;
    interactiveIntent.geometry = {0, 0, 300, 300};

    scheduler.enqueueIntent(backgroundIntent);
    scheduler.enqueueIntent(criticalIntent);
    scheduler.enqueueIntent(interactiveIntent);

    // Tick to run arbitration
    scheduler.tick();

    // Verify order of realization via realizer
    // With double buffered swap complete, check that they applied
    auto committed101 = sceneState.getCommittedState(101);
    auto committed102 = sceneState.getCommittedState(102);
    auto committed100 = sceneState.getCommittedState(100);

    ASSERT_NE(committed101, nullptr);
    ASSERT_NE(committed102, nullptr);
    ASSERT_NE(committed100, nullptr);

    EXPECT_EQ(committed101->desiredGeometry.width, 200);
    EXPECT_EQ(committed102->desiredGeometry.width, 300);
    EXPECT_EQ(committed100->desiredGeometry.width, 100);
}

// 4. Test Intent Cancellation Safety & Invalidation
TEST(MorphicKernelTest, IntentCancellationSafety) {
    RuntimeSceneState sceneState;
    auto realizer = std::make_shared<MockSurfaceRealizer>();
    RuntimeCommitScheduler scheduler(sceneState, realizer, nullptr);

    NodeId surfaceId = 55;
    WorkspaceId ws{1};

    RuntimeMutationIntent geomIntent;
    geomIntent.type = RuntimeMutationIntent::Type::GeometryChange;
    geomIntent.surfaceId = surfaceId;
    geomIntent.priority = MutationPriority::Interactive;
    geomIntent.workspaceId = ws;
    geomIntent.geometry = {10, 10, 150, 150};

    scheduler.enqueueIntent(geomIntent);

    // Invalidate surface intents (e.g. during surface destruction)
    scheduler.invalidateSurface(surfaceId);

    // Tick scheduler
    scheduler.tick();

    // Verify that geometry change was NOT realized, since it was cancelled
    EXPECT_EQ(realizer->realizedTopologies.count(surfaceId), 0);
    EXPECT_EQ(scheduler.metrics().queueDepthBeforeCommit, 0);
}

// 5. Test Starvation Prevention & Priority Escalation (Aging)
TEST(MorphicKernelTest, StarvationPreventionAging) {
    RuntimeSceneState sceneState;
    auto realizer = std::make_shared<MockSurfaceRealizer>();
    RuntimeCommitScheduler scheduler(sceneState, realizer, nullptr);

    NodeId surfaceId = 77;
    WorkspaceId ws{1};

    RuntimeMutationIntent deferredIntent;
    deferredIntent.type = RuntimeMutationIntent::Type::GeometryChange;
    deferredIntent.surfaceId = surfaceId;
    deferredIntent.priority = MutationPriority::Deferred;
    deferredIntent.workspaceId = ws;
    deferredIntent.geometry = {10, 10, 120, 120};

    scheduler.enqueueIntent(deferredIntent);

    // Perform 5 ticks with zero execution budget or manually using the queue to age them.
    // Let's tick the scheduler. The first tick would process the intent if budget is not set to 0.
    // So let's set maxOpsPerCycle to 0 to prevent it from committing, and age remaining items.
    scheduler.mutableBudget().maxOpsPerCycle = 0;
    
    for (int i = 0; i < 5; i++) {
        scheduler.tick();
    }

    // Now, let's restore budget and tick once to process the escalated intent
    scheduler.mutableBudget().maxOpsPerCycle = 15;
    scheduler.tick();

    // Verify the intent was successfully processed
    EXPECT_EQ(realizer->realizedTopologies.count(surfaceId), 1);
}

// 6. Test Divergence Severity Escalation & Quarantine Transition
TEST(MorphicKernelTest, DivergenceSeverityEscalation) {
    RuntimeSceneState sceneState;
    RuntimeSceneMutationAuthority auth(true);

    NodeId surfaceId = 99;
    Transform desiredGeom{0, 0, 100, 100};
    
    // Register desired geometry
    sceneState.updateDesiredGeometry(auth, surfaceId, desiredGeom);
    
    // Make physical state divergent
    Transform physicalGeom{0, 0, 50, 50}; // different
    sceneState.updateRealizedGeometry(surfaceId, physicalGeom);

    // Verify initially Transient
    sceneState.checkDivergences();
    auto* state = sceneState.getWorkingState(surfaceId);
    ASSERT_NE(state, nullptr);
    EXPECT_EQ(state->severity, DivergenceSeverity::Transient);
    EXPECT_EQ(state->presence, RuntimePresence::ResidencyBudgeted);

    // Check multiple times to trigger escalation
    for (int i = 0; i < 4; i++) {
        sceneState.checkDivergences();
    }
    EXPECT_EQ(state->severity, DivergenceSeverity::Persistent);

    for (int i = 0; i < 8; i++) {
        sceneState.checkDivergences();
    }
    EXPECT_EQ(state->severity, DivergenceSeverity::Critical);

    for (int i = 0; i < 10; i++) {
        sceneState.checkDivergences();
    }
    EXPECT_EQ(state->severity, DivergenceSeverity::Terminal);
    EXPECT_EQ(state->presence, RuntimePresence::Quarantined);
}

// 7. GTest Suite for Phase 9B - Operational Runtime Warfare
TEST(MorphicKernelTest, RuntimeWarfareValidatorSuite) {
    RuntimeSceneState sceneState;
    auto realizer = std::make_shared<MockSurfaceRealizer>();
    RuntimeCommitScheduler scheduler(sceneState, realizer, nullptr);

    RuntimeWarfareValidator validator(scheduler);
    auto report = validator.runFullValidation();

    EXPECT_TRUE(report.allPassed()) << "Operational Runtime Warfare validation failed!";
    for (const auto& res : report.results) {
        EXPECT_TRUE(res.passed) << "Warfare sub-test failed: " << res.testName << " -> " << res.failureReason;
    }
}

// 8. Test Deterministic Replay and Injection overrides
TEST(MorphicKernelTest, TraceReplayAndInjection) {
    RuntimeSceneState sceneState;
    auto realizer = std::make_shared<MockSurfaceRealizer>();
    RuntimeCommitScheduler scheduler(sceneState, realizer, nullptr);

    NodeId sId = 123;
    WorkspaceId ws{1};

    // Step 1: Record intents
    scheduler.traceRecorder()->setOptInPersistence(false);
    
    RuntimeMutationIntent intent1;
    intent1.type = RuntimeMutationIntent::Type::GeometryChange;
    intent1.surfaceId = sId;
    intent1.workspaceId = ws;
    intent1.priority = MutationPriority::Interactive;
    intent1.geometry = {10, 10, 200, 200};
    
    scheduler.enqueueIntent(intent1);
    scheduler.tick(); // commits and records frame 0

    RuntimeMutationIntent intent2;
    intent2.type = RuntimeMutationIntent::Type::ActivationChange;
    intent2.surfaceId = sId;
    intent2.workspaceId = ws;
    intent2.priority = MutationPriority::Interactive;
    intent2.active = true;

    scheduler.enqueueIntent(intent2);
    scheduler.tick(); // commits and records frame 1

    // Flush snapshot to file
    std::string testDumpFile = "test_replay.mtrace";
    scheduler.traceRecorder()->flushSnapshot(testDumpFile);

    // Step 2: Playback without injection overrides
    {
        RuntimeSceneState replaySceneState;
        auto replayRealizer = std::make_shared<MockSurfaceRealizer>();
        RuntimeCommitScheduler replayScheduler(replaySceneState, replayRealizer, nullptr);

        ASSERT_TRUE(replayScheduler.traceRecorder()->loadSnapshot(testDumpFile));
        EXPECT_EQ(replayScheduler.traceRecorder()->getRingBuffer().size(), static_cast<size_t>(2));

        replayScheduler.traceRecorder()->enableReplayMode(true);
        replayScheduler.resetReplayIndex();

        // Tick 1 (Geometry change realization)
        replayScheduler.tick();
        EXPECT_EQ(replayRealizer->realizedTopologies.count(sId), 1);
        EXPECT_EQ(replayRealizer->realizedTopologies[sId].geom.width, 200);

        // Tick 2 (Activation change realization)
        replayScheduler.tick();
        EXPECT_TRUE(replayRealizer->realizedActivations[sId]);
    }

    // Step 3: Playback WITH injection overrides (forceHwndCreationFailure)
    {
        RuntimeSceneState replaySceneState;
        auto replayRealizer = std::make_shared<MockSurfaceRealizer>();
        RuntimeCommitScheduler replayScheduler(replaySceneState, replayRealizer, nullptr);

        ASSERT_TRUE(replayScheduler.traceRecorder()->loadSnapshot(testDumpFile));

        TraceInjectionConfig config;
        config.forceHwndCreationFailure = true;
        replayScheduler.traceRecorder()->enableReplayMode(true, config);
        replayScheduler.resetReplayIndex();

        // Tick 1 (Geometry change realization skipped due to HWND creation failure injection)
        replayScheduler.tick();
        EXPECT_EQ(replayRealizer->realizedTopologies.count(sId), 0); // realization skipped!
    }

    // Step 4: Playback WITH injection overrides (forceActivationDenial)
    {
        RuntimeSceneState replaySceneState;
        auto replayRealizer = std::make_shared<MockSurfaceRealizer>();
        RuntimeCommitScheduler replayScheduler(replaySceneState, replayRealizer, nullptr);

        ASSERT_TRUE(replayScheduler.traceRecorder()->loadSnapshot(testDumpFile));

        TraceInjectionConfig config;
        config.forceActivationDenial = true;
        replayScheduler.traceRecorder()->enableReplayMode(true, config);
        replayScheduler.resetReplayIndex();

        // Tick 1 (Geometry change realization succeeds)
        replayScheduler.tick();
        
        // Tick 2 (Activation change realization fails/skipped)
        replayScheduler.tick();
        EXPECT_EQ(replayRealizer->realizedActivations.count(sId), 0); // realization skipped!
    }

    // Clean up
    DeleteFileA(testDumpFile.c_str());
}

// 9. Automated Multi-Workspace Simulated IDE Workload Test
TEST(MorphicKernelTest, HumanWorkspaceSimulatedIDEWorkload) {
    RuntimeSceneState sceneState;
    auto realizer = std::make_shared<MockSurfaceRealizer>(&sceneState);
    RuntimeCommitScheduler scheduler(sceneState, realizer, nullptr);

    // Setup typical IDE workspace layouts
    WorkspaceId editorWs{1};
    WorkspaceId terminalWs{2};

    NodeId sidebarId = 100;
    NodeId editorPaneId = 101;
    NodeId terminalPaneId = 102;

    // Phase 1: Initialize IDE workspace layout
    RuntimeSceneMutationAuthority auth(true);
    
    // Sidebar on active workspace 1
    RuntimeMutationIntent sidebarIntent;
    sidebarIntent.type = RuntimeMutationIntent::Type::OrchChange;
    sidebarIntent.surfaceId = sidebarId;
    sidebarIntent.workspaceId = editorWs;
    sidebarIntent.presence = RuntimePresence::ResidencyBudgeted;
    sidebarIntent.active = true;
    sidebarIntent.visible = true;
    sidebarIntent.priority = MutationPriority::Interactive;
    scheduler.enqueueIntent(sidebarIntent);

    sidebarIntent.type = RuntimeMutationIntent::Type::GeometryChange;
    sidebarIntent.geometry = {0, 0, 250, 1080}; // Left sidebar
    scheduler.enqueueIntent(sidebarIntent);

    // Editor on active workspace 1
    RuntimeMutationIntent editorIntent;
    editorIntent.type = RuntimeMutationIntent::Type::OrchChange;
    editorIntent.surfaceId = editorPaneId;
    editorIntent.workspaceId = editorWs;
    editorIntent.presence = RuntimePresence::ResidencyBudgeted;
    editorIntent.active = true;
    editorIntent.visible = true;
    editorIntent.priority = MutationPriority::Interactive;
    scheduler.enqueueIntent(editorIntent);

    editorIntent.type = RuntimeMutationIntent::Type::GeometryChange;
    editorIntent.geometry = {250, 0, 1670, 1080}; // Editor pane
    scheduler.enqueueIntent(editorIntent);

    // Terminal initially on inactive workspace 2
    RuntimeMutationIntent termIntent;
    termIntent.type = RuntimeMutationIntent::Type::OrchChange;
    termIntent.surfaceId = terminalPaneId;
    termIntent.workspaceId = terminalWs;
    termIntent.presence = RuntimePresence::Hibernating;
    termIntent.active = false;
    termIntent.visible = false;
    termIntent.priority = MutationPriority::Background;
    scheduler.enqueueIntent(termIntent);

    termIntent.type = RuntimeMutationIntent::Type::GeometryChange;
    termIntent.geometry = {0, 800, 1920, 280}; // Bottom terminal
    scheduler.enqueueIntent(termIntent);

    // Tick scheduler to apply initial layout
    scheduler.tick();

    // Verify initial layout realization
    EXPECT_EQ(realizer->realizedTopologies[sidebarId].geom.width, 250);
    EXPECT_EQ(realizer->realizedTopologies[editorPaneId].geom.width, 1670);
    EXPECT_EQ(realizer->realizedTopologies.count(terminalPaneId), 0); // Not realized because it is hibernating on workspace 2

    // Phase 2: User triggers heavy resizing (moving splitter pane)
    // Sidebar shrunk to 200, Editor expanded to 1720
    sidebarIntent.type = RuntimeMutationIntent::Type::GeometryChange;
    sidebarIntent.geometry = {0, 0, 200, 1080};
    scheduler.enqueueIntent(sidebarIntent);

    editorIntent.type = RuntimeMutationIntent::Type::GeometryChange;
    editorIntent.geometry = {200, 0, 1720, 1080};
    scheduler.enqueueIntent(editorIntent);

    scheduler.tick();

    EXPECT_EQ(realizer->realizedTopologies[sidebarId].geom.width, 200);
    EXPECT_EQ(realizer->realizedTopologies[editorPaneId].geom.width, 1720);

    // Phase 3: Rapid workspace switching & focus promotion
    // User moves Terminal to active workspace 1, makes it active and resident
    termIntent.type = RuntimeMutationIntent::Type::OrchChange;
    termIntent.workspaceId = editorWs;
    termIntent.presence = RuntimePresence::ResidencyBudgeted;
    termIntent.active = true;
    termIntent.visible = true;
    termIntent.priority = MutationPriority::Interactive;
    scheduler.enqueueIntent(termIntent);

    scheduler.tick();

    // Now terminal must be realized
    EXPECT_EQ(realizer->realizedTopologies[terminalPaneId].geom.height, 280);
    EXPECT_TRUE(realizer->realizedActivations[terminalPaneId]);

    // Check that health stats remain High confidence and convergent
    EXPECT_EQ(scheduler.health().confidence, KernelConfidence::High);
}

// 10. Test Panic Abort Forensics
TEST(MorphicKernelTest, PanicForensicsDeathTest) {
    // Assert that triggering a fatal violation aborts and prints the panic reason
    ASSERT_DEATH({
        RuntimeSceneState sceneState;
        auto realizer = std::make_shared<MockSurfaceRealizer>();
        RuntimeCommitScheduler scheduler(sceneState, realizer, nullptr);
        
        // Delete any existing dump/log files
        DeleteFileA("morphic_panic.crashdump");
        DeleteFileA("morphic_panic.log");

        // Force a panic via MORPHIC_FATAL_KERNEL_VIOLATION
        MORPHIC_FATAL_KERNEL_VIOLATION("Intentional Test Semantic Violation Invariant Breach");
    }, "MORPHIC FATAL KERNEL VIOLATION");

    // After the death test executes the child process, the files should be present in the main process directory
    std::ifstream crashDump("morphic_panic.crashdump");
    EXPECT_TRUE(crashDump.is_open());
    crashDump.close();

    std::ifstream crashLog("morphic_panic.log");
    ASSERT_TRUE(crashLog.is_open());
    std::string line;
    std::getline(crashLog, line);
    EXPECT_NE(line.find("Reason: Intentional Test Semantic Violation Invariant Breach"), std::string::npos);
    crashLog.close();

    // Clean up
    DeleteFileA("morphic_panic.crashdump");
    DeleteFileA("morphic_panic.log");
}

// 11. Test Bootstrap Sequential Validation & Failure Policy
TEST(MorphicKernelTest, BootstrapSequentialValidation) {
    // A. Recoverable phase with non-sequential advance should return false without panicking
    {
        RuntimeBootstrap bootstrap;
        // Current: Uninitialized. Advance to ConstitutionLoad -> OK
        EXPECT_TRUE(bootstrap.advanceTo(BootstrapPhase::ConstitutionLoad, true));

        // Attempt non-sequential advance to TraceInit (4) instead of RuntimeInit (2).
        // Since TraceInit is RecoverableDegraded, this returns false without panicking.
        EXPECT_FALSE(bootstrap.advanceTo(BootstrapPhase::TraceInit, true));
        EXPECT_EQ(bootstrap.currentPhase(), BootstrapPhase::ConstitutionLoad);
    }

    // B. Fatal phase with non-sequential advance should trigger a fatal panic (assert death)
    ASSERT_DEATH({
        RuntimeBootstrap bootstrap;
        bootstrap.advanceTo(BootstrapPhase::ConstitutionLoad, true);

        // Attempt non-sequential advance to SchedulerInit (3) instead of RuntimeInit (2).
        // Since SchedulerInit is Fatal, it must panic.
        bootstrap.advanceTo(BootstrapPhase::SchedulerInit, true);
    }, "BOOTSTRAP: Cannot advance from ConstitutionLoad to SchedulerInit");
}

// 12. Test Replay Passive Boundary Law (Replay owns zero authority)
TEST(MorphicKernelTest, ReplayPassiveBoundaryValidation) {
    // A. Direct queue insertion during replay bypasses the passive boundary check (no panic)
    {
        RuntimeSceneState sceneState;
        auto realizer = std::make_shared<MockSurfaceRealizer>();
        RuntimeCommitScheduler scheduler(sceneState, realizer, nullptr);

        // Enable replay mode
        scheduler.traceRecorder()->enableReplayMode(true);

        RuntimeMutationIntent intent;
        intent.surfaceId = 123;
        intent.type = RuntimeMutationIntent::Type::GeometryChange;
        intent.geometry = {10, 10, 100, 100};

        // Directly enqueue into queue_ bypasses the public interface passive check
        scheduler.queueForTesting().enqueue(intent);
        
        // Tick executes normal commit cycle
        scheduler.tick();
        EXPECT_EQ(realizer->realizedTopologies.count(123), 1);
    }

    // B. External enqueueIntent during replay triggers a panic (assert death)
    ASSERT_DEATH({
        RuntimeSceneState sceneState;
        auto realizer = std::make_shared<MockSurfaceRealizer>();
        RuntimeCommitScheduler scheduler(sceneState, realizer, nullptr);

        scheduler.traceRecorder()->enableReplayMode(true);

        RuntimeMutationIntent intent;
        intent.surfaceId = 123;
        intent.type = RuntimeMutationIntent::Type::GeometryChange;
        
        scheduler.enqueueIntent(intent);
    }, "REPLAY PASSIVE BOUNDARY VIOLATION: Cannot enqueue mutation intent during replay");

    // C. External invalidateSurface during replay triggers a panic (assert death)
    ASSERT_DEATH({
        RuntimeSceneState sceneState;
        auto realizer = std::make_shared<MockSurfaceRealizer>();
        RuntimeCommitScheduler scheduler(sceneState, realizer, nullptr);

        scheduler.traceRecorder()->enableReplayMode(true);

        scheduler.invalidateSurface(123);
    }, "REPLAY PASSIVE BOUNDARY VIOLATION: Cannot invalidate surface during replay");

    // D. External invalidateWorkspace during replay triggers a panic (assert death)
    ASSERT_DEATH({
        RuntimeSceneState sceneState;
        auto realizer = std::make_shared<MockSurfaceRealizer>();
        RuntimeCommitScheduler scheduler(sceneState, realizer, nullptr);

        scheduler.traceRecorder()->enableReplayMode(true);

        scheduler.invalidateWorkspace(WorkspaceId{1});
    }, "REPLAY PASSIVE BOUNDARY VIOLATION: Cannot invalidate workspace during replay");
}

} // namespace test
} // namespace morphic

import 'core/surface.dart';
import 'core/surface_group.dart';
import 'core/display_info.dart';
import 'morphic_platform_interface.dart';

/// Main API for the Morphic spatial composition framework.
///
/// Usage:
/// ```dart
/// final morphic = MorphicController();
/// await morphic.initialize();
///
/// final s1 = await morphic.createSurface(SurfaceConfig(x: 100, y: 100, color: 0xE94560));
/// final s2 = await morphic.createSurface(SurfaceConfig(x: 510, y: 100, color: 0x0F3460));
/// final group = await morphic.createGroup([s1.id, s2.id]);
/// ```
class MorphicController {
  bool _initialized = false;
  bool get isInitialized => _initialized;

  /// Initialize the compositor engine.
  Future<void> initialize() async {
    if (_initialized) return;
    await MorphicPlatform.instance.initialize();
    _initialized = true;
  }

  /// Shutdown the compositor and destroy all surfaces.
  Future<void> shutdown() async {
    if (!_initialized) return;
    await MorphicPlatform.instance.shutdown();
    _initialized = false;
  }

  /// Create a new composition surface backed by a native HWND.
  Future<Surface> createSurface(SurfaceConfig config) async {
    final id = await MorphicPlatform.instance.createSurface(config.toMap());
    return Surface(id: id, config: config);
  }

  /// Destroy a surface and its backing HWND.
  Future<void> destroySurface(int surfaceId) async {
    await MorphicPlatform.instance.destroySurface(surfaceId);
  }

  /// Change a surface's role at runtime.
  /// This reapplies HWND topology (styles, owner, activation) and
  /// notifies governance of the role change.
  Future<void> setSurfaceRole(int surfaceId, SurfaceRole role) async {
    await MorphicPlatform.instance.setSurfaceRole(surfaceId, role.name);
  }

  /// Create a group — surfaces in a group move, resize, and elevate together.
  Future<SurfaceGroup> createGroup(List<int> memberIds) async {
    final id = await MorphicPlatform.instance.createGroup(memberIds);
    return SurfaceGroup(id: id, memberIds: memberIds);
  }

  /// Destroy a group (surfaces become detached).
  Future<void> destroyGroup(int groupId) async {
    await MorphicPlatform.instance.destroyGroup(groupId);
  }

  /// Move a surface to screen coordinates.
  Future<void> moveSurface(int surfaceId, int x, int y) async {
    await MorphicPlatform.instance.moveSurface(surfaceId, x, y);
  }

  /// Resize a surface.
  Future<void> resizeSurface(int surfaceId, int width, int height) async {
    await MorphicPlatform.instance.resizeSurface(surfaceId, width, height);
  }

  /// Toggle the debug overlay (shows bounds, elevation, frame metrics).
  Future<void> setDebugOverlay(bool enabled) async {
    await MorphicPlatform.instance.setDebugOverlay(enabled);
  }

  /// Get information about all connected displays.
  Future<List<DisplayInfo>> getDisplays() async {
    final raw = await MorphicPlatform.instance.getDisplays();
    return raw.map((m) => DisplayInfo.fromMap(m)).toList();
  }

  /// Get current compositor performance metrics.
  Future<Map<String, dynamic>> getMetrics() async {
    final raw = await MorphicPlatform.instance.getMetrics();
    return raw.map((k, v) => MapEntry(k.toString(), v));
  }

  /// Run the Phase 1B stress test suite.
  /// Returns a list of test results, each with name, passed, metrics.
  Future<List<Map<String, dynamic>>> runStressTest() async {
    final raw = await MorphicPlatform.instance.runStressTest();
    return raw.map((m) => m.map((k, v) => MapEntry(k.toString(), v))).toList();
  }

  /// Run the replay determinism test.
  /// Generates synthetic events, replays 3 times, compares snapshots.
  Future<Map<String, dynamic>> runReplayTest() async {
    final raw = await MorphicPlatform.instance.runReplayTest();
    return raw.map((k, v) => MapEntry(k.toString(), v));
  }

  /// Run endurance soak test for the specified duration.
  Future<Map<String, dynamic>> runSoakTest({int durationMinutes = 1}) async {
    final raw = await MorphicPlatform.instance.runSoakTest(durationMinutes: durationMinutes);
    return raw.map((k, v) => MapEntry(k.toString(), v));
  }

  /// Run crash recovery test — validates clean HWND cleanup.
  Future<Map<String, dynamic>> runCrashRecoveryTest() async {
    final raw = await MorphicPlatform.instance.runCrashRecoveryTest();
    return raw.map((k, v) => MapEntry(k.toString(), v));
  }

  /// Attach a renderer to an existing surface.
  /// Types: 'gdi' (default Phase 1 paint), 'flutter' (Phase 2A), 'null' (no rendering).
  Future<Map<String, dynamic>> attachRenderer(int surfaceId, {String type = 'gdi'}) async {
    final raw = await MorphicPlatform.instance.attachRenderer(surfaceId, type);
    return raw.map((k, v) => MapEntry(k.toString(), v));
  }

  /// Detach renderer from a surface — falls back to GDI paint.
  Future<bool> detachRenderer(int surfaceId) async {
    return await MorphicPlatform.instance.detachRenderer(surfaceId);
  }

  /// Run resize storm test — continuous/grouped/overlapping resize scenarios.
  Future<Map<String, dynamic>> runResizeStorm() async {
    final raw = await MorphicPlatform.instance.runResizeStorm();
    return raw.map((k, v) => MapEntry(k.toString(), v));
  }

  /// Run zombie behavioral audit — measures hidden engine activity over time.
  Future<Map<String, dynamic>> runZombieAudit() async {
    final raw = await MorphicPlatform.instance.runZombieAudit();
    return raw.map((k, v) => MapEntry(k.toString(), v));
  }

  /// Run GPU pressure profile — captures VRAM usage and decay curves.
  Future<Map<String, dynamic>> runGpuProfile() async {
    final raw = await MorphicPlatform.instance.runGpuProfile();
    return raw.map((k, v) => MapEntry(k.toString(), v));
  }

  /// Run long-horizon zombie decay tracking (~48s observation).
  Future<Map<String, dynamic>> runDecayTracking() async {
    final raw = await MorphicPlatform.instance.runDecayTracking();
    return raw.map((k, v) => MapEntry(k.toString(), v));
  }

  /// Run per-thread activity attribution (5s observation).
  Future<Map<String, dynamic>> runThreadAudit() async {
    final raw = await MorphicPlatform.instance.runThreadAudit();
    return raw.map((k, v) => MapEntry(k.toString(), v));
  }

  /// Query frame cadence from all secondary engines.
  Future<Map<String, dynamic>> queryFrameCadence() async {
    final raw = await MorphicPlatform.instance.queryFrameCadence();
    return raw.map((k, v) => MapEntry(k.toString(), v));
  }

  /// Pause animations in all secondary engines.
  Future<Map<String, dynamic>> pauseRendererAnimations() async {
    final raw = await MorphicPlatform.instance.pauseRendererAnimations();
    return raw.map((k, v) => MapEntry(k.toString(), v));
  }

  /// Resume animations in all secondary engines.
  Future<Map<String, dynamic>> resumeRendererAnimations() async {
    final raw = await MorphicPlatform.instance.resumeRendererAnimations();
    return raw.map((k, v) => MapEntry(k.toString(), v));
  }

  /// Phase 2B: Test lifecycle orchestration on secondary engines.
  /// target: "active", "throttled", "parked", "dormant"
  Future<Map<String, dynamic>> testLifecycleOrchestration(String target) async {
    final raw = await MorphicPlatform.instance.testLifecycleOrchestration(target);
    return raw.map((k, v) => MapEntry(k.toString(), v));
  }

  /// Phase 2B.1: Take a single benchmark telemetry snapshot.
  Future<Map<String, dynamic>> takeBenchmarkSnapshot(
      double elapsedSec, String phase, int prevTotalFrames) async {
    final raw = await MorphicPlatform.instance.takeBenchmarkSnapshot(
        elapsedSec, phase, prevTotalFrames);
    return raw.map((k, v) => MapEntry(k.toString(), v));
  }

  /// Declare workload traits for a surface (Dart → host).
  /// Host computes the effective profile from declared traits.
  Future<Map<String, dynamic>> declareWorkloadTraits(
      int surfaceId, {
      bool animationSensitive = false,
      bool cadenceIntensive = false,
      bool latencyCritical = false,
      bool interactionCritical = false,
      bool memoryHeavy = false,
      bool backgroundCompute = false,
      bool continuitySensitive = false,
      bool ephemeral = false,
      double estimatedWarmMB = 117.0,
  }) async {
    final raw = await MorphicPlatform.instance.declareWorkloadTraits(
      surfaceId,
      animationSensitive: animationSensitive,
      cadenceIntensive: cadenceIntensive,
      latencyCritical: latencyCritical,
      interactionCritical: interactionCritical,
      memoryHeavy: memoryHeavy,
      backgroundCompute: backgroundCompute,
      continuitySensitive: continuitySensitive,
      ephemeral: ephemeral,
      estimatedWarmMB: estimatedWarmMB,
    );
    return raw.map((k, v) => MapEntry(k.toString(), v));
  }

  // Phase 3B.3: Governance semantics validation

  /// Inject simulated visibility state for governance reasoning.
  Future<Map<String, dynamic>> simulateVisibility(
      int surfaceId, String state, {double confidence = 1.0}) async {
    final raw = await MorphicPlatform.instance.simulateVisibility(
      surfaceId, state, confidence: confidence);
    return raw.map((k, v) => MapEntry(k.toString(), v));
  }

  /// Configure budget constraints for economic governance testing.
  Future<Map<String, dynamic>> configureBudget({
    int maxEngines = 10,
    double maxMB = 2000.0,
  }) async {
    final raw = await MorphicPlatform.instance.configureBudget(
      maxEngines: maxEngines, maxMB: maxMB);
    return raw.map((k, v) => MapEntry(k.toString(), v));
  }

  /// Clear all simulated visibility — revert to real VisibilityObserver.
  Future<bool> clearSimulatedVisibility() =>
      MorphicPlatform.instance.clearSimulatedVisibility();

  /// Reset budget to generous defaults.
  Future<bool> resetBudget() =>
      MorphicPlatform.instance.resetBudget();

  /// Phase 3D: Simulate battery state for deterministic adaptive testing.
  Future<bool> simulateBattery({required bool onBattery, double percent = 100.0}) =>
      MorphicPlatform.instance.simulateBattery(onBattery: onBattery, percent: percent);

  /// Phase 3D: Clear simulated battery — revert to real hardware.
  Future<bool> clearSimulatedBattery() =>
      MorphicPlatform.instance.clearSimulatedBattery();

  /// Phase 3D: Transactional batch wake — parks all, then wakes all
  /// via collect→arbitrate→commit. Bypasses method channel serialization.
  Future<Map<String, dynamic>> batchWakeAll() =>
      MorphicPlatform.instance.batchWakeAll();

  /// Phase 3E: Scoped lifecycle — only affects specified surface IDs.
  /// Avoids zombie renderer resurrection.
  Future<Map<String, dynamic>> lifecycleOrchestrateSurfaces(
      String target, List<int> surfaceIds) =>
      MorphicPlatform.instance.lifecycleOrchestrateSurfaces(target, surfaceIds);

  /// Phase 3E: Scoped batch wake — only parks+wakes specified surface IDs.
  Future<Map<String, dynamic>> batchWakeSurfaces(List<int> surfaceIds) =>
      MorphicPlatform.instance.batchWakeSurfaces(surfaceIds);

  /// Phase X: Hard destruction — 7-stage authoritative renderer destruction.
  /// Truly destroys Flutter engines (unlike detachRenderer which creates zombies).
  Future<Map<String, dynamic>> hardDetachRenderer(int surfaceId) =>
      MorphicPlatform.instance.hardDetachRenderer(surfaceId);

  /// Phase X: HWND census — enumerate all top-level HWNDs in process.
  Future<Map<String, dynamic>> hwndCensus() =>
      MorphicPlatform.instance.hwndCensus();

  // ==========================================================================
  // MORPHIC STUDIO: Workspace Lifecycle (PUBLIC API)
  // ==========================================================================

  /// Create a workspace with semantic configuration.
  Future<int> createWorkspace({
    String activity = 'editing',
    String disposition = 'persistent',
    String label = '',
  }) => MorphicPlatform.instance.createWorkspace({
    'activity': activity,
    'disposition': disposition,
    'label': label,
  });

  /// Destroy a workspace.
  Future<bool> destroyWorkspace(int workspaceId) =>
      MorphicPlatform.instance.destroyWorkspace(workspaceId);

  /// Switch to a workspace.
  Future<bool> switchWorkspace(int workspaceId) =>
      MorphicPlatform.instance.switchWorkspace(workspaceId);

  /// Get the active workspace ID.
  Future<int> activeWorkspace() =>
      MorphicPlatform.instance.activeWorkspace();

  /// Update a workspace's intent declaration.
  Future<bool> setWorkspaceIntent(int workspaceId, {
    String activity = 'editing',
    String disposition = 'persistent',
    String label = '',
  }) => MorphicPlatform.instance.setWorkspaceIntent(workspaceId, {
    'activity': activity,
    'disposition': disposition,
    'label': label,
  });

  /// Declare surface attention level (advisory).
  Future<bool> setSurfaceAttention(int surfaceId, String attention) =>
      MorphicPlatform.instance.setSurfaceAttention(surfaceId, attention);

  /// Associate two surfaces (NOT dependency — just association).
  Future<bool> associateSurfaces(int a, int b, String association) =>
      MorphicPlatform.instance.associateSurfaces(a, b, association);

  /// Remove all associations for a surface.
  Future<bool> dissociateSurface(int surfaceId) =>
      MorphicPlatform.instance.dissociateSurface(surfaceId);

  // ==========================================================================
  // MORPHIC STUDIO: Session
  // ==========================================================================

  /// Save the current session state.
  Future<bool> saveSession(String reason) =>
      MorphicPlatform.instance.saveSession(reason);

  /// Restore a previously saved session.
  Future<bool> restoreSession() =>
      MorphicPlatform.instance.restoreSession();

  // ==========================================================================
  // MORPHIC STUDIO: Diagnostics
  // ==========================================================================

  /// Get runtime health diagnostics.
  Future<Map<String, dynamic>> getDiagnostics() =>
      MorphicPlatform.instance.getDiagnostics();

  /// Run all validation suites.
  Future<Map<String, dynamic>> runValidation() =>
      MorphicPlatform.instance.runValidation();

  /// Get the current bootstrap phase.
  Future<String> getBootstrapPhase() =>
      MorphicPlatform.instance.getBootstrapPhase();

  /// Dispose — alias for shutdown.
  Future<void> dispose() => shutdown();
}

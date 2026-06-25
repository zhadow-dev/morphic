import 'package:plugin_platform_interface/plugin_platform_interface.dart';
import 'morphic_method_channel.dart';

/// Platform interface for Morphic compositor.
abstract class MorphicPlatform extends PlatformInterface {
  MorphicPlatform() : super(token: _token);
  static final Object _token = Object();

  static MorphicPlatform _instance = MethodChannelMorphic();
  static MorphicPlatform get instance => _instance;
  static set instance(MorphicPlatform instance) {
    PlatformInterface.verifyToken(instance, _token);
    _instance = instance;
  }

  Future<bool> initialize();
  Future<bool> shutdown();
  Future<int> createSurface(Map<String, dynamic> config);
  Future<bool> destroySurface(int id);
  Future<bool> setSurfaceRole(int id, String role);
  Future<int> createGroup(List<int> memberIds);
  Future<bool> destroyGroup(int id);
  Future<bool> moveSurface(int id, int x, int y);
  Future<bool> resizeSurface(int id, int width, int height);
  Future<bool> setDebugOverlay(bool enabled);
  Future<List<Map<Object?, Object?>>> getDisplays();
  Future<Map<Object?, Object?>> getMetrics();
  Future<List<Map<Object?, Object?>>> runStressTest();
  Future<Map<Object?, Object?>> runReplayTest();
  Future<Map<Object?, Object?>> runSoakTest({int durationMinutes = 1});
  Future<Map<Object?, Object?>> runCrashRecoveryTest();
  Future<Map<Object?, Object?>> attachRenderer(int surfaceId, String type);
  Future<bool> detachRenderer(int surfaceId);
  Future<Map<Object?, Object?>> runResizeStorm();
  Future<Map<Object?, Object?>> runZombieAudit();
  Future<Map<Object?, Object?>> runGpuProfile();
  Future<Map<Object?, Object?>> runDecayTracking();
  Future<Map<Object?, Object?>> runThreadAudit();
  Future<Map<Object?, Object?>> queryFrameCadence();
  Future<Map<Object?, Object?>> pauseRendererAnimations();
  Future<Map<Object?, Object?>> resumeRendererAnimations();
  Future<Map<Object?, Object?>> testLifecycleOrchestration(String target);
  Future<Map<Object?, Object?>> takeBenchmarkSnapshot(double elapsedSec, String phase, int prevTotalFrames);
  Future<Map<Object?, Object?>> declareWorkloadTraits(int surfaceId, {
    bool animationSensitive, bool cadenceIntensive, bool latencyCritical,
    bool interactionCritical, bool memoryHeavy, bool backgroundCompute,
    bool continuitySensitive, bool ephemeral, double estimatedWarmMB,
  });

  // Phase 3B.3: Governance semantics validation
  Future<Map<Object?, Object?>> simulateVisibility(int surfaceId, String state, {double confidence});
  Future<Map<Object?, Object?>> configureBudget({int maxEngines, double maxMB});
  Future<bool> clearSimulatedVisibility();
  Future<bool> resetBudget();
  Future<bool> simulateBattery({required bool onBattery, double percent});
  Future<bool> clearSimulatedBattery();
  Future<Map<String, dynamic>> batchWakeAll();
  Future<Map<String, dynamic>> lifecycleOrchestrateSurfaces(String target, List<int> surfaceIds);
  Future<Map<String, dynamic>> batchWakeSurfaces(List<int> surfaceIds);

  // Phase X: Hard destruction
  Future<Map<String, dynamic>> hardDetachRenderer(int surfaceId);
  Future<Map<String, dynamic>> hwndCensus();

  // Morphic Studio: Workspace lifecycle
  Future<int> createWorkspace(Map<String, dynamic> config);
  Future<bool> destroyWorkspace(int workspaceId);
  Future<bool> switchWorkspace(int workspaceId);
  Future<int> activeWorkspace();
  Future<bool> setWorkspaceIntent(int workspaceId, Map<String, dynamic> config);
  Future<bool> setSurfaceAttention(int surfaceId, String attention);
  Future<bool> associateSurfaces(int a, int b, String association);
  Future<bool> dissociateSurface(int surfaceId);

  // Morphic Studio: Session
  Future<bool> saveSession(String reason);
  Future<bool> restoreSession();

  // Morphic Studio: Diagnostics
  Future<Map<String, dynamic>> getDiagnostics();
  Future<Map<String, dynamic>> runValidation();
  Future<String> getBootstrapPhase();
}

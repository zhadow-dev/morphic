#ifndef FLUTTER_PLUGIN_MORPHIC_PLUGIN_H_
#define FLUTTER_PLUGIN_MORPHIC_PLUGIN_H_

#include <flutter/method_channel.h>
#include <flutter/plugin_registrar_windows.h>
#include <flutter/standard_method_codec.h>
#include <flutter/event_channel.h>
#include <flutter/event_stream_handler_functions.h>

#include <functional>
#include <memory>

#include "src/composition/compositor.h"
#include "src/api/morphic_runtime_impl.h"
#include "src/api/workspace_channel.h"
#include "src/api/session_channel.h"
#include "src/api/diagnostics_channel.h"

namespace morphic {

class MorphicPlugin : public flutter::Plugin {
 public:
  static void RegisterWithRegistrar(flutter::PluginRegistrarWindows *registrar);

  MorphicPlugin();
  virtual ~MorphicPlugin();

  MorphicPlugin(const MorphicPlugin&) = delete;
  MorphicPlugin& operator=(const MorphicPlugin&) = delete;

  void HandleMethodCall(
      const flutter::MethodCall<flutter::EncodableValue> &method_call,
      std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);

  void onMainWindowActivated();

  Compositor* compositor() const { return compositor_.get(); }

 private:
  std::unique_ptr<Compositor> compositor_;
  std::unique_ptr<MorphicRuntimeImpl> runtime_;  // THE runtime facade
  std::unique_ptr<WorkspaceChannel> workspaceChannel_;
  std::unique_ptr<SessionChannel> sessionChannel_;
  std::unique_ptr<DiagnosticsChannel> diagnosticsChannel_;
  HWND mainWindowHwnd_ = nullptr;
  // Method handlers
  void handleInitialize(std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);
  void handleShutdown(std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);
  void handleCreateSurface(const flutter::EncodableMap& args,
                           std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);
  void handleDestroySurface(const flutter::EncodableMap& args,
                            std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);
  void handleCreateGroup(const flutter::EncodableMap& args,
                         std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);
  void handleDestroyGroup(const flutter::EncodableMap& args,
                          std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);
  void handleMoveSurface(const flutter::EncodableMap& args,
                         std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);
  void handleResizeSurface(const flutter::EncodableMap& args,
                           std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);
  void handleSetDebugOverlay(const flutter::EncodableMap& args,
                             std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);
  void handleGetDisplays(std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);
  void handleGetMetrics(std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);
  void handleRunStressTest(const flutter::EncodableMap& args,
                           std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);
  void handleRunReplayTest(std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);
  void handleRunSoakTest(const flutter::EncodableMap& args,
                         std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);
  void handleRunCrashRecoveryTest(std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);
  void handleAttachRenderer(const flutter::EncodableMap& args,
                            std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);
  void handleDetachRenderer(const flutter::EncodableMap& args,
                            std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);
  void handleRunResizeStorm(std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);
  void handleRunZombieAudit(std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);
  void handleRunGpuProfile(std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);
  void handleRunDecayTracking(std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);
  void handleRunThreadAudit(std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);
  void handleFrameProduced(const flutter::EncodableMap& args,
                           std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);
  void handleQueryFrameCadence(std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);
  void handlePauseRendererAnimations(std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);
  void handleResumeRendererAnimations(std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);
  void handleTestLifecycleOrchestration(const flutter::EncodableMap& args,
                                        std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);
  void handleTakeBenchmarkSnapshot(const flutter::EncodableMap& args,
                                    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);
  void handleDeclareWorkloadTraits(const flutter::EncodableMap& args,
                                    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);
  void handleSimulateVisibility(const flutter::EncodableMap& args,
                                 std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);
  void handleConfigureBudget(const flutter::EncodableMap& args,
                              std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);

  // Helpers
  int sendCommandToSecondaryEngines(const std::string& command);
  static int64_t getInt(const flutter::EncodableMap& map, const std::string& key, int64_t def = 0);
  static double getDouble(const flutter::EncodableMap& map, const std::string& key, double def = 0.0);
  static bool getBool(const flutter::EncodableMap& map, const std::string& key, bool def = false);

  // Phase 3C: Static callback for FrameCadenceMonitor frame events.
  // Broadcasts to all renderer recovery trackers.
  static void onFrameProduced(int engineId, void* userData);
};

}  // namespace morphic

#endif  // FLUTTER_PLUGIN_MORPHIC_PLUGIN_H_

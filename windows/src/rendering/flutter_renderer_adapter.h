#pragma once

#include "renderer_adapter.h"
#include "renderer_capabilities.h"
#include "activity_state.h"
#include <flutter_windows.h>
#include <flutter/standard_method_codec.h>
#include <flutter/method_call.h>
#include <string>
#include <windows.h>

namespace morphic {

// Phase 2B — Flutter Renderer Adapter
//
// Translates abstract orchestration commands into Flutter-specific actions:
//   - Lifecycle state via `flutter/lifecycle` platform channel (StringCodec)
//   - Animation control via `morphic` method channel (StandardMethodCodec)
//
// RENDERER CONTRACT:
//   This adapter MAY send hints and request cadence changes.
//   This adapter MAY NOT mutate compositor topology or bypass policy.
//
// THREAD: UI thread only.
class FlutterRendererAdapter : public RendererAdapter {
public:
    explicit FlutterRendererAdapter(FlutterDesktopEngineRef engine)
        : engine_(engine) {}

    CommandResult requestActivityTransition(ActivityState desired) override {
        if (!engine_) return CommandResult::Failed;

        auto* messenger = FlutterDesktopEngineGetMessenger(engine_);
        if (!messenger) return CommandResult::Failed;

        switch (desired) {
            case ActivityState::Active:
                sendLifecycleState(messenger, "AppLifecycleState.resumed");
                sendMorphicCommand(messenger, "resumeAnimations");
                lastRequestedState_ = desired;
                return CommandResult::Accepted;

            case ActivityState::Throttled:
                sendLifecycleState(messenger, "AppLifecycleState.inactive");
                sendMorphicCommand(messenger, "pauseAnimations");
                lastRequestedState_ = desired;
                return CommandResult::PartiallyApplied;

            case ActivityState::Parked:
                sendLifecycleState(messenger, "AppLifecycleState.paused");
                sendMorphicCommand(messenger, "pauseAnimations");
                lastRequestedState_ = desired;
                return CommandResult::PartiallyApplied;

            case ActivityState::Dormant:
                sendLifecycleState(messenger, "AppLifecycleState.hidden");
                sendMorphicCommand(messenger, "pauseAnimations");
                lastRequestedState_ = desired;
                return CommandResult::PartiallyApplied;
        }

        return CommandResult::Failed;
    }

    RendererCapabilities capabilities() const override {
        return RendererCapabilities{
            true,   // supportsCadenceThrottle
            true,   // supportsAnimationPause
            false,  // supportsPresentationFreeze
            false,  // supportsVisibilityHints
            false,  // supportsWarmParking
            CooperationLevel::SemiCooperative,
            std::chrono::milliseconds{0}
        };
    }

    ActivityState lastRequestedState() const { return lastRequestedState_; }

private:
    // Send lifecycle state string on flutter/lifecycle channel.
    // Uses StringCodec — raw string bytes, no JSON encoding.
    void sendLifecycleState(FlutterDesktopMessengerRef messenger,
                            const char* state) {
        bool ok = FlutterDesktopMessengerSend(
            messenger,
            "flutter/lifecycle",
            reinterpret_cast<const uint8_t*>(state),
            strlen(state));

        OutputDebugStringA(("FLUTTER_ADAPTER: lifecycle -> " +
            std::string(state) + (ok ? " [ok]" : " [FAIL]") + "\n").c_str());
    }

    // Send command on existing morphic method channel.
    // Uses StandardMethodCodec — matches the Dart side's setMethodCallHandler.
    void sendMorphicCommand(FlutterDesktopMessengerRef messenger,
                            const char* method) {
        auto& codec = flutter::StandardMethodCodec::GetInstance();
        flutter::MethodCall<flutter::EncodableValue> call(
            std::string(method),
            std::make_unique<flutter::EncodableValue>(flutter::EncodableMap()));
        auto encoded = codec.EncodeMethodCall(call);

        bool ok = FlutterDesktopMessengerSend(
            messenger,
            "morphic",
            encoded->data(), encoded->size());

        OutputDebugStringA(("FLUTTER_ADAPTER: morphic -> " +
            std::string(method) + (ok ? " [ok]" : " [FAIL]") + "\n").c_str());
    }

    FlutterDesktopEngineRef engine_ = nullptr;
    ActivityState lastRequestedState_ = ActivityState::Active;
};

}  // namespace morphic

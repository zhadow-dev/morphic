#include "surface_lifecycle_relay.h"

#include <flutter/method_channel.h>
#include <flutter/standard_method_codec.h>

#include <memory>

#include "forensic_trace.h"
#include "surface_manager.h"
#include "surface_shell.h"

namespace {

// Push `app.event {topic, payload:{surfaceId}}` on a surface's `morphic/app` channel — the same
// envelope app.broadcast uses, so the Dart AppBus dispatches it by topic. Fire-and-forget.
void Push(flutter::BinaryMessenger* messenger, const char* topic,
          const std::string& surface_id) {
  if (messenger == nullptr) return;  // engine not up (or torn down) — skip
  flutter::EncodableMap payload{
      {flutter::EncodableValue("surfaceId"), flutter::EncodableValue(surface_id)}};
  flutter::EncodableMap event{
      {flutter::EncodableValue("topic"), flutter::EncodableValue(std::string(topic))},
      {flutter::EncodableValue("payload"), flutter::EncodableValue(payload)}};
  flutter::MethodChannel<flutter::EncodableValue> ch(
      messenger, "morphic/app", &flutter::StandardMethodCodec::GetInstance());
  ch.InvokeMethod("app.event", std::make_unique<flutter::EncodableValue>(event));
}

}  // namespace

SurfaceLifecycleRelay::SurfaceLifecycleRelay(EventBus* bus, SurfaceManager* manager)
    : bus_(bus), manager_(manager) {
  if (bus_) {
    bus_token_ = bus_->Subscribe(
        [this](RuntimeEvent e, SurfaceShell* s) { OnEvent(e, s); });
  }
  forensic::Log("LIFECYCLE",
                "SurfaceLifecycleRelay up (generic surface.created/destroyed/identity)");
}

SurfaceLifecycleRelay::~SurfaceLifecycleRelay() {
  if (bus_ && bus_token_ != 0) {
    bus_->Unsubscribe(bus_token_);
    bus_token_ = 0;
  }
}

void SurfaceLifecycleRelay::OnEvent(RuntimeEvent event, SurfaceShell* surface) {
  if (surface == nullptr) return;
  switch (event) {
    case RuntimeEvent::SurfaceReady:
      // Hand THIS surface its own opaque id (reliable: engine rendered its first frame).
      Push(surface->messenger(), "surface.identity", surface->id());
      break;
    case RuntimeEvent::SurfaceCreated:
      BroadcastToAll("surface.created", surface->id());
      break;
    case RuntimeEvent::SurfaceDestroyed:
      // Note: at publish time the dying surface is still in Surfaces(); pushing to its own
      // (tearing-down) engine is a harmless no-op — the point is every OTHER surface hears it.
      BroadcastToAll("surface.destroyed", surface->id());
      break;
    default:
      break;
  }
}

void SurfaceLifecycleRelay::BroadcastToAll(const char* topic,
                                           const std::string& surface_id) {
  if (manager_ == nullptr) return;
  for (SurfaceShell* s : manager_->Surfaces()) {
    if (s == nullptr) continue;
    Push(s->messenger(), topic, surface_id);
  }
}

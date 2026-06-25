#include "workspace/plane_visual_projector.h"

#include <flutter/method_channel.h>
#include <flutter/standard_method_codec.h>

#include <memory>
#include <string>

#include "forensic_trace.h"
#include "surface_manager.h"
#include "surface_policy/surface_appearance.h"  // kEnablePlaneMaterialCoherence
#include "surface_shell.h"
#include "workspace/plane_activation_model.h"

namespace morphic::workspace {

PlaneVisualProjector::PlaneVisualProjector(
    PlaneActivationModel* model, SurfaceManager* manager,
    std::function<std::string(const std::string&)> material_of)
    : model_(model), manager_(manager), material_of_(std::move(material_of)) {
  if (model_) {
    model_->SetOnChanged([this]() { ProjectAll(); });
  }
  forensic::Log("PLANE VISUAL",
                "PlaneVisualProjector created (Stage 2A — content-level dim)");
}

PlaneVisualProjector::~PlaneVisualProjector() {
  // Detach BEFORE we die so a late activation event can't fire into a freed projector.
  if (model_) model_->SetOnChanged(nullptr);
}

void PlaneVisualProjector::ProjectAll() {
  if constexpr (!kEnablePlaneVisualProjection) {
    return;
  }
  if (model_ == nullptr || manager_ == nullptr) return;

  int active_count = 0;
  int total = 0;
  for (SurfaceShell* s : manager_->Surfaces()) {
    if (s == nullptr) continue;
    flutter::BinaryMessenger* messenger = s->messenger();
    if (messenger == nullptr) continue;  // engine not up yet — skip (will catch next change)
    const bool active = model_->IsSurfaceVisuallyActive(s->id());
    // Transient per-surface channel: MethodChannel only wraps (messenger,name,codec) and
    // registers nothing on InvokeMethod, so constructing per push is cheap + lifecycle-free.
    flutter::MethodChannel<flutter::EncodableValue> channel(
        messenger, "morphic/plane", &flutter::StandardMethodCodec::GetInstance());
    channel.InvokeMethod(
        "setActive", std::make_unique<flutter::EncodableValue>(active));

    // Material identity (Stage M1) — push the surface's material token to its content, which
    // renders the matching Morphic MaterialRecipe over the native blur ("standard" = opaque).
    // Driven by the surface's current applied appearance (ecology query), so it's correct for a
    // glass workspace, a member that adopted the plane's glass, AND a grounded standard surface.
    const std::string material =
        (morphic::policy::kEnablePlaneMaterialCoherence && material_of_) ? material_of_(s->id())
                                                                         : std::string("standard");
    flutter::MethodChannel<flutter::EncodableValue> material_channel(
        messenger, "morphic/plane_material", &flutter::StandardMethodCodec::GetInstance());
    material_channel.InvokeMethod(
        "setMaterial", std::make_unique<flutter::EncodableValue>(material));

    ++total;
    if (active) ++active_count;
  }
  forensic::Log("PLANE VISUAL", "projected active=" + std::to_string(active_count) +
                                    "/" + std::to_string(total) + " surfaces");
}

}  // namespace morphic::workspace

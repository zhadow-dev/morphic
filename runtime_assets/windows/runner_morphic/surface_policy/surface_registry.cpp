#include "surface_policy/surface_registry.h"

#include "forensic_trace.h"
#include "runtime_events.h"
#include "surface_shell.h"

namespace morphic::policy {

SurfaceRegistry::SurfaceRegistry(EventBus* bus) : bus_(bus) {
  if (bus_) {
    bus_token_ = bus_->Subscribe(
        [this](RuntimeEvent event, SurfaceShell* surface) {
          OnEvent(event, surface);
        });
  }
  forensic::Log("ECOLOGY", "SurfaceRegistry created");
}

SurfaceRegistry::~SurfaceRegistry() {
  if (bus_ && bus_token_ != 0) {
    bus_->Unsubscribe(bus_token_);
    bus_token_ = 0;
  }
}

void SurfaceRegistry::Register(const SurfaceDescriptor& descriptor) {
  descriptors_[descriptor.id] = descriptor;
  forensic::Log("ECOLOGY", "register id=" + descriptor.id + " kind=" +
                               std::string(ToString(descriptor.kind)) +
                               " workspace=" + descriptor.workspace_id);
}

void SurfaceRegistry::Unregister(const std::string& id) {
  if (descriptors_.erase(id) > 0) {
    forensic::Log("ECOLOGY", "unregister id=" + id);
  }
}

const SurfaceDescriptor* SurfaceRegistry::Get(const std::string& id) const {
  auto it = descriptors_.find(id);
  return it == descriptors_.end() ? nullptr : &it->second;
}

bool SurfaceRegistry::Has(const std::string& id) const {
  return descriptors_.find(id) != descriptors_.end();
}

std::vector<SurfaceDescriptor> SurfaceRegistry::ByKind(SurfaceKind kind) const {
  std::vector<SurfaceDescriptor> out;
  for (const auto& [_, d] : descriptors_) {
    if (d.kind == kind) out.push_back(d);
  }
  return out;
}

std::vector<SurfaceDescriptor> SurfaceRegistry::ByWorkspace(
    const std::string& workspace_id) const {
  std::vector<SurfaceDescriptor> out;
  for (const auto& [_, d] : descriptors_) {
    if (d.workspace_id == workspace_id) out.push_back(d);
  }
  return out;
}

std::vector<SurfaceDescriptor> SurfaceRegistry::All() const {
  std::vector<SurfaceDescriptor> out;
  out.reserve(descriptors_.size());
  for (const auto& [_, d] : descriptors_) out.push_back(d);
  return out;
}

void SurfaceRegistry::OnEvent(RuntimeEvent event, SurfaceShell* surface) {
  // Auto-unregister on destroy so the registry can't leak stale descriptors. The
  // surface pointer is still readable here (SurfaceDestroyed fires before reap);
  // we read its id (the descriptor key), not retain the pointer.
  if (event == RuntimeEvent::SurfaceDestroyed && surface != nullptr) {
    Unregister(surface->id());
  }
}

}  // namespace morphic::policy

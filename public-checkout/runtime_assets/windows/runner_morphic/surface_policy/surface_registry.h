#ifndef RUNNER_SURFACE_POLICY_SURFACE_REGISTRY_H_
#define RUNNER_SURFACE_POLICY_SURFACE_REGISTRY_H_

#include <string>
#include <unordered_map>
#include <vector>

#include "runtime_events.h"  // EventBus + Token (runtime header — arrow points DOWN, allowed)
#include "surface_policy/surface_descriptor.h"

class EventBus;
class SurfaceShell;

// PHASE 10C — SurfaceRegistry.
//
// The central source of SEMANTIC surface metadata. Owns the id → SurfaceDescriptor
// map and nothing else: NO geometry, z-order, interaction, or presentation. It is
// a product-layer lookup table that lives ABOVE the runtime.
//
// LAYERING NOTE: this header includes a RUNTIME header (runtime_events.h). That is
// CORRECT — the sacred law is one-directional: policy MAY depend on runtime
// (arrow points down); runtime must NEVER depend on policy. The registry keys by
// the opaque string id (NOT a SurfaceShell*), so a descriptor survives the
// surface pointer's death and auto-unregisters on SurfaceDestroyed.
namespace morphic::policy {

class SurfaceRegistry {
 public:
  explicit SurfaceRegistry(EventBus* bus);
  ~SurfaceRegistry();

  SurfaceRegistry(const SurfaceRegistry&) = delete;
  SurfaceRegistry& operator=(const SurfaceRegistry&) = delete;

  // Register / replace a descriptor (keyed by descriptor.id).
  void Register(const SurfaceDescriptor& descriptor);
  // Remove a descriptor by id (no-op if absent). Called by the bus subscription on
  // SurfaceDestroyed, and available manually.
  void Unregister(const std::string& id);

  // Lookup. Get returns nullptr if absent (the descriptor is owned by the map).
  const SurfaceDescriptor* Get(const std::string& id) const;
  bool Has(const std::string& id) const;

  // Queries (copies — small N).
  std::vector<SurfaceDescriptor> ByKind(SurfaceKind kind) const;
  std::vector<SurfaceDescriptor> ByWorkspace(const std::string& workspace_id) const;
  std::vector<SurfaceDescriptor> All() const;

  size_t size() const { return descriptors_.size(); }

 private:
  void OnEvent(RuntimeEvent event, SurfaceShell* surface);

  EventBus* bus_;             // not owned (runtime-owned)
  EventBus::Token bus_token_ = 0;
  std::unordered_map<std::string, SurfaceDescriptor> descriptors_;
};

}  // namespace morphic::policy

#endif  // RUNNER_SURFACE_POLICY_SURFACE_REGISTRY_H_

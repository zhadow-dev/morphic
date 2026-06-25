#ifndef RUNNER_SURFACE_POLICY_SURFACE_DESCRIPTOR_H_
#define RUNNER_SURFACE_POLICY_SURFACE_DESCRIPTOR_H_

#include <optional>
#include <string>

#include "surface_policy/composition_types.h"
#include "surface_policy/surface_appearance.h"  // SurfaceVisualMode
#include "surface_policy/surface_kind.h"

// PHASE 10B — SurfaceDescriptor.
//
// Separates RUNTIME IDENTITY (the opaque string id the runtime knows) from
// PRODUCT MEANING (kind, workspace membership, behavioral flags). This is semantic
// METADATA, NOT runtime authority — nothing here mutates geometry/z/interaction.
// The runtime never sees this struct (sacred law: product semantics live above the
// runtime).
//
// This phase is secretly about OWNERSHIP semantics, not the enum. The flags below
// (detachable/groupable/focusable/parent) are the real substance — who owns what,
// what follows what, what survives detach. Expect to discover some of these are
// wrong/awkward in real use; that's the point (and why persistence waits).

namespace morphic::policy {

struct SurfaceDescriptor {
  std::string id;  // == the runtime's opaque surface id (the join key)

  SurfaceKind kind = SurfaceKind::Workspace;
  // PHASE 12A — spatial mode, ORTHOGONAL to kind (Q5). Default Floating; an app sets
  // Composed when a surface joins a workspace composition plane (Phase 12B). Drives
  // appearance (shared-plane shadow) + future composition projection — never topology.
  SurfaceCompositionMode composition_mode = SurfaceCompositionMode::Floating;
  // SPATIAL MIGRATION — visual mode (grounded vs atmospheric). DEFAULT Standard: most surfaces
  // are opaque/grounded; Glass is a selective opt-in. Drives appearance (transparency +
  // backdrop) in ResolveAppearance — never topology/z/interaction.
  SurfaceVisualMode visual_mode = SurfaceVisualMode::Standard;
  std::string workspace_id;  // which workspace owns this surface ("" = none yet)

  // Behavioral flags (ownership semantics). Defaults are the Workspace defaults;
  // SurfacePolicy::DefaultDescriptorFor overrides per-kind.
  bool detachable = true;   // may be pulled out of its group/workspace
  bool groupable = true;    // may participate in grouped drag/dock
  bool focusable = true;    // may hold keyboard focus

  // Persistence flags — FORWARD-LOOKING. Present now so the descriptor shape is
  // stable, but CONSUMED ONLY when persistence (10F) lands. Inert this phase.
  bool persistent = false;                 // survives restart
  bool participates_in_restore = false;    // enumerated during restore

  // Ownership lineage: the surface this one belongs to (e.g. an Inspector's
  // workspace surface, a palette's parent). nullopt = top-level.
  std::optional<std::string> parent_surface;
};

}  // namespace morphic::policy

#endif  // RUNNER_SURFACE_POLICY_SURFACE_DESCRIPTOR_H_

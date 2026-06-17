#ifndef RUNNER_WORKSPACE_COMPOSITION_PLANE_H_
#define RUNNER_WORKSPACE_COMPOSITION_PLANE_H_

#include <string>
#include <vector>

#include "surface_policy/composition_types.h"

// PHASE 12B — CompositionPlane.
//
// A workspace ROOT + its visually-unified composed members. This is a PROJECTION
// relationship ONLY (shared movement / shadow / elevation) — it is NOT topology,
// interaction, docking, or grouping (those stay in SurfaceGraph / the runtime, which
// knows nothing about planes). Product layer; surfaces are referenced by opaque ids.
//
// Constraints (Q8): a surface belongs to ≤1 plane and planes do NOT nest (a plane root
// cannot also be another plane's member). `active=false` disables a plane without
// deleting it (experiment / rollback / hot-detach — Q refinement).
namespace morphic::workspace {

struct CompositionMember {
  std::string surface_id;
  // Per-member drag semantics (Q10). STORED now; the projector currently enforces only
  // root-drag-follows — member-as-leader behaviors (MovesPlane/Pinned) are a follow-on.
  morphic::policy::MemberDragBehavior drag_behavior =
      morphic::policy::MemberDragBehavior::MovesPlane;
};

struct CompositionPlane {
  std::string plane_id;          // stable identity (telemetry / plane-level ops)
  std::string root_surface_id;   // the workspace root
  std::vector<CompositionMember> members;
  bool shared_shadow_plane = true;
  bool shared_elevation = true;
  bool active = true;            // false = disabled-but-retained
};

}  // namespace morphic::workspace

#endif  // RUNNER_WORKSPACE_COMPOSITION_PLANE_H_

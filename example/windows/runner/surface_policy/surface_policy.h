#ifndef RUNNER_SURFACE_POLICY_SURFACE_POLICY_H_
#define RUNNER_SURFACE_POLICY_SURFACE_POLICY_H_

#include <optional>
#include <string>

#include "surface_policy/surface_descriptor.h"
#include "surface_policy/surface_kind.h"

// PHASE 10D — SurfacePolicy.
//
// The behavioral BRAIN of the product layer: it encodes the RULES about how
// surface kinds relate, spawn, persist, and own one another. It is the single home
// for these rules — they NEVER live in SurfaceModel / InteractionSession /
// CompositorRuntime (that would be semantic pollution of the runtime).
//
// CRITICAL DISCIPLINE: SurfacePolicy is PURELY DECLARATIVE. Every method is a pure
// function over kinds/descriptors. It has:
//   - NO runtime handles (no SurfaceModel/Manager/Router/Graph/Epoch)
//   - NO geometry / z-order / interaction
//   - NO lifecycle orchestration or callbacks
// If policy ever starts mutating runtime state or holding runtime pointers, it has
// become "a second hidden runtime" — refuse that. Keep policy boring.
//
// The taxonomy + these rules are EXPECTED to be partly wrong; they'll be tuned
// after lived use, which is why persistence (10F) is deferred.
namespace morphic::policy {

class SurfacePolicy {
 public:
  // May a surface of `parent_kind` spawn a child of `child_kind`? (e.g. a
  // Workspace may spawn a ToolPalette; a ToolPalette may NOT spawn a Workspace.)
  static bool CanSpawn(SurfaceKind parent_kind, SurfaceKind child_kind);

  // The default descriptor for a freshly-spawned surface of `kind` in `workspace`
  // with an optional `parent`. Sets the behavioral flags (detachable/groupable/
  // focusable/persistent/restore) per the kind's role. This is THE place per-kind
  // ownership semantics are decided.
  static SurfaceDescriptor DefaultDescriptorFor(
      const std::string& id, SurfaceKind kind, const std::string& workspace_id,
      std::optional<std::string> parent_surface);

  // Per-kind behavioral predicates (forward-looking for 10F; declarative now).
  static bool IsPersistent(SurfaceKind kind);    // survives restart
  static bool IsTransient(SurfaceKind kind);     // dies with its context / session
  static bool IsDetachable(SurfaceKind kind);    // may leave its group/workspace
  static bool IsGroupable(SurfaceKind kind);     // may participate in grouped drag
  static bool IsFocusable(SurfaceKind kind);     // may hold keyboard focus
  static bool FollowsParent(SurfaceKind kind);   // ownership follows a parent surface
  // PHASE 10.3 — GLOBAL meta surface: belongs to no workspace, excluded from
  // workspace ownership + activation clusters (today only EcologyLauncher).
  static bool IsGlobal(SurfaceKind kind);

  // PHASE 10.4G — Surface relationship matrix (pure, declarative). Formalizes the
  // behavioral truth the native fixes implement + feeds the coherency auditor's
  // expectations. These are the rows of the spec's matrix.
  static bool CanActivate(SurfaceKind kind);      // may become the semantic-active root
  static bool CanRaiseCluster(SurfaceKind kind);  // activating it raises owned utilities
  static bool CanOwn(SurfaceKind kind);           // may own (be the parent of) surfaces
  static bool CanBeOwned(SurfaceKind kind);       // may be owned by another surface
};

}  // namespace morphic::policy

#endif  // RUNNER_SURFACE_POLICY_SURFACE_POLICY_H_

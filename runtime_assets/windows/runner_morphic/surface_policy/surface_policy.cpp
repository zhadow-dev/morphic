#include "surface_policy/surface_policy.h"

#include "spatial_config.h"  // R5-lite — toggle source of truth

namespace morphic::policy {

// ---------------------------------------------------------------------------
// Behavioral predicates. These ARE the ownership semantics — the real substance
// of Phase 10. Expect to revise these after lived use (and that's the point).
// ---------------------------------------------------------------------------

bool SurfacePolicy::IsPersistent(SurfaceKind kind) {
  switch (kind) {
    case SurfaceKind::Workspace:
    case SurfaceKind::DetachedWorkspace:
      return true;   // workspaces are the durable inhabitable surfaces
    case SurfaceKind::ToolPalette:
    case SurfaceKind::Inspector:
    case SurfaceKind::Utility:
      return false;  // re-derived from their workspace on restore (tuned in 10F)
    case SurfaceKind::Overlay:
    case SurfaceKind::Command:
    case SurfaceKind::EcologyLauncher:
      return false;  // transient / global control — never persist as content
  }
  return false;
}

bool SurfacePolicy::IsTransient(SurfaceKind kind) {
  return kind == SurfaceKind::Overlay || kind == SurfaceKind::Command;
}

bool SurfacePolicy::IsDetachable(SurfaceKind kind) {
  switch (kind) {
    case SurfaceKind::Workspace:
    case SurfaceKind::DetachedWorkspace:
    case SurfaceKind::ToolPalette:
    case SurfaceKind::Inspector:
    case SurfaceKind::Utility:
      return true;
    case SurfaceKind::Overlay:
    case SurfaceKind::Command:
    case SurfaceKind::EcologyLauncher:
      return false;  // transient / global control — not pulled out of anything
  }
  return true;
}

// PROJECT LAW (user-stated): geometry may INFLUENCE PRESENTATION but may NEVER MUTATE SEMANTIC
// TOPOLOGY. Geometric docking (8D) violates this — its sole effect is SurfaceGraph::Group, i.e.
// it mutates membership from overlap, so two stacked workspaces auto-group and co-move. It is
// therefore DISABLED by default here, the policy chokepoint (docking inherits the rejection,
// per the comment below). Composition planes (explicit, AUTHORED membership via the composition
// graph + CompositionProjector) are a SEPARATE system and are UNAFFECTED — shared-drag for
// composed members still works. Flip to true ONLY to revive the 8D semantic-docking experiment.
constexpr bool kEnableGeometricDocking =
    morphic::config::kEnableGeometricDocking;  // value: spatial_config.h

bool SurfacePolicy::IsGroupable(SurfaceKind kind) {
  // PHASE 10.5b — ONLY workspace roots are topology-groupable (peer structural
  // co-movement: symmetric drag, shared interaction session, dock/fracture
  // semantics). Support surfaces — ToolPalette / Inspector / Utility — are
  // HIERARCHICAL ATTACHMENTS, not peers; if they enter a topology group (via
  // docking) they inherit symmetric shared-drag + fracture semantics that are wrong
  // for them ("independent for a few seconds, then become one"). Their intended
  // "move/raise/minimize with the workspace" behavior is workspace-ATTACHMENT
  // semantics — a future, deliberately-designed product feature — NOT grouping.
  // Overlay / Command (transient) and EcologyLauncher (global control) are likewise
  // non-groupable. This is the policy DATA; enforcement is the existing groupability
  // predicate chokepoint at SurfaceGraph::Group (PHASE 10.5 Fix 2), so docking —
  // whose only effect is Group() — inherits the rejection for free.
  if constexpr (!kEnableGeometricDocking) {
    // Law: geometry never mutates topology → NO surface is topology-groupable → docking's
    // Group() is refused → no auto-group/co-move from overlap. (Composition planes unaffected.)
    (void)kind;
    return false;
  } else {
    switch (kind) {
      case SurfaceKind::Workspace:
      case SurfaceKind::DetachedWorkspace:
        return true;
      case SurfaceKind::ToolPalette:
      case SurfaceKind::Inspector:
      case SurfaceKind::Utility:
      case SurfaceKind::Overlay:
      case SurfaceKind::Command:
      case SurfaceKind::EcologyLauncher:
        return false;
    }
    return false;
  }
}

bool SurfacePolicy::IsFocusable(SurfaceKind kind) {
  // Overlays are visual-only; everything else can take keyboard focus.
  return kind != SurfaceKind::Overlay;
}

bool SurfacePolicy::IsGlobal(SurfaceKind kind) {
  // GLOBAL surfaces belong to no workspace and are excluded from workspace
  // ownership / activation clusters. Today only the EcologyLauncher is global.
  return kind == SurfaceKind::EcologyLauncher;
}

// ---------------------------------------------------------------------------
// PHASE 10.4G — relationship matrix (declarative behavioral truth).
//
//   Kind               Activate  RaiseCluster  Own  BeOwned
//   Workspace          yes       yes           yes  no
//   DetachedWorkspace  yes       yes           yes  no
//   ToolPalette        limited   no            no   yes
//   Inspector          limited   no            no   yes
//   Utility            limited   no            no   yes
//   Overlay            transient no            no   yes
//   Command            transient no            no   yes
//   EcologyLauncher    global    no            no   no
// ---------------------------------------------------------------------------

bool SurfacePolicy::CanActivate(SurfaceKind kind) {
  // Everything can RECEIVE activation (be clicked/foregrounded); the distinctions
  // (limited/transient/global) are about cluster-raise + ownership, captured by the
  // other predicates. Overlays activate transiently but still activate.
  (void)kind;
  return true;
}

bool SurfacePolicy::CanRaiseCluster(SurfaceKind kind) {
  // Only workspace roots raise their owned utilities as a cluster. Utilities,
  // overlays, command, and the global launcher raise only themselves.
  return kind == SurfaceKind::Workspace ||
         kind == SurfaceKind::DetachedWorkspace;
}

bool SurfacePolicy::CanOwn(SurfaceKind kind) {
  // Workspaces own their palettes/inspectors. (The launcher is a control surface,
  // not an owner — its spawned surfaces go to a workspace, not under the launcher.)
  return kind == SurfaceKind::Workspace ||
         kind == SurfaceKind::DetachedWorkspace;
}

bool SurfacePolicy::CanBeOwned(SurfaceKind kind) {
  // Utilities/overlays/command may be owned by a workspace. Workspaces are roots
  // (never owned); the global launcher is never owned.
  switch (kind) {
    case SurfaceKind::ToolPalette:
    case SurfaceKind::Inspector:
    case SurfaceKind::Utility:
    case SurfaceKind::Overlay:
    case SurfaceKind::Command:
      return true;
    case SurfaceKind::Workspace:
    case SurfaceKind::DetachedWorkspace:
    case SurfaceKind::EcologyLauncher:
      return false;
  }
  return false;
}

bool SurfacePolicy::FollowsParent(SurfaceKind kind) {
  // Inspectors + palettes follow their workspace/parent; commands follow the
  // active context. Workspaces are roots (own nothing above them).
  switch (kind) {
    case SurfaceKind::Inspector:
    case SurfaceKind::ToolPalette:
    case SurfaceKind::Command:
      return true;
    case SurfaceKind::Workspace:
    case SurfaceKind::DetachedWorkspace:
    case SurfaceKind::Overlay:
    case SurfaceKind::Utility:
    case SurfaceKind::EcologyLauncher:  // global — follows nothing
      return false;
  }
  return false;
}

// ---------------------------------------------------------------------------
// Spawn rules.
// ---------------------------------------------------------------------------

bool SurfacePolicy::CanSpawn(SurfaceKind parent_kind, SurfaceKind child_kind) {
  // A ToolPalette / Overlay / Command / Inspector cannot become a workspace root,
  // and cannot themselves host child surfaces (flat product topology this phase).
  // The EcologyLauncher (global meta-control) MAY spawn surfaces — it's how the
  // user launches workspaces/palettes — but never a sub-surface OF itself.
  switch (parent_kind) {
    case SurfaceKind::ToolPalette:
    case SurfaceKind::Overlay:
    case SurfaceKind::Command:
    case SurfaceKind::Inspector:
    case SurfaceKind::Utility:
      return false;  // these are leaves — they don't spawn sub-surfaces (yet)

    case SurfaceKind::EcologyLauncher:
      // The launcher is a control surface, not a workspace parent: it spawns
      // top-level surfaces (the policy gate only checks parent ownership, and the
      // launcher owns nothing — spawned surfaces go to a workspace, not under it).
      return child_kind != SurfaceKind::EcologyLauncher;

    case SurfaceKind::Workspace:
    case SurfaceKind::DetachedWorkspace:
      // Workspaces may spawn auxiliary surfaces, but NOT another workspace root
      // (workspaces are created at the workspace level, not spawned by a surface).
      return child_kind != SurfaceKind::Workspace &&
             child_kind != SurfaceKind::DetachedWorkspace &&
             child_kind != SurfaceKind::EcologyLauncher;
  }
  return false;
}

SurfaceDescriptor SurfacePolicy::DefaultDescriptorFor(
    const std::string& id, SurfaceKind kind, const std::string& workspace_id,
    std::optional<std::string> parent_surface) {
  SurfaceDescriptor d;
  d.id = id;
  d.kind = kind;
  d.workspace_id = workspace_id;
  d.detachable = IsDetachable(kind);
  d.groupable = IsGroupable(kind);
  d.focusable = IsFocusable(kind);
  d.persistent = IsPersistent(kind);
  d.participates_in_restore = IsPersistent(kind);  // restore == persistent for now
  // Surfaces that follow a parent record it; roots don't.
  d.parent_surface = FollowsParent(kind) ? std::move(parent_surface)
                                         : std::nullopt;
  return d;
}

}  // namespace morphic::policy

#ifndef RUNNER_SURFACE_POLICY_COMPOSITION_TYPES_H_
#define RUNNER_SURFACE_POLICY_COMPOSITION_TYPES_H_

// PHASE 12A — composition taxonomy.
//
// PRODUCT-LAYER spatial vocabulary. The runtime NEVER sees these — composition is
// PROJECTION semantics, never topology / interaction / ownership / z. Spatial MODE is
// ORTHOGONAL to SurfaceKind (Q5): kind = behavioral role, composition = spatial mode;
// a ToolPalette may be Floating in one context and Composed in another. These types
// are consumed only by the surface_policy/ + workspace/ layers (appearance policy +
// the Phase 12B composition graph).
namespace morphic::policy {

// Is a surface an independent desktop window, or a visually-unified child of a
// workspace composition plane? Default Floating; an app marks a surface Composed and
// adds it to a CompositionPlane (Phase 12B). Composed does NOT mean grouped/owned in
// the topology sense — it is purely a visual/projection relationship.
enum class SurfaceCompositionMode {
  Floating,
  Composed,
};

// Per-MEMBER drag semantics within a composition plane. STORED now (12A), ENFORCED in
// the deferred 12D movement-attachment phase. Per-member, NOT derived from kind (Q10) —
// the same kind can be Pinned in one plane and MovesPlane in another.
enum class MemberDragBehavior {
  MovesPlane,    // dragging this member translates the whole plane (navbar/searchbar/root)
  Pinned,        // not directly draggable (sidebar/HUD ornament)
  Independent,   // moves alone (floating tool / overlay)
};

inline const char* ToString(SurfaceCompositionMode m) {
  switch (m) {
    case SurfaceCompositionMode::Floating: return "floating";
    case SurfaceCompositionMode::Composed: return "composed";
  }
  return "?";
}

inline const char* ToString(MemberDragBehavior d) {
  switch (d) {
    case MemberDragBehavior::MovesPlane:  return "moves_plane";
    case MemberDragBehavior::Pinned:      return "pinned";
    case MemberDragBehavior::Independent: return "independent";
  }
  return "?";
}

}  // namespace morphic::policy

#endif  // RUNNER_SURFACE_POLICY_COMPOSITION_TYPES_H_

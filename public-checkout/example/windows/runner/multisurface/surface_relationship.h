#ifndef RUNNER_MULTISURFACE_SURFACE_RELATIONSHIP_H_
#define RUNNER_MULTISURFACE_SURFACE_RELATIONSHIP_H_

// PHASE 8A — SurfaceRelationship
//
// SEMANTIC relationship kinds between surfaces. These are runtime concepts,
// NOT Win32 topology (SetParent, owner HWND, modal hierarchy etc. are still
// deliberately untouched — see CORE_HARDENING.md). The SurfaceGraph maintains
// the mapping; subsequent phases (8B/8C/8D) consume it for grouped
// interaction, extraction, and docking respectively.
//
// Cardinality:
//   - A child may have at most ONE Parent / DockHost / ExtractionTether
//     anchor at a time.
//   - A child may be in at most ONE Grouped set.
//   - Overlay / ToolPalette / Transient pin to exactly one anchor.
//   - Detached is a transient state (no anchor) — recorded for traceability
//     so observers see the topology mutation, not just an absence.
enum class SurfaceRelationship {
  None,

  // Hierarchical attachment. Semantic only — no SetParent.
  Parent,
  Child,

  // Visual subordination without ownership semantics.
  Overlay,
  ToolPalette,

  // Docking edges (Phase 8D consumes).
  DockHost,
  Docked,

  // Co-mover bundle (Phase 8B consumes — grouped drag/resize).
  Grouped,

  // Surface that just left an attachment (transient state, observable).
  Detached,

  // In-progress extraction (Phase 8C — tether is the soft pre-detach link).
  ExtractionTether,

  // Activation-coupled ephemeral overlay (closes when anchor deactivates).
  Transient,
};

const char* ToString(SurfaceRelationship r);

#endif  // RUNNER_MULTISURFACE_SURFACE_RELATIONSHIP_H_

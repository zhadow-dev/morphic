#pragma once

namespace morphic {

// Phase 3E.3 — Workspace mode foundation.
//
// This is architecture scaffolding ONLY.
// No rendering/layout work yet.
//
// Future: WorkspaceMode will drive compositor-wide behavior changes
// such as overview/exposé layout, surface enumeration, and
// animation orchestration.
enum class WorkspaceMode {
    Normal,     // Standard compositor operation
    Overview    // Future: zoom-out surface enumeration (Mission Control style)
};

}  // namespace morphic

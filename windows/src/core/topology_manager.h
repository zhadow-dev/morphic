#pragma once

#include "types.h"
#include "surface_role.h"
#include "mutation_authority.h"
#include <windows.h>

namespace morphic {

// Phase 4 Step 1B — TopologyManager.
//
// AUTHORITY: HWND topology and style derivation.
// This is the SINGLE authority for defining and applying window styles,
// extended styles, and parent/owner relationships.
//
// TopologyManager OWNS:
//   - GWL_STYLE mutation
//   - GWL_EXSTYLE mutation
//   - GWLP_HWNDPARENT mutation
//   - HWND creation policy derivation
//
// TopologyManager DOES NOT:
//   - Call CreateWindow (SurfaceRegistry/WindowHost does)
//   - Call DestroyWindow (SurfaceRegistry/WindowHost does)
//   - Call SetWindowPos for size/pos (Interaction/Compositor does)
//   - Call DeferWindowPos for z-order (ActivationManager does)
//
// DEPENDENCY LAYER: Layer 2 (Policy)
//   MAY NOT query RendererRuntime, OrchestrationRuntime, etc.
class TopologyManager {
public:
    TopologyManager() = default;

    struct TopologyPolicy {
        DWORD style;
        DWORD exStyle;
        HWND owner;       // nullptr = unowned (only for fallback)
    };

    // Derive policy for initial HWND creation (pure function, no mutation).
    static TopologyPolicy buildPolicy(SurfaceRole role, HWND mainWindow);

    // Apply policy to an existing HWND.
    // Acquires TOPOLOGY_AUTHORITY before mutating Win32 state.
    void applyTopology(HWND hwnd, SurfaceRole role, HWND mainWindow);
};

}  // namespace morphic

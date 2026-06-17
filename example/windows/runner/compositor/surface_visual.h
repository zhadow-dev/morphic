#ifndef RUNNER_COMPOSITOR_SURFACE_VISUAL_H_
#define RUNNER_COMPOSITOR_SURFACE_VISUAL_H_

#include <windows.h>

class SurfaceShell;

// PHASE 9 — SurfaceVisual.
//
// The per-surface PROJECTION record owned by CompositorRuntime. It is NOT a visual
// authority — it's a bookkeeping struct that records what the projection backend
// last applied, so divergence (presented ↔ projected) is observable.
//
// There is deliberately NO IDCompositionVisual* here today: Flutter 3.29's Windows
// embedder is HWND-only (renders into its own child HWND/swapchain, exposes no
// DComp surface), so there is nothing for a DComp visual to wrap. When a future
// renderer strategy exposes a composition surface, an `IDCompositionVisual* visual`
// field is added HERE and the HwndProjectionBackend is joined by a DCompBackend —
// a backend swap, NOT a semantic change. (That is the entire point of the seam.)
struct SurfaceVisual {
  SurfaceShell* shell = nullptr;
  RECT presented{};   // last presented rect handed to the backend (from coordinator/model)
  RECT projected{};   // what the backend actually applied (== presented with HWND backend)
};

#endif  // RUNNER_COMPOSITOR_SURFACE_VISUAL_H_

#ifndef RUNNER_SURFACE_POLICY_NATIVE_APPEARANCE_PROJECTION_H_
#define RUNNER_SURFACE_POLICY_NATIVE_APPEARANCE_PROJECTION_H_

#include <windows.h>  // HWND (policy/Win32 boundary — allowed here, like native_surface_policy)

#include "surface_policy/surface_appearance.h"

// PHASE 11B — native appearance projection.
//
// Maps SurfaceAppearance → concrete DWM attributes on a live HWND. This is the ONLY
// place appearance touches Win32. Like native_surface_policy (ex-style/owner), what
// crosses DOWN to the runtime is a PLAIN HWND + a SurfaceAppearance value — never
// SurfaceKind (firewall holds). Re-callable at any time (Q4 — appearance is runtime-
// mutable). NEVER affects semantics: this only calls DwmSetWindowAttribute /
// DwmExtendFrameIntoClientArea, which change pixels, not behavior.
namespace morphic::policy {

// Compile-time kill switch (default true; false = current bare frameless look, instant
// rollback — like kCompositorEnabled / kPresentationEnabled). Falls back cleanly: if a
// DWM attribute is unsupported on the host, the call is a harmless no-op.
inline constexpr bool kEnableNativeAppearance = true;

void ApplyAppearance(HWND hwnd, const SurfaceAppearance& appearance);

}  // namespace morphic::policy

#endif  // RUNNER_SURFACE_POLICY_NATIVE_APPEARANCE_PROJECTION_H_

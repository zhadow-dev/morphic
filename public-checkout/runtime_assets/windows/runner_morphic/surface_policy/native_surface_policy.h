#ifndef RUNNER_SURFACE_POLICY_NATIVE_SURFACE_POLICY_H_
#define RUNNER_SURFACE_POLICY_NATIVE_SURFACE_POLICY_H_

#include <windows.h>  // DWORD / WS_EX_* (this is the policy/Win32 boundary, allowed)

#include "surface_policy/native_surface_behavior.h"
#include "surface_policy/surface_descriptor.h"

// PHASE 10.3 — NativeSurfacePolicy.
//
// Resolves SurfaceKind/descriptor → NativeSurfaceBehavior → plain Win32 ex-style.
// This is the ONLY place SurfaceKind is mapped to native shell behavior. The
// output (a DWORD ex-style + the owner-resolution decision) is what the policy
// layer hands DOWN to the runtime — the runtime never sees the kind.
//
// `WS_EX_TOOLWINDOW` is the single Win32 mechanism that removes a window from BOTH
// the Alt+Tab list AND the taskbar — exactly "utility" semantics. `WS_EX_APPWINDOW`
// makes a window a first-class task-switchable app citizen.
namespace morphic::policy {

// Map a descriptor (its kind) to the native behavior intent.
NativeSurfaceBehavior ResolveNativeBehavior(const SurfaceDescriptor& descriptor);

// Map native behavior to the concrete Win32 extended window style. Pure function.
DWORD ToExStyle(const NativeSurfaceBehavior& behavior);

}  // namespace morphic::policy

#endif  // RUNNER_SURFACE_POLICY_NATIVE_SURFACE_POLICY_H_

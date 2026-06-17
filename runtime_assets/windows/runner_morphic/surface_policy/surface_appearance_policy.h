#ifndef RUNNER_SURFACE_POLICY_SURFACE_APPEARANCE_POLICY_H_
#define RUNNER_SURFACE_POLICY_SURFACE_APPEARANCE_POLICY_H_

#include "surface_policy/surface_appearance.h"
#include "surface_policy/surface_descriptor.h"

// PHASE 11A — appearance policy. Pure function: descriptor → SurfaceAppearance, per the
// per-kind table (Q1). Declarative, no runtime handles, no Win32.
namespace morphic::policy {

// `is_plane_root` comes from the Phase 12B composition graph (false until composition
// exists). It decides shared-plane shadow: a Composed plane ROOT casts the plane's
// shadow (SharedPlane); a Composed MEMBER suppresses its own (None, no stacking); a
// Floating surface casts an Independent shadow. The launcher is always flat (None).
SurfaceAppearance ResolveAppearance(const SurfaceDescriptor& descriptor,
                                    bool is_plane_root = false,
                                    bool in_plane = false);

}  // namespace morphic::policy

#endif  // RUNNER_SURFACE_POLICY_SURFACE_APPEARANCE_POLICY_H_

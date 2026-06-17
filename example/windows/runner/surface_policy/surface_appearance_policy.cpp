#include "surface_policy/surface_appearance_policy.h"

namespace morphic::policy {

SurfaceAppearance ResolveAppearance(const SurfaceDescriptor& d, bool is_plane_root,
                                    bool in_plane) {
  SurfaceAppearance a;
  a.immersive_dark_mode = true;

  // Corners per kind (role aesthetics — independent of visual mode).
  switch (d.kind) {
    case SurfaceKind::Workspace:
    case SurfaceKind::DetachedWorkspace:
    case SurfaceKind::Overlay:
    case SurfaceKind::Command:
    case SurfaceKind::EcologyLauncher:
      a.corners = SurfaceCornerStyle::Rounded;
      break;
    case SurfaceKind::ToolPalette:
    case SurfaceKind::Inspector:
    case SurfaceKind::Utility:
      a.corners = SurfaceCornerStyle::SmallRounded;
      break;
  }

  // Visual mode → transparency + backdrop. Standard (the DEFAULT) is opaque/grounded — most
  // surfaces stay grounded so the desktop isn't visual soup. Glass is a selective atmospheric
  // upgrade; its backdrop FLAVOR (Mica vs Acrylic) is a starting point, overridable per-surface
  // (the launcher's backdrop selector). NOTE (verified): a backdrop only renders live blur
  // under DWM conditions; the intent is recorded regardless.
  if (d.visual_mode == SurfaceVisualMode::Glass) {
    a.transparency_mode = SurfaceTransparencyMode::FullGlass;
    a.backdrop = (d.kind == SurfaceKind::Overlay || d.kind == SurfaceKind::Command)
                     ? SurfaceBackdrop::Acrylic
                     : SurfaceBackdrop::Mica;
  } else {
    a.transparency_mode = SurfaceTransparencyMode::Opaque;
    a.backdrop = SurfaceBackdrop::None;
  }

  // Shadow participation. Launcher is flat. A Composed surface that is actually IN a
  // composition plane casts only if it's the ROOT (members suppressed → no stacking); a
  // Floating surface — or a Composed one NOT yet in a plane — casts its own Independent
  // shadow.
  if (d.kind == SurfaceKind::EcologyLauncher) {
    a.shadow = ShadowParticipation::None;
  } else if (d.composition_mode == SurfaceCompositionMode::Composed && in_plane) {
    a.shadow = is_plane_root ? ShadowParticipation::SharedPlane
                             : ShadowParticipation::None;
  } else {
    a.shadow = ShadowParticipation::Independent;
  }

  return a;
}

}  // namespace morphic::policy

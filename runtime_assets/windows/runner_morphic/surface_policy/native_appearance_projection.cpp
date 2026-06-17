#include "surface_policy/native_appearance_projection.h"

#include <dwmapi.h>

#include "forensic_trace.h"
#include "qualification_probe.h"

#pragma comment(lib, "dwmapi.lib")  // defensive: ensure DWM linkage for this TU

// DWM attribute constants (mirror surface_shell.cpp's anon defines; some SDKs lack them).
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#define DWMWA_SYSTEMBACKDROP_TYPE 38
#endif
#ifndef DWMWCP_DEFAULT
#define DWMWCP_DEFAULT 0
#endif
#ifndef DWMWCP_DONOTROUND
#define DWMWCP_DONOTROUND 1
#endif
#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2
#endif
#ifndef DWMWCP_ROUNDSMALL
#define DWMWCP_ROUNDSMALL 3
#endif
#ifndef DWMSBT_NONE
#define DWMSBT_NONE 1
#endif
#ifndef DWMSBT_MAINWINDOW
#define DWMSBT_MAINWINDOW 2  // Mica
#endif
#ifndef DWMSBT_TRANSIENTWINDOW
#define DWMSBT_TRANSIENTWINDOW 3  // Acrylic
#endif
#ifndef DWMSBT_TABBEDWINDOW
#define DWMSBT_TABBEDWINDOW 4  // Tabbed (Mica Alt)
#endif

namespace morphic::policy {
namespace {

DWORD CornerPref(SurfaceCornerStyle c) {
  switch (c) {
    case SurfaceCornerStyle::Default:      return DWMWCP_DEFAULT;
    case SurfaceCornerStyle::Rounded:      return DWMWCP_ROUND;
    case SurfaceCornerStyle::SmallRounded: return DWMWCP_ROUNDSMALL;
    case SurfaceCornerStyle::Square:       return DWMWCP_DONOTROUND;
  }
  return DWMWCP_DEFAULT;
}

// ---------------------------------------------------------------------------
// PHASE 11E-B.3 — Legacy ACCENT blur-behind (SetWindowCompositionAttribute).
//
// The Win11 DWMWA_SYSTEMBACKDROP_TYPE materials never engaged on our frameless WS_POPUP
// (they require WS_CAPTION/overlapped-window "main window" heuristics — confirmed by
// probes 2P/2Q/2R, all fallback-grey). This is the OTHER Windows blur API — the one
// flutter_acrylic / Electron / PowerToys use — which engages on ARBITRARY windows,
// including WS_POPUP, with NO window-style or WM_NCCALCSIZE change. So it pursues real
// blur with ZERO interaction (7A) risk. Undocumented (reverse-engineered struct +
// GetProcAddress), but stable and widely shipped.
// ---------------------------------------------------------------------------
enum AccentState : int {
  ACCENT_DISABLED = 0,
  ACCENT_ENABLE_BLURBEHIND = 3,         // Win10 blur
  ACCENT_ENABLE_ACRYLICBLURBEHIND = 4,  // Win10 1803+ acrylic
};

struct ACCENTPOLICY {
  int state;
  int flags;
  unsigned int gradient_color;  // 0xAABBGGRR — AA = tint opacity over the blur
  int animation_id;
};

struct WINCOMPATTRDATA {
  int attribute;  // WCA_ACCENT_POLICY = 19
  void* data;
  unsigned long data_size;
};

using PFN_SetWindowCompositionAttribute = BOOL(WINAPI*)(HWND, WINCOMPATTRDATA*);

void ApplyAccent(HWND hwnd, AccentState state, unsigned int gradient_abgr) {
  static const auto set_attr =
      reinterpret_cast<PFN_SetWindowCompositionAttribute>(GetProcAddress(
          GetModuleHandleW(L"user32.dll"), "SetWindowCompositionAttribute"));
  if (set_attr == nullptr) return;  // not available → no-op (safe)
  ACCENTPOLICY policy{};
  policy.state = state;
  policy.flags = 0;  // apply to the whole window
  policy.gradient_color = gradient_abgr;
  policy.animation_id = 0;
  WINCOMPATTRDATA data{};
  data.attribute = 19;  // WCA_ACCENT_POLICY
  data.data = &policy;
  data.data_size = sizeof(policy);
  set_attr(hwnd, &data);
}

}  // namespace

void ApplyAppearance(HWND hwnd, const SurfaceAppearance& a) {
  if constexpr (!kEnableNativeAppearance) {
    return;
  }
  if (hwnd == nullptr) {
    return;
  }

  // Rounded corners (Win11). Overrides the shell's create-time DONOTROUND default.
  DWORD corner = CornerPref(a.corners);
  DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));

  // Immersive dark mode.
  BOOL dark = a.immersive_dark_mode ? TRUE : FALSE;
  DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));

  // Frame extension for the glass base (full sheet-of-glass for transparent modes; a 1px
  // sliver for a drop shadow otherwise). The transparent Flutter content composites over
  // whatever the window's DWM backdrop is (desktop for plain glass; the ACCENT blur below
  // for material backdrops).
  const bool casts = (a.shadow == ShadowParticipation::Independent ||
                      a.shadow == ShadowParticipation::SharedPlane);
  MARGINS margins = ModeWantsFullGlass(a.transparency_mode) ? MARGINS{-1, -1, -1, -1}
                    : casts ? MARGINS{0, 0, 0, 1}
                            : MARGINS{0, 0, 0, 0};
  DwmExtendFrameIntoClientArea(hwnd, &margins);

  // PHASE 11G-PROBE — on the overlapped-qualified shell (WS_OVERLAPPEDWINDOW + surviving
  // frame, see surface_shell.cpp), apply the REAL Win11 SYSTEMBACKDROP materials and DISABLE
  // the legacy ACCENT path entirely, so the result is an uncontaminated test of whether
  // Mica/Acrylic/Tabbed differentiate into their proper DWM material classes. Either outcome
  // settles the architecture question. Default-off path below is unchanged.
  if constexpr (morphic::probe::kQualificationProbe) {
    DWORD sbt = DWMSBT_NONE;
    switch (a.backdrop) {
      case SurfaceBackdrop::None:    sbt = DWMSBT_NONE; break;
      case SurfaceBackdrop::Mica:    sbt = DWMSBT_MAINWINDOW; break;
      case SurfaceBackdrop::Acrylic: sbt = DWMSBT_TRANSIENTWINDOW; break;
      case SurfaceBackdrop::Tabbed:  sbt = DWMSBT_TABBEDWINDOW; break;
    }
    DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &sbt, sizeof(sbt));
    ApplyAccent(hwnd, ACCENT_DISABLED, 0);  // no ACCENT contamination
    forensic::Log("11G-PROBE",
                  std::string("real SYSTEMBACKDROP applied sbt=") + std::to_string(sbt) +
                      " backdrop=" + std::to_string(static_cast<int>(a.backdrop)));
    return;  // skip the ACCENT path below — this is the qualified-material test
  }

  // Disable the Win11 SYSTEMBACKDROP material — it never rendered on our frameless WS_POPUP
  // (fallback-grey), and leaving it set would fight the ACCENT blur below.
  DWORD sbt_none = DWMSBT_NONE;
  DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &sbt_none, sizeof(sbt_none));

  // Real backdrop via ACCENT (engages on WS_POPUP). Tints are 0xAABBGGRR over the surface
  // frame color RGB(40,42,54); the alpha (AA) controls how much tint sits over the blur.
  // Mica isn't an ACCENT state, so it's approximated as a lightly-tinted acrylic.
  switch (a.backdrop) {
    case SurfaceBackdrop::None:
      ApplyAccent(hwnd, ACCENT_DISABLED, 0);  // plain glass — desktop shows through
      break;
    case SurfaceBackdrop::Mica:
      ApplyAccent(hwnd, ACCENT_ENABLE_ACRYLICBLURBEHIND, 0x40362A28);  // subtle
      break;
    case SurfaceBackdrop::Acrylic:
      ApplyAccent(hwnd, ACCENT_ENABLE_ACRYLICBLURBEHIND, 0x99362A28);  // stronger tint
      break;
    case SurfaceBackdrop::Tabbed:
      ApplyAccent(hwnd, ACCENT_ENABLE_BLURBEHIND, 0x33362A28);  // plain blur
      break;
  }
}

}  // namespace morphic::policy

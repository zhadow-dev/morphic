#ifndef RUNNER_WINDOW_TOPOLOGY_H_
#define RUNNER_WINDOW_TOPOLOGY_H_

#include <windows.h>

#include <string>
#include <vector>

// MORPHIC WINDOW TOPOLOGY — explicit window-role model + auditor.
//
// The permanent shell architecture (doc/WINDOW_TOPOLOGY.md) separates the five
// responsibilities that used to be conflated in the Flutter HWND into distinct
// roles. This header is the single source of that vocabulary and a diagnostic
// that logs a window node's full shell/composition state, so every topology
// transition (and the pre/post-inversion baseline) is observable, not assumed.
namespace morphic::topo {

enum class WindowRole {
  kShellRoot,      // canonical workspace window: Alt-Tab/taskbar/activation/preview
  kSpatialHost,    // DComp visual host + input router; non-shell tool window
  kRenderSource,   // cloaked Flutter engine; WGC source only; non-shell, non-activatable
  kToolSurface,    // auxiliary native window (palette/etc.); non-shell tool window
  kPoolOwner,      // ENGINE RETENTION (R1): process-owned "graveyard root" that owns
                   // DORMANT render_sources. Pure infrastructure — never shown, never
                   // a shell/Alt-Tab/preview/activation citizen, never cloaked, no
                   // ownership cycle. A dormant engine re-parents here so it survives
                   // workspace teardown and stays semantically dead. See
                   // doc/ENGINE_RETENTION_POOL_DESIGN.md.
};

const char* RoleName(WindowRole role);

// THE SINGLE PLACE window behavior is set. Derive owner + ex-styles + cloak
// PURELY from the role — no ad-hoc style mutation anywhere else. Re-asserts the
// frame so the shell re-reads taskbar/Alt-Tab eligibility, then logs the result.
//
//   shell_root    — unowned, APPWINDOW, activatable, not cloaked  (shell identity)
//   spatial_host  — owned by shell_root, TOOLWINDOW, not cloaked  (visible scene)
//   render_source — owned by spatial_host, TOOLWINDOW|NOACTIVATE, CLOAKED  (worker)
//   tool_surface  — owned by shell_root, TOOLWINDOW
void ApplyRolePolicy(HWND hwnd, WindowRole role, HWND owner);

// Cloak / uncloak via DWMWA_CLOAK (the only desktop-concealment that preserves
// presents + WGC capture). Inherits owner→owned, so only leaf render_sources.
void SetCloaked(HWND hwnd, bool cloaked);

// Log one window node: role, owner, parent, decoded style/exstyle, visibility,
// DWM cloak state, and shell (taskbar/Alt-Tab) eligibility per the Win32 rule
// (visible && unowned && (APPWINDOW || !TOOLWINDOW)). Subsystem tag "WINTOPO".
void LogWindowNode(WindowRole role, HWND hwnd, const std::string& tag);

// Is `hwnd` currently a taskbar/Alt-Tab citizen by the documented rule?
bool IsShellEligible(HWND hwnd);

// Is `hwnd` DWM-cloaked?
bool IsCloaked(HWND hwnd);

// ---------------------------------------------------------------------------
// INVARIANT ASSERTION LAYER (Phase 11) — runtime infrastructure, not tooling.
//
// The runtime is self-defending: after every critical topology transition it
// re-validates the role/ownership/cloak/shell-eligibility invariants over the
// live window graph. In STRICT mode a violation fails LOUDLY and IMMEDIATELY
// (logs [INVARIANT FAIL] + the offending node + the transition name, then
// raises so the crash handler writes a minidump pinpointing the exact invalid
// transition) — no soft-warning, no deferred recovery.

// STRICT = hard-fail on violation (debug/stress). false = log-only (production).
inline constexpr bool kInvariantStrict = true;

// One node of the live graph, with the owner the role REQUIRES (the checker
// compares it against the actual GW_OWNER — that single comparison validates
// the whole ownership chain, hence I-W9).
struct TopoNode {
  WindowRole role;
  HWND hwnd;
  HWND expected_owner;  // null for shell_root
  std::string id;       // surface id, for diagnostics
};

// Validate the structural topology invariants (I-W1/I-W2/I-W9 + single-identity
// + cloak placement + ownership chain) over [shell_root] + [nodes] after the
// named [transition]. Returns the violation count. In strict mode the first
// violation fails loudly (see above).
int AssertTopologyInvariants(HWND shell_root, const std::vector<TopoNode>& nodes,
                             const char* transition);

}  // namespace morphic::topo

#endif  // RUNNER_WINDOW_TOPOLOGY_H_

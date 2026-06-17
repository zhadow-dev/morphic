#include "window_topology.h"

#include <dwmapi.h>

#include <cstdint>
#include <cstdio>

#include "forensic_trace.h"

#ifndef DWMWA_CLOAK
#define DWMWA_CLOAK 13
#endif
#ifndef DWMWA_CLOAKED
#define DWMWA_CLOAKED 14
#endif
#ifndef DWMWA_EXCLUDED_FROM_PEEK
#define DWMWA_EXCLUDED_FROM_PEEK 12
#endif

namespace morphic::topo {

const char* RoleName(WindowRole role) {
  switch (role) {
    case WindowRole::kShellRoot:
      return "shell_root";
    case WindowRole::kSpatialHost:
      return "spatial_host";
    case WindowRole::kRenderSource:
      return "render_source";
    case WindowRole::kPoolOwner:
      return "pool_owner";
    case WindowRole::kToolSurface:
      return "tool_surface";
  }
  return "?";
}

bool IsCloaked(HWND hwnd) {
  if (hwnd == nullptr) return false;
  BOOL cloaked = FALSE;
  DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
  return cloaked != FALSE;
}

bool IsShellEligible(HWND hwnd) {
  if (hwnd == nullptr) return false;
  const LONG ex = static_cast<LONG>(GetWindowLongPtr(hwnd, GWL_EXSTYLE));
  const bool tool = (ex & WS_EX_TOOLWINDOW) != 0;
  const bool app = (ex & WS_EX_APPWINDOW) != 0;
  const bool visible = IsWindowVisible(hwnd) != 0;
  const HWND owner = GetWindow(hwnd, GW_OWNER);
  return visible && owner == nullptr && (app || !tool);
}

void SetCloaked(HWND hwnd, bool cloaked) {
  if (hwnd == nullptr) return;
  BOOL v = cloaked ? TRUE : FALSE;
  DwmSetWindowAttribute(hwnd, DWMWA_CLOAK, &v, sizeof(v));
}

void ApplyRolePolicy(HWND hwnd, WindowRole role, HWND owner) {
  if (hwnd == nullptr) return;
  // Ownership is the reliable shell-eligibility lever (owned => off-shell),
  // independent of style caching.
  SetWindowLongPtr(hwnd, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(owner));

  LONG ex = static_cast<LONG>(GetWindowLongPtr(hwnd, GWL_EXSTYLE));
  bool cloak = false;
  switch (role) {
    case WindowRole::kShellRoot:
      ex |= WS_EX_APPWINDOW;
      ex &= ~(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE);
      break;
    case WindowRole::kSpatialHost:
      ex |= WS_EX_TOOLWINDOW;
      ex &= ~WS_EX_APPWINDOW;
      break;
    case WindowRole::kRenderSource:
      ex |= (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE);
      ex &= ~WS_EX_APPWINDOW;
      cloak = true;
      break;
    case WindowRole::kToolSurface:
      ex |= WS_EX_TOOLWINDOW;
      ex &= ~WS_EX_APPWINDOW;
      break;
    case WindowRole::kPoolOwner:
      // Pure infrastructure: a tool window (never an Alt-Tab/taskbar citizen even
      // if it somehow became visible), never cloaked (it owns cloaked dormant
      // render_sources; a cloaked owner would be fine, but it is never shown so
      // cloak is moot — keep it false to avoid any inheritance surprise), never
      // activatable. It is unowned (a root). Peek-excluded below like all
      // non-shell roles.
      ex |= WS_EX_TOOLWINDOW;
      ex &= ~WS_EX_APPWINDOW;
      break;
  }
  SetWindowLongPtr(hwnd, GWL_EXSTYLE, ex);
  // Force the shell to re-read tool/app eligibility from the new styles.
  SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
                   SWP_FRAMECHANGED);
  SetCloaked(hwnd, cloak);

  // PREVIEW OWNERSHIP (I-W4) — only the shell_root participates in Aero-Peek (it
  // supplies an iconic bitmap of the scene; see EnsureShellRoot). Hosts and
  // render_sources are EXCLUDED FROM PEEK so the raw rectangular swapchains never
  // leak into the taskbar/Alt-Tab live-preview. (Cloak hides them from the
  // DESKTOP but not from peek — different system.)
  BOOL exclude_peek = (role != WindowRole::kShellRoot) ? TRUE : FALSE;
  DwmSetWindowAttribute(hwnd, DWMWA_EXCLUDED_FROM_PEEK, &exclude_peek,
                        sizeof(exclude_peek));

  LogWindowNode(role, hwnd, "policy-applied");
}

namespace {
// Custom SEH code for an invariant failure (so the crash handler's [CRASH] line
// + minidump clearly attribute the death to a topology violation).
constexpr DWORD kInvariantExceptionCode = 0xE0000001;

void FailInvariant(const char* invariant, const std::string& detail,
                   const char* transition) {
  forensic::Log("INVARIANT FAIL", std::string(invariant) + " | " + detail +
                                      " | after=" + transition);
  if (kInvariantStrict) {
    // Loud + immediate + captured. The runtime does NOT limp on with a broken
    // window graph; it dies at the exact transition with a minidump.
    if (IsDebuggerPresent()) __debugbreak();
    RaiseException(kInvariantExceptionCode, EXCEPTION_NONCONTINUABLE, 0, nullptr);
  }
}
}  // namespace

int AssertTopologyInvariants(HWND shell_root, const std::vector<TopoNode>& nodes,
                             const char* transition) {
  int violations = 0;
  auto fail = [&](const char* inv, const std::string& detail) {
    ++violations;
    FailInvariant(inv, detail, transition);
  };

  int shell_citizens = 0;

  // shell_root: unowned, APPWINDOW, NOT cloaked, the sole shell identity.
  if (shell_root != nullptr) {
    if (IsShellEligible(shell_root)) {
      ++shell_citizens;
    } else {
      fail("shell_root-eligible", "shell_root is not shell-eligible");
    }
    if (IsCloaked(shell_root)) fail("shell_root-uncloaked", "shell_root cloaked");
  }

  for (const auto& n : nodes) {
    const HWND owner = GetWindow(n.hwnd, GW_OWNER);
    // Ownership chain (validates I-W9: a render_source's required owner is a
    // spatial_host, a spatial_host's is the shell_root — never inverted).
    if (owner != n.expected_owner) {
      fail("ownership-chain",
           std::string(RoleName(n.role)) + " " + n.id + " owner=0x" +
               std::to_string(reinterpret_cast<uintptr_t>(owner)) +
               " expected=0x" +
               std::to_string(reinterpret_cast<uintptr_t>(n.expected_owner)));
    }
    if (n.role == WindowRole::kRenderSource) {
      if (IsShellEligible(n.hwnd)) {
        ++shell_citizens;
        fail("I-W1/W2", "render_source " + n.id + " is shell-eligible");
      }
      if (!IsCloaked(n.hwnd))
        fail("render-cloaked", "render_source " + n.id + " is NOT cloaked");
    } else if (n.role == WindowRole::kSpatialHost) {
      if (IsShellEligible(n.hwnd)) {
        ++shell_citizens;
        fail("host-not-shell", "spatial_host " + n.id + " is shell-eligible");
      }
      if (IsCloaked(n.hwnd))
        fail("I-W9", "spatial_host " + n.id + " is cloaked (inherited?)");
    }
  }

  // Exactly one shell identity per workspace.
  if (shell_root != nullptr && shell_citizens != 1) {
    fail("single-identity",
         "shell_citizens=" + std::to_string(shell_citizens) + " (want 1)");
  }
  return violations;
}

void LogWindowNode(WindowRole role, HWND hwnd, const std::string& tag) {
  if (hwnd == nullptr) {
    forensic::Log("WINTOPO", tag + " role=" + RoleName(role) + " hwnd=NULL");
    return;
  }
  const LONG style = static_cast<LONG>(GetWindowLongPtr(hwnd, GWL_STYLE));
  const LONG ex = static_cast<LONG>(GetWindowLongPtr(hwnd, GWL_EXSTYLE));
  const HWND owner = GetWindow(hwnd, GW_OWNER);
  const HWND parent = GetParent(hwnd);
  const bool visible = IsWindowVisible(hwnd) != 0;
  const bool cloaked = IsCloaked(hwnd);
  const bool eligible = IsShellEligible(hwnd);

  char buf[420];
  _snprintf_s(
      buf, sizeof(buf), _TRUNCATE,
      "%s role=%s hwnd=0x%p owner=0x%p parent=0x%p visible=%d cloaked=%d "
      "APPWINDOW=%d TOOLWINDOW=%d NOREDIR=%d NOACTIVATE=%d LAYERED=%d "
      "EXSTYLE=0x%08lX -> shell_eligible=%d",
      tag.c_str(), RoleName(role), static_cast<void*>(hwnd),
      static_cast<void*>(owner), static_cast<void*>(parent), visible ? 1 : 0,
      cloaked ? 1 : 0, (ex & WS_EX_APPWINDOW) ? 1 : 0,
      (ex & WS_EX_TOOLWINDOW) ? 1 : 0, (ex & WS_EX_NOREDIRECTIONBITMAP) ? 1 : 0,
      (ex & WS_EX_NOACTIVATE) ? 1 : 0, (ex & WS_EX_LAYERED) ? 1 : 0,
      static_cast<unsigned long>(ex), eligible ? 1 : 0);
  (void)style;
  forensic::Log("WINTOPO", buf);
}

}  // namespace morphic::topo

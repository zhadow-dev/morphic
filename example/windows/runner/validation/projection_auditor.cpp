#include "validation/projection_auditor.h"

#include <windows.h>
#include <dwmapi.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "forensic_trace.h"
#include "interaction_router.h"
#include "runtime_events.h"
#include "surface_model.h"
#include "surface_shell.h"
#include "validation/forensic_logger.h"
#include "validation/runtime_telemetry.h"

#ifndef DWMWA_CLOAKED
#define DWMWA_CLOAKED 14
#endif
#ifndef DWMWA_EXTENDED_FRAME_BOUNDS
#define DWMWA_EXTENDED_FRAME_BOUNDS 9
#endif

namespace {

// PHASE 8B.6.6 — shell inset constants (must match surface_shell.cpp).
constexpr int kTitleStrip = 30;
constexpr int kBorder = 6;

long MaxAxisDiff(const RECT& a, const RECT& b) {
  return std::max({std::abs(static_cast<long>(a.left   - b.left)),
                   std::abs(static_cast<long>(a.top    - b.top)),
                   std::abs(static_cast<long>(a.right  - b.right)),
                   std::abs(static_cast<long>(a.bottom - b.bottom))});
}

std::string RectStr(const RECT& r) {
  char buf[128];
  _snprintf_s(buf, sizeof(buf), _TRUNCATE,
              "(%ld,%ld %ldx%ld)",
              r.left, r.top,
              r.right - r.left, r.bottom - r.top);
  return buf;
}

}  // namespace

ProjectionAuditor::ProjectionAuditor(EventBus* bus, InteractionRouter* router,
                                     SurfaceModel* model,
                                     RuntimeTelemetry* telemetry,
                                     ForensicLogger* trace)
    : bus_(bus), router_(router), model_(model), telemetry_(telemetry),
      trace_(trace) {
  if (bus_) {
    bus_token_ = bus_->Subscribe(
        [this](RuntimeEvent event, SurfaceShell* surface) {
          OnEvent(event, surface);
        });
  }
  forensic::Log("AUDIT", "ProjectionAuditor installed (8B.6.6 coherence)");
}

ProjectionAuditor::~ProjectionAuditor() {
  if (bus_ && bus_token_ != 0) bus_->Unsubscribe(bus_token_);
}

void ProjectionAuditor::OnEvent(RuntimeEvent event, SurfaceShell* surface) {
  switch (event) {
    case RuntimeEvent::InteractionBegan:   Reset(surface);     break;
    case RuntimeEvent::InteractionUpdated: AuditTick(surface); break;
    case RuntimeEvent::InteractionEnded:   Summarize(surface); break;
    default: break;
  }
}

void ProjectionAuditor::Reset(SurfaceShell* surface) {
  ticks_audited_ = 0;
  peak_target_presented_px_ = 0;
  peak_presented_native_px_ = 0;
  warn_count_ = 0;
  child_divergence_count_ = 0;
  peak_child_divergence_px_ = 0;
  shell_divergence_count_ = 0;
  visibility_fail_count_ = 0;
  deep_audit_counter_ = 0;
  forensic::Log("AUDIT", "begin id=" +
                             (surface ? surface->id() : std::string("?")));
}

void ProjectionAuditor::AuditTick(SurfaceShell* surface) {
  if (!router_ || !surface || surface->GetHandle() == nullptr) return;
  ++ticks_audited_;
  if (telemetry_) telemetry_->audit_ticks.fetch_add(1);

  const RECT target = router_->target_bounds();
  const auto presented_opt = router_->presented_bounds();
  if (!presented_opt) return;
  const RECT presented = *presented_opt;

  RECT native{};
  if (!GetWindowRect(surface->GetHandle(), &native)) return;

  // PHASE 8C — CLAMP-AWARE projection coherence. The runtime's intended-AND-
  // actually-projected geometry is model.bounds (the POST-reachability-clamp rect
  // the model stored and sent to ApplyGeometry), NOT the session's pre-clamp
  // `presented`. The reachability clamp legitimately mutates geometry UPSTREAM of
  // model.bounds, so `presented != native` whenever the clamp engages — that is
  // intentional, not a projection bug. The true "did the projection land?" check
  // is model.bounds <-> native (clamp-agnostic). Comparing presented<->native
  // produced thousands of false-positive [AUDIT WARN]s during edge drags.
  const RECT projected = model_ ? model_->bounds(surface) : presented;
  const long d_tp = MaxAxisDiff(target, presented);   // smoothing lag (0 = off)
  const long d_pn = MaxAxisDiff(projected, native);   // real projection coherence
  const long d_clamp = MaxAxisDiff(presented, projected);  // intentional clamp delta

  if (d_tp > peak_target_presented_px_) peak_target_presented_px_ = d_tp;
  if (d_pn > peak_presented_native_px_) peak_presented_native_px_ = d_pn;

  if (telemetry_) {
    long prev_tp = telemetry_->peak_target_presented_lag_px.load();
    if (d_tp > prev_tp)
      telemetry_->peak_target_presented_lag_px.store(d_tp);
    long prev_pn = telemetry_->peak_presented_native_lag_px.load();
    if (d_pn > prev_pn)
      telemetry_->peak_presented_native_lag_px.store(d_pn);
  }

  if (trace_) {
    JsonLine line;
    line.Str("kind", "projection")
        .Str("surface", surface->id())
        .Rect("target", target)
        .Rect("presented", presented)
        .Rect("model_bounds", projected)
        .Rect("native", native)
        .Int("diff_tp_px", d_tp)
        .Int("diff_pn_px", d_pn)
        .Int("clamp_delta_px", d_clamp);  // intentional; NOT a warning
    trace_->LogEvent(line);
  }

  if (d_tp > kWarnThresholdPx || d_pn > kWarnThresholdPx) {
    ++warn_count_;
    if (telemetry_) telemetry_->audit_warnings.fetch_add(1);
    forensic::Log("AUDIT WARN",
                  std::string("id=") + surface->id() +
                      " diff(T,P)=" + std::to_string(d_tp) + "px" +
                      " diff(P,N)=" + std::to_string(d_pn) + "px");
  }

  // PHASE 8B.6.6 — native projection coherence audits (every tick).
  AuditChildProjection(surface);
  AuditShellCoherence(surface);
  AuditMonitorAffinity(surface);

  // Deep audit (expensive DWM calls) every kDeepAuditInterval ticks.
  ++deep_audit_counter_;
  if (deep_audit_counter_ >= kDeepAuditInterval) {
    deep_audit_counter_ = 0;
    AuditDeep(surface);
  }
}

// ---------------------------------------------------------------------------
// I-P1: child HWND rect must match projected client rect
// ---------------------------------------------------------------------------
void ProjectionAuditor::AuditChildProjection(SurfaceShell* surface) {
  HWND shell = surface->GetHandle();
  HWND child = surface->child_hwnd();
  if (!shell || !child) return;

  // Expected child rect (physical screen coords).
  RECT shell_rect{};
  GetWindowRect(shell, &shell_rect);
  POINT shell_client_origin{0, 0};
  ClientToScreen(shell, &shell_client_origin);

  RECT client_rc{};
  GetClientRect(shell, &client_rc);
  const int cw = client_rc.right - client_rc.left;
  const int ch = client_rc.bottom - client_rc.top;
  const int exp_w = std::max(0, cw - 2 * kBorder);
  const int exp_h = std::max(0, ch - kTitleStrip - kBorder);

  RECT expected{};
  expected.left   = shell_client_origin.x + kBorder;
  expected.top    = shell_client_origin.y + kTitleStrip;
  expected.right  = expected.left + exp_w;
  expected.bottom = expected.top + exp_h;

  // Actual child rect.
  RECT actual{};
  GetWindowRect(child, &actual);

  const long divergence = MaxAxisDiff(expected, actual);
  if (divergence > peak_child_divergence_px_)
    peak_child_divergence_px_ = divergence;

  if (divergence > kChildDivergenceThresholdPx) {
    ++child_divergence_count_;

    // Get DPI for coordinate-space attribution.
    const UINT dpi = GetDpiForWindow(shell);

    if (trace_) {
      JsonLine line;
      line.Str("kind", "child_projection_fail")
          .Str("surface", surface->id())
          .Rect("expected_physical", expected)
          .Rect("actual_physical", actual)
          .Int("divergence_px", divergence)
          .Int("dpi", dpi)
          .Int("shell_client_w", cw)
          .Int("shell_client_h", ch)
          .Bool("child_visible", IsWindowVisible(child) != 0)
          .Bool("parent_ok", GetParent(child) == shell);
      trace_->LogEvent(line);
    }

    // Throttle forensic log to avoid flooding.
    if (child_divergence_count_ <= 5 ||
        (child_divergence_count_ % 100) == 0) {
      forensic::Log("PROJECTION FAIL",
                    "I-P1 child divergence id=" + surface->id() +
                        " expected=" + RectStr(expected) +
                        " actual=" + RectStr(actual) +
                        " diff=" + std::to_string(divergence) + "px" +
                        " dpi=" + std::to_string(dpi) +
                        " count=" + std::to_string(child_divergence_count_));
    }
  }
}

// ---------------------------------------------------------------------------
// I-P2: shell client rect must match semantic projected rect
// ---------------------------------------------------------------------------
void ProjectionAuditor::AuditShellCoherence(SurfaceShell* surface) {
  if (!model_) return;

  HWND shell = surface->GetHandle();
  if (!shell) return;

  // Semantic bounds from the model (this is what the runtime intended).
  const RECT semantic = model_->bounds(surface);

  // Actual native shell rect.
  RECT native{};
  GetWindowRect(shell, &native);

  const long divergence = MaxAxisDiff(semantic, native);
  if (divergence > kChildDivergenceThresholdPx) {
    ++shell_divergence_count_;

    const UINT dpi = GetDpiForWindow(shell);

    if (trace_) {
      JsonLine line;
      line.Str("kind", "shell_coherence_fail")
          .Str("surface", surface->id())
          .Rect("semantic_logical", semantic)
          .Rect("native_physical", native)
          .Int("divergence_px", divergence)
          .Int("dpi", dpi);
      trace_->LogEvent(line);
    }

    if (shell_divergence_count_ <= 5 ||
        (shell_divergence_count_ % 100) == 0) {
      forensic::Log("PROJECTION FAIL",
                    "I-P2 shell coherence id=" + surface->id() +
                        " semantic=" + RectStr(semantic) +
                        " native=" + RectStr(native) +
                        " diff=" + std::to_string(divergence) + "px" +
                        " dpi=" + std::to_string(dpi));
    }
  }
}

// ---------------------------------------------------------------------------
// I-P3: shell must intersect at least one monitor work area
// Monitor affinity + DPI tracking
// ---------------------------------------------------------------------------
void ProjectionAuditor::AuditMonitorAffinity(SurfaceShell* surface) {
  HWND shell = surface->GetHandle();
  if (!shell) return;

  RECT shell_rect{};
  GetWindowRect(shell, &shell_rect);

  // MonitorFromWindow with MONITOR_DEFAULTTONULL returns NULL if the window
  // is entirely off all monitors.
  HMONITOR monitor = MonitorFromWindow(shell, MONITOR_DEFAULTTONULL);

  if (!monitor) {
    ++visibility_fail_count_;

    if (trace_) {
      JsonLine line;
      line.Str("kind", "visibility_fail")
          .Str("surface", surface->id())
          .Rect("shell_physical", shell_rect)
          .Str("reason", "no_monitor_intersection");
      trace_->LogEvent(line);
    }

    if (visibility_fail_count_ <= 5 ||
        (visibility_fail_count_ % 100) == 0) {
      forensic::Log("VISIBILITY FAIL",
                    "I-P3 off-screen id=" + surface->id() +
                        " rect=" + RectStr(shell_rect) +
                        " count=" + std::to_string(visibility_fail_count_));
    }
    return;
  }

  // Log monitor affinity with DPI + work area (traces only, not to forensic).
  MONITORINFO mi{};
  mi.cbSize = sizeof(mi);
  GetMonitorInfo(monitor, &mi);

  const UINT dpi = GetDpiForWindow(shell);

  if (trace_) {
    // Check partial visibility (shell extends beyond work area).
    RECT intersection{};
    const bool intersects = IntersectRect(&intersection, &shell_rect,
                                          &mi.rcWork) != 0;
    const long vis_w = intersection.right - intersection.left;
    const long vis_h = intersection.bottom - intersection.top;
    const long shell_w = shell_rect.right - shell_rect.left;
    const long shell_h = shell_rect.bottom - shell_rect.top;

    // Only log if partially off-screen or at monitor edge.
    if (!intersects || vis_w < shell_w || vis_h < shell_h) {
      JsonLine line;
      line.Str("kind", "monitor_affinity")
          .Str("surface", surface->id())
          .Rect("shell_physical", shell_rect)
          .Rect("work_area", mi.rcWork)
          .Rect("monitor_bounds", mi.rcMonitor)
          .Int("visible_w", vis_w)
          .Int("visible_h", vis_h)
          .Int("dpi", dpi)
          .Bool("partial", vis_w < shell_w || vis_h < shell_h);
      trace_->LogEvent(line);
    }
  }
}

// ---------------------------------------------------------------------------
// Deep audit (every 60 ticks): DWM cloaking, extended frame bounds,
// parent/child ownership, child visibility
// ---------------------------------------------------------------------------
void ProjectionAuditor::AuditDeep(SurfaceShell* surface) {
  HWND shell = surface->GetHandle();
  HWND child = surface->child_hwnd();
  if (!shell) return;

  // DWM cloaking state for shell.
  DWORD shell_cloaked = 0;
  DwmGetWindowAttribute(shell, DWMWA_CLOAKED,
                        &shell_cloaked, sizeof(shell_cloaked));

  // DWM extended frame bounds for shell.
  RECT shell_extended{};
  DwmGetWindowAttribute(shell, DWMWA_EXTENDED_FRAME_BOUNDS,
                        &shell_extended, sizeof(shell_extended));

  // Shell native rect for comparison.
  RECT shell_native{};
  GetWindowRect(shell, &shell_native);

  // Child audits (if child exists).
  DWORD child_cloaked = 0;
  RECT child_extended{};
  RECT child_native{};
  bool child_visible = false;
  bool parent_ok = false;

  if (child) {
    DwmGetWindowAttribute(child, DWMWA_CLOAKED,
                          &child_cloaked, sizeof(child_cloaked));
    DwmGetWindowAttribute(child, DWMWA_EXTENDED_FRAME_BOUNDS,
                          &child_extended, sizeof(child_extended));
    GetWindowRect(child, &child_native);
    child_visible = IsWindowVisible(child) != 0;
    parent_ok = GetParent(child) == shell;
  }

  if (trace_) {
    JsonLine line;
    line.Str("kind", "deep_audit")
        .Str("surface", surface->id())
        .Rect("shell_native", shell_native)
        .Rect("shell_dwm_extended", shell_extended)
        .Int("shell_cloaked", shell_cloaked)
        .Bool("shell_visible", IsWindowVisible(shell) != 0);

    if (child) {
      line.Rect("child_native", child_native)
          .Rect("child_dwm_extended", child_extended)
          .Int("child_cloaked", child_cloaked)
          .Bool("child_visible", child_visible)
          .Bool("parent_ok", parent_ok);
    }

    const UINT dpi = GetDpiForWindow(shell);
    line.Int("dpi", dpi);
    trace_->LogEvent(line);
  }

  // Integrity failures.
  if (shell_cloaked != 0) {
    forensic::Log("INTEGRITY FAIL",
                  "shell cloaked id=" + surface->id() +
                      " cloaked=" + std::to_string(shell_cloaked));
  }
  if (child && child_cloaked != 0) {
    forensic::Log("INTEGRITY FAIL",
                  "child cloaked id=" + surface->id() +
                      " cloaked=" + std::to_string(child_cloaked));
  }
  if (child && !parent_ok) {
    forensic::Log("INTEGRITY FAIL",
                  "parent/child ownership broken id=" + surface->id() +
                      " GetParent(child)=0x" +
                      std::to_string(reinterpret_cast<uintptr_t>(
                          GetParent(child))));
  }
  if (child && !child_visible) {
    forensic::Log("INTEGRITY FAIL",
                  "child invisible id=" + surface->id());
  }
}

void ProjectionAuditor::Summarize(SurfaceShell* surface) {
  char buf[512];
  _snprintf_s(buf, sizeof(buf), _TRUNCATE,
              "id=%s ticks_audited=%d peak_TP=%ldpx peak_PN=%ldpx warns=%d "
              "child_diverge=%d peak_child=%ldpx shell_diverge=%d "
              "visibility_fail=%d",
              surface ? surface->id().c_str() : "?", ticks_audited_,
              peak_target_presented_px_, peak_presented_native_px_,
              warn_count_,
              child_divergence_count_, peak_child_divergence_px_,
              shell_divergence_count_, visibility_fail_count_);
  forensic::Log("AUDIT SUMMARY", buf);
}

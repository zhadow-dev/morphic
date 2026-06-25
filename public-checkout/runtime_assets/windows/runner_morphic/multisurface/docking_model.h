#ifndef RUNNER_MULTISURFACE_DOCKING_MODEL_H_
#define RUNNER_MULTISURFACE_DOCKING_MODEL_H_

#include <windows.h>

#include <algorithm>
#include <cmath>

// PHASE 8D — Docking model.
//
// PURE semantics. No session/HWND coupling (it takes RECTs) — just the math +
// types that decide WHEN two detached surfaces should become one topology. This
// is the inverse of 8C extraction: membership ADD instead of REMOVE.
//
// Docking = topology mutation triggered by INTENTIONAL spatial convergence, NOT
// window-snapping visuals. There is deliberately NO velocity, physics, easing,
// magnetism, or preview here. distance + overlap + sustained is the whole model.
// Keep it that way (boring + deterministic = trustworthy, which matters because
// there are NO visual affordances yet).
//
// Determinism: pure function of its inputs with documented, clamped, weighted
// normalization — same drag replays to the same verdict.

namespace morphic {

// Tiny temporal state (mirrors ExtractionState's restraint). No tether/magnetic/
// preview states. None → Candidate (over threshold, accumulating) → Docked (dock
// applied). Candidate decays back to None after the pair stays out of range for
// sustained_ticks (hysteresis, anti-flicker / anti-spam).
enum class DockingState {
  None,
  Candidate,
  Docked,
};

inline const char* ToString(DockingState s) {
  switch (s) {
    case DockingState::None:      return "none";
    case DockingState::Candidate: return "candidate";
    case DockingState::Docked:    return "docked";
  }
  return "?";
}

// Tunable thresholds. Conservative defaults so accidental docking is rare; one
// struct, one place to tune after manual testing. The overlap requirement is what
// makes intent DELIBERATE — mere proximity (a fly-by) must not dock.
struct DockingThresholds {
  double dock_distance_px = 24.0;   // max edge gap to be a candidate (0 = overlapping)
  double overlap_ratio = 0.15;      // min intersection/min-area to count as intent
  int sustained_ticks = 6;          // ticks of sustained candidacy before docking
  double score_threshold = 0.75;    // score >= this AND hard gates → over_threshold
};

struct DockingAttribution {
  double distance_px = 0.0;      // edge gap (0 if overlapping)
  double overlap_ratio = 0.0;    // intersection / min(area_a, area_b)
  int sustained_ticks = 0;

  double n_dist = 0.0;           // normalized [0,1] (closer = higher)
  double n_overlap = 0.0;        // normalized [0,1]
  double n_sus = 0.0;

  double score = 0.0;            // [0,1]
};

struct DockingIntent {
  bool over_threshold = false;
  DockingAttribution attribution;
};

namespace detail {
inline double DClamp01(double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }

inline double RectArea(const RECT& r) {
  const double w = static_cast<double>(r.right - r.left);
  const double h = static_cast<double>(r.bottom - r.top);
  return (w > 0 && h > 0) ? w * h : 0.0;
}

// Intersection area of two rects (0 if disjoint).
inline double IntersectArea(const RECT& a, const RECT& b) {
  const long l = (std::max)(a.left, b.left);
  const long t = (std::max)(a.top, b.top);
  const long r = (std::min)(a.right, b.right);
  const long bo = (std::min)(a.bottom, b.bottom);
  if (r <= l || bo <= t) return 0.0;
  return static_cast<double>(r - l) * static_cast<double>(bo - t);
}

// Minimum edge gap between two rects. 0 when they overlap or touch; otherwise the
// straight-line gap along the separated axis/axes (axis gaps combined as a corner
// distance when separated on both axes).
inline double EdgeGap(const RECT& a, const RECT& b) {
  double dx = 0.0, dy = 0.0;
  if (b.left > a.right) dx = static_cast<double>(b.left - a.right);
  else if (a.left > b.right) dx = static_cast<double>(a.left - b.right);
  if (b.top > a.bottom) dy = static_cast<double>(b.top - a.bottom);
  else if (a.top > b.bottom) dy = static_cast<double>(a.top - b.bottom);
  if (dx == 0.0) return dy;
  if (dy == 0.0) return dx;
  return std::sqrt(dx * dx + dy * dy);  // corner gap
}
}  // namespace detail

// THE scorer. Pure. `a` is the dragged surface, `b` the candidate target. Weights:
// distance 0.4, overlap 0.4, sustained 0.2 (sum 1.0). HARD GATES: score alone
// can't dock — proximity AND overlap must both clear their thresholds, so a fast
// pass-over (high overlap, but only for a frame) or a near-but-not-overlapping
// hover never docks.
//
//   distance_px   = edge gap (0 if overlapping)
//   overlap_ratio = intersection / min(area_a, area_b)
//   n_dist    = clamp(1 - distance/dock_distance)        // closer = higher
//   n_overlap = clamp(overlap_ratio / overlap_threshold)
//   n_sus     = clamp(sustained / sustained_ticks)
//   score     = 0.4*n_dist + 0.4*n_overlap + 0.2*n_sus
//   over      = score >= score_threshold
//               AND distance_px < dock_distance_px
//               AND overlap_ratio > overlap_threshold     // deliberate intent
inline DockingIntent EvaluateDock(const RECT& a, const RECT& b, int sustained,
                                  const DockingThresholds& t) {
  using detail::DClamp01;
  using detail::RectArea;
  using detail::IntersectArea;
  using detail::EdgeGap;

  DockingIntent out;
  DockingAttribution& at = out.attribution;

  const double area_a = RectArea(a);
  const double area_b = RectArea(b);
  const double min_area = (std::min)(area_a, area_b);
  const double inter = IntersectArea(a, b);

  at.distance_px = EdgeGap(a, b);
  at.overlap_ratio = (min_area > 0.0) ? inter / min_area : 0.0;
  at.sustained_ticks = sustained;

  at.n_dist = DClamp01(1.0 - at.distance_px /
                                 (t.dock_distance_px > 0 ? t.dock_distance_px : 1.0));
  at.n_overlap = DClamp01(at.overlap_ratio /
                          (t.overlap_ratio > 0 ? t.overlap_ratio : 1.0));
  at.n_sus = DClamp01(static_cast<double>(sustained) /
                      (t.sustained_ticks > 0 ? t.sustained_ticks : 1));

  at.score = 0.4 * at.n_dist + 0.4 * at.n_overlap + 0.2 * at.n_sus;

  out.over_threshold = at.score >= t.score_threshold &&
                       at.distance_px < t.dock_distance_px &&
                       at.overlap_ratio > t.overlap_ratio;
  return out;
}

}  // namespace morphic

#endif  // RUNNER_MULTISURFACE_DOCKING_MODEL_H_

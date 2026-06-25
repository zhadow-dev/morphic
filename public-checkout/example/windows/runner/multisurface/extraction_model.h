#ifndef RUNNER_MULTISURFACE_EXTRACTION_MODEL_H_
#define RUNNER_MULTISURFACE_EXTRACTION_MODEL_H_

#include <algorithm>
#include <cmath>

// PHASE 8C — Extraction model.
//
// PURE semantics. No Win32, no session/HWND coupling — just the math + types that
// decide WHEN a grouped surface stops semantically belonging. This is the
// "extraction intent" calculator; the session feeds it per-member geometry +
// velocity each tick and acts on the verdict.
//
// 8C is a SEMANTIC decision (does this surface still belong?), NOT a physics
// event. There is deliberately NO elasticity / inertia / tether simulation /
// energy here — distance + velocity + direction + sustained divergence is the
// whole model. Keep it that way.
//
// Determinism: the scorer is a pure function of its inputs with documented,
// clamped, weighted normalization — so the same drag replays to the same verdict.

namespace morphic {

// Why a fracture happened. (No ExplicitDetach yet — there is no detach
// gesture/command in 8C. ExternalGraphMutation is reserved for a forced graph
// edit driving a fracture; not emitted by the velocity/distance path.)
enum class FractureReason {
  Distance,
  Velocity,
  ExternalGraphMutation,
};

// Per-member temporal extraction state. (No "Fracturing" — that transition lives
// exactly one epoch tick boundary and IS the epoch's apply step; it doesn't need
// durable identity.) None → Candidate (over threshold, accumulating) →
// Extracted (fracture applied). Candidate decays back to None only after the
// member stays under release_dist for sustained_ticks (hysteresis, anti-flicker).
enum class ExtractionState {
  None,
  Candidate,
  Extracted,
};

inline bool IsLegalTransition(ExtractionState from, ExtractionState to) {
  if (from == to) return true;
  switch (from) {
    case ExtractionState::None:      return to == ExtractionState::Candidate;
    case ExtractionState::Candidate: return to == ExtractionState::None ||
                                            to == ExtractionState::Extracted;
    case ExtractionState::Extracted: return false;  // no backward this phase
  }
  return false;
}

inline const char* ToString(FractureReason r) {
  switch (r) {
    case FractureReason::Distance:              return "distance";
    case FractureReason::Velocity:              return "velocity";
    case FractureReason::ExternalGraphMutation: return "external_graph_mutation";
  }
  return "?";
}

inline const char* ToString(ExtractionState s) {
  switch (s) {
    case ExtractionState::None:      return "none";
    case ExtractionState::Candidate: return "candidate";
    case ExtractionState::Extracted: return "extracted";
  }
  return "?";
}

// Tunable thresholds. Conservative defaults so accidental extraction is rare;
// one struct, one place to tune after manual testing.
struct ExtractionThresholds {
  double extract_dist_px = 120.0;      // centroid distance that contributes full weight
  double release_dist_px = 80.0;       // hysteresis floor for Candidate decay
  double velocity_impulse_px_s = 900.0;// relative speed that contributes full weight
  int sustained_ticks = 6;             // ticks of divergence for full sustained weight
  double directional_escape_dot = 0.5; // (reserved gate; scorer uses the raw dot)
  double score_threshold = 0.75;       // score >= this → over_threshold
};

// Raw signals + normalized components + the dominant reason + final score. Rich
// enough to understand any decision from a single log line, without recomputation.
struct ExtractionAttribution {
  double dist_px = 0.0;
  double velocity_px_s = 0.0;      // relative speed (member vs group)
  double directional_dot = 0.0;    // alignment of relative motion with escape dir
  int sustained_ticks = 0;

  double n_dist = 0.0;             // normalized [0,1]
  double n_vel = 0.0;
  double n_dir = 0.0;
  double n_sus = 0.0;

  FractureReason dominant = FractureReason::Distance;
  double score = 0.0;              // [0,1]
};

struct ExtractionIntent {
  bool over_threshold = false;
  ExtractionAttribution attribution;
};

namespace detail {
struct V2 { double x, y; };
inline double Len(V2 v) { return std::sqrt(v.x * v.x + v.y * v.y); }
inline double Clamp01(double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }
}  // namespace detail

// THE scorer. Pure. Centers/velocities are caller-computed (centroid is a
// heuristic aid, not topology authority). Weights: distance 0.4, velocity 0.3,
// direction 0.2, sustained 0.1 (sum 1.0) — distance-dominant, the rest refine.
//
//   escape_dir   = normalize(member_center - group_centroid)
//   rel_vel      = member_velocity - group_velocity
//   directional  = dot(normalize(rel_vel), escape_dir)         // how much it's escaping
//   n_dist = clamp(dist / extract_dist)
//   n_vel  = clamp(|rel_vel| / velocity_impulse) * max(0, directional)  // DIRECTION-GATED:
//            speed only counts toward extraction when moving AWAY from the group, so a
//            surface being pulled BACK (directional < 0) doesn't extract on raw speed.
//   n_dir  = clamp(max(0, directional))
//   n_sus  = clamp(sustained / sustained_ticks)
//   score  = 0.4*n_dist + 0.3*n_vel + 0.2*n_dir + 0.1*n_sus
inline ExtractionIntent EvaluateExtraction(
    double member_cx, double member_cy, double member_vx, double member_vy,
    double centroid_cx, double centroid_cy, double group_vx, double group_vy,
    int sustained, const ExtractionThresholds& t) {
  using detail::V2;
  using detail::Len;
  using detail::Clamp01;

  ExtractionIntent out;
  ExtractionAttribution& a = out.attribution;

  const V2 escape{member_cx - centroid_cx, member_cy - centroid_cy};
  const double dist = Len(escape);
  const V2 rel_vel{member_vx - group_vx, member_vy - group_vy};
  const double rel_speed = Len(rel_vel);

  double directional = 0.0;
  if (dist > 1e-6 && rel_speed > 1e-6) {
    const V2 esc_n{escape.x / dist, escape.y / dist};
    const V2 vel_n{rel_vel.x / rel_speed, rel_vel.y / rel_speed};
    directional = esc_n.x * vel_n.x + esc_n.y * vel_n.y;  // [-1,1]
  }

  a.dist_px = dist;
  a.velocity_px_s = rel_speed;
  a.directional_dot = directional;
  a.sustained_ticks = sustained;

  a.n_dist = Clamp01(dist / (t.extract_dist_px > 0 ? t.extract_dist_px : 1.0));
  a.n_dir = Clamp01((std::max)(0.0, directional));
  // Direction-gated velocity: raw speed only counts when moving AWAY (n_dir>0),
  // so a far surface being pulled BACK toward the group cannot extract on speed.
  a.n_vel = Clamp01(rel_speed /
                    (t.velocity_impulse_px_s > 0 ? t.velocity_impulse_px_s : 1.0)) *
            a.n_dir;
  a.n_sus = Clamp01(static_cast<double>(sustained) /
                    (t.sustained_ticks > 0 ? t.sustained_ticks : 1));

  a.score = 0.4 * a.n_dist + 0.3 * a.n_vel + 0.2 * a.n_dir + 0.1 * a.n_sus;

  // Dominant = the largest WEIGHTED contribution (so reporting matches the score).
  const double c_dist = 0.4 * a.n_dist;
  const double c_vel = 0.3 * a.n_vel;
  a.dominant = (c_vel > c_dist) ? FractureReason::Velocity : FractureReason::Distance;

  out.over_threshold = a.score >= t.score_threshold;
  return out;
}

}  // namespace morphic

#endif  // RUNNER_MULTISURFACE_EXTRACTION_MODEL_H_

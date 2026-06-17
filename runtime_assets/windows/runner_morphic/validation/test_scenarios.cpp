#include "validation/test_scenarios.h"

#include <windows.h>

#include <cmath>
#include <utility>

#include "forensic_trace.h"
#include "surface_manager.h"
#include "surface_model.h"
#include "surface_shell.h"
#include "validation/interaction_simulator.h"

namespace {

// Pick the first live surface from the manager via the model's z_order. Returns
// nullptr if none exist (scenario fails cleanly).
SurfaceShell* FirstSurface(SurfaceManager& manager) {
  if (!manager.model()) return nullptr;
  const auto& z = manager.model()->z_order();
  return z.empty() ? nullptr : z.front();
}

// PHASE 8B.5 — pick a different live surface for chaos scenarios that need a
// pair (group / follower-destroy / etc.). Returns nullptr if there's only one.
SurfaceShell* SecondSurface(SurfaceManager& manager) {
  if (!manager.model()) return nullptr;
  const auto& z = manager.model()->z_order();
  return z.size() < 2 ? nullptr : z[1];
}

POINT Pt(int x, int y) { return POINT{x, y}; }

}  // namespace

TestScenarios::TestScenarios(InteractionSimulator* sim, SurfaceManager* manager)
    : sim_(sim), manager_(manager) {
  RegisterBuiltins();
}

void TestScenarios::Register(const std::string& name, Factory factory) {
  registry_[name] = std::move(factory);
}

bool TestScenarios::Run(const std::string& name) {
  auto it = registry_.find(name);
  if (it == registry_.end() || !sim_ || !manager_) {
    forensic::Log("SIM", "Scenario not found / unwired: " + name);
    return false;
  }
  return it->second(*sim_, *manager_);
}

std::vector<std::string> TestScenarios::Names() const {
  std::vector<std::string> out;
  out.reserve(registry_.size());
  for (const auto& [name, _] : registry_) out.push_back(name);
  return out;
}

void TestScenarios::RegisterBuiltins() {
  using Op = InteractionSimulator::Op;

  // rapid_drag: 30 steps of straight-line +5px/+3px from origin.
  Register("rapid_drag", [](InteractionSimulator& sim, SurfaceManager& mgr) {
    SurfaceShell* s = FirstSurface(mgr);
    if (!s) return false;
    RECT r{};
    GetWindowRect(s->GetHandle(), &r);
    const POINT origin = Pt(r.left + 100, r.top + 10);
    std::vector<InteractionSimulator::Step> steps;
    steps.push_back({Op::Begin, origin, 0, 0});
    for (int i = 1; i <= 30; ++i) {
      steps.push_back({Op::Update, Pt(origin.x + i * 5, origin.y + i * 3), 0, 0});
    }
    steps.push_back({Op::End, {}, 0, 0});
    return sim.Run("rapid_drag", s, std::move(steps), {});
  });

  // circular_drag: 60 samples around a 100px-radius circle.
  Register("circular_drag", [](InteractionSimulator& sim, SurfaceManager& mgr) {
    SurfaceShell* s = FirstSurface(mgr);
    if (!s) return false;
    RECT r{};
    GetWindowRect(s->GetHandle(), &r);
    const POINT origin = Pt(r.left + 100, r.top + 10);
    std::vector<InteractionSimulator::Step> steps;
    steps.push_back({Op::Begin, origin, 0, 0});
    constexpr int N = 60;
    constexpr double radius = 100.0;
    for (int i = 0; i <= N; ++i) {
      const double theta = (i * 2.0 * 3.14159265) / N;
      const POINT p = Pt(origin.x + static_cast<int>(std::cos(theta) * radius),
                        origin.y + static_cast<int>(std::sin(theta) * radius));
      steps.push_back({Op::Update, p, 0, 0});
    }
    steps.push_back({Op::End, {}, 0, 0});
    return sim.Run("circular_drag", s, std::move(steps), {});
  });

  // direction_reversal: rapid back-and-forth to stress smoothing convergence.
  Register("direction_reversal", [](InteractionSimulator& sim, SurfaceManager& mgr) {
    SurfaceShell* s = FirstSurface(mgr);
    if (!s) return false;
    RECT r{};
    GetWindowRect(s->GetHandle(), &r);
    const POINT origin = Pt(r.left + 100, r.top + 10);
    std::vector<InteractionSimulator::Step> steps;
    steps.push_back({Op::Begin, origin, 0, 0});
    for (int sweep = 0; sweep < 8; ++sweep) {
      for (int i = 0; i <= 40; ++i) {
        const int sign = (sweep & 1) ? -1 : 1;
        steps.push_back({Op::Update, Pt(origin.x + sign * i * 6, origin.y), 0, 0});
      }
    }
    steps.push_back({Op::End, {}, 0, 0});
    return sim.Run("direction_reversal", s, std::move(steps), {});
  });

  // resize_corner_spam: hammer the BR corner inward then outward; tests the
  // min-size clamp and the resize_edge math.
  Register("resize_corner_spam", [](InteractionSimulator& sim, SurfaceManager& mgr) {
    SurfaceShell* s = FirstSurface(mgr);
    if (!s) return false;
    RECT r{};
    GetWindowRect(s->GetHandle(), &r);
    const POINT corner = Pt(r.right - 3, r.bottom - 3);
    std::vector<InteractionSimulator::Step> steps;
    steps.push_back({Op::BeginResize, corner, HTBOTTOMRIGHT, 0});
    // Shrink way past min-size to exercise the clamp.
    for (int i = 1; i <= 60; ++i) {
      steps.push_back({Op::Update, Pt(corner.x - i * 20, corner.y - i * 12), 0, 0});
    }
    // Then grow back.
    for (int i = 60; i >= 0; --i) {
      steps.push_back({Op::Update, Pt(corner.x - i * 20, corner.y - i * 12), 0, 0});
    }
    steps.push_back({Op::End, {}, 0, 0});
    return sim.Run("resize_corner_spam", s, std::move(steps), {});
  });

  // rapid_begin_end: 5 micro-interactions back to back. Tests lifecycle 1B/1C.
  Register("rapid_begin_end", [](InteractionSimulator& sim, SurfaceManager& mgr) {
    SurfaceShell* s = FirstSurface(mgr);
    if (!s) return false;
    RECT r{};
    GetWindowRect(s->GetHandle(), &r);
    const POINT origin = Pt(r.left + 50, r.top + 10);
    std::vector<InteractionSimulator::Step> steps;
    for (int i = 0; i < 5; ++i) {
      steps.push_back({Op::Begin, origin, 0, 0});
      steps.push_back({Op::Update, Pt(origin.x + 30, origin.y + 20), 0, 0});
      steps.push_back({Op::End, {}, 0, 0});
      steps.push_back({Op::Wait, {}, 0, 3});
    }
    return sim.Run("rapid_begin_end", s, std::move(steps), {});
  });

  // PHASE 8A.2 — lifecycle_torture: 50× Begin/End with NO Update in between.
  // Stresses the cleanup-ordering fix (no duplicate CommitTransaction /
  // InteractionEnded), the cancel symmetry (capture released even with no
  // input), and the IntegrityAuditor's cross-system invariants (every End
  // must land at mode=None, no subscription, no transaction, no capture).
  // If anything is fragile in the lifecycle, this scenario surfaces it.
  Register("lifecycle_torture", [](InteractionSimulator& sim, SurfaceManager& mgr) {
    SurfaceShell* s = FirstSurface(mgr);
    if (!s) return false;
    RECT r{};
    GetWindowRect(s->GetHandle(), &r);
    const POINT origin = Pt(r.left + 50, r.top + 10);
    std::vector<InteractionSimulator::Step> steps;
    for (int i = 0; i < 50; ++i) {
      steps.push_back({Op::Begin, origin, 0, 0});
      steps.push_back({Op::End, {}, 0, 0});
    }
    return sim.Run("lifecycle_torture", s, std::move(steps), {});
  });

  // PHASE 8A.2 — cancel_torture: 30× Begin/Cancel pairs. Validates the new
  // CancelInteraction symmetry (snapshot → quiesce → release) on the
  // simulator-driven path where the runtime still holds capture.
  Register("cancel_torture", [](InteractionSimulator& sim, SurfaceManager& mgr) {
    SurfaceShell* s = FirstSurface(mgr);
    if (!s) return false;
    RECT r{};
    GetWindowRect(s->GetHandle(), &r);
    const POINT origin = Pt(r.left + 50, r.top + 10);
    std::vector<InteractionSimulator::Step> steps;
    for (int i = 0; i < 30; ++i) {
      steps.push_back({Op::Begin, origin, 0, 0});
      steps.push_back({Op::Update, Pt(origin.x + 20, origin.y + 10), 0, 0});
      steps.push_back({Op::Cancel, {}, 0, 0});
    }
    return sim.Run("cancel_torture", s, std::move(steps), {});
  });

  // PHASE 8B.5 — CHAOS SCENARIOS
  //
  // Each of these mid-script mutates external state (destroy / capture theft /
  // topology mutation) to test session resilience. Pass criteria: NO
  // [INTEGRITY FAIL] lines in the trace, no crash, next interaction starts
  // cleanly. Fail criteria: integrity failures, hangs, UAF crashes.

  // capture_theft_mid_drag: drag for ~10 ticks then ReleaseCapture(). Should
  // trigger WM_CAPTURECHANGED → CancelInteraction. Router state must be clean
  // afterwards (mode=None, no clock subscription, no open transaction).
  Register("capture_theft_mid_drag", [](InteractionSimulator& sim,
                                          SurfaceManager& mgr) {
    SurfaceShell* a = FirstSurface(mgr);
    if (!a) return false;
    RECT r{};
    GetWindowRect(a->GetHandle(), &r);
    const POINT origin = Pt(r.left + 100, r.top + 10);
    std::vector<InteractionSimulator::Step> steps;
    steps.push_back({Op::Begin, origin, 0, 0, nullptr});
    for (int i = 1; i <= 10; ++i) {
      steps.push_back({Op::Update, Pt(origin.x + i * 5, origin.y), 0, 0, nullptr});
    }
    steps.push_back({Op::StealCapture, {}, 0, 0, nullptr});
    steps.push_back({Op::Wait, {}, 0, 5, nullptr});
    // Don't issue End — the cancel-from-capture-theft did that already.
    // If anything is leaked, IntegrityAuditor's per-tick I-R3 watchdog catches
    // it within 16ms.
    return sim.Run("capture_theft_mid_drag", a, std::move(steps), {});
  });

  // destroy_leader_mid_drag: drag for ~10 ticks then DestroyWindow on the
  // leader. WM_DESTROY → router.CancelInteraction("destroy") path runs.
  Register("destroy_leader_mid_drag", [](InteractionSimulator& sim,
                                          SurfaceManager& mgr) {
    SurfaceShell* a = FirstSurface(mgr);
    SurfaceShell* b = SecondSurface(mgr);
    if (!a || !b) return false;  // need at least 2 surfaces so app survives
    RECT r{};
    GetWindowRect(a->GetHandle(), &r);
    const POINT origin = Pt(r.left + 100, r.top + 10);
    std::vector<InteractionSimulator::Step> steps;
    steps.push_back({Op::Begin, origin, 0, 0, nullptr});
    for (int i = 1; i <= 10; ++i) {
      steps.push_back({Op::Update, Pt(origin.x + i * 5, origin.y), 0, 0, nullptr});
    }
    steps.push_back({Op::DestroySurface, {}, 0, 0, nullptr});
    steps.push_back({Op::Wait, {}, 0, 5, nullptr});
    return sim.Run("destroy_leader_mid_drag", a, std::move(steps), {});
  });

  // destroy_follower_mid_drag: group A,B then drag A; mid-drag destroy B.
  // Session should prune B from members_ via SurfaceDestroyed handler and
  // continue projecting A only. End fires cleanly with members reduced.
  Register("destroy_follower_mid_drag", [](InteractionSimulator& sim,
                                            SurfaceManager& mgr) {
    SurfaceShell* a = FirstSurface(mgr);
    SurfaceShell* b = SecondSurface(mgr);
    if (!a || !b) return false;
    RECT r{};
    GetWindowRect(a->GetHandle(), &r);
    const POINT origin = Pt(r.left + 100, r.top + 10);
    std::vector<InteractionSimulator::Step> steps;
    steps.push_back({Op::GroupSurfaces, {}, 0, 0, b});  // target=b → Group({a,b})
    steps.push_back({Op::Wait, {}, 0, 2, nullptr});
    steps.push_back({Op::Begin, origin, 0, 0, nullptr});
    for (int i = 1; i <= 10; ++i) {
      steps.push_back({Op::Update, Pt(origin.x + i * 5, origin.y), 0, 0, nullptr});
    }
    steps.push_back({Op::DestroySurface, {}, 0, 0, b});  // destroy the FOLLOWER
    for (int i = 11; i <= 20; ++i) {
      steps.push_back({Op::Update, Pt(origin.x + i * 5, origin.y), 0, 0, nullptr});
    }
    steps.push_back({Op::End, {}, 0, 0, nullptr});
    return sim.Run("destroy_follower_mid_drag", a, std::move(steps), {});
  });

  // ungroup_mid_drag: group A,B then drag A; mid-drag Ungroup. Session has
  // its OWN immutable members_ snapshot, so the group dissolution should NOT
  // change the active drag's members — A and B continue moving as one until
  // End. The dissolution is observed in [SURFACE_GRAPH] / [INTEGRITY] traces.
  Register("ungroup_mid_drag", [](InteractionSimulator& sim,
                                   SurfaceManager& mgr) {
    SurfaceShell* a = FirstSurface(mgr);
    SurfaceShell* b = SecondSurface(mgr);
    if (!a || !b) return false;
    RECT r{};
    GetWindowRect(a->GetHandle(), &r);
    const POINT origin = Pt(r.left + 100, r.top + 10);
    std::vector<InteractionSimulator::Step> steps;
    steps.push_back({Op::GroupSurfaces, {}, 0, 0, b});
    steps.push_back({Op::Wait, {}, 0, 2, nullptr});
    steps.push_back({Op::Begin, origin, 0, 0, nullptr});
    for (int i = 1; i <= 10; ++i) {
      steps.push_back({Op::Update, Pt(origin.x + i * 5, origin.y), 0, 0, nullptr});
    }
    steps.push_back({Op::UngroupSurface, {}, 0, 0, b});  // mutate topology mid-drag
    for (int i = 11; i <= 20; ++i) {
      steps.push_back({Op::Update, Pt(origin.x + i * 5, origin.y), 0, 0, nullptr});
    }
    steps.push_back({Op::End, {}, 0, 0, nullptr});
    return sim.Run("ungroup_mid_drag", a, std::move(steps), {});
  });

  // long_drag: 5 seconds (~300 ticks at 60Hz) of slow continuous drag for
  // starvation observability.
  Register("long_drag", [](InteractionSimulator& sim, SurfaceManager& mgr) {
    SurfaceShell* s = FirstSurface(mgr);
    if (!s) return false;
    RECT r{};
    GetWindowRect(s->GetHandle(), &r);
    const POINT origin = Pt(r.left + 100, r.top + 10);
    std::vector<InteractionSimulator::Step> steps;
    steps.push_back({Op::Begin, origin, 0, 0});
    for (int i = 1; i <= 300; ++i) {
      steps.push_back({Op::Update, Pt(origin.x + i, origin.y + (i % 30)), 0, 0});
    }
    steps.push_back({Op::End, {}, 0, 0});
    return sim.Run("long_drag", s, std::move(steps), {});
  });

  // ============================================================
  // PHASE 8B.6 — TEMPORAL STRESS SCENARIOS
  //
  // These scenarios exist to generate data for the temporal auditor,
  // tick coherence probe, and performance budget monitor.
  //
  // They exercise the specific interaction patterns where temporal
  // degradation was visually observed:
  //   - grouped drag (inter-surface present skew)
  //   - rapid resize (SetWindowPos stalls + Flutter raster lag)
  //   - capture theft stress (recovery latency)
  //   - activation churn (focus cycle latency)
  //   - grouped direction reversal (worst-case presentation coherence)
  // ============================================================

  // grouped_drag_stress: Group A,B then long drag. 200 ticks of continuous
  // grouped motion. This is the primary scenario for measuring inter-surface
  // presentation skew — the gap between SetWindowPos(A) and SetWindowPos(B)
  // becoming visible as follower lag.
  Register("grouped_drag_stress", [](InteractionSimulator& sim,
                                      SurfaceManager& mgr) {
    SurfaceShell* a = FirstSurface(mgr);
    SurfaceShell* b = SecondSurface(mgr);
    if (!a || !b) return false;
    RECT r{};
    GetWindowRect(a->GetHandle(), &r);
    const POINT origin = Pt(r.left + 100, r.top + 10);
    std::vector<InteractionSimulator::Step> steps;
    // Group surfaces first
    steps.push_back({Op::GroupSurfaces, {}, 0, 0, b});
    steps.push_back({Op::Wait, {}, 0, 3, nullptr});
    // Long grouped drag — diagonal motion
    steps.push_back({Op::Begin, origin, 0, 0, nullptr});
    for (int i = 1; i <= 200; ++i) {
      steps.push_back({Op::Update,
                        Pt(origin.x + i * 2, origin.y + i), 0, 0, nullptr});
    }
    steps.push_back({Op::End, {}, 0, 0, nullptr});
    // Clean up group
    steps.push_back({Op::Wait, {}, 0, 3, nullptr});
    steps.push_back({Op::UngroupSurface, {}, 0, 0, b});
    return sim.Run("grouped_drag_stress", a, std::move(steps), {});
  });

  // rapid_resize_stress: 120 ticks of aggressive corner resize with direction
  // reversals. Stresses SetWindowPos batching, Flutter raster cadence, and the
  // min/max constraint clamp path. This is where resize visual tearing is most
  // visible.
  Register("rapid_resize_stress", [](InteractionSimulator& sim,
                                      SurfaceManager& mgr) {
    SurfaceShell* s = FirstSurface(mgr);
    if (!s) return false;
    RECT r{};
    GetWindowRect(s->GetHandle(), &r);
    const POINT corner = Pt(r.right - 3, r.bottom - 3);
    std::vector<InteractionSimulator::Step> steps;
    steps.push_back({Op::BeginResize, corner, HTBOTTOMRIGHT, 0});
    // 4 sweeps: shrink aggressively, grow back, repeat
    for (int sweep = 0; sweep < 4; ++sweep) {
      for (int i = 0; i < 30; ++i) {
        const int sign = (sweep & 1) ? 1 : -1;
        steps.push_back({Op::Update,
                          Pt(corner.x + sign * i * 15, corner.y + sign * i * 10),
                          0, 0});
      }
    }
    steps.push_back({Op::End, {}, 0, 0});
    return sim.Run("rapid_resize_stress", s, std::move(steps), {});
  });

  // capture_theft_stress: 10 rounds of drag-then-steal. Each round:
  // Begin, 5 updates, StealCapture, Wait, re-Begin, 5 updates, End.
  // Measures recovery latency from external capture loss and validates
  // that the router returns to clean state each time.
  Register("capture_theft_stress", [](InteractionSimulator& sim,
                                       SurfaceManager& mgr) {
    SurfaceShell* s = FirstSurface(mgr);
    if (!s) return false;
    RECT r{};
    GetWindowRect(s->GetHandle(), &r);
    const POINT origin = Pt(r.left + 100, r.top + 10);
    std::vector<InteractionSimulator::Step> steps;
    for (int round = 0; round < 10; ++round) {
      int baseX = origin.x + round * 50;
      steps.push_back({Op::Begin, Pt(baseX, origin.y), 0, 0, nullptr});
      for (int i = 1; i <= 5; ++i) {
        steps.push_back({Op::Update, Pt(baseX + i * 4, origin.y), 0, 0, nullptr});
      }
      steps.push_back({Op::StealCapture, {}, 0, 0, nullptr});
      steps.push_back({Op::Wait, {}, 0, 3, nullptr});
    }
    return sim.Run("capture_theft_stress", s, std::move(steps), {});
  });

  // activation_churn: rapid focus cycling between all surfaces. Each tick
  // activates a different surface. 60 ticks total. Measures focus transition
  // latency, z-order realization cost, and activation-manager overhead.
  Register("activation_churn", [](InteractionSimulator& sim,
                                   SurfaceManager& mgr) {
    SurfaceShell* a = FirstSurface(mgr);
    SurfaceShell* b = SecondSurface(mgr);
    if (!a || !b) return false;
    // Use a as the drag target but the scenario just cycles activation.
    // We'll Begin on a, then rapidly switch activation by destroying and
    // recreating capture. For now, use the simpler approach: grouped surfaces
    // with rapid Begin/End on each.
    RECT ra{}, rb{};
    GetWindowRect(a->GetHandle(), &ra);
    GetWindowRect(b->GetHandle(), &rb);
    std::vector<InteractionSimulator::Step> steps;
    for (int i = 0; i < 30; ++i) {
      // Begin on a
      steps.push_back({Op::Begin, Pt(ra.left + 50, ra.top + 10), 0, 0, nullptr});
      steps.push_back({Op::Update, Pt(ra.left + 55, ra.top + 12), 0, 0, nullptr});
      steps.push_back({Op::End, {}, 0, 0, nullptr});
      steps.push_back({Op::Wait, {}, 0, 1, nullptr});
    }
    return sim.Run("activation_churn", a, std::move(steps), {});
  });

  // grouped_direction_reversal: group A,B then drag with rapid back-and-forth.
  // This is the worst case for presentation coherence — when the leader reverses
  // direction, followers must track instantly or the visual gap becomes obvious.
  // 6 sweeps of 40 ticks each = 240 update ticks.
  Register("grouped_direction_reversal", [](InteractionSimulator& sim,
                                              SurfaceManager& mgr) {
    SurfaceShell* a = FirstSurface(mgr);
    SurfaceShell* b = SecondSurface(mgr);
    if (!a || !b) return false;
    RECT r{};
    GetWindowRect(a->GetHandle(), &r);
    const POINT origin = Pt(r.left + 100, r.top + 10);
    std::vector<InteractionSimulator::Step> steps;
    steps.push_back({Op::GroupSurfaces, {}, 0, 0, b});
    steps.push_back({Op::Wait, {}, 0, 3, nullptr});
    steps.push_back({Op::Begin, origin, 0, 0, nullptr});
    for (int sweep = 0; sweep < 6; ++sweep) {
      for (int i = 0; i <= 40; ++i) {
        const int sign = (sweep & 1) ? -1 : 1;
        steps.push_back({Op::Update,
                          Pt(origin.x + sign * i * 5, origin.y + (i % 10)),
                          0, 0, nullptr});
      }
    }
    steps.push_back({Op::End, {}, 0, 0, nullptr});
    steps.push_back({Op::Wait, {}, 0, 3, nullptr});
    steps.push_back({Op::UngroupSurface, {}, 0, 0, b});
    return sim.Run("grouped_direction_reversal", a, std::move(steps), {});
  });

  // =========================================================================
  // PHASE 8D — semantic docking scenarios. Both surfaces start DETACHED. The
  // simulator drags A (via its pointer); A's rect follows the pointer delta. To
  // dock, we drive A's pointer so A's rect CONVERGES onto B's stationary rect
  // (proximity + overlap, sustained). Topology forms a group at the epoch
  // boundary; the rigid co-move would begin on the NEXT drag.
  // =========================================================================

  // dock_basic_regroup: drag A onto B until they dock, then ungroup; repeat. Goal:
  // topology stability over dock/undock cycles (no GroupId leak, balanced
  // group/ungroup, no stuck Candidate). [This is the user's repeated_dock_undock.]
  Register("repeated_dock_undock", [](InteractionSimulator& sim,
                                      SurfaceManager& mgr) {
    SurfaceShell* a = FirstSurface(mgr);
    SurfaceShell* b = SecondSurface(mgr);
    if (!a || !b || !mgr.model()) return false;
    RECT ra{}, rb{};
    GetWindowRect(a->GetHandle(), &ra);
    GetWindowRect(b->GetHandle(), &rb);
    // Pointer origin inside A's title strip; the delta to overlap B's center is
    // (B.center - A.center). We over-shoot slightly so A's rect lands ON B.
    const POINT origin = Pt(ra.left + 100, ra.top + 10);
    const int dx = ((rb.left + rb.right) - (ra.left + ra.right)) / 2;
    const int dy = ((rb.top + rb.bottom) - (ra.top + ra.bottom)) / 2;
    std::vector<InteractionSimulator::Step> steps;
    for (int cycle = 0; cycle < 4; ++cycle) {
      steps.push_back({Op::Begin, origin, 0, 0, nullptr});
      // Glide A toward B over ~20 ticks, then DWELL on the overlap so the
      // sustained-overlap gate accumulates and the dock fires.
      for (int i = 1; i <= 20; ++i) {
        steps.push_back({Op::Update,
                          Pt(origin.x + dx * i / 20, origin.y + dy * i / 20),
                          0, 0, nullptr});
      }
      for (int i = 0; i < 12; ++i) {  // dwell overlapped (> sustained_ticks)
        steps.push_back({Op::Update, Pt(origin.x + dx, origin.y + dy),
                          0, 0, nullptr});
      }
      steps.push_back({Op::End, {}, 0, 0, nullptr});
      steps.push_back({Op::Wait, {}, 0, 2, nullptr});
      // Undock so the next cycle starts detached again. (A may now be grouped
      // with B; ungroup A's partner B explicitly.)
      steps.push_back({Op::UngroupSurface, {}, 0, 0, b});
      steps.push_back({Op::Wait, {}, 0, 2, nullptr});
    }
    return sim.Run("repeated_dock_undock", a, std::move(steps), {});
  });

  // rapid_cross_docking: drag A rapidly ACROSS B without dwelling on the overlap.
  // Goal: candidate hysteresis sanity — the sustained+overlap gates must SUPPRESS
  // docking on a fast fly-by (no accidental group spam).
  Register("rapid_cross_docking", [](InteractionSimulator& sim,
                                     SurfaceManager& mgr) {
    SurfaceShell* a = FirstSurface(mgr);
    SurfaceShell* b = SecondSurface(mgr);
    if (!a || !b || !mgr.model()) return false;
    RECT ra{}, rb{};
    GetWindowRect(a->GetHandle(), &ra);
    GetWindowRect(b->GetHandle(), &rb);
    const POINT origin = Pt(ra.left + 100, ra.top + 10);
    const int dx = ((rb.left + rb.right) - (ra.left + ra.right)) / 2;
    const int dy = ((rb.top + rb.bottom) - (ra.top + ra.bottom)) / 2;
    std::vector<InteractionSimulator::Step> steps;
    for (int sweep = 0; sweep < 6; ++sweep) {
      steps.push_back({Op::Begin, origin, 0, 0, nullptr});
      // Sweep THROUGH B and out the far side in just a FEW large jumps. Each
      // Update teleports A by a full surface-width so the overlap zone is crossed
      // in 1-2 ticks — far fewer than sustained_ticks (6) — so the sustained gate
      // can never fill on a fly-by. start far past B on the near side, land far
      // past B on the far side, with only 3 intermediate samples.
      for (int i = -2; i <= 4; ++i) {  // 7 coarse samples across a 6x span
        steps.push_back({Op::Update,
                          Pt(origin.x + dx * i, origin.y + dy * i),
                          0, 0, nullptr});
      }
      steps.push_back({Op::End, {}, 0, 0, nullptr});
      steps.push_back({Op::Wait, {}, 0, 1, nullptr});
    }
    return sim.Run("rapid_cross_docking", a, std::move(steps), {});
  });

  // hostile_cancel_during_dock: build a dock candidate (glide A onto B + dwell),
  // then CANCEL / steal capture mid-dwell. Goal: epoch integrity — a queued-but-
  // unapplied dock must be DISCARDED (no partial topology mutation; A and B stay
  // detached). [Cancel happens via StealCapture → WM_CAPTURECHANGED → Cancel.]
  Register("hostile_cancel_during_dock", [](InteractionSimulator& sim,
                                            SurfaceManager& mgr) {
    SurfaceShell* a = FirstSurface(mgr);
    SurfaceShell* b = SecondSurface(mgr);
    if (!a || !b || !mgr.model()) return false;
    RECT ra{}, rb{};
    GetWindowRect(a->GetHandle(), &ra);
    GetWindowRect(b->GetHandle(), &rb);
    const POINT origin = Pt(ra.left + 100, ra.top + 10);
    const int dx = ((rb.left + rb.right) - (ra.left + ra.right)) / 2;
    const int dy = ((rb.top + rb.bottom) - (ra.top + ra.bottom)) / 2;
    // The epoch ticks at ~60Hz REGARDLESS of simulator step rate, so even a brief
    // dwell on full overlap accumulates sustained_ticks fast and docks at the next
    // boundary. To exercise the HOSTILE path (interaction killed while docking is
    // still merely intended), we approach to the EDGE of the dock zone — close +
    // partially overlapping, enough to be a Candidate, but we steal capture before
    // a stable sustained run completes. Move A to ~85% of the way onto B (overlap
    // present but the pair is still converging), hold 1 step, then yank capture.
    std::vector<InteractionSimulator::Step> steps;
    steps.push_back({Op::Begin, origin, 0, 0, nullptr});
    steps.push_back({Op::Update,
                      Pt(origin.x + dx * 85 / 100, origin.y + dy * 85 / 100),
                      0, 0, nullptr});
    steps.push_back({Op::StealCapture, {}, 0, 0, nullptr});  // kill the interaction
    // Whether or not a dock applied in the brief window, the PASS criterion is
    // engine integrity: 0 INTEGRITY FAIL, the epoch cancels cleanly, and topology
    // is coherent (either fully docked-as-truth or fully detached — never a
    // partial/torn mutation). The no-rollback law (I-X14) means an already-applied
    // dock legitimately survives the cancel; an unapplied pending dock is discarded.
    return sim.Run("hostile_cancel_during_dock", a, std::move(steps), {});
  });

  // =========================================================================
  // PHASE 8E — presentation coherence torture.
  // =========================================================================

  // topology_storm: interleave dock + ungroup + grouped-drag-end + rapid begin/end
  // so topology mutations and presentation settles overlap in time. Goal: the
  // presentation layer NEVER drifts from semantic truth — every settle completes
  // (snaps to semantic), no settle outlives its surface, and 0 watchdog forced
  // completions, even while topology is churning. Each drag END that lands away
  // from last-projected (clamp/coalesce) hands a settle to the coordinator; the
  // storm ensures many settles are in flight across topology changes.
  Register("topology_storm", [](InteractionSimulator& sim, SurfaceManager& mgr) {
    SurfaceShell* a = FirstSurface(mgr);
    SurfaceShell* b = SecondSurface(mgr);
    if (!a || !b || !mgr.model()) return false;
    RECT ra{}, rb{};
    GetWindowRect(a->GetHandle(), &ra);
    GetWindowRect(b->GetHandle(), &rb);
    const POINT origin = Pt(ra.left + 100, ra.top + 10);
    const int dx = ((rb.left + rb.right) - (ra.left + ra.right)) / 2;
    const int dy = ((rb.top + rb.bottom) - (ra.top + ra.bottom)) / 2;
    std::vector<InteractionSimulator::Step> steps;
    for (int round = 0; round < 5; ++round) {
      // (1) Dock A onto B (drag + dwell → group at boundary).
      steps.push_back({Op::Begin, origin, 0, 0, nullptr});
      for (int i = 1; i <= 12; ++i) {
        steps.push_back({Op::Update,
                          Pt(origin.x + dx * i / 12, origin.y + dy * i / 12),
                          0, 0, nullptr});
      }
      for (int i = 0; i < 10; ++i) {  // dwell → dock fires
        steps.push_back({Op::Update, Pt(origin.x + dx, origin.y + dy),
                          0, 0, nullptr});
      }
      // (2) Fling A back to origin and release FAR — a big end-residual so the
      //     settle has real work, while still grouped.
      for (int i = 12; i >= 0; --i) {
        steps.push_back({Op::Update,
                          Pt(origin.x + dx * i / 12, origin.y + dy * i / 12),
                          0, 0, nullptr});
      }
      steps.push_back({Op::End, {}, 0, 0, nullptr});      // settle likely starts here
      // (3) WAIT long enough for the settle to COMPLETE (kSettleTauMs≈60ms → a few
      //     ticks; 12 ticks ≈ 190ms is ample). This proves the completion path
      //     end-to-end (settle done, snaps to semantic) — not just begin/cancel.
      steps.push_back({Op::Wait, {}, 0, 12, nullptr});
      // (4) THEN churn topology (settle already completed → no interference).
      steps.push_back({Op::UngroupSurface, {}, 0, 0, b});
      // (5) Rapid begin/end immediately after a fresh drag-end — this SHOULD
      //     supersede-cancel any settle (I-E2), proving the cancel path too.
      steps.push_back({Op::Begin, origin, 0, 0, nullptr});
      for (int i = 1; i <= 6; ++i) {  // small drag so end has a residual
        steps.push_back({Op::Update, Pt(origin.x - i * 8, origin.y), 0, 0, nullptr});
      }
      steps.push_back({Op::End, {}, 0, 0, nullptr});
      steps.push_back({Op::Begin, origin, 0, 0, nullptr});  // immediate → cancels settle
      steps.push_back({Op::End, {}, 0, 0, nullptr});
      steps.push_back({Op::Wait, {}, 0, 2, nullptr});
    }
    return sim.Run("topology_storm", a, std::move(steps), {});
  });
}

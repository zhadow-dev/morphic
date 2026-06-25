#ifndef RUNNER_COMPOSITOR_PROJECTION_BACKEND_H_
#define RUNNER_COMPOSITOR_PROJECTION_BACKEND_H_

#include <windows.h>

#include <algorithm>
#include <vector>

#include "surface_shell.h"

// PHASE 9 — ProjectionBackend.
//
// THE seam. The single abstraction for "how presented geometry reaches the
// screen." Today there is exactly one implementation (HwndProjectionBackend) and
// it resolves to the existing ApplyGeometry / SetWindowPos path — i.e. Phase 9 is
// behaviorally identical to 8E. The value is the BOUNDARY: callers (SurfaceModel,
// PresentationCoordinator) hand presented rects to the backend without knowing
// whether projection is HWND, DComp, or anything else. A future backend swap (when
// a renderer strategy exposes a composition surface) is a one-class change with
// ZERO semantic impact.
//
// CONTRACT DISCIPLINE (do not rot the seam):
//   - The interface stays MINIMAL: project geometry, mirror z-order. Nothing else.
//   - Callers are backend-AGNOSTIC: no `if (backend_supports_x)` at call sites; the
//     backend encapsulates capability.
//   - The backend is DUMB: no policy (animation/interpolation/topology/z heuristics).
//     Those live in PresentationCoordinator / SurfaceModel / InteractionEpoch.
class ProjectionBackend {
 public:
  virtual ~ProjectionBackend() = default;

  // Project `rect` onto `surface`'s native presentation. Synchronous + immediate
  // (no interpolation — that's the coordinator's job upstream). For the HWND
  // backend this is a straight ApplyGeometry, so direct manipulation stays 1:1.
  virtual void ProjectGeometry(SurfaceShell* surface, const RECT& rect) = 0;

  // Mirror semantic z-order (front == index 0) onto native projection order. The
  // backend MIRRORS; it never decides z (SurfaceModel owns that truth).
  virtual void ProjectZOrder(const std::vector<SurfaceShell*>& z_front_first) = 0;
};

// The ONLY backend today: HWND projection. Geometry → ApplyGeometry (which the
// shell implements via SetWindowPos + child layout). Z-order → SetWindowPos
// reordering (relocated verbatim from SurfaceModel::ReconcileZOrder).
class HwndProjectionBackend : public ProjectionBackend {
 public:
  void ProjectGeometry(SurfaceShell* surface, const RECT& rect) override {
    if (surface && surface->GetHandle()) {
      surface->ApplyGeometry(rect);
    }
  }

  // PHASE 10.4D/F — TOPMOST-AWARE z reconciliation. The OS keeps WS_EX_TOPMOST windows
  // in a SEPARATE always-on-top band; we reconcile each band INTERNALLY (non-topmost
  // vs HWND_TOP, topmost vs HWND_TOPMOST). WS_EX_TOPMOST is read via GWL_EXSTYLE — a
  // GENERIC Win32 fact, NOT SurfaceKind (the compositor stays surface-type-agnostic).
  // SWP_NOACTIVATE so reordering never changes activation.
  //
  // PHASE 10.5d — OWNER-AWARE reconciliation (default ON; flip kOwnerAwareZ=false for
  // the legacy flat path — instant rollback, like kCompositorEnabled). The legacy chain
  // walked raw semantic order and issued owned-below-owner SetWindowPos calls whenever a
  // workspace was semantically above its owned tools. Win32 forbids that (an owned
  // window stays above its owner), so the OS overrode the move NONDETERMINISTICALLY and
  // reshuffled the sibling tools — the audit's owner-BLIND z (Confirmed Fact 5), proven
  // by the [ZPROJ] trace. Owner-aware projection reorders the semantic list into an
  // OWNER-LEGAL order first (every owned window above its owner; each owner-cluster
  // ordered by its FRONTMOST member; within a cluster owned siblings by semantic z and
  // the owner pinned at the cluster BOTTOM), then chains per band. Every SetWindowPos is
  // then OS-legal, so the result is STABLE: clicking a tool reorders only tool↔inspector;
  // clicking the workspace does NOT reshuffle its tools (it rests at the bottom of its
  // own owned cluster — standard Photoshop/Blender/VS tool-window behavior). The seam +
  // firewall are untouched: ownership is read from GW_OWNER (generic Win32), not kinds.
  static constexpr bool kOwnerAwareZ = true;

  void ProjectZOrder(const std::vector<SurfaceShell*>& z) override {
    if constexpr (!kOwnerAwareZ) {
      HWND prev_normal = HWND_TOP;
      HWND prev_topmost = HWND_TOPMOST;
      for (SurfaceShell* s : z) {
        if (s == nullptr || s->GetHandle() == nullptr) continue;
        const HWND h = s->GetHandle();
        const bool topmost = (GetWindowLong(h, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
        HWND& anchor = topmost ? prev_topmost : prev_normal;
        SetWindowPos(h, anchor, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        anchor = h;
      }
      return;
    }

    const int n = static_cast<int>(z.size());
    if (n == 0) return;

    auto index_of_hwnd = [&z, n](HWND h) -> int {
      if (h == nullptr) return -1;
      for (int i = 0; i < n; ++i)
        if (z[i] && z[i]->GetHandle() == h) return i;
      return -1;
    };

    // owner-IN-SET index per surface (-1 = root / owner not one of ours).
    std::vector<int> owner_idx(n, -1);
    for (int i = 0; i < n; ++i)
      if (z[i] && z[i]->GetHandle())
        owner_idx[i] = index_of_hwnd(GetWindow(z[i]->GetHandle(), GW_OWNER));

    // depth in the owner chain (root = 0) + root-in-set index. Bounded by n (acyclic).
    std::vector<int> depth(n, 0), root_idx(n);
    for (int i = 0; i < n; ++i) {
      int r = i, d = 0, guard = 0;
      while (owner_idx[r] >= 0 && guard < n) { r = owner_idx[r]; ++d; ++guard; }
      depth[i] = d;
      root_idx[i] = r;
    }

    // cluster_rep[i] = min semantic index across the whole cluster sharing root_idx[i]
    // (owner + its children sort as ONE block, positioned by the frontmost member — so
    // clicking any member brings the cluster forward identically). Clusters partition
    // the index set, so distinct clusters have distinct min indices.
    std::vector<int> root_min(n, n);  // n = +inf sentinel (every real index < n)
    for (int i = 0; i < n; ++i)
      if (i < root_min[root_idx[i]]) root_min[root_idx[i]] = i;
    std::vector<int> cluster_rep(n);
    for (int i = 0; i < n; ++i) cluster_rep[i] = root_min[root_idx[i]];

    // Owner-legal stable order (top→bottom): cluster block by frontmost member; within a
    // cluster, deeper (owned) above shallower (owner); ties by semantic index.
    std::vector<int> order(n);
    for (int i = 0; i < n; ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
      if (cluster_rep[a] != cluster_rep[b]) return cluster_rep[a] < cluster_rep[b];
      if (depth[a] != depth[b]) return depth[a] > depth[b];  // children above owner
      return a < b;                                          // stable by semantic index
    });

    // Apply in owner-legal order, chaining per band (topmost above non-topmost). Every
    // move is OS-legal now (owner never placed below its owned), so projection is stable.
    HWND prev_normal = HWND_TOP;
    HWND prev_topmost = HWND_TOPMOST;
    for (int idx : order) {
      SurfaceShell* s = z[idx];
      if (s == nullptr || s->GetHandle() == nullptr) continue;
      const HWND h = s->GetHandle();
      const bool topmost = (GetWindowLong(h, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
      HWND& anchor = topmost ? prev_topmost : prev_normal;
      SetWindowPos(h, anchor, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
      anchor = h;
    }
  }
};

#endif  // RUNNER_COMPOSITOR_PROJECTION_BACKEND_H_

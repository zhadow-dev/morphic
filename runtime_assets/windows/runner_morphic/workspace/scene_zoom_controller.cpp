#include "workspace/scene_zoom_controller.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>

#include "forensic_trace.h"
#include "presentation/presentation_coordinator.h"
#include "surface_manager.h"
#include "surface_model.h"
#include "surface_shell.h"
#include "workspace/composition_plane.h"
#include "workspace/workspace_composition_graph.h"

namespace morphic::workspace {

namespace {
constexpr double kFill = 0.98;  // breathing margin so the immersive composition isn't edge-to-edge
}  // namespace

SceneZoomController::SceneZoomController(WorkspaceCompositionGraph* graph, SurfaceModel* model,
                                        SurfaceManager* manager, PresentationCoordinator* coordinator)
    : graph_(graph), model_(model), manager_(manager), coordinator_(coordinator) {}

std::string SceneZoomController::RootOf(const std::string& any_id) const {
  if (graph_ == nullptr) return std::string();
  if (graph_->IsRoot(any_id)) return any_id;
  return graph_->RootOfMember(any_id);  // "" if not a member
}

RECT SceneZoomController::WorkAreaFor(const RECT& bbox) const {
  RECT box = bbox;
  HMONITOR mon = MonitorFromRect(&box, MONITOR_DEFAULTTONEAREST);
  MONITORINFO mi;
  mi.cbSize = sizeof(mi);
  if (mon != nullptr && GetMonitorInfo(mon, &mi)) return mi.rcWork;
  RECT work{0, 0, 0, 0};
  SystemParametersInfo(SPI_GETWORKAREA, 0, &work, 0);
  return work;
}

void SceneZoomController::ApplyMemberBounds(SurfaceShell* surface, const RECT& target) {
  // Snapshot stays canonical; the PRESENTED projection EASES to the target via the EXISTING settle seam
  // (bounded, self-terminating). SetSemanticBounds sets clamped truth WITHOUT projecting (no flash); the
  // settle then eases native from where it visually IS (presented(), so a re-trigger mid-ease is smooth)
  // to the new truth. Falls back to instant SetBounds when animation or the presentation seam is off.
  if (kAnimated && coordinator_ != nullptr && PresentationCoordinator::kPresentationEnabled) {
    const RECT from = coordinator_->presented(surface);
    model_->SetSemanticBounds(surface, target);
    coordinator_->RequestSettle(surface, from);
  } else {
    model_->SetBounds(surface, target);  // instant (clamp keeps it on-screen)
  }
}

void SceneZoomController::ZoomScene(const std::string& any_id, double zoom) {
  if (graph_ == nullptr || model_ == nullptr || manager_ == nullptr) return;
  if (reprojecting_) return;  // CONTRACT: one-shot only — reject any re-entrant reprojection.
  const std::string root_id = RootOf(any_id);
  if (root_id.empty()) return;
  const CompositionPlane* plane = graph_->PlaneOfRoot(root_id);
  SurfaceShell* root = manager_->FindById(root_id);
  if (plane == nullptr || root == nullptr) return;

  // Capture the AUTHORED snapshot ONCE — root + every plane member. Always reproject FROM it. We do
  // NOT cache a lone-root snapshot: if the companions haven't composed yet, bail so a later call (the
  // habitat retries) captures the FULL authored composition. Never re-reads live geometry afterward.
  auto it = authored_.find(root_id);
  if (it == authored_.end()) {
    std::unordered_map<std::string, RECT> cand;
    cand[root_id] = model_->bounds(root);
    for (const CompositionMember& m : plane->members) {
      if (SurfaceShell* s = manager_->FindById(m.surface_id)) cand[m.surface_id] = model_->bounds(s);
    }
    if (cand.size() < 2) return;  // composition not assembled yet — retry on a later call
    it = authored_.emplace(root_id, std::move(cand)).first;
    // Snapshot LOCKED: from here these authored rects are canonical and are NEVER re-read from live
    // geometry — every zoom AND the reset reproject from THIS, so drift cannot accumulate (CONTRACT).
    forensic::Log("SCENEZOOM", "snapshot LOCKED root=" + root_id + " members=" +
                                   std::to_string(it->second.size()));
  }
  const std::unordered_map<std::string, RECT>& snap = it->second;

  // Authored bounding box (union of authored rects).
  RECT bbox = snap.begin()->second;
  for (const auto& kv : snap) {
    bbox.left = std::min(bbox.left, kv.second.left);
    bbox.top = std::min(bbox.top, kv.second.top);
    bbox.right = std::max(bbox.right, kv.second.right);
    bbox.bottom = std::max(bbox.bottom, kv.second.bottom);
  }
  const double bw = static_cast<double>(bbox.right - bbox.left);
  const double bh = static_cast<double>(bbox.bottom - bbox.top);
  if (bw <= 0.0 || bh <= 0.0) return;

  const RECT work = WorkAreaFor(bbox);
  const double ww = static_cast<double>(work.right - work.left);
  const double wh = static_cast<double>(work.bottom - work.top);

  // scale = requested zoom, clamped so the authored bbox never overflows the work area. So a large
  // value resolves to "fill the field" (immersive); 1.0 leaves the authored composition untouched.
  const double fit = std::min(ww * kFill / bw, wh * kFill / bh);
  double scale = zoom;
  if (scale > fit) scale = fit;
  if (scale < 0.1) scale = 0.1;

  const double bcx = (bbox.left + bbox.right) / 2.0;
  const double bcy = (bbox.top + bbox.bottom) / 2.0;
  const double wcx = (work.left + work.right) / 2.0;
  const double wcy = (work.top + work.bottom) / 2.0;

  reprojecting_ = true;  // CONTRACT guard — any re-entrant zoom triggered during SetBounds is rejected.
  for (const auto& kv : snap) {
    SurfaceShell* s = manager_->FindById(kv.first);
    if (s == nullptr) continue;
    const RECT& a = kv.second;
    const double aw = static_cast<double>(a.right - a.left);
    const double ah = static_cast<double>(a.bottom - a.top);
    if (aw <= 0.0 || ah <= 0.0) continue;  // never project a degenerate authored rect
    const double acx = (a.left + a.right) / 2.0;
    const double acy = (a.top + a.bottom) / 2.0;
    // Scale the member's SIZE and its OFFSET from the composition centre by the same factor, then
    // re-centre the whole composition on the work area. Relationships/rhythm preserved exactly.
    const double ncx = wcx + (acx - bcx) * scale;
    const double ncy = wcy + (acy - bcy) * scale;
    const double nw = aw * scale;
    const double nh = ah * scale;
    RECT r;
    r.left = static_cast<LONG>(std::lround(ncx - nw / 2.0));
    r.top = static_cast<LONG>(std::lround(ncy - nh / 2.0));
    r.right = static_cast<LONG>(std::lround(ncx + nw / 2.0));
    r.bottom = static_cast<LONG>(std::lround(ncy + nh / 2.0));
    ApplyMemberBounds(s, r);
  }
  reprojecting_ = false;
  forensic::Log("SCENEZOOM", "zoom root=" + root_id + " members=" +
                                 std::to_string(snap.size()) + " scale=" +
                                 std::to_string(scale));
}

void SceneZoomController::ResetScene(const std::string& any_id) {
  if (manager_ == nullptr || model_ == nullptr) return;
  if (reprojecting_) return;  // CONTRACT: one-shot only.
  const std::string root_id = RootOf(any_id);
  if (root_id.empty()) return;
  auto it = authored_.find(root_id);
  if (it == authored_.end()) return;
  // Restore EXACTLY the authored snapshot (zoom/reset parity) — the presented bounds were throwaway.
  reprojecting_ = true;
  for (const auto& kv : it->second) {
    if (SurfaceShell* s = manager_->FindById(kv.first)) ApplyMemberBounds(s, kv.second);
  }
  reprojecting_ = false;
  authored_.erase(it);
  forensic::Log("SCENEZOOM", "reset root=" + root_id);
}

}  // namespace morphic::workspace

#include "workspace/relationship_gravity_controller.h"

#include <algorithm>
#include <string>
#include <utility>

#include "forensic_trace.h"
#include "surface_manager.h"
#include "surface_model.h"
#include "surface_shell.h"
#include "workspace/workspace_composition_graph.h"

namespace morphic::workspace {

namespace {
// AUTHORED immersive slots — hand-tuned staging, emphatically NOT a formula / distribution / balancing
// engine. Each slot is an explicit offset of a companion's LEFT/TOP from the maximized field's
// right/top edges.
//
// M2.7 RESTAGE (the real fix — staging, not polish). The earlier slots ({420,140}+{610,480}) scattered
// the inspector high-right and the palette low-middle with a big void between → read as TWO UNRELATED
// CARDS in accidental space (the spatial illusion's death). Now the companions form ONE COHERENT CONTEXT
// CLUSTER hugging the field's right edge: a tight VERTICAL RHYTHM — inspector anchored high, palette
// DIRECTLY below it and stepped further left — so they read as a single descending group that ORBITS the
// editor (compositionally dependent), not neighbouring windows. Assigned by member ORDER (= the app's
// authoring order). The numbers ARE the composition: tune them, do NOT re-add placement math. If the
// center still reads "dead", pull the whole cluster LEFT toward the writing column by raising dx.
struct Slot {
  LONG dx;  // left edge = field.right - dx
  LONG dy;  // top  edge = field.top  + dy
};
constexpr Slot kSlots[] = {
    {336, 88},   // 0 — inspector: the cluster's upper anchor, hugging the right (right edge ~96px inset)
    {396, 572},  // 1 — palette: DIRECTLY below inspector, stepped ~60px further left — tight vertical rhythm
};
constexpr int kSlotCount = 2;
constexpr LONG kCascadeStep = 150;  // extra companions (rare; Notes has 2) cascade below the last slot
}  // namespace

RelationshipGravityController::RelationshipGravityController(WorkspaceCompositionGraph* graph,
                                                            SurfaceModel* model,
                                                            SurfaceManager* manager)
    : graph_(graph), model_(model), manager_(manager) {}

void RelationshipGravityController::OnWindowState(SurfaceShell* root) {
  if constexpr (!kEnabled) {
    (void)root;  // toggle off — inert; dead branch discarded (no /WX warning)
    return;
  } else {
    if (root == nullptr || graph_ == nullptr || model_ == nullptr || manager_ == nullptr) return;
    const std::string id = root->id();
    if (!graph_->IsRoot(id)) return;  // ONLY plane roots emit gravity (ignore companions/overlays)
    const bool maximized = model_->IsSurfaceMaximized(root);
    const bool applied = remembered_.count(id) != 0;
    if (maximized && !applied) {
      ApplyGravity(id);
    } else if (!maximized && applied) {
      RestoreGravity(id);
    }
    // (maximized && applied) / (!maximized && !applied) — nothing to do (e.g. minimize while max).
  }
}

void RelationshipGravityController::ApplyGravity(const std::string& root_id) {
  const CompositionPlane* plane = graph_->PlaneOfRoot(root_id);
  SurfaceShell* root = manager_->FindById(root_id);
  if (plane == nullptr || root == nullptr) return;
  // The maximized editor fills the work area = the canvas (companions float OVER it, never over
  // wallpaper). We RECOMPOSE companions into an authored right-side cluster so the composition reads as
  // intentional — preserving exact positions left them scattered in the field ("unstructured empty
  // space"). INITIAL staging ONLY: companions stay fully independent (drag away / focus / close), and
  // on un-maximize we restore. No resize, no continuous enforcement, no snapping.
  const RECT field = model_->bounds(root);

  std::unordered_map<std::string, RECT> remembered;
  int idx = 0;
  for (const CompositionMember& m : plane->members) {
    SurfaceShell* comp = manager_->FindById(m.surface_id);
    if (comp == nullptr) continue;
    const RECT cur = model_->bounds(comp);
    const LONG w = cur.right - cur.left;  // PRESERVE SIZE — never resize a companion
    const LONG h = cur.bottom - cur.top;
    LONG dx, dy;
    if (idx < kSlotCount) {
      dx = kSlots[idx].dx;  // explicit AUTHORED slot
      dy = kSlots[idx].dy;
    } else {  // beyond the authored slots (rare): cascade below the last
      dx = kSlots[kSlotCount - 1].dx;
      dy = kSlots[kSlotCount - 1].dy + (idx - kSlotCount + 1) * kCascadeStep;
    }
    RECT slot;
    slot.left = field.right - dx;
    slot.top = field.top + dy;
    slot.right = slot.left + w;
    slot.bottom = slot.top + h;
    remembered[m.surface_id] = cur;
    model_->SetBounds(comp, slot);  // clamp keeps it on-screen
    ++idx;
  }
  remembered_[root_id] = std::move(remembered);
  forensic::Log("GRAVITY", "apply(authored) root=" + root_id +
                               " companions=" + std::to_string(plane->members.size()));
}

void RelationshipGravityController::RestoreGravity(const std::string& root_id) {
  auto it = remembered_.find(root_id);
  if (it == remembered_.end()) return;
  for (const auto& entry : it->second) {
    SurfaceShell* comp = manager_->FindById(entry.first);
    // Always return to the remembered rect (the "user moved it while maximized" heuristic is
    // deferred until lived pain proves it necessary — per spec). A companion closed while maximized
    // is simply gone (FindById null) and skipped.
    if (comp != nullptr) model_->SetBounds(comp, entry.second);
  }
  remembered_.erase(it);
  forensic::Log("GRAVITY", "restore root=" + root_id);
}

}  // namespace morphic::workspace

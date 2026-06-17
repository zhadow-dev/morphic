#include "surface_policy/surface_ecology.h"

#include <cstdio>

#include "forensic_trace.h"
#include "runtime_events.h"
#include "surface_manager.h"
#include "surface_policy/activation_policy.h"
#include "surface_policy/native_appearance_projection.h"
#include "surface_policy/native_surface_policy.h"
#include "surface_policy/surface_appearance_policy.h"
#include "surface_shell.h"

namespace morphic::policy {

SurfaceEcology::SurfaceEcology(SurfaceManager* manager, EventBus* bus)
    : manager_(manager), bus_(bus), registry_(bus), composition_graph_(bus) {
  forensic::Log("ECOLOGY", "SurfaceEcology created (policy layer above runtime)");
  // PHASE 10.3E — observe activation to compute + log the activation cluster.
  if (bus_) {
    activation_token_ = bus_->Subscribe(
        [this](RuntimeEvent e, SurfaceShell* s) {
          if (e == RuntimeEvent::SurfaceActivated) OnActivation(s);
        });
  }
}

SurfaceEcology::~SurfaceEcology() {
  if (bus_ && activation_token_ != 0) {
    bus_->Unsubscribe(activation_token_);
    activation_token_ = 0;
  }
}

void SurfaceEcology::OnActivation(SurfaceShell* surface) {
  if (surface == nullptr) return;
  const std::string root = surface->id();
  const auto cluster = ActivationClusterFor(root, registry_, workspaces_);
  if (cluster.size() <= 1) return;  // solo activation — nothing to report
  std::string ids;
  for (const auto& sid : cluster) {
    if (!ids.empty()) ids += ",";
    ids += sid;
  }
  forensic::Log("ACTIVATION", root + " cluster=[" + ids + "]");
  // OBSERVE-ONLY: owner chains already raise owned windows with their owner; we
  // do not invoke a runtime cluster-raise (no such public entry, and inventing one
  // would pollute the runtime). Verification reads this line; behavior is judged
  // manually.
}

std::optional<std::string> SurfaceEcology::SpawnSurface(
    const flutter::DartProject& base_project, SurfaceKind kind,
    const std::string& id, const std::string& entrypoint, int x, int y,
    int width, int height, const std::string& workspace_id,
    std::optional<std::string> parent_id, const SpawnOverrides& overrides) {
  if (manager_ == nullptr) return std::nullopt;

  const bool is_global = SurfacePolicy::IsGlobal(kind);

  // GLOBAL surfaces (the ecology launcher) belong to NO workspace. Workspace
  // surfaces resolve their target workspace (default to current, create one if
  // none exists yet).
  std::string ws;
  if (!is_global) {
    ws = workspace_id.empty() ? workspaces_.Current() : workspace_id;
    if (ws.empty()) ws = workspaces_.Create("Workspace");
  }

  // Policy gate: if a parent is given, the parent's kind must be allowed to spawn
  // this child kind.
  if (parent_id.has_value()) {
    const SurfaceDescriptor* parent = registry_.Get(*parent_id);
    if (parent && !SurfacePolicy::CanSpawn(parent->kind, kind)) {
      forensic::Log("ECOLOGY", "spawn REJECTED by policy: parent kind=" +
                                   std::string(ToString(parent->kind)) +
                                   " cannot spawn child kind=" +
                                   std::string(ToString(kind)));
      return std::nullopt;
    }
  }

  // Build the descriptor FIRST so native behavior can be resolved from it.
  SurfaceDescriptor desc =
      SurfacePolicy::DefaultDescriptorFor(id, kind, ws, parent_id);
  // PHASE 12A — apply the optional spatial-mode override (Q5: orthogonal to kind).
  if (overrides.composition_mode) {
    desc.composition_mode = *overrides.composition_mode;
  }

  // SPATIAL MIGRATION — visual mode (grounded vs atmospheric). DEFAULT Standard for everything
  // EXCEPT the role-floaters (Overlay/Command), so the desktop is grounded by default. The
  // launcher's existing backdrop selector is the per-surface opt-in: backdrop None → Standard
  // (opaque), any material → Glass (translucent + that backdrop). Glass is selective, not
  // universal.
  desc.visual_mode = (kind == SurfaceKind::Overlay || kind == SurfaceKind::Command)
                         ? SurfaceVisualMode::Glass
                         : SurfaceVisualMode::Standard;
  // The launcher's backdrop selector only UPGRADES to Glass (picking a material). A 'none'
  // backdrop leaves the role default untouched — so a role-glass floater (overlay) is never
  // accidentally forced opaque (which, with its transparent content, would render black).
  if (overrides.backdrop && *overrides.backdrop != SurfaceBackdrop::None) {
    desc.visual_mode = SurfaceVisualMode::Glass;
  }

  // PHASE 10.3 — resolve SurfaceKind → native behavior → PLAIN Win32 flags. This
  // is the policy/Win32 boundary; what crosses DOWN into the runtime is a DWORD
  // ex-style + an owner HWND — never the kind.
  const NativeSurfaceBehavior nb = ResolveNativeBehavior(desc);
  const DWORD ex_style = ToExStyle(nb);

  // Owner HWND: an owned surface (palette/inspector/overlay) is Win32-owned by its
  // parent surface's HWND, so it stays grouped with the owner (minimize/Alt+Tab).
  // NOTE: Win32 owner is a SHELL-behavior projection of the semantic parent — it is
  // NOT the semantic ownership truth (that's desc.parent_surface). Don't conflate.
  HWND owner = nullptr;
  if (nb.owned_by_parent_hwnd && desc.parent_surface.has_value()) {
    if (SurfaceShell* p = manager_->FindById(*desc.parent_surface)) {
      owner = p->GetHandle();
    }
  }

  // Create the runtime surface (UNCHANGED entry; we only pass extra plain Win32).
  // SPATIAL CHROME — chromeless crosses down as a plain value. The corner radius
  // does NOT (see SurfaceAppearance::corner_radius_px) — it is stored here and
  // delivered to the surface's own engine, which shapes the window via content
  // alpha.
  const bool chromeless = overrides.chromeless.value_or(false);
  const int corner_radius_px = overrides.corner_radius_px.value_or(0);
  // Record the declared backend BEFORE CreateSurface — CreateSurface publishes
  // SurfaceCreated, and the NG compositor reads the backend THERE to pre-cloak
  // a spatial engine before its first desktop-visible frame (atomic bring-up).
  // Recording it afterwards left the compositor seeing "native" at create time.
  if (overrides.backend) {
    backend_[id] = *overrides.backend;
    forensic::Log("ECOLOGY", "backend id=" + id + " -> " + *overrides.backend);
  }
  if (!manager_->CreateSurface(base_project, id, entrypoint, x, y, width, height,
                               ex_style, owner, chromeless)) {
    forensic::Log("ECOLOGY", "spawn FAILED: runtime CreateSurface id=" + id);
    return std::nullopt;
  }
  if (chromeless || corner_radius_px > 0) {
    forensic::Log("ECOLOGY", "spatial chrome id=" + id +
                                 " chromeless=" + (chromeless ? "1" : "0") +
                                 " corner_radius=" +
                                 std::to_string(corner_radius_px));
  }
  // (backend recorded above, before CreateSurface, so SurfaceCreated sees it.)
  if (overrides.shape || overrides.material || overrides.material_tint_argb ||
      overrides.elevation_px) {
    SpatialMaterialSpec spec;
    if (overrides.shape) spec.shape = *overrides.shape;
    if (overrides.material) spec.material = *overrides.material;
    if (overrides.material_tint_argb) spec.tint_argb = *overrides.material_tint_argb;
    if (overrides.elevation_px) spec.elevation_px = *overrides.elevation_px;
    spatial_material_[id] = spec;
    forensic::Log("ECOLOGY", "spatial material id=" + id + " shape=" +
                                 spec.shape + " material=" + spec.material +
                                 " elevation=" +
                                 std::to_string(spec.elevation_px));
  }

  // Semantic logging (native behavior visible at the product layer). PHASE 10.4A —
  // assert the owner HWND was actually resolved at create time. A null owner HWND
  // for an owned surface is the Alt+Tab-drift bug (TOOLWINDOW without an owner can
  // still leak into the switcher / breaks owner-chain raising) — surface it LOUDLY.
  if (nb.owned_by_parent_hwnd && desc.parent_surface.has_value()) {
    if (owner != nullptr) {
      forensic::Log("OWNERSHIP", id + " owner=" + *desc.parent_surface +
                                     " hwnd=set");
    } else {
      forensic::Log("OWNERSHIP FAIL",
                    id + " owner=" + *desc.parent_surface +
                        " hwnd=NULL — parent not found at create; owned surface "
                        "will leak into Alt+Tab / lose owner-chain raising");
    }
  }
  // PHASE 10.5 Fix 3 — verify the topmost projection fact for overlays/commands.
  // Topmost z-banding only works if the HWND actually carries WS_EX_TOPMOST; assert
  // it at create (log-only, mirrors OWNERSHIP FAIL above) so a missing flag is caught
  // as a creation fault, not misdiagnosed downstream as a z-reconcile bug.
  if (nb.topmost) {
    if (SurfaceShell* s = manager_->FindById(id)) {
      if (!(GetWindowLong(s->GetHandle(), GWL_EXSTYLE) & WS_EX_TOPMOST)) {
        forensic::Log("OVERLAY FAIL",
                      id + " kind=" + std::string(ToString(kind)) +
                          " — WS_EX_TOPMOST NOT set at create; overlay will fall "
                          "into the non-topmost z-band");
      }
    }
  }
  if (!nb.appears_in_alt_tab) {
    forensic::Log("ALT_TAB", "suppressed " + id + " (" +
                                 std::string(ToString(kind)) + ")");
  }

  // PHASE 11A/11B — project native appearance (DWM shadow / corners / dark mode). Policy
  // resolves the descriptor → SurfaceAppearance; per-surface overrides (from the Dart
  // ecology channel) then refine it; projection hands plain DWM calls to the HWND. The
  // shell never sees SurfaceKind (firewall). At spawn there are no composition planes yet,
  // so is_plane_root / in_plane default false.
  if (SurfaceShell* shell = manager_->FindById(id)) {
    SurfaceAppearance app = ResolveAppearance(desc);
    if (overrides.corners) app.corners = *overrides.corners;
    if (overrides.backdrop) app.backdrop = *overrides.backdrop;
    if (overrides.transparency_mode) app.transparency_mode = *overrides.transparency_mode;
    if (overrides.shadow) {
      app.shadow = *overrides.shadow ? ShadowParticipation::Independent
                                     : ShadowParticipation::None;
    }
    app.corner_radius_px = corner_radius_px;  // content-delivered, never projected
    ApplyAppearance(shell->GetHandle(), app);
    applied_appearance_[id] = app;  // F1 — remember it so plane-shadow reconciliation can
                                    // adjust ONLY the shadow later, preserving overrides.
  }

  // PHASE 12B/12D — if this is a Composed surface with a parent, enroll it as a member of
  // the parent's composition plane (parent = plane root). The CompositionProjector then
  // carries it when the parent is dragged. Projection-only — NOT topology / grouping.
  if (desc.composition_mode == SurfaceCompositionMode::Composed &&
      parent_id.has_value()) {
    composition_graph_.AddMember(*parent_id, id,
                                 MemberDragBehavior::MovesPlane);
    // SPATIAL MIGRATION F1 — the plane now exists: re-project shadow for the new MEMBER
    // (suppress its independent drop-shadow so it doesn't stack over the rest of the plane)
    // and the ROOT (it just became a plane root). Closes the spawn→membership sequencing gap
    // where surface_appearance_policy.cpp's member-shadow suppression never fired.
    ReconcilePlaneShadow(id);
    ReconcilePlaneShadow(*parent_id);
    // ...and the member adopts the root's glass material (native half of plane coherence).
    ReconcilePlaneMaterial(id);
  }

  // Record metadata + workspace membership (global kinds skip workspace).
  registry_.Register(desc);
  if (!is_global) {
    workspaces_.AddSurface(ws, id);
  }

  return id;
}

bool SurfaceEcology::DestroySurface(const std::string& id) {
  if (manager_ == nullptr) return false;

  const SurfaceDescriptor* desc = registry_.Get(id);
  if (!desc) {
    forensic::Log("POLICY", "destroy REJECTED: unknown surface id=" + id);
    return false;
  }

  const std::string kind_name = ToString(desc->kind);
  const std::string ws = desc->workspace_id;

  // OWNERSHIP CASCADE: find children whose parent_surface == this surface.
  // Destroy them FIRST (depth-first cascade). Copy ids to avoid iterator
  // invalidation during destruction.
  std::vector<std::string> children_to_destroy;
  for (const auto& d : registry_.All()) {
    if (d.parent_surface.has_value() && *d.parent_surface == id) {
      children_to_destroy.push_back(d.id);
    }
  }
  for (const auto& child_id : children_to_destroy) {
    const SurfaceDescriptor* child = registry_.Get(child_id);
    if (child) {
      forensic::Log("POLICY", "cascade destroy " +
                                   std::string(ToString(child->kind)) +
                                   " id=" + child_id + " (parent=" + id + ")");
    }
    DestroySurface(child_id);  // recursive cascade
  }

  // Now destroy this surface.
  forensic::Log("POLICY", "destroy " + kind_name + " id=" + id +
                               " workspace=" + ws);
  workspaces_.RemoveSurface(id);
  registry_.Unregister(id);
  applied_appearance_.erase(id);
  backend_.erase(id);
  spatial_material_.erase(id);  // F1 — drop remembered appearance (composition graph
                                  // auto-cleans its own membership on SurfaceDestroyed).
  return manager_->DestroySurfaceById(id);
}

bool SurfaceEcology::DestroyWorkspace(const std::string& workspace_id) {
  if (manager_ == nullptr) return false;

  forensic::Log("WORKSPACE", "destroying workspace id=" + workspace_id);

  // Get all surface ids BEFORE destroying the workspace entry.
  auto surface_ids = workspaces_.SurfacesOf(workspace_id);

  // Destroy each surface (which cascades owned children).
  for (const auto& sid : surface_ids) {
    DestroySurface(sid);
  }

  // Remove the workspace itself.
  workspaces_.Destroy(workspace_id);
  return true;
}

SurfaceEcology::EcologySummary SurfaceEcology::GetSummary() const {
  EcologySummary summary;
  summary.current_workspace = workspaces_.Current();

  for (const auto& d : registry_.All()) {
    EcologySummary::SurfaceInfo info;
    info.id = d.id;
    info.kind = ToString(d.kind);
    info.workspace_id = d.workspace_id;
    info.parent = d.parent_surface.value_or("");
    info.detachable = d.detachable;
    info.groupable = d.groupable;
    info.focusable = d.focusable;
    info.persistent = d.persistent;
    info.follows_parent = SurfacePolicy::FollowsParent(d.kind);
    summary.surfaces.push_back(std::move(info));
  }

  for (const auto& w : workspaces_.All()) {
    EcologySummary::WorkspaceInfo ws;
    ws.id = w.id;
    ws.title = w.title;
    ws.surface_ids = w.surface_ids;
    summary.workspaces.push_back(std::move(ws));
  }

  return summary;
}

void SurfaceEcology::LogSummary() const {
  char buf[256];
  _snprintf_s(buf, sizeof(buf), _TRUNCATE,
              "ecology summary: surfaces=%zu workspaces=%zu current=%s",
              registry_.size(), workspaces_.size(),
              workspaces_.Current().c_str());
  forensic::Log("ECOLOGY", buf);
  for (const auto& d : registry_.All()) {
    forensic::Log("ECOLOGY",
                  "  surface id=" + d.id + " kind=" +
                      std::string(ToString(d.kind)) + " workspace=" +
                      d.workspace_id +
                      " detachable=" + (d.detachable ? "1" : "0") +
                      " persistent=" + (d.persistent ? "1" : "0"));
  }
}

void SurfaceEcology::ReapplyAppearance(const std::string& id) {
  if (manager_ == nullptr) return;
  const SurfaceDescriptor* d = registry_.Get(id);
  if (d == nullptr) return;
  if (SurfaceShell* shell = manager_->FindById(id)) {
    // is_plane_root stays false until the Phase 12B composition graph exists; once it
    // does, pass the surface's plane-root status here so a Composed root casts the
    // shared-plane shadow.
    ApplyAppearance(shell->GetHandle(), ResolveAppearance(*d));
  }
}

void SurfaceEcology::ReconcilePlaneShadow(const std::string& id) {
  if constexpr (!kEnablePlaneShadowReconcile) {
    return;  // F1 disabled — members keep their independent spawn shadow (pre-F1 behavior)
  }
  if (manager_ == nullptr) return;
  auto it = applied_appearance_.find(id);
  if (it == applied_appearance_.end()) return;  // not spawned through here — nothing to adjust

  // Plane status from the composition graph (the ONLY source of plane truth).
  const bool is_root = composition_graph_.IsRoot(id);
  const bool in_plane = is_root || !composition_graph_.RootOfMember(id).empty();
  if (!in_plane) return;  // floating surface — leave its shadow exactly as spawned

  // Plane policy (mirrors surface_appearance_policy.cpp:49-54, applied now the plane exists):
  // the ROOT casts the single shared-plane shadow; MEMBERS cast none so they don't stack
  // their own drop-shadows over the rest of the plane. This deliberately wins over a
  // per-surface shadow override for plane members — coplanar coherence is the point of F1.
  const ShadowParticipation desired =
      is_root ? ShadowParticipation::SharedPlane : ShadowParticipation::None;
  if (it->second.shadow == desired) return;  // already correct — no reproject

  it->second.shadow = desired;
  if (SurfaceShell* shell = manager_->FindById(id)) {
    ApplyAppearance(shell->GetHandle(), it->second);  // preserves all other overrides
    forensic::Log("PLANE SHADOW",
                  "reconciled id=" + id + " root=" + std::string(is_root ? "1" : "0") +
                      " shadow=" +
                      std::string(desired == ShadowParticipation::None ? "None"
                                                                       : "SharedPlane"));
  }
}

std::string SurfaceEcology::GetSurfaceBackend(const std::string& id) const {
  auto it = backend_.find(id);
  return it == backend_.end() ? "native" : it->second;
}

SurfaceEcology::SpatialMaterialSpec SurfaceEcology::GetSpatialMaterial(
    const std::string& id) const {
  auto it = spatial_material_.find(id);
  return it == spatial_material_.end() ? SpatialMaterialSpec{} : it->second;
}

int SurfaceEcology::GetCornerRadius(const std::string& id) const {
  auto it = applied_appearance_.find(id);
  return it == applied_appearance_.end() ? 0 : it->second.corner_radius_px;
}

bool SurfaceEcology::SetCornerRadius(const std::string& id, int radius_px) {
  auto it = applied_appearance_.find(id);
  if (it == applied_appearance_.end()) return false;
  it->second.corner_radius_px = radius_px > 0 ? radius_px : 0;
  forensic::Log("ECOLOGY", "corner radius id=" + id + " -> " +
                               std::to_string(it->second.corner_radius_px));
  return true;
}

bool SurfaceEcology::IsSurfaceGlass(const std::string& id) const {
  auto it = applied_appearance_.find(id);
  if (it == applied_appearance_.end()) return false;
  return ModeWantsFullGlass(it->second.transparency_mode);
}

std::string SurfaceEcology::GetSurfaceMaterial(const std::string& id) const {
  auto it = applied_appearance_.find(id);
  if (it == applied_appearance_.end()) return "standard";
  if (!ModeWantsFullGlass(it->second.transparency_mode)) return "standard";
  switch (it->second.backdrop) {
    case SurfaceBackdrop::Mica:    return "mica";
    case SurfaceBackdrop::Acrylic: return "acrylic";
    case SurfaceBackdrop::Tabbed:  return "tabbed";
    case SurfaceBackdrop::None:    return "glass";  // glass without a named material (rare)
  }
  return "standard";
}

void SurfaceEcology::ReconcilePlaneMaterial(const std::string& id) {
  if constexpr (!kEnablePlaneMaterialCoherence) {
    return;  // disabled — members keep their own (opaque) spawn material
  }
  if (manager_ == nullptr) return;
  auto it = applied_appearance_.find(id);
  if (it == applied_appearance_.end()) return;

  // Only a MEMBER adopts the plane material; the root already defines it.
  const std::string root = composition_graph_.RootOfMember(id);
  if (root.empty()) return;
  auto rit = applied_appearance_.find(root);
  if (rit == applied_appearance_.end()) return;  // root not spawned through here

  // Adopt the root's glass (transparency + backdrop) so the member joins the same visual
  // field. Keep the member's own corners + shadow (shadow already None via F1).
  const bool changed =
      it->second.transparency_mode != rit->second.transparency_mode ||
      it->second.backdrop != rit->second.backdrop;
  if (!changed) return;

  it->second.transparency_mode = rit->second.transparency_mode;
  it->second.backdrop = rit->second.backdrop;
  if (SurfaceShell* shell = manager_->FindById(id)) {
    ApplyAppearance(shell->GetHandle(), it->second);  // preserves corners/shadow/overrides
    forensic::Log("PLANE MATERIAL",
                  "member " + id + " adopted root=" + root +
                      " glass (transparency=" + ToString(it->second.transparency_mode) + ")");
  }
}

}  // namespace morphic::policy

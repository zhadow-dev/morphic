#include "morphic_runtime.h"

#include <windows.h>

#include <string>

#include "churn_harness.h"
#include "experiment_config.h"
#include "fault_injector.h"
#include "forensic_trace.h"
#include "frame_clock.h"
#include "interaction_router.h"
#include "compositor/compositor_runtime.h"
#include "multisurface/orchestration_debugger.h"
#include "multisurface/surface_graph.h"
#include "presentation/presentation_coordinator.h"
#include "runtime_events.h"
#if defined(MORPHIC_SPATIAL)
#include "compositor_ng/projection_reconciler.h"
#include "compositor_ng/spatial_compositor.h"
#endif
#include "scene/scene_mirror.h"
#include "surface_policy/surface_ecology.h"
#include "workspace/composition_projector.h"
#include "surface_lifecycle_relay.h"  // M2.1D — surface identity + lifecycle projection
#include "surface_shell.h"  // M2.2 app-relay — SurfaceShell::messenger() per-surface push
#include "workspace/plane_activation_model.h"
#include "workspace/plane_visual_projector.h"
#include "workspace/relationship_gravity_controller.h"
#include "workspace/scene_zoom_controller.h"
#include "surface_model.h"
#include "surface_shell.h"
#include "validation/validation_harness.h"
#include <morphic/morphic_extension.h>  // PHASE 10.2 — DLL-exported extension handler

namespace {
// PHASE 7D — validation mode. PassiveTelemetry is the recommended default for
// development; flip to InteractiveStress to expose scenarios, FullTorture to
// auto-run them on startup, Disabled for production-style zero overhead.
// PHASE 8B.7 — InteractiveStress mode with auto-soak for 2h temporal gate.
// PHASE 10.2 — PassiveTelemetry for ecology usage testing. Auto-soak disabled.
constexpr ValidationMode kValidationMode = ValidationMode::PassiveTelemetry;

// PHASE 8B.7 — soak duration in minutes. 0 = no auto-soak.
constexpr double kSoakDurationMinutes = 0.0;

// PHASE 7C-T — chaos injection flags. Default off. Each maps to a 7C
// validation test (4A/4B/6A).
constexpr int  kFaultInject_DummySubscribers   = 0;
constexpr int  kFaultInject_SlowSubscriberMs   = 0;
constexpr bool kFaultInject_ReentrancyHook     = false;

// PHASE 10.2 — ecology surface ID counter.
static unsigned g_ecologyIdCounter = 1;
std::string NextEcologySurfaceId(const std::string& kind) {
  return kind + "_" + std::to_string(g_ecologyIdCounter++);
}
}  // namespace

MorphicRuntime::MorphicRuntime(const flutter::DartProject& project)
    : project_(project) {}

MorphicRuntime::~MorphicRuntime() {
  Destroy();
}

bool MorphicRuntime::Create() {
  forensic::Log("RUNTIME",
                "Create: event bus + surface model + frame clock + interaction router + surface manager");
  bus_ = std::make_unique<EventBus>();
  model_ = std::make_unique<SurfaceModel>();
  model_->SetEventBus(bus_.get());
  clock_ = std::make_unique<FrameClock>();  // PHASE 7C — runtime temporal authority

  // PHASE 9 — projection seam. The CompositorRuntime is the single "presented →
  // screen" authority (HWND backend today). Model + coordinator route their native
  // hand-off through it. Dumb projection router: no policy, no semantic handles.
  // kCompositorEnabled=false leaves model/coordinator with a null compositor → they
  // fall back to direct ApplyGeometry (byte-identical rollback to pre-9).
  if constexpr (CompositorRuntime::kCompositorEnabled) {
    compositor_ = std::make_unique<CompositorRuntime>();
    model_->SetCompositor(compositor_.get());
  }

  // PHASE 8B — graph must exist before the router so the router can take a
  // SurfaceGraph* to expand group members on BeginDrag. Graph self-subscribes
  // to bus lifecycle events; no external wiring needed.
  graph_ = std::make_unique<SurfaceGraph>(bus_.get());

  // PHASE 8E — presentation layer (settle-only). Read-only on model_, lazy-idle on
  // clock_; structurally incapable of mutating semantics (no graph/session/epoch).
  coordinator_ = std::make_unique<PresentationCoordinator>(model_.get(),
                                                           clock_.get());
  coordinator_->SetCompositor(compositor_.get());  // PHASE 9 — settle projects through seam

  router_ = std::make_unique<InteractionRouter>(
      model_.get(), bus_.get(), clock_.get(), graph_.get(), coordinator_.get());
  manager_ = std::make_unique<SurfaceManager>();

  manager_->SetModel(model_.get());
  manager_->SetRouter(router_.get());
  manager_->SetEventBus(bus_.get());

  // PHASE 10.1 — surface ecology (product policy layer ABOVE the runtime). Owns
  // the registry + workspace manager + policy; spawns route through it down into
  // the unchanged SurfaceManager::CreateSurface. The runtime never learns
  // SurfaceKind (sacred law: arrow points DOWN only).
  ecology_ = std::make_unique<morphic::policy::SurfaceEcology>(manager_.get(),
                                                               bus_.get());

  // MORPHIC NG · STAGE 1 — the retained SCENE GRAPH enters as a MIRROR of the
  // ecology truth (surfaces, planes, materials, corner radii) — observable in
  // the trace, zero authority. When a compositor backend exists, authority
  // migrates into this graph; the runtime below never learns it exists.
  scene_mirror_ = std::make_unique<morphic::scene::SceneMirror>(bus_.get(),
                                                                ecology_.get());

  // MORPHIC NG · STAGE 5 — the spatial compositor (V2 hybrid slice). Surfaces
  // spawned with backend='spatial' are adopted at first frame: engine HWND
  // parked offscreen, pixels captured + composited as a shaped GPU visual,
  // geometry-aware input routed back. Everything else stays pure native.
  // MORPHIC NG / PHASE 1 — ProjectionTruth seam: the reconciler decides where
  // native placement lands per projection (scene-authoritative surfaces move
  // their composited visual; engines park at the semantic translation). The
  // interceptor is OPAQUE to the runtime (groupability-predicate pattern).
#if defined(MORPHIC_SPATIAL)
  projection_reconciler_ =
      std::make_unique<morphic::ng::ProjectionReconciler>();
  if (compositor_) {
    compositor_->SetProjectionInterceptor(
        [r = projection_reconciler_.get()](SurfaceShell* s, const RECT& rect) {
          return r->Intercept(s, rect);
        });
  }

  spatial_compositor_ = std::make_unique<morphic::ng::SpatialCompositor>(
      bus_.get(), manager_.get(), ecology_.get(),
      projection_reconciler_.get());
#endif  // MORPHIC_SPATIAL

  // PHASE 10.5 Fix 2 — wire the groupability enforcement predicate. The graph (runtime)
  // holds only an OPAQUE callback; THIS seam supplies the policy: map each surface's
  // opaque id → registry descriptor → SurfacePolicy::IsGroupable. Both members must be
  // groupable; unknown surfaces (no descriptor) fail safe to NOT groupable. The runtime
  // never learns SurfaceKind (sacred law: arrow points DOWN only). Enforced at the
  // single chokepoint SurfaceGraph::Group, so docking inherits enforcement for free.
  graph_->SetGroupabilityPredicate(
      [this](SurfaceShell* a, SurfaceShell* b) -> bool {
        if (a == nullptr || b == nullptr) return false;
        const morphic::policy::SurfaceRegistry& reg = ecology_->registry();
        const morphic::policy::SurfaceDescriptor* da = reg.Get(a->id());
        const morphic::policy::SurfaceDescriptor* db = reg.Get(b->id());
        if (da == nullptr || db == nullptr) return false;  // unknown → not groupable
        return morphic::policy::SurfacePolicy::IsGroupable(da->kind) &&
               morphic::policy::SurfacePolicy::IsGroupable(db->kind);
      });

  // PHASE 12D — shared-drag movement projector (product layer; above the runtime). It
  // watches a composition plane root's drag and re-projects its composed members. Reads
  // ecology's composition graph; uses only public SurfaceModel / SurfaceManager — the
  // firewall holds (runtime stays composition-agnostic).
  composition_projector_ = std::make_unique<morphic::workspace::CompositionProjector>(
      model_.get(), manager_.get(), bus_.get(), &ecology_->composition_graph());

  // SPATIAL MIGRATION Stage 1 — plane activation. Derives which composition plane reads
  // "active" from runtime surface activation (observe-and-derive on the bus; projects
  // nothing yet). Product layer; the runtime stays plane-agnostic. VISUAL/SEMANTIC ONLY —
  // it never routes input (SPATIAL_RUNTIME_MIGRATION.md §4b).
  plane_activation_ = std::make_unique<morphic::workspace::PlaneActivationModel>(
      &ecology_->composition_graph(), bus_.get());

  // SPATIAL MIGRATION Stage 2A — content-level plane visual projection. Pushes per-surface
  // active/inactive dim state to each surface's Flutter content on activation change. Reads
  // PlaneActivationModel truth + enumerates surfaces via the manager; VISUAL ONLY (§4b).
  plane_visual_ = std::make_unique<morphic::workspace::PlaneVisualProjector>(
      plane_activation_.get(), manager_.get(),
      // material_of: surface id → material token ("standard"/"mica"/"acrylic"/"tabbed").
      // Opaque callback (the runtime/projector never learns SurfaceKind/appearance types —
      // firewall holds).
      [this](const std::string& id) -> std::string {
        return ecology_ ? ecology_->GetSurfaceMaterial(id) : std::string("standard");
      });

  // M2.3G — Relationship Gravity (product layer). Reposition a plane root's composed companions into
  // top-right contextual slots when it maximizes; restore on un-maximize. Reads ecology's composition
  // graph + public model_/manager_; the runtime stays composition-agnostic. Driven by the generic
  // window-state hook below.
  gravity_ = std::make_unique<morphic::workspace::RelationshipGravityController>(
      &ecology_->composition_graph(), model_.get(), manager_.get());

  // M2.8.3 — scene zoom (camera-move composition reprojection; sibling of gravity). Also takes the
  // PresentationCoordinator so the transition can EASE via the existing settle seam (Phase B animation).
  scene_zoom_ = std::make_unique<morphic::workspace::SceneZoomController>(
      &ecology_->composition_graph(), model_.get(), manager_.get(), coordinator_.get());

  // M2.1D — surface identity + lifecycle projection. Bridges generic runtime events
  // (SurfaceCreated/Destroyed/Ready) onto the app bus as opaque {surfaceId} facts. Constructed
  // BEFORE any surface spawns so it catches the launcher's first SurfaceReady. Generic-only —
  // the runtime never learns app meaning (the relay layer keeps runtime↔app separation honest).
  lifecycle_relay_ =
      std::make_unique<SurfaceLifecycleRelay>(bus_.get(), manager_.get());

  // PHASE 10.2 — register the ecology extension handler so the Dart-side
  // EcologyController (using the 'morphic' channel) can spawn/destroy/query
  // surfaces through the product-layer ecology. The handler takes ownership
  // of `result` and must always respond.
  {
    auto* ecology = ecology_.get();
    auto* project_ptr = &project_;
    auto* manager = manager_.get();  // M2.2 app-relay — enumerate surfaces to broadcast to
    auto* model = model_.get();      // M2.3E — native window controls route through geometry authority
    auto* scene_zoom = scene_zoom_.get();  // M2.8.3 — scene zoom (camera move) for spatial scenes
    morphic::SetMorphicExtensionHandler(
        [ecology, project_ptr, manager, model, scene_zoom](
            const std::string& method,
            const flutter::EncodableValue* args,
            std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
          using morphic::policy::SurfaceKind;
          const auto* map = std::get_if<flutter::EncodableMap>(args);

          if (method == "ecology.spawn" && map) {
            auto kindIt = map->find(flutter::EncodableValue("kind"));
            if (kindIt == map->end()) {
              result->Error("MISSING_KIND", "kind argument required");
              return;
            }
            const auto kind_str = std::get<std::string>(kindIt->second);

            // Map string to SurfaceKind + entrypoint.
            SurfaceKind kind;
            std::string entrypoint;
            if (kind_str == "workspace") {
              kind = SurfaceKind::Workspace;
              entrypoint = "workspaceMain";
            } else if (kind_str == "tool_palette") {
              kind = SurfaceKind::ToolPalette;
              entrypoint = "toolPaletteMain";
            } else if (kind_str == "inspector") {
              kind = SurfaceKind::Inspector;
              entrypoint = "inspectorMain";
            } else if (kind_str == "overlay") {
              kind = SurfaceKind::Overlay;
              entrypoint = "overlayMain";
            } else {
              result->Error("UNKNOWN_KIND", "Unknown surface kind: " + kind_str);
              return;
            }

            // SECOND-APP ENABLER (M2.2 friction #1) — decouple CONTENT (the Dart entrypoint)
            // from BEHAVIOR (SurfaceKind). The kind→entrypoint above is only the DEFAULT; an app
            // may override the entrypoint to run ITS OWN Dart content in a surface of that kind,
            // with NO C++ edit. The named function must be a @pragma('vm:entry-point') exported
            // from the app's main.dart (so it resolves by name + survives tree-shaking). The
            // runtime never sees this — kind still drives all policy; entrypoint is pure content.
            if (auto it = map->find(flutter::EncodableValue("entrypoint"));
                it != map->end() &&
                std::holds_alternative<std::string>(it->second)) {
              entrypoint = std::get<std::string>(it->second);
            }

            // Extract position/size.
            auto getInt = [&](const char* key, int def) -> int {
              auto it = map->find(flutter::EncodableValue(std::string(key)));
              if (it == map->end()) return def;
              if (auto* v32 = std::get_if<int32_t>(&it->second)) return *v32;
              if (auto* v64 = std::get_if<int64_t>(&it->second)) return static_cast<int>(*v64);
              return def;
            };
            int x = getInt("x", 200);
            int y = getInt("y", 200);
            int w = getInt("width", 400);
            int h = getInt("height", 300);

            // Extract optional parent.
            std::optional<std::string> parent_id;
            auto parentIt = map->find(flutter::EncodableValue("parentId"));
            if (parentIt != map->end() &&
                std::holds_alternative<std::string>(parentIt->second)) {
              parent_id = std::get<std::string>(parentIt->second);
            }

            // PHASE 11/12 — optional per-surface appearance + composition overrides.
            // Strings/bools map to plain policy enums; any omitted field falls back to
            // the kind's policy default. The runtime never sees these — they go to the
            // product-layer ecology only.
            morphic::policy::SpawnOverrides overrides;
            if (auto it = map->find(flutter::EncodableValue("corners"));
                it != map->end() &&
                std::holds_alternative<std::string>(it->second)) {
              const auto& cs = std::get<std::string>(it->second);
              if (cs == "rounded")
                overrides.corners = morphic::policy::SurfaceCornerStyle::Rounded;
              else if (cs == "small")
                overrides.corners = morphic::policy::SurfaceCornerStyle::SmallRounded;
              else if (cs == "square")
                overrides.corners = morphic::policy::SurfaceCornerStyle::Square;
              else if (cs == "default")
                overrides.corners = morphic::policy::SurfaceCornerStyle::Default;
            }
            if (auto it = map->find(flutter::EncodableValue("shadow"));
                it != map->end() && std::holds_alternative<bool>(it->second)) {
              overrides.shadow = std::get<bool>(it->second);
            }
            if (auto it = map->find(flutter::EncodableValue("backdrop"));
                it != map->end() &&
                std::holds_alternative<std::string>(it->second)) {
              const auto& bd = std::get<std::string>(it->second);
              if (bd == "mica")
                overrides.backdrop = morphic::policy::SurfaceBackdrop::Mica;
              else if (bd == "acrylic")
                overrides.backdrop = morphic::policy::SurfaceBackdrop::Acrylic;
              else if (bd == "tabbed")
                overrides.backdrop = morphic::policy::SurfaceBackdrop::Tabbed;
              else if (bd == "none")
                overrides.backdrop = morphic::policy::SurfaceBackdrop::None;
            }
            if (auto it = map->find(flutter::EncodableValue("transparency"));
                it != map->end() &&
                std::holds_alternative<std::string>(it->second)) {
              const auto& tm = std::get<std::string>(it->second);
              using TM = morphic::policy::SurfaceTransparencyMode;
              if (tm == "opaque") overrides.transparency_mode = TM::Opaque;
              else if (tm == "titlebar") overrides.transparency_mode = TM::TitlebarMaterial;
              else if (tm == "transparent" || tm == "transparent_content")
                overrides.transparency_mode = TM::TransparentContent;
              else if (tm == "full_glass" || tm == "fullglass")
                overrides.transparency_mode = TM::FullGlass;
              else if (tm == "hybrid") overrides.transparency_mode = TM::Hybrid;
            }
            if (auto it = map->find(flutter::EncodableValue("composed"));
                it != map->end() && std::holds_alternative<bool>(it->second)) {
              overrides.composition_mode =
                  std::get<bool>(it->second)
                      ? morphic::policy::SurfaceCompositionMode::Composed
                      : morphic::policy::SurfaceCompositionMode::Floating;
            }
            // SPATIAL CHROME — chromeless (no native strip/borders, full-bleed
            // content) + arbitrary cornerRadius (window-region clip).
            if (auto it = map->find(flutter::EncodableValue("chromeless"));
                it != map->end() && std::holds_alternative<bool>(it->second)) {
              overrides.chromeless = std::get<bool>(it->second);
            }
            if (auto it = map->find(flutter::EncodableValue("cornerRadius"));
                it != map->end()) {
              if (auto* v32 = std::get_if<int32_t>(&it->second))
                overrides.corner_radius_px = *v32;
              else if (auto* v64 = std::get_if<int64_t>(&it->second))
                overrides.corner_radius_px = static_cast<int>(*v64);
            }
            // MORPHIC NG Stage 2/5 -- declared backend ("native"|"spatial").
            if (auto it = map->find(flutter::EncodableValue("backend"));
                it != map->end() &&
                std::holds_alternative<std::string>(it->second)) {
              const auto& b = std::get<std::string>(it->second);
              if (b == "native" || b == "spatial") overrides.backend = b;
            }
            // MORPHIC NG Phase 3 -- spatial material spec (compositor-level).
            if (auto it = map->find(flutter::EncodableValue("shape"));
                it != map->end() &&
                std::holds_alternative<std::string>(it->second)) {
              overrides.shape = std::get<std::string>(it->second);
            }
            if (auto it = map->find(flutter::EncodableValue("material"));
                it != map->end() &&
                std::holds_alternative<std::string>(it->second)) {
              overrides.material = std::get<std::string>(it->second);
            }
            if (auto it = map->find(flutter::EncodableValue("materialTint"));
                it != map->end()) {
              if (auto* v32 = std::get_if<int32_t>(&it->second))
                overrides.material_tint_argb = static_cast<unsigned int>(*v32);
              else if (auto* v64 = std::get_if<int64_t>(&it->second))
                overrides.material_tint_argb =
                    static_cast<unsigned int>(*v64 & 0xFFFFFFFF);
            }
            if (auto it = map->find(flutter::EncodableValue("elevation"));
                it != map->end()) {
              if (auto* v32 = std::get_if<int32_t>(&it->second))
                overrides.elevation_px = *v32;
              else if (auto* v64 = std::get_if<int64_t>(&it->second))
                overrides.elevation_px = static_cast<int>(*v64);
            }

            std::string id = NextEcologySurfaceId(kind_str);
            auto spawned = ecology->SpawnSurface(
                *project_ptr, kind, id, entrypoint, x, y, w, h,
                "", std::move(parent_id), overrides);

            if (spawned.has_value()) {
              flutter::EncodableMap rm;
              rm[flutter::EncodableValue("id")] =
                  flutter::EncodableValue(*spawned);
              result->Success(flutter::EncodableValue(rm));
            } else {
              result->Error("SPAWN_REJECTED", "Policy rejected spawn or runtime failed");
            }
            return;
          }

          // M2.2 friction #2 — generic, §4b-SAFE app-data relay. Forwards an opaque
          // {topic,payload} to EVERY live surface's 'morphic/app' channel. The runtime is a DUMB
          // transport: it NEVER reads topic/payload, NEVER touches activation/input/focus/
          // topology. App surfaces (separate engines) coordinate CONTENT over this; the runtime
          // stays app-agnostic (like ecology.* + the firewall). The spine of cross-surface
          // coordination / AppSession — NOT input routing (that stays HWND-native, §4b).
          if (method == "app.broadcast" && map && manager) {
            for (SurfaceShell* s : manager->Surfaces()) {
              if (s == nullptr) continue;
              flutter::BinaryMessenger* messenger = s->messenger();
              if (messenger == nullptr) continue;
              flutter::MethodChannel<flutter::EncodableValue> ch(
                  messenger, "morphic/app",
                  &flutter::StandardMethodCodec::GetInstance());
              ch.InvokeMethod("app.event",
                              std::make_unique<flutter::EncodableValue>(*args));
            }
            result->Success(flutter::EncodableValue(true));
            return;
          }

          // M2.2E — generic, USER-DRIVEN activation request: bring surface {surfaceId} to the OS
          // foreground (a command palette focusing itself on open, or switching to a doc's editor).
          // §4b HOLDS: the RUNTIME owns activation — SetForegroundWindow → WM_ACTIVATE →
          // RequestActivate runs the existing pipeline; the app only RELAYS a user's explicit
          // "go there" intent (Ctrl+K / Enter), exactly like a taskbar click. It never routes the
          // keyboard itself; the OS foreground does. Generic (opaque surfaceId), not app-aware.
          if (method == "surface.activate" && map && manager) {
            auto it = map->find(flutter::EncodableValue("surfaceId"));
            if (it != map->end() &&
                std::holds_alternative<std::string>(it->second)) {
              if (SurfaceShell* s =
                      manager->FindById(std::get<std::string>(it->second))) {
                if (HWND h = s->GetHandle()) SetForegroundWindow(h);
              }
            }
            result->Success(flutter::EncodableValue(true));
            return;
          }

          // M2.2E.1 — TRANSIENT ACTIVATION HANDOFF (ordered, atomic). A transient overlay (the
          // command palette) switching to a target doc: foreground the TARGET first, THEN destroy
          // the transient — in ONE runtime op, in this order. Because the transient is no longer
          // the foreground window when it dies, Windows performs NO restore-to-previous, which is
          // the s1->s2 race the palette hit (two separate activate+destroy calls let the OS pick
          // the restore target). Correctness comes from ORDER, not delays/refocus-loops/retries.
          // §4b holds: still only user-driven activation + destroy; the runtime owns the ordering,
          // never the keyboard. Generic (opaque ids) — narrow to the palette workflow for now.
          if (method == "surface.handoff" && map && manager) {
            std::string activate_id, close_id;
            if (auto it = map->find(flutter::EncodableValue("activate"));
                it != map->end() &&
                std::holds_alternative<std::string>(it->second))
              activate_id = std::get<std::string>(it->second);
            if (auto it = map->find(flutter::EncodableValue("close"));
                it != map->end() &&
                std::holds_alternative<std::string>(it->second))
              close_id = std::get<std::string>(it->second);
            if (!activate_id.empty()) {
              if (SurfaceShell* s = manager->FindById(activate_id)) {
                if (HWND h = s->GetHandle()) SetForegroundWindow(h);
              }
            }
            forensic::Log("HANDOFF",
                          "activate=" + activate_id + " then close=" + close_id);
            if (!close_id.empty()) ecology->DestroySurface(close_id);
            result->Success(flutter::EncodableValue(true));
            return;
          }

          // M2.3E — NATIVE WINDOW CONTROLS (generic, surface-local). Minimize/maximize/restore/
          // toggleMaximize route into the SurfaceModel (the geometry authority — semantic maximize,
          // native minimize); close routes through the ecology so the ownership cascade still fires.
          // All take an opaque {surfaceId}; window-state is pushed back via SetOnWindowStateChanged.
          if ((method == "surface.minimize" || method == "surface.maximize" ||
               method == "surface.restore" || method == "surface.toggleMaximize") &&
              map && manager && model) {
            auto it = map->find(flutter::EncodableValue("surfaceId"));
            if (it != map->end() &&
                std::holds_alternative<std::string>(it->second)) {
              if (SurfaceShell* s =
                      manager->FindById(std::get<std::string>(it->second))) {
                if (method == "surface.minimize") model->Minimize(s);
                else if (method == "surface.maximize") model->Maximize(s);
                else if (method == "surface.restore") model->Restore(s);
                else model->ToggleMaximize(s);
              }
            }
            result->Success(flutter::EncodableValue(true));
            return;
          }

          // SPATIAL CHROME — corner-radius property channel. The radius is a
          // policy-layer surface property (SurfaceEcology); the surface's own
          // engine shapes the window via content alpha (a region/DWM native
          // projection is impossible — see SurfaceAppearance::corner_radius_px).
          //   surface.chrome.get      {surfaceId} → {cornerRadius}
          //   surface.setCornerRadius {surfaceId, radius} → store + push
          //                           'surface.chrome' to that surface's engine
          if (method == "surface.chrome.get" && map && ecology) {
            int radius = 0;
            if (auto it = map->find(flutter::EncodableValue("surfaceId"));
                it != map->end() &&
                std::holds_alternative<std::string>(it->second)) {
              radius = ecology->GetCornerRadius(std::get<std::string>(it->second));
            }
            flutter::EncodableMap rm;
            rm[flutter::EncodableValue("cornerRadius")] =
                flutter::EncodableValue(radius);
            result->Success(flutter::EncodableValue(rm));
            return;
          }
          if (method == "surface.setCornerRadius" && map && ecology && manager) {
            std::string sid;
            if (auto it = map->find(flutter::EncodableValue("surfaceId"));
                it != map->end() &&
                std::holds_alternative<std::string>(it->second)) {
              sid = std::get<std::string>(it->second);
            }
            int radius = 0;
            if (auto it = map->find(flutter::EncodableValue("radius"));
                it != map->end()) {
              if (auto* v32 = std::get_if<int32_t>(&it->second)) radius = *v32;
              else if (auto* v64 = std::get_if<int64_t>(&it->second))
                radius = static_cast<int>(*v64);
            }
            bool ok = !sid.empty() && ecology->SetCornerRadius(sid, radius);
            // Push the new radius to THAT surface's engine so its content
            // re-shapes live (same per-engine 'morphic/app' push the relay and
            // app.broadcast use; data-only, §4b-safe).
            if (ok) {
              if (SurfaceShell* s = manager->FindById(sid)) {
                if (flutter::BinaryMessenger* messenger = s->messenger()) {
                  flutter::EncodableMap payload;
                  payload[flutter::EncodableValue("surfaceId")] =
                      flutter::EncodableValue(sid);
                  payload[flutter::EncodableValue("cornerRadius")] =
                      flutter::EncodableValue(radius);
                  flutter::EncodableMap event;
                  event[flutter::EncodableValue("topic")] =
                      flutter::EncodableValue(std::string("surface.chrome"));
                  event[flutter::EncodableValue("payload")] =
                      flutter::EncodableValue(payload);
                  flutter::MethodChannel<flutter::EncodableValue> ch(
                      messenger, "morphic/app",
                      &flutter::StandardMethodCodec::GetInstance());
                  ch.InvokeMethod("app.event",
                                  std::make_unique<flutter::EncodableValue>(
                                      flutter::EncodableValue(event)));
                }
              }
            }
            result->Success(flutter::EncodableValue(ok));
            return;
          }

          // SPATIAL CHROME — surface-local HIDE. The boot-shell root of a
          // dissolving spatial app hides itself the moment its engine is live
          // (before spawning the scene), so no root chip ever lingers on
          // screen. Pure native visibility (ShowWindow SW_HIDE) — not a
          // semantic state: the surface stays in the model and is closed (or
          // re-shown by an OS restore) later. Generic + kind-agnostic.
          if (method == "surface.hide" && map && manager) {
            auto it = map->find(flutter::EncodableValue("surfaceId"));
            if (it != map->end() &&
                std::holds_alternative<std::string>(it->second)) {
              if (SurfaceShell* s =
                      manager->FindById(std::get<std::string>(it->second))) {
                ShowWindow(s->GetHandle(), SW_HIDE);
                forensic::Log("SHELL", "surface.hide id=" + s->id());
              }
            }
            result->Success(flutter::EncodableValue(true));
            return;
          }

          // M2.8.3 — SCENE ZOOM (spatial scenes only; the camera-move replacement for "maximize").
          // Reprojects the WHOLE composition plane proportionally (anchor + companions together),
          // ONE-SHOT, from the authored snapshot — never live geometry. Opaque {surfaceId, zoom}; the
          // product-layer controller resolves the caller's plane root + owns all the geometry.
          if (method == "scene.zoom" && map && scene_zoom) {
            double zoom = 1.0;
            if (auto zit = map->find(flutter::EncodableValue("zoom"));
                zit != map->end() && std::holds_alternative<double>(zit->second))
              zoom = std::get<double>(zit->second);
            if (auto it = map->find(flutter::EncodableValue("surfaceId"));
                it != map->end() && std::holds_alternative<std::string>(it->second))
              scene_zoom->ZoomScene(std::get<std::string>(it->second), zoom);
            result->Success(flutter::EncodableValue(true));
            return;
          }
          if (method == "scene.resetZoom" && map && scene_zoom) {
            if (auto it = map->find(flutter::EncodableValue("surfaceId"));
                it != map->end() && std::holds_alternative<std::string>(it->second))
              scene_zoom->ResetScene(std::get<std::string>(it->second));
            result->Success(flutter::EncodableValue(true));
            return;
          }
          if (method == "surface.close" && map && ecology) {
            auto it = map->find(flutter::EncodableValue("surfaceId"));
            if (it != map->end() &&
                std::holds_alternative<std::string>(it->second)) {
              ecology->DestroySurface(std::get<std::string>(it->second));  // cascade-aware
            }
            result->Success(flutter::EncodableValue(true));
            return;
          }

          if (method == "ecology.destroy" && map) {
            auto idIt = map->find(flutter::EncodableValue("id"));
            if (idIt == map->end()) {
              result->Error("MISSING_ID", "id argument required");
              return;
            }
            const auto id = std::get<std::string>(idIt->second);
            bool ok = ecology->DestroySurface(id);
            result->Success(flutter::EncodableValue(ok));
            return;
          }

          if (method == "ecology.summary") {
            auto summary = ecology->GetSummary();
            flutter::EncodableMap rm;

            // Surfaces.
            flutter::EncodableList surfaces;
            for (const auto& s : summary.surfaces) {
              flutter::EncodableMap sm;
              sm[flutter::EncodableValue("id")] = flutter::EncodableValue(s.id);
              sm[flutter::EncodableValue("kind")] = flutter::EncodableValue(s.kind);
              sm[flutter::EncodableValue("workspace")] = flutter::EncodableValue(s.workspace_id);
              sm[flutter::EncodableValue("parent")] = flutter::EncodableValue(s.parent);
              sm[flutter::EncodableValue("detachable")] = flutter::EncodableValue(s.detachable);
              sm[flutter::EncodableValue("groupable")] = flutter::EncodableValue(s.groupable);
              sm[flutter::EncodableValue("focusable")] = flutter::EncodableValue(s.focusable);
              sm[flutter::EncodableValue("persistent")] = flutter::EncodableValue(s.persistent);
              sm[flutter::EncodableValue("followsParent")] = flutter::EncodableValue(s.follows_parent);
              surfaces.push_back(flutter::EncodableValue(sm));
            }
            rm[flutter::EncodableValue("surfaces")] = flutter::EncodableValue(surfaces);

            // Workspaces.
            flutter::EncodableList workspaces;
            for (const auto& w : summary.workspaces) {
              flutter::EncodableMap wm;
              wm[flutter::EncodableValue("id")] = flutter::EncodableValue(w.id);
              wm[flutter::EncodableValue("title")] = flutter::EncodableValue(w.title);
              flutter::EncodableList sids;
              for (const auto& sid : w.surface_ids) {
                sids.push_back(flutter::EncodableValue(sid));
              }
              wm[flutter::EncodableValue("surfaces")] = flutter::EncodableValue(sids);
              workspaces.push_back(flutter::EncodableValue(wm));
            }
            rm[flutter::EncodableValue("workspaces")] = flutter::EncodableValue(workspaces);
            rm[flutter::EncodableValue("currentWorkspace")] =
                flutter::EncodableValue(summary.current_workspace);

            result->Success(flutter::EncodableValue(rm));
            return;
          }

          // Unhandled ecology method.
          result->NotImplemented();
        });
    forensic::Log("ECOLOGY", "extension handler registered for ecology.* methods");
  }

  // PHASE 8A — orchestration debugger needs the validation forensic logger
  // for structured output; wire it after the harness is up so the trace
  // stream is available (passed below).

  // PHASE 7D — ValidationHarness wires telemetry / forensic JSON streams /
  // tick monitor / projection auditor / integrity auditor / simulator /
  // scenarios based on kValidationMode. Constructed AFTER manager_ so it can
  // capture a manager pointer (scenarios need it to look up surfaces).
  harness_ = std::make_unique<ValidationHarness>(kValidationMode, clock_.get(),
                                                  bus_.get(), model_.get(),
                                                  router_.get(), manager_.get(),
                                                  graph_.get());

  // PHASE 8A — orchestration debugger. Passes nullptr for the structured-trace
  // logger today (forensic::Log alone is sufficient for 8A; a dedicated
  // orchestration_trace.ndjson can be added to ValidationHarness in a follow-on
  // once 8B/8C/8D produce events worth structured ingestion).
  orchestration_debugger_ = std::make_unique<OrchestrationDebugger>(
      bus_.get(), graph_.get(), router_.get(), nullptr);

  // PHASE 7C-T — orthogonal chaos injection (not validation observability).
  // `if constexpr` so MSVC /WX doesn't flag compile-time-constant conditions.
  faults_ = std::make_unique<FaultInjector>(clock_.get(), bus_.get(),
                                            model_.get(), router_.get());
  if constexpr (kFaultInject_DummySubscribers > 0) {
    faults_->InstallDummySubscribers(kFaultInject_DummySubscribers);
  }
  if constexpr (kFaultInject_SlowSubscriberMs > 0) {
    faults_->InstallSlowSubscriber(kFaultInject_SlowSubscriberMs);
  }
  if constexpr (kFaultInject_ReentrancyHook) {
    faults_->InstallReentrancyHook();
  }

  // LIFECYCLE — TIER-SPECIFIC lifetime ownership (the native and spatial runtimes
  // are genuinely different shapes; do NOT force identical semantics).
  manager_->SetOnEmpty([]() {
#if defined(MORPHIC_SPATIAL)
    // SPATIAL tier: workspace lifetime is owned by the shell_root (its WM_CLOSE
    // terminates the loop). An empty surface set is a VALID empty workspace (you
    // can spawn into it), NOT app death — so a render_source destruction must
    // NEVER quit. (See doc/WINDOW_TOPOLOGY.md; this retires the pre-inversion
    // "surface == engine == app lifetime" assumption.)
    forensic::Log("LIFETIME",
                  "workspace empty — valid (spatial: shell_root owns lifetime); "
                  "NO quit");
#else
    // NATIVE tier: there is NO shell_root and no detached workspace identity. The
    // canonical root is the EcologyLauncher running the app's main(); every
    // surface is a sovereign HWND. The honest native contract is the classic
    // desktop one: when the LAST surface closes, the app exits. Robust to
    // dissolving-root apps (their scene surfaces hold the count > 0 until all
    // close — matching the dissolveRootIntoScene "closing them all quits cleanly"
    // design). PostQuitMessage exits the loop -> main()'s R0 abrupt exit.
    forensic::Log("LIFETIME",
                  "all surfaces closed (native tier) -> PostQuitMessage (app exit)");
    PostQuitMessage(0);
#endif
  });

  // PHASE 11 — churn/leak stress harness (env-armed via MORPHIC_CHURN). Drives
  // spawn/destroy + samples resources from the same frame tick (UI-thread-safe).
  morphic::experiment::LogConfig();
  if (morphic::experiment::Get().churn_armed) {
    churn_ = std::make_unique<morphic::ChurnHarness>(&project_, ecology_.get(),
                                                     /*dxgi_adapter3=*/nullptr);
  }

  // MEMORY — reap closed surfaces on the frame tick. Closing a surface defers
  // its SurfaceShell to the graveyard (it can't be freed inside its own
  // WM_DESTROY); ~SurfaceShell releases the FlutterViewController — the whole
  // Flutter ENGINE (tens of MB). Reap() previously ran ONLY on the next spawn,
  // so closed surfaces' engines leaked (zombie memory) until you created one.
  // The clock tick is a safe point OUTSIDE the WM_DESTROY stack; Reap is a cheap
  // no-op when the graveyard is empty.
  if (clock_) {
    clock_->Subscribe([this](double) {
      if (manager_) {
        manager_->TickTeardown();  // paced/quiesced deferred engine teardown
        manager_->Reap();          // legacy graveyard path (non-deferred destroys)
      }
      if (churn_) churn_->Tick();
    });
  }

  // Observability: log every runtime event (CORE_HARDENING §6).
  bus_->Subscribe([](RuntimeEvent event, SurfaceShell* surface) {
    forensic::Log("EVENT", std::string(ToString(event)) +
                               (surface ? " id=" + surface->id()
                                        : std::string()));
  });

  // PHASE 9 — compositor visual-map maintenance. Drop a surface's projection
  // record when it's destroyed (just erases a map key — no deref of the dying
  // pointer, safe). Surfaces are ADDED lazily by the first Project call.
  bus_->Subscribe([this](RuntimeEvent event, SurfaceShell* surface) {
    if (event == RuntimeEvent::SurfaceDestroyed && compositor_) {
      compositor_->OnSurfaceRemoved(surface);
    }
  });

  // M2.3E — project window state ({maximized,minimized}) to a surface's content whenever it changes
  // (a channel command OR a titlebar double-click). Pushed to ONLY that surface's morphic/app channel
  // as a surface.windowState app event, so MorphicSurface.onWindowState hears its own.
  model_->SetOnWindowStateChanged([this](SurfaceShell* s) {
    if (s == nullptr) return;
    flutter::BinaryMessenger* messenger = s->messenger();
    if (messenger == nullptr) return;
    flutter::EncodableMap payload;
    payload[flutter::EncodableValue("surfaceId")] = flutter::EncodableValue(s->id());
    payload[flutter::EncodableValue("maximized")] =
        flutter::EncodableValue(model_->IsSurfaceMaximized(s));
    payload[flutter::EncodableValue("minimized")] =
        flutter::EncodableValue(model_->IsSurfaceMinimized(s));
    flutter::EncodableMap event;
    event[flutter::EncodableValue("topic")] =
        flutter::EncodableValue(std::string("surface.windowState"));
    event[flutter::EncodableValue("payload")] = flutter::EncodableValue(payload);
    flutter::MethodChannel<flutter::EncodableValue> ch(
        messenger, "morphic/app", &flutter::StandardMethodCodec::GetInstance());
    ch.InvokeMethod("app.event", std::make_unique<flutter::EncodableValue>(event));
    // M2.3G — same GENERIC hook drives relationship gravity. The runtime only knows "window state
    // changed"; the product controller decides everything spatial (roots-only, companion slots).
    if (gravity_) gravity_->OnWindowState(s);
  });

  // PHASE 10.2/10.3 — spawn the LAUNCHER as a GLOBAL meta-control surface
  // (SurfaceKind::EcologyLauncher). It belongs to NO workspace, is never grouped /
  // detached / workspace-activated, and is hidden from Alt+Tab/taskbar
  // (WS_EX_TOOLWINDOW). This fixes the 10.2 mismatch where the launcher behaved
  // workspace-owned (dragging a workspace dragged the launcher). It runs the
  // 'main' entrypoint; all other surfaces spawn on-demand through the ecology.
  using morphic::policy::SurfaceKind;
  // M2.3E — the root/launcher is a SMALL corner presence chip (was a 420x640 slab that read as a
  // broken/forgotten window). Tiny window = ambient app-presence, not a panel. (Truly invisible =
  // the deferred HiddenRootEngine host-lifetime work.)
  if (!ecology_->SpawnSurface(project_, SurfaceKind::EcologyLauncher, "launcher",
                              "main", 24, 24, 200, 52)) {
    return false;
  }

  model_->ReconcileZOrder();  // project initial semantic z onto native HWND order
  forensic::Log("RUNTIME", "Create: launcher surface up; initial z-order reconciled");
  ecology_->LogSummary();     // PHASE 10.1 — prove descriptors/workspaces wired

  // PHASE 8B.7 — auto-start soak if configured. `if constexpr` on the compile-time
  // duration so MSVC /WX doesn't flag the constant condition (C4127) when soak is
  // disabled (kSoakDurationMinutes == 0); the dead branch is discarded entirely.
  if constexpr (kSoakDurationMinutes > 0.0) {
   if (harness_) {
    // Delay soak start so surfaces have time to render their first frame.
    // The soak runner subscribes to the clock and self-manages timing.
    forensic::Log("RUNTIME", "Auto-soak scheduled: " +
                                 std::to_string(static_cast<int>(kSoakDurationMinutes)) +
                                 " minutes");
    // Start soak on the next clock tick (surfaces are created + visible).
    // One-shot: unsubscribes itself after the first fire.
    soak_start_token_ = clock_->Subscribe([this](double) {
      if (soak_start_token_ != 0) {
        const int t = soak_start_token_;
        soak_start_token_ = 0;
        clock_->Unsubscribe(t);
        harness_->StartSoak(kSoakDurationMinutes);
      }
    });
   }
  }

  return true;
}

void MorphicRuntime::Destroy() {
  // PHASE 7D/8A destruction order:
  //   1. harness->FinalizeSession  — write session_summary.json while every
  //      telemetry counter and ref-bag is still live.
  //   2. manager  — fires SurfaceDestroyed events that ALL subscribers (graph,
  //      harness, debugger) must still be able to handle (all alive).
  //   3. faults   — unsubscribes from clock, disarms model test callback.
  //   4. orchestration_debugger / graph / harness — observers that captured
  //      `this` in bus lambdas; tear down after the manager so the lifecycle
  //      events have completed firing.
  //   5. router, clock, model, bus — runtime core, reverse construction order.
  if (harness_) {
    harness_->FinalizeSession("normal shutdown");
  }
  // PHASE 8B.7 — clean up soak one-shot if it never fired.
  if (clock_ && soak_start_token_ != 0) {
    clock_->Unsubscribe(soak_start_token_);
    soak_start_token_ = 0;
  }
  manager_.reset();
  // PHASE 10.5 Fix 2 — un-wire the groupability predicate (it captures ecology_) BEFORE
  // ecology_ is destroyed, mirroring the SetCompositor(nullptr) defense below. No
  // Group() call happens during teardown today (router_ and its epochs are torn down
  // later), so this makes the dangling-capture impossibility structural, not incidental.
  if (graph_) graph_->SetGroupabilityPredicate(nullptr);
  // M2.3G — relationship gravity reads ecology's composition graph + model_/manager_; reset before
  // ecology_ (no bus subscription; holds only raw pointers, its dtor touches nothing).
  // MORPHIC NG Stage 5 — the spatial compositor reads ecology/manager and
  // subscribes to bus_; reset first (closes capture sessions + host windows).
#if defined(MORPHIC_SPATIAL)
  spatial_compositor_.reset();
  // MORPHIC NG Phase 1 — detach the projection interceptor BEFORE the
  // reconciler dies (projections during teardown then pass through).
  if (compositor_) compositor_->SetProjectionInterceptor(nullptr);
  projection_reconciler_.reset();
#endif  // MORPHIC_SPATIAL
  // MORPHIC NG Stage 1 — the scene mirror reads ecology and subscribes to bus_;
  // reset before ecology_ while bus_ still exists (it unsubscribes in its dtor).
  scene_mirror_.reset();
  scene_zoom_.reset();  // M2.8.3 — reset before ecology_ (reads its composition graph), like gravity_
  gravity_.reset();
  // PHASE 12D — destroy the composition projector BEFORE ecology_ (it reads ecology's
  // composition graph) and while bus_/model_ still exist (it unsubscribes in its dtor).
  composition_projector_.reset();
  // SPATIAL MIGRATION Stage 2A — destroy the visual projector BEFORE plane_activation_ (it
  // holds a callback registered on the model and detaches it in its dtor).
  plane_visual_.reset();
  // M2.1D — lifecycle relay unsubscribes from bus_ in its dtor; reset while bus_/manager_ exist.
  lifecycle_relay_.reset();
  // SPATIAL MIGRATION Stage 1 — same ordering: plane activation reads ecology's graph and
  // unsubscribes from bus_ in its dtor; reset before ecology_ while bus_ still exists.
  plane_activation_.reset();
  // PHASE 10.1 — ecology after manager_ (so its registry handled the
  // SurfaceDestroyed events manager_ fired) and before bus_ (its registry
  // unsubscribes from bus_ in its dtor). Holds raw manager_ (now gone) but its
  // dtor never calls into the manager.
  ecology_.reset();
  // PHASE 10.2 — clear the ecology extension handler (ecology_ is now gone).
  morphic::ClearMorphicExtensionHandler();
  faults_.reset();
  orchestration_debugger_.reset();
  harness_.reset();
  router_.reset();   // router holds raw graph_ + coordinator_ — destroy router first
  coordinator_.reset();  // PHASE 8E — after router, before clock_ (unsubscribes) + model_
  // PHASE 9 — compositor after its consumers (router/coordinator) and before
  // model_ stops being used. model_ holds a raw compositor_ but its dtor never
  // projects, so resetting compositor_ before model_ is safe. Clear the model's
  // pointer first defensively so no late projection touches a freed compositor.
  if (model_) model_->SetCompositor(nullptr);
  compositor_.reset();
  graph_.reset();
  clock_.reset();
  model_.reset();
  bus_.reset();
}

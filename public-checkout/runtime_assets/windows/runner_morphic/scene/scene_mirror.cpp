#include "scene/scene_mirror.h"

#include <sstream>
#include <unordered_map>

#include "forensic_trace.h"
#include "surface_policy/surface_ecology.h"

namespace morphic::scene {

SceneMirror::SceneMirror(EventBus* bus,
                         morphic::policy::SurfaceEcology* ecology)
    : bus_(bus), ecology_(ecology) {
  forensic::Log("SCENE", "SceneMirror up (Morphic NG Stage 1 — observer only)");
  if (bus_) {
    token_ = bus_->Subscribe([this](RuntimeEvent e, SurfaceShell*) {
      if (e == RuntimeEvent::SurfaceCreated ||
          e == RuntimeEvent::SurfaceDestroyed ||
          e == RuntimeEvent::TopologyMutated) {
        Rebuild();
      }
    });
  }
  Rebuild();
}

SceneMirror::~SceneMirror() {
  if (bus_ && token_ != 0) {
    bus_->Unsubscribe(token_);
    token_ = 0;
  }
}

void SceneMirror::Rebuild() {
  if (ecology_ == nullptr) return;
  graph_.Clear();

  const auto summary = ecology_->GetSummary();
  auto& composition = ecology_->composition_graph();

  // Build one SurfaceNode (+ material child) per live surface. Plane MEMBERS
  // nest under their plane root's node — the scene-graph expression of "these
  // surfaces are one spatial object".
  std::unordered_map<std::string, SceneNode*> by_surface;

  auto make_node = [this, &composition](
                       const decltype(summary.surfaces)::value_type& info) {
    auto node = std::make_unique<SurfaceNode>("surface:" + info.id);
    node->surface_id = info.id;
    node->surface_kind = info.kind;
    node->plane_root = composition.IsRoot(info.id);
    node->corner_radius =
        static_cast<float>(ecology_->GetCornerRadius(info.id));
    node->native_hosted =
        ecology_->GetSurfaceBackend(info.id) != "spatial";

    auto material =
        std::make_unique<MaterialNode>("material:" + info.id);
    material->material = ecology_->GetSurfaceMaterial(info.id);
    node->AddChild(std::move(material));
    return node;
  };

  // Pass 1 — plane roots and floating surfaces hang off the scene root.
  for (const auto& info : summary.surfaces) {
    const bool is_member = !composition.RootOfMember(info.id).empty();
    if (is_member) continue;
    by_surface[info.id] = graph_.root().AddChild(make_node(info));
  }
  // Pass 2 — plane members nest under their root's node.
  for (const auto& info : summary.surfaces) {
    const std::string root = composition.RootOfMember(info.id);
    if (root.empty()) continue;
    auto it = by_surface.find(root);
    SceneNode* parent =
        it != by_surface.end() ? it->second : &graph_.root();
    by_surface[info.id] = parent->AddChild(make_node(info));
  }

  // Verifiable observability: the whole structure, one line per node.
  std::ostringstream header;
  header << "graph rebuilt: nodes=" << graph_.NodeCount()
         << " surfaces=" << summary.surfaces.size();
  forensic::Log("SCENE", header.str());
  std::istringstream lines(graph_.DumpStructure());
  std::string line;
  while (std::getline(lines, line)) {
    forensic::Log("SCENE", "  " + line);
  }
}

}  // namespace morphic::scene

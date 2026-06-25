#include "scene/scene_graph.h"

#include <sstream>

namespace morphic::scene {

SceneGraph::SceneGraph()
    : root_(std::make_unique<SceneNode>(NodeKind::Scene, "scene:root")) {}

void SceneGraph::Clear() { root_->children.clear(); }

namespace {

void VisitNode(const SceneNode& node, int depth,
               const std::function<void(const SceneNode&, int)>& fn) {
  fn(node, depth);
  for (const auto& child : node.children) {
    VisitNode(*child, depth + 1, fn);
  }
}

SceneNode* FindIn(SceneNode& node, const std::string& id) {
  if (node.id == id) return &node;
  for (const auto& child : node.children) {
    if (SceneNode* found = FindIn(*child, id)) return found;
  }
  return nullptr;
}

}  // namespace

void SceneGraph::Visit(
    const std::function<void(const SceneNode&, int depth)>& fn) const {
  VisitNode(*root_, 0, fn);
}

SceneNode* SceneGraph::FindById(const std::string& id) {
  return FindIn(*root_, id);
}

std::string SceneGraph::DumpStructure() const {
  std::ostringstream out;
  Visit([&out](const SceneNode& node, int depth) {
    for (int i = 0; i < depth; ++i) out << "  ";
    out << ToString(node.kind) << " " << node.id;
    if (node.kind == NodeKind::Surface) {
      const auto& s = static_cast<const SurfaceNode&>(node);
      out << " kind=" << s.surface_kind
          << (s.plane_root ? " PLANE-ROOT" : "")
          << " backend=" << (s.native_hosted ? "hwnd" : "compositor");
      if (s.corner_radius > 0) out << " r=" << s.corner_radius;
    } else if (node.kind == NodeKind::Material) {
      out << " material=" << static_cast<const MaterialNode&>(node).material;
    }
    out << "\n";
  });
  return out.str();
}

size_t SceneGraph::NodeCount() const {
  size_t count = 0;
  Visit([&count](const SceneNode&, int) { ++count; });
  return count;
}

}  // namespace morphic::scene

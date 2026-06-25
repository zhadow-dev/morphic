#ifndef RUNNER_SCENE_SCENE_GRAPH_H_
#define RUNNER_SCENE_SCENE_GRAPH_H_

#include <functional>
#include <memory>
#include <string>

#include "scene/scene_node.h"

// MORPHIC NG · STAGE 1 — the retained scene graph container.
//
// Owns one root SceneNode and offers traversal + a structural dump (the
// observability that lets the mirror be VERIFIED against the live runtime
// before any backend consumes it). Pure data structure — no Win32/Flutter.
namespace morphic::scene {

class SceneGraph {
 public:
  SceneGraph();

  SceneNode& root() { return *root_; }
  const SceneNode& root() const { return *root_; }

  // Drop everything below the root (the mirror rebuilds from product truth).
  void Clear();

  // Depth-first pre-order traversal; `depth` starts at 0 for the root.
  void Visit(const std::function<void(const SceneNode&, int depth)>& fn) const;

  // Find a node by id anywhere in the graph (nullptr if absent).
  SceneNode* FindById(const std::string& id);

  // One-line-per-node structural dump (for the forensic trace).
  std::string DumpStructure() const;

  size_t NodeCount() const;

 private:
  std::unique_ptr<SceneNode> root_;
};

}  // namespace morphic::scene

#endif  // RUNNER_SCENE_SCENE_GRAPH_H_

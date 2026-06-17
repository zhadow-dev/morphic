#ifndef RUNNER_SCENE_SCENE_MIRROR_H_
#define RUNNER_SCENE_SCENE_MIRROR_H_

#include "runtime_events.h"
#include "scene/scene_graph.h"

namespace morphic::policy {
class SurfaceEcology;
}

// MORPHIC NG · STAGE 1 — SceneMirror: the scene graph as an OBSERVER.
//
// Builds the retained scene graph FROM the live product truth (registry +
// composition planes + applied appearance) on every lifecycle event, and dumps
// the structure to the forensic trace. This is deliberately authority-free:
// it proves the node model can represent everything the runtime actually does
// (surfaces, planes, materials, the runtime-owned corner radius) before any
// backend consumes it. When the compositor backend lands, authority migrates
// INTO this graph; until then it observes and never mutates.
//
// Firewall: product layer only (reads SurfaceEcology + EventBus). The runtime
// never learns the scene graph exists.
namespace morphic::scene {

class SceneMirror {
 public:
  SceneMirror(EventBus* bus, morphic::policy::SurfaceEcology* ecology);
  ~SceneMirror();

  SceneMirror(const SceneMirror&) = delete;
  SceneMirror& operator=(const SceneMirror&) = delete;

  const SceneGraph& graph() const { return graph_; }

  // Rebuild the graph from current product truth and log the structure.
  void Rebuild();

 private:
  EventBus* bus_;                            // not owned
  morphic::policy::SurfaceEcology* ecology_; // not owned
  int token_ = 0;
  SceneGraph graph_;
};

}  // namespace morphic::scene

#endif  // RUNNER_SCENE_SCENE_MIRROR_H_

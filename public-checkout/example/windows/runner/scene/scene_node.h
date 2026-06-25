#ifndef RUNNER_SCENE_SCENE_NODE_H_
#define RUNNER_SCENE_SCENE_NODE_H_

#include <memory>
#include <string>
#include <vector>

// MORPHIC NG · STAGE 1 — the SCENE GRAPH node vocabulary.
//
// This is the beginning of the compositor-era truth: a surface is a LOGICAL
// SPATIAL ENTITY in a retained scene graph; a window is one possible BACKEND
// for it. Today every SurfaceNode is native-hosted (projected onto an HWND by
// the existing runtime); the node model is deliberately backend-agnostic so a
// GPU compositor backend (DirectComposition-class) can host the same nodes
// without the graph changing shape.
//
// DISCIPLINE (verify-before-expand): the graph enters as a MIRROR of the live
// product-layer truth (see SceneMirror) — observable, log-verified, zero
// authority. It becomes the renderer truth only when a compositor backend
// exists to consume it. No node here owns geometry/activation/input today.
//
// PURE DATA: no Win32, no Flutter, no rendering. The arrow points down only.
namespace morphic::scene {

enum class NodeKind { Scene, Transform, Surface, Material, Effect };

inline const char* ToString(NodeKind k) {
  switch (k) {
    case NodeKind::Scene:     return "scene";
    case NodeKind::Transform: return "transform";
    case NodeKind::Surface:   return "surface";
    case NodeKind::Material:  return "material";
    case NodeKind::Effect:    return "effect";
  }
  return "?";
}

struct Vec2 {
  float x = 0;
  float y = 0;
};

struct SceneRect {
  float x = 0;
  float y = 0;
  float w = 0;
  float h = 0;
};

class SceneNode {
 public:
  SceneNode(NodeKind kind, std::string id) : kind(kind), id(std::move(id)) {}
  virtual ~SceneNode() = default;

  SceneNode(const SceneNode&) = delete;
  SceneNode& operator=(const SceneNode&) = delete;

  const NodeKind kind;
  const std::string id;

  SceneNode* parent = nullptr;  // non-owning back-pointer
  std::vector<std::unique_ptr<SceneNode>> children;

  SceneNode* AddChild(std::unique_ptr<SceneNode> child) {
    child->parent = this;
    children.push_back(std::move(child));
    return children.back().get();
  }
};

// Pure spatial transform applied to the subtree. (The future compositor's
// translation/scale/opacity animation target; inert in the mirror.)
class TransformNode : public SceneNode {
 public:
  explicit TransformNode(std::string id)
      : SceneNode(NodeKind::Transform, std::move(id)) {}

  Vec2 translation{};
  float scale = 1.0f;
  float opacity = 1.0f;
};

// A logical spatial surface — THE decoupling. `surface_id` links to the
// product-layer descriptor; `native_hosted` records which backend realizes it
// (today: always true = an HWND via the existing projection seam).
class SurfaceNode : public SceneNode {
 public:
  explicit SurfaceNode(std::string id)
      : SceneNode(NodeKind::Surface, std::move(id)) {}

  std::string surface_id;     // product-layer surface id (registry key)
  std::string surface_kind;   // descriptor kind name (informational)
  SceneRect bounds{};         // authored/observed bounds (mirror: unset)
  float corner_radius = 0;    // the runtime-owned shape property
  bool plane_root = false;    // roots carry composed members as children
  bool native_hosted = true;  // HWND backend today; compositor backend later
};

// Material parameters for the subtree (what the GPU material pipeline will
// consume: blur/tint/saturation/noise). Mirror carries the material TOKEN the
// policy layer already resolves ("standard"/"mica"/"acrylic"/"tabbed"/"glass").
class MaterialNode : public SceneNode {
 public:
  explicit MaterialNode(std::string id)
      : SceneNode(NodeKind::Material, std::move(id)) {}

  std::string material;  // policy material token
  float blur_radius = 0;
  unsigned int tint_argb = 0;
  float saturation = 1.0f;
};

// Post effects for the subtree (shadow/glow). Placeholder in the mirror —
// shadow truth still lives in the DWM projection until the compositor owns it.
class EffectNode : public SceneNode {
 public:
  explicit EffectNode(std::string id)
      : SceneNode(NodeKind::Effect, std::move(id)) {}

  float shadow_blur = 0;
  Vec2 shadow_offset{};
  unsigned int shadow_argb = 0;
};

}  // namespace morphic::scene

#endif  // RUNNER_SCENE_SCENE_NODE_H_

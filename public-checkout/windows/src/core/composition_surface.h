#pragma once

#include "visual_node.h"

namespace morphic {

class WindowHost;

// Tier 2 — A visual that participates in composition.
// Extends VisualNode with composition-specific state and optional host binding.
// In Phase 1A, every CompositionSurface binds 1:1 to a WindowHost.
// In future phases, multiple surfaces may share a single host,
// and only detached surfaces get their own host.
class CompositionSurface : public VisualNode {
public:
    explicit CompositionSurface(NodeId id, const SurfaceConfig& config)
        : VisualNode(id), config_(config) {
        localTransform_ = config.transform;
        constraints_ = config.constraints;
        layer_ = config.layer;
        sublevel_ = config.elevation;
        visible_ = config.visible;
    }

    // --- Composition state ---
    float opacity() const { return opacity_; }
    void setOpacity(float o) {
        if (opacity_ != o) {
            opacity_ = o;
            markDirty();
        }
    }

    SurfaceType surfaceType() const { return config_.type; }
    SurfaceRole role() const { return config_.role; }
    void setRole(SurfaceRole r) { config_.role = r; markDirty(); }
    uint32_t color() const { return config_.color; }
    void setColor(uint32_t c) { config_.color = c; markDirty(); }

    // --- Attach state ---
    AttachState attachState() const { return attachState_; }
    void setAttachState(AttachState s) { attachState_ = s; }

    // --- Group ---
    NodeId groupId() const { return groupId_; }
    void setGroupId(NodeId id) { groupId_ = id; }

    // --- Host binding ---
    // In Phase 1A: always bound. Future: optional.
    WindowHost* host() const { return host_; }
    void bindToHost(WindowHost* h) { host_ = h; }
    void unbindFromHost() { host_ = nullptr; }
    bool hasHost() const { return host_ != nullptr; }

    const SurfaceConfig& config() const { return config_; }

private:
    SurfaceConfig config_;
    float opacity_ = 1.0f;
    AttachState attachState_ = AttachState::Attached;
    NodeId groupId_ = kInvalidNodeId;
    WindowHost* host_ = nullptr;
};

}  // namespace morphic

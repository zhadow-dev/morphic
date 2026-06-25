#pragma once

#include "types.h"
#include <vector>
#include <memory>
#include <algorithm>

namespace morphic {

class CompositionSurface;

// Tier 1 — Abstract visual element in the scene graph.
// Owns transform, elevation, dirty tracking, and child hierarchy.
class VisualNode {
public:
    explicit VisualNode(NodeId id) : id_(id) {}
    virtual ~VisualNode() = default;

    // --- Identity ---
    NodeId id() const { return id_; }

    // --- Transform ---
    const Transform& localTransform() const { return localTransform_; }
    const Transform& worldTransform() const { return worldTransform_; }

    void setLocalTransform(const Transform& t) {
        if (localTransform_ != t) {
            localTransform_ = t;
            markDirty();
        }
    }

    void setPosition(int x, int y) {
        if (localTransform_.x != x || localTransform_.y != y) {
            localTransform_.x = x;
            localTransform_.y = y;
            markDirty();
        }
    }

    void setSize(int w, int h) {
        if (localTransform_.width != w || localTransform_.height != h) {
            localTransform_.width = w;
            localTransform_.height = h;
            markDirty();
        }
    }

    // --- Constraints ---
    const Constraints& constraints() const { return constraints_; }
    void setConstraints(const Constraints& c) { constraints_ = c; }

    // --- Elevation ---
    ElevationLayer elevationLayer() const { return layer_; }
    int elevationSublevel() const { return sublevel_; }

    void setElevation(ElevationLayer layer, int sublevel = 0) {
        if (layer_ != layer || sublevel_ != sublevel) {
            layer_ = layer;
            sublevel_ = sublevel;
            elevationDirty_ = true;
            markDirty();
        }
    }

    bool isElevationDirty() const { return elevationDirty_; }
    void clearElevationDirty() { elevationDirty_ = false; }

    // --- Visibility ---
    bool isVisible() const { return visible_; }
    void setVisible(bool v) {
        if (visible_ != v) {
            visible_ = v;
            markDirty();
        }
    }

    // --- Dirty tracking ---
    bool isDirty() const { return dirty_; }
    void clearDirty() { dirty_ = false; }

    void markDirty() {
        dirty_ = true;
        for (auto& child : children_) {
            child->markDirty();
        }
    }

    // --- Hierarchy ---
    VisualNode* parent() const { return parent_; }
    const std::vector<VisualNode*>& children() const { return children_; }

    void addChild(VisualNode* child) {
        if (child->parent_) {
            child->parent_->removeChild(child);
        }
        child->parent_ = this;
        children_.push_back(child);
        child->markDirty();
    }

    void removeChild(VisualNode* child) {
        auto it = std::find(children_.begin(), children_.end(), child);
        if (it != children_.end()) {
            children_.erase(it);
            child->parent_ = nullptr;
        }
    }

    // --- World transform computation ---
    void computeWorldTransform() {
        if (parent_) {
            worldTransform_.x = parent_->worldTransform_.x + localTransform_.x;
            worldTransform_.y = parent_->worldTransform_.y + localTransform_.y;
        } else {
            worldTransform_.x = localTransform_.x;
            worldTransform_.y = localTransform_.y;
        }
        worldTransform_.width = localTransform_.width;
        worldTransform_.height = localTransform_.height;

        for (auto* child : children_) {
            child->computeWorldTransform();
        }
    }

protected:
    NodeId id_;
    VisualNode* parent_ = nullptr;
    std::vector<VisualNode*> children_;

    Transform localTransform_;
    Transform worldTransform_;
    Constraints constraints_;

    ElevationLayer layer_ = ElevationLayer::Base;
    int sublevel_ = 0;

    bool dirty_ = true;
    bool elevationDirty_ = true;
    bool visible_ = true;
};

}  // namespace morphic

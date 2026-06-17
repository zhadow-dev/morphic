#pragma once

#include <windows.h>
#include <cstdint>
#include <vector>
#include <string>
#include "surface_role.h"

namespace morphic {

// --- Identifiers ---
using NodeId = uint32_t;
constexpr NodeId kInvalidNodeId = 0;

// Phase 5A: Stable Semantic Identity.
// Survives destroy/recreate, detach/reattach, promotion, recovery.
// persistentId = semantic continuity anchor (assigned once, never changes)
// lineageId    = ancestry chain across recreation/promotion
// generation   = runtime incarnation count (increments on recreate)
struct SurfaceIdentity {
    uint64_t persistentId = 0;
    uint64_t lineageId = 0;
    uint32_t generation = 0;

    bool isValid() const { return persistentId != 0; }
    bool sameLineage(const SurfaceIdentity& other) const {
        return lineageId == other.lineageId;
    }
    bool isStale(const SurfaceIdentity& current) const {
        return persistentId == current.persistentId &&
               generation < current.generation;
    }
};

// --- Enums ---

enum class SurfaceType {
    Content,    // Interactive content surface
    Shadow,     // Non-interactive shadow (future)
    Overlay,    // Tooltips, menus (future)
};

enum class AttachState {
    Attached,   // Bound to a group, moves with it
    Detached,   // Independent, free-floating
};

enum class ElevationLayer {
    Background = 0,
    Base       = 100,
    Attached   = 200,
    Detached   = 300,
    Overlay    = 400,
    Modal      = 500,
    DragPreview = 600,
    Debug      = 900,
};

enum class ResizeEdge {
    None,
    Left, Right, Top, Bottom,
    TopLeft, TopRight, BottomLeft, BottomRight,
};

// Phase 9 — Semantic Continuity / Presence types
enum class SemanticVisibility {
    Full,
    Obscured,
    Hidden
};

enum class RuntimePresence {
    ResidencyBudgeted,
    ThrottledFrameRate,
    Hibernating,
    Quarantined
};

// --- Structs ---

struct Transform {
    int x = 0;
    int y = 0;
    int width = 400;
    int height = 300;

    RECT toRECT() const {
        return { x, y, x + width, y + height };
    }

    static Transform fromRECT(const RECT& r) {
        return { r.left, r.top, r.right - r.left, r.bottom - r.top };
    }

    bool operator==(const Transform& o) const {
        return x == o.x && y == o.y && width == o.width && height == o.height;
    }
    bool operator!=(const Transform& o) const { return !(*this == o); }
};

struct Constraints {
    int minWidth  = 100;
    int minHeight = 100;
    int maxWidth  = 0;  // 0 = unconstrained
    int maxHeight = 0;
};

struct SurfaceConfig {
    Transform transform;
    Constraints constraints;
    SurfaceType type = SurfaceType::Content;
    ElevationLayer layer = ElevationLayer::Base;
    int elevation = 0;        // sub-level within layer
    uint32_t color = 0x1a1a2e; // RGB fill color for Phase 1A
    bool visible = true;
    NodeId groupId = kInvalidNodeId;
    SurfaceRole role = SurfaceRole::Workspace;
    SurfaceIdentity identity;  // Phase 5A: stable semantic identity
};

struct DisplayInfo {
    HMONITOR handle = nullptr;
    RECT bounds{};
    RECT workArea{};
    float dpiX = 96.0f;
    float dpiY = 96.0f;
    float scaleFactor = 1.0f;
    int refreshRate = 60;
    bool isPrimary = false;
    std::wstring deviceName;
};

}  // namespace morphic

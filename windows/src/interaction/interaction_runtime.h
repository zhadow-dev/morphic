#pragma once

namespace morphic {

// Phase 4 — InteractionRuntime stub.
//
// Prevents drag/resize logic from growing inside Compositor.
//
// Drag/resize is NOT compositor work. It is interaction orchestration.
// Future extensions (snapping, docking, magnetic edges, workspace transitions,
// overview animations, detach gestures) all grow from drag/resize.
// Without this boundary, the Compositor becomes a second god object.
//
// Currently: DragController + ResizeController live in Compositor.
// Future: they migrate here.
//
// InteractionRuntime will own:
//   - DragController        — surface drag with group sync
//   - ResizeController      — resize edge detection and constraints
//   - CaptureManager        — mouse capture across surfaces
//   - Future: SnapController     — snap-to-edge, magnetic docking
//   - Future: DockController     — workspace docking regions
//   - Future: GestureController  — overview drag-reorder, detach gesture
//
// InteractionRuntime consumes:
//   - TopologyManager (for snap rules, docking regions)
//   - SurfaceRegistry (for surface bounds)
//   - ActivationManager (for z-order during drag)
//
// InteractionRuntime emits:
//   - RuntimeEvents: TopologyMutated (after drag/resize commits)
//
// THREAD: UI thread only (mouse capture, SetWindowPos).
class InteractionRuntime {
public:
    InteractionRuntime() = default;

    // Future API surface:
    //
    // void beginDrag(NodeId surfaceId, POINT screenPos);
    // void updateDrag(NodeId surfaceId, POINT screenPos);
    // void endDrag(NodeId surfaceId, POINT screenPos);
    //
    // void beginResize(NodeId surfaceId, ResizeEdge edge, POINT screenPos);
    // void updateResize(NodeId surfaceId, POINT screenPos);
    // void endResize(NodeId surfaceId, POINT screenPos);
    //
    // Snap/dock:
    // void setSnapEnabled(bool enabled);
    // void setDockRegions(const std::vector<DockRegion>& regions);
    //
    // Overview drag:
    // void beginOverviewReorder(NodeId surfaceId);
    // void commitOverviewReorder(NodeId surfaceId, int newIndex);
};

}  // namespace morphic

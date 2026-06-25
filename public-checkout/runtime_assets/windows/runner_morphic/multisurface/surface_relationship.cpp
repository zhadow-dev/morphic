#include "multisurface/surface_relationship.h"

const char* ToString(SurfaceRelationship r) {
  switch (r) {
    case SurfaceRelationship::None:             return "None";
    case SurfaceRelationship::Parent:           return "Parent";
    case SurfaceRelationship::Child:            return "Child";
    case SurfaceRelationship::Overlay:          return "Overlay";
    case SurfaceRelationship::ToolPalette:      return "ToolPalette";
    case SurfaceRelationship::DockHost:         return "DockHost";
    case SurfaceRelationship::Docked:           return "Docked";
    case SurfaceRelationship::Grouped:          return "Grouped";
    case SurfaceRelationship::Detached:         return "Detached";
    case SurfaceRelationship::ExtractionTether: return "ExtractionTether";
    case SurfaceRelationship::Transient:        return "Transient";
  }
  return "?";
}

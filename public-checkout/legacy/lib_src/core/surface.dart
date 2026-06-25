/// Surface role — determines HWND topology, activation semantics,
/// governance hints, and compositor navigation participation.
///
/// Role semantics are declarative and composable.
/// Each role maps to a fixed set of Win32 style/owner decisions.
enum SurfaceRole {
  /// Default compositor-managed surface.
  /// Hidden from Alt+Tab/taskbar, owned by main window.
  workspace,

  /// User-promoted independent surface.
  /// Appears in Alt+Tab/taskbar, owned by main window (same destroy group).
  /// Navigation independence, NOT process independence.
  detached,

  /// Floating utility window (inspector, controls).
  /// Hidden from Alt+Tab/taskbar, non-resizable.
  toolPalette,

  /// HUD/topmost/display-only surface.
  /// Always-on-top, never steals activation, NOT interactive.
  overlay,
}

/// Surface configuration for creating a new composition surface.
class SurfaceConfig {
  final int x;
  final int y;
  final int width;
  final int height;
  final int color;
  final int elevation;
  final bool visible;
  final int minWidth;
  final int minHeight;
  final SurfaceRole role;

  const SurfaceConfig({
    this.x = 100,
    this.y = 100,
    this.width = 400,
    this.height = 300,
    this.color = 0x1a1a2e,
    this.elevation = 0,
    this.visible = true,
    this.minWidth = 100,
    this.minHeight = 100,
    this.role = SurfaceRole.workspace,
  });

  Map<String, dynamic> toMap() => {
        'x': x,
        'y': y,
        'width': width,
        'height': height,
        'color': color,
        'elevation': elevation,
        'visible': visible,
        'minWidth': minWidth,
        'minHeight': minHeight,
        'role': role.name,
      };
}

/// A composition surface — a visual node that may be backed by an HWND.
class Surface {
  final int id;
  final SurfaceConfig config;

  Surface({required this.id, required this.config});
}

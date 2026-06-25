/// MORPHIC AUTHORING LAYER (M2.2B) — SurfaceSpec.
///
/// A declarative ORCHESTRATION DESCRIPTOR for one surface. **NOT a widget.** This is the honest
/// model: it does not pretend surfaces compose into a tree. Each spec becomes a SEPARATE Flutter
/// engine + HWND running its `entrypoint`, spawned and positioned by the runtime. The spec only
/// *describes* what to spawn and how it relates — it never renders the surface inline.
///
///   - [kind]       = runtime BEHAVIOR (workspace/inspector/tool_palette/overlay) → policy.
///   - [entrypoint] = CONTENT: the `@pragma('vm:entry-point')` function NAME (a String, because
///                    it is resolved natively by name in its own engine — there is no function
///                    reference fiction here).
///   - [parent]     = the APP-LOCAL [id] of another spec; resolved to the real native id at spawn
///                    time (so relationships are declared, not hand-plumbed).
///   - [composed]   = join the parent's composition plane (lights/coheres as one object).
class SurfaceSpec {
  final String id; // app-local id (wires parent relationships within an app's spec list)
  final String kind;
  final String entrypoint;
  final String? parent;
  final bool composed;
  final int x;
  final int y;
  final int width;
  final int height;
  final String? backdrop; // glass tint/blur: 'none'|'mica'|'acrylic'|'tabbed' (null = kind default)
  final String? transparency; // glass mode: 'opaque'|'transparent_content'|'full_glass' (null = default)
  final String? corners; // DWM corner preset: 'default'|'rounded'|'small'|'square' (antialiased)
  final int? cornerRadius; // ANY radius in px (window-region clip; edges aliased — presets preferred)
  final bool chromeless; // true = no native title strip / borders; content fills the window
  final String? backend; // 'native' (default) | 'spatial' (NG compositor: shaped GPU visual)
  final String? shape; // spatial: 'rounded' (default, uses cornerRadius) | 'capsule' | 'hexagon'
  final String? material; // spatial: 'none' | 'acrylic' (REAL compositor backdrop blur)
  final int? materialTint; // spatial: ARGB tint over the blur (null = material default)
  final int? elevation; // spatial: compositor shadow depth in px (0/null = none)

  const SurfaceSpec({
    required this.id,
    required this.kind,
    required this.entrypoint,
    this.parent,
    this.composed = false,
    this.x = 200,
    this.y = 200,
    this.width = 400,
    this.height = 300,
    this.backdrop,
    this.transparency,
    this.corners,
    this.cornerRadius,
    this.chromeless = false,
    this.backend,
    this.shape,
    this.material,
    this.materialTint,
    this.elevation,
  });

  /// A grounded root surface (a document/workspace). Typically a plane root.
  const SurfaceSpec.workspace({
    required this.id,
    required this.entrypoint,
    this.parent,
    this.composed = false,
    this.x = 120,
    this.y = 120,
    this.width = 600,
    this.height = 480,
    this.backdrop,
    this.transparency,
    this.corners,
    this.cornerRadius,
    this.chromeless = false,
    this.backend,
    this.shape,
    this.material,
    this.materialTint,
    this.elevation,
  }) : kind = 'workspace';

  /// A support inspector (metadata/outline). Usually [composed] under its [parent].
  const SurfaceSpec.inspector({
    required this.id,
    required this.entrypoint,
    this.parent,
    this.composed = true,
    this.x = 740,
    this.y = 120,
    this.width = 260,
    this.height = 480,
    this.backdrop,
    this.transparency,
    this.corners,
    this.cornerRadius,
    this.chromeless = false,
    this.backend,
    this.shape,
    this.material,
    this.materialTint,
    this.elevation,
  }) : kind = 'inspector';

  /// A tool palette. Usually [composed] under its [parent].
  const SurfaceSpec.toolPalette({
    required this.id,
    required this.entrypoint,
    this.parent,
    this.composed = true,
    this.x = 800,
    this.y = 120,
    this.width = 240,
    this.height = 320,
    this.backdrop,
    this.transparency,
    this.corners,
    this.cornerRadius,
    this.chromeless = false,
    this.backend,
    this.shape,
    this.material,
    this.materialTint,
    this.elevation,
  }) : kind = 'tool_palette';

  /// A transient overlay (command palette, search). Role-glass floater; belongs to the app, not a
  /// parent surface.
  const SurfaceSpec.overlay({
    required this.id,
    required this.entrypoint,
    this.x = 300,
    this.y = 200,
    this.width = 420,
    this.height = 320,
    this.backdrop,
    this.transparency,
    this.corners,
    this.cornerRadius,
    this.chromeless = false,
    this.backend,
    this.shape,
    this.material,
    this.materialTint,
    this.elevation,
  })  : kind = 'overlay',
        parent = null,
        composed = false;
}

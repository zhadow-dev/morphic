# legacy/ — quarantined dead lineage (M3 Phase 1.3)

This folder holds the **abandoned compositor-as-Flutter-plugin lineage** of Morphic. None of it is
imported by the live runtime or the real app. It is preserved (not deleted) so the history is
recoverable and so nobody re-derives it from scratch. **Do not build on anything in here.**

| Path | What it was |
|---|---|
| `lib_src/` | The stale Dart plugin API: `MorphicController`, `createSurface`, `attachRenderer`, `SurfaceConfig`, surface/group/display models, platform-interface + method-channel. The old "drive a native compositor over a MethodChannel" model. |
| `example_lib/morphic_studio.dart` | "Morphic Studio" harness page — the *only* consumer of the dead `MorphicController` API. Orphaned (imported by nothing). |
| `morphic_test.dart` | Unit test for the dead `SurfaceConfig` / `MorphicController`. |
| `plugin_integration_test.dart` | Integration test against the dead plugin Dart API. |
| (removed) `README_OLD_compositor_plugin.md` | The old root README describing the dead "Spatial Desktop Compositor Runtime" architecture — deleted in the docs consolidation; recoverable from git history. |

## Important: what was NOT quarantined

The root `windows/` Flutter **plugin** (`MorphicPluginCApi`) is **still live and load-bearing**. Its
`createSurface`/`attachRenderer`/compositor methods are dead, but the plugin **shell** hosts the
`morphic` MethodChannel and the DLL-exported extension seam (`windows/include/morphic/morphic_extension.h`)
that the real runner (`example/windows/runner/`) registers its product-layer handlers into. Removing it
would break the entire runtime. See the M2 friction-log record in `ARCHITECTURE.md` (Part II).

## Reversal

Everything here was moved with `git mv`. To restore a file: `git mv legacy/<path> <original-path>` and
re-add its export to `lib/morphic.dart` if applicable.
